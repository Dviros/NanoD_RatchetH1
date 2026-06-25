/**
 * nanod_e2e.mjs — Nano_D++ Protocol E2E Harness
 *
 * USAGE
 * ─────
 * TCP mode (primary, no deps required):
 *   node fw/test/integration/nanod_e2e.mjs <device-ip>
 *   node fw/test/integration/nanod_e2e.mjs 192.168.1.42
 *
 * Serial mode (optional, requires 'serialport' npm package):
 *   node fw/test/integration/nanod_e2e.mjs --serial /dev/tty.usbmodem101
 *   node fw/test/integration/nanod_e2e.mjs --serial COM3
 *
 * WHAT IT VERIFIES
 * ────────────────
 * Step 1  Boot greeting / ACK
 *         TCP: expects {"connected":true,...} greeting on connect.
 *         Serial: expects {"ack":"boot","ok":true} emitted on power-up.
 *         Validates: transport is alive and device firmware responded.
 *
 * Step 2  Get settings
 *         Sends {"settings":"?"} and validates the response has the
 *         required fields: pdVoltage, wifiPassword (redacted to "***"),
 *         deviceName, firmwareVersion, serialNumber, ledMaxBrightness,
 *         maxVelocity, maxVoltage, idleTimeout, wifiEnabled, wifiSsid.
 *
 * Step 3  Set a setting
 *         Sends {"settings":{"debug":false}} (safe no-op change) and
 *         asserts the ACK is {"ack":"settings","ok":true}.
 *
 * Step 4  List profiles
 *         Sends {"profiles":"#all"} and asserts the response contains a
 *         "profiles" array and a "current" string field.
 *
 * Step 5a Sprite upload — correct CRC (expects ok:true)
 *         Uploads a small synthetic 32-byte payload via the begin/data/end
 *         state machine, computing the CRC-32 (IEEE 802.3, poly 0xEDB88320)
 *         in JS and asserting ok:true on the end ACK.
 *
 * Step 5b Sprite upload — wrong CRC (expects ok:false)
 *         Repeats the upload but sends a deliberately wrong crc32 value in
 *         the end frame, asserting the device returns ok:false (CRC mismatch).
 *
 * Step 6  WiFi set (optional, skipped if SKIP_WIFI=1 env var is set)
 *         Sends {"wifi":{"enabled":false}} and asserts {"ack":"wifi","ok":true}.
 *
 * REQUIREMENTS
 * ────────────
 * - Node 22+, no npm dependencies for TCP mode.
 * - A running Nano_D++ device (or compatible emulator) reachable at the
 *   given IP:3333 (TCP) or serial port (serial mode).
 * - The device must be flashed with a nanofoc_d_wifi or nanofoc_d_full
 *   build for TCP mode; any build works for serial mode.
 * - Serial mode additionally requires: npm install serialport
 *
 * EXIT CODES
 * ──────────
 * 0  All steps passed.
 * 1  One or more steps failed, or connection error.
 */

import net from 'node:net';
import { Buffer } from 'node:buffer';
import process from 'node:process';

// ─── CRC-32 (IEEE 802.3, poly 0xEDB88320) ────────────────────────────────────
// Self-check: crc32(Buffer.from("123456789")) === 0xCBF43926

function buildCrcTable() {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) {
      c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    }
    table[i] = c;
  }
  return table;
}

const CRC_TABLE = buildCrcTable();

function crc32(buf) {
  let crc = 0xFFFFFFFF;
  for (let i = 0; i < buf.length; i++) {
    crc = CRC_TABLE[(crc ^ buf[i]) & 0xFF] ^ (crc >>> 8);
  }
  return (crc ^ 0xFFFFFFFF) >>> 0; // unsigned 32-bit
}

// Self-check runs before any network I/O.
const CRC_SELF_CHECK = crc32(Buffer.from('123456789'));
if (CRC_SELF_CHECK !== 0xCBF43926) {
  process.stderr.write(
    `FATAL: CRC-32 self-check failed: got 0x${CRC_SELF_CHECK.toString(16).toUpperCase()}, ` +
    `expected 0xCBF43926\n`
  );
  process.exit(1);
}

// ─── Result tracking ──────────────────────────────────────────────────────────

const results = [];
let passed = 0;
let failed = 0;

function pass(label) {
  results.push({ ok: true, label });
  passed++;
  console.log(`  PASS  ${label}`);
}

function fail(label, reason) {
  results.push({ ok: false, label, reason });
  failed++;
  console.log(`  FAIL  ${label}`);
  if (reason) console.log(`        Reason: ${reason}`);
}

function assert(condition, passLabel, failLabel, reason) {
  if (condition) {
    pass(passLabel);
  } else {
    fail(failLabel ?? passLabel, reason);
  }
}

// ─── Transport abstraction ───────────────────────────────────────────────────
// Both TCP and serial are wrapped into a common {send, readLine, close} shape.

const CMD_TIMEOUT_MS = 5000; // per-command wait for a matching reply

/**
 * Build a line-oriented reader from an event emitter that fires 'data' with
 * Buffer chunks (node:net.Socket and SerialPort both do this).
 *
 * Returns an async function readLine(matchFn, timeoutMs) that resolves with
 * the first parsed JSON object where matchFn(obj) is truthy, or rejects on
 * timeout.  All lines are also logged at debug level.
 */
function makeLineReader(emitter) {
  let buf = '';
  const waiters = []; // [{matchFn, resolve, reject, timer}]

  emitter.on('data', (chunk) => {
    buf += chunk.toString('utf8');
    let nl;
    while ((nl = buf.indexOf('\n')) !== -1) {
      const line = buf.slice(0, nl).trim();
      buf = buf.slice(nl + 1);
      if (!line.startsWith('{')) {
        // Non-JSON diagnostic — log and skip.
        if (line.length > 0) console.log(`  [device] ${line}`);
        continue;
      }
      let obj;
      try { obj = JSON.parse(line); } catch { continue; }
      // Deliver to the first waiter whose matchFn accepts this object.
      for (let i = 0; i < waiters.length; i++) {
        if (waiters[i].matchFn(obj)) {
          const w = waiters.splice(i, 1)[0];
          clearTimeout(w.timer);
          w.resolve(obj);
          return;
        }
      }
      // No waiter matched — log as unsolicited.
      // Filter out noisy idle/telemetry frames to keep output clean.
      const keys = Object.keys(obj);
      const silent = keys.every(k => ['idle','p','a','t','v','kd','ku','ks','debug'].includes(k));
      if (!silent) console.log(`  [unsolicited] ${line}`);
    }
  });

  function readLine(matchFn, timeoutMs = CMD_TIMEOUT_MS) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        const idx = waiters.findIndex(w => w.resolve === resolve);
        if (idx !== -1) waiters.splice(idx, 1);
        reject(new Error(`Timeout waiting for matching frame (${timeoutMs} ms)`));
      }, timeoutMs);
      waiters.push({ matchFn, resolve, reject, timer });
    });
  }

  function drainWaiters(err) {
    for (const w of waiters) {
      clearTimeout(w.timer);
      w.reject(err);
    }
    waiters.length = 0;
  }

  return { readLine, drainWaiters };
}

// ─── TCP transport ────────────────────────────────────────────────────────────

function connectTcp(host, port = 3333) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host, port });
    socket.setEncoding('utf8');

    const { readLine, drainWaiters } = makeLineReader(socket);

    socket.once('connect', () => {
      resolve({
        send(obj) {
          socket.write(JSON.stringify(obj) + '\n');
        },
        readLine,
        close() {
          drainWaiters(new Error('Connection closed'));
          socket.destroy();
        },
        mode: 'tcp',
      });
    });

    socket.once('error', (err) => {
      reject(new Error(`TCP connect to ${host}:${port} failed: ${err.message}`));
    });

    socket.once('close', () => {
      drainWaiters(new Error('TCP connection closed unexpectedly'));
    });
  });
}

// ─── Serial transport (optional, dynamic import) ─────────────────────────────

async function connectSerial(path, baudRate = 115200) {
  let SerialPort;
  try {
    const mod = await import('serialport');
    SerialPort = mod.SerialPort ?? mod.default?.SerialPort ?? mod.default;
    if (typeof SerialPort !== 'function') throw new Error('Could not locate SerialPort constructor');
  } catch (e) {
    throw new Error(
      `'serialport' package not available (${e.message}).\n` +
      `  Install it with:  npm install serialport\n` +
      `  Or use TCP mode:  node fw/test/integration/nanod_e2e.mjs <device-ip>`
    );
  }

  return new Promise((resolve, reject) => {
    const port = new SerialPort({ path, baudRate, autoOpen: false });
    const { readLine, drainWaiters } = makeLineReader(port);

    port.open((err) => {
      if (err) return reject(new Error(`Serial open ${path} failed: ${err.message}`));
      resolve({
        send(obj) {
          port.write(JSON.stringify(obj) + '\n');
        },
        readLine,
        close() {
          drainWaiters(new Error('Serial port closed'));
          port.close();
        },
        mode: 'serial',
      });
    });

    port.on('error', (err) => {
      drainWaiters(err);
    });
  });
}

// ─── Test steps ──────────────────────────────────────────────────────────────

async function stepBootGreeting(transport) {
  console.log('\nStep 1: Boot greeting / ACK');
  try {
    if (transport.mode === 'tcp') {
      // On TCP connect the device immediately sends a greeting object.
      const greeting = await transport.readLine(
        (o) => o.connected === true || (o.ack === 'boot' && o.ok === true),
        8000
      );
      assert(
        greeting.connected === true || (greeting.ack === 'boot' && greeting.ok === true),
        'Boot greeting received',
        'Boot greeting received',
        `Got: ${JSON.stringify(greeting)}`
      );
      if (greeting.connected) {
        console.log(`        device=${greeting.device}  fw=${greeting.fw}  ip=${greeting.ip}`);
      }
    } else {
      // Serial: device emits {"ack":"boot","ok":true} on power-up.
      // If already running, send a no-op settings query to confirm liveness.
      const bootAck = await transport.readLine(
        (o) => (o.ack === 'boot' && o.ok === true) || o.settings != null,
        10000
      );
      assert(
        bootAck != null,
        'Boot ACK or settings response received',
        'Boot ACK or settings response received',
        `Got: ${JSON.stringify(bootAck)}`
      );
    }
  } catch (e) {
    fail('Boot greeting received', e.message);
  }
}

async function stepGetSettings(transport) {
  console.log('\nStep 2: Get settings');
  try {
    transport.send({ settings: '?' });
    const resp = await transport.readLine((o) => o.settings != null && typeof o.settings === 'object');
    const s = resp.settings;

    const requiredFields = [
      'pdVoltage', 'wifiPassword', 'deviceName', 'firmwareVersion',
      'serialNumber', 'ledMaxBrightness', 'maxVelocity', 'maxVoltage',
      'idleTimeout', 'wifiEnabled', 'wifiSsid',
    ];

    const missing = requiredFields.filter(f => !(f in s));
    assert(
      missing.length === 0,
      'Settings response has all required fields',
      'Settings response has all required fields',
      missing.length > 0 ? `Missing: ${missing.join(', ')}` : undefined
    );

    // wifiPassword must always be redacted
    assert(
      s.wifiPassword === '***',
      'wifiPassword is redacted to "***"',
      'wifiPassword is redacted to "***"',
      `Got: ${JSON.stringify(s.wifiPassword)}`
    );

    // pdVoltage must be a number in [5, 9]
    assert(
      typeof s.pdVoltage === 'number' && s.pdVoltage >= 5 && s.pdVoltage <= 9,
      `pdVoltage is a valid voltage (got ${s.pdVoltage} V)`,
      `pdVoltage is a valid voltage`,
      `Got: ${JSON.stringify(s.pdVoltage)}`
    );

    console.log(`        device=${s.deviceName}  fw=${s.firmwareVersion}  pdVoltage=${s.pdVoltage}V`);
  } catch (e) {
    fail('Get settings', e.message);
  }
}

async function stepSetSetting(transport) {
  console.log('\nStep 3: Set a setting');
  try {
    // Use debug:false — a safe no-op write that won't change device behaviour.
    transport.send({ settings: { debug: false } });
    const ack = await transport.readLine((o) => o.ack === 'settings');
    assert(
      ack.ack === 'settings' && ack.ok === true,
      'settings ACK ok:true',
      'settings ACK ok:true',
      `Got: ${JSON.stringify(ack)}`
    );
  } catch (e) {
    fail('Set setting ACK', e.message);
  }
}

async function stepListProfiles(transport) {
  console.log('\nStep 4: List profiles');
  try {
    transport.send({ profiles: '#all' });
    const resp = await transport.readLine((o) => Array.isArray(o.profiles));
    assert(
      Array.isArray(resp.profiles) && resp.profiles.length > 0,
      `Profiles list is a non-empty array (got ${resp.profiles?.length} profile(s))`,
      'Profiles list is a non-empty array',
      `Got profiles: ${JSON.stringify(resp.profiles)}`
    );
    assert(
      typeof resp.current === 'string' && resp.current.length > 0,
      `"current" profile field is a non-empty string (got "${resp.current}")`,
      '"current" profile field present',
      `Got current: ${JSON.stringify(resp.current)}`
    );
    console.log(`        profiles=[${resp.profiles.join(', ')}]  current="${resp.current}"`);
  } catch (e) {
    fail('List profiles', e.message);
  }
}

/**
 * Sprite upload helper.
 * Sends begin -> data -> end and returns the end ACK object.
 * The caller is responsible for assert-ing ok.
 */
async function spriteUpload(transport, name, payload, crcOverride) {
  const size = payload.length;
  const actualCrc = crc32(payload);
  const sentCrc = crcOverride !== undefined ? crcOverride : actualCrc;

  // begin
  transport.send({ sprite: { op: 'begin', name, size } });
  const beginAck = await transport.readLine((o) => o.ack === 'sprite');
  if (!beginAck.ok) {
    throw new Error(`sprite begin nack: ${JSON.stringify(beginAck)}`);
  }

  // data (single chunk — payload is small enough)
  const b64 = payload.toString('base64');
  transport.send({ sprite: { op: 'data', seq: 0, data: b64 } });
  const dataAck = await transport.readLine((o) => o.ack === 'sprite');
  if (!dataAck.ok) {
    throw new Error(`sprite data nack: ${JSON.stringify(dataAck)}`);
  }

  // end
  transport.send({ sprite: { op: 'end', crc32: sentCrc } });
  const endAck = await transport.readLine((o) => o.ack === 'sprite');
  return endAck;
}

async function stepSpriteUpload(transport) {
  console.log('\nStep 5a: Sprite upload — correct CRC (expect ok:true)');

  // A minimal 32-byte synthetic BMP-like payload (does not need to be a valid
  // image — the firmware only validates CRC and byte count, not image contents).
  const payload = Buffer.alloc(32);
  payload.write('BM', 0, 'ascii'); // BMP magic
  payload.writeUInt32LE(32, 2);    // file size field
  const testName = '__e2e_test__.bmp';

  try {
    const endAck = await spriteUpload(transport, testName, payload);
    assert(
      endAck.ok === true,
      'Sprite upload (correct CRC) ok:true',
      'Sprite upload (correct CRC) ok:true',
      `Got: ${JSON.stringify(endAck)}`
    );
  } catch (e) {
    fail('Sprite upload (correct CRC)', e.message);
    return; // step 5b would also fail if the device is in a bad state
  }

  console.log('\nStep 5b: Sprite upload — wrong CRC (expect ok:false)');
  try {
    // Re-upload the same file but corrupt the crc32 by XOR-ing with 0xDEADBEEF.
    const badCrc = (crc32(payload) ^ 0xDEADBEEF) >>> 0;
    const endAck = await spriteUpload(transport, testName, payload, badCrc);
    assert(
      endAck.ok === false,
      'Sprite upload (wrong CRC) ok:false (CRC mismatch caught)',
      'Sprite upload (wrong CRC) ok:false',
      `Got: ${JSON.stringify(endAck)}`
    );
  } catch (e) {
    // A timeout here means the device didn't respond — likely an emulator gap.
    fail('Sprite upload (wrong CRC)', e.message);
  }

  // Clean up: delete the test sprite if it was committed (step 5a succeeded).
  try {
    transport.send({ sprite: { op: 'delete', name: testName } });
    await transport.readLine((o) => o.ack === 'sprite', 3000);
  } catch {
    // Deletion failure is not a test error — the sprite may have been removed
    // by the CRC-mismatch path already.
  }
}

async function stepWifiSet(transport) {
  if (process.env.SKIP_WIFI === '1') {
    console.log('\nStep 6: WiFi set — SKIPPED (SKIP_WIFI=1)');
    return;
  }
  console.log('\nStep 6: WiFi set (disable, safe no-op)');
  try {
    transport.send({ wifi: { enabled: false } });
    const ack = await transport.readLine((o) => o.ack === 'wifi');
    assert(
      ack.ack === 'wifi' && ack.ok === true,
      'wifi ACK ok:true',
      'wifi ACK ok:true',
      `Got: ${JSON.stringify(ack)}`
    );
  } catch (e) {
    fail('wifi ACK', e.message);
  }
}

// ─── Main ─────────────────────────────────────────────────────────────────────

async function main() {
  const args = process.argv.slice(2);

  let transport;

  if (args[0] === '--serial') {
    const serialPath = args[1];
    if (!serialPath) {
      console.error('Usage: node nanod_e2e.mjs --serial <path>');
      console.error('  e.g. node nanod_e2e.mjs --serial /dev/tty.usbmodem101');
      process.exit(1);
    }
    console.log(`Nano_D++ E2E Harness — serial mode: ${serialPath}`);
    console.log('Connecting...');
    try {
      transport = await connectSerial(serialPath);
    } catch (e) {
      console.error(`\nCould not open serial port: ${e.message}`);
      process.exit(1);
    }
  } else if (args[0]) {
    const host = args[0];
    const port = parseInt(args[1] ?? '3333', 10);
    console.log(`Nano_D++ E2E Harness — TCP mode: ${host}:${port}`);
    console.log('Connecting...');
    try {
      transport = await connectTcp(host, port);
    } catch (e) {
      console.error(`\nConnection failed: ${e.message}`);
      console.error('\nHint: make sure the device is running a nanofoc_d_wifi or nanofoc_d_full');
      console.error('firmware build and is reachable at the given IP address.');
      process.exit(1);
    }
    console.log('Connected.');
  } else {
    console.error('Usage:');
    console.error('  TCP mode:    node fw/test/integration/nanod_e2e.mjs <device-ip>');
    console.error('  Serial mode: node fw/test/integration/nanod_e2e.mjs --serial <port>');
    console.error('');
    console.error('Examples:');
    console.error('  node fw/test/integration/nanod_e2e.mjs 192.168.1.42');
    console.error('  node fw/test/integration/nanod_e2e.mjs --serial /dev/tty.usbmodem101');
    process.exit(1);
  }

  console.log('\n━━━ Running test suite ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━');

  try {
    await stepBootGreeting(transport);
    await stepGetSettings(transport);
    await stepSetSetting(transport);
    await stepListProfiles(transport);
    await stepSpriteUpload(transport);
    await stepWifiSet(transport);
  } finally {
    transport.close();
  }

  // ── Summary ─────────────────────────────────────────────────────────────────
  console.log('\n━━━ Summary ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━');
  for (const r of results) {
    const mark = r.ok ? 'PASS' : 'FAIL';
    console.log(`  ${mark}  ${r.label}${r.reason ? `  [${r.reason}]` : ''}`);
  }
  console.log(`\n  ${passed} passed, ${failed} failed`);

  if (failed > 0) {
    console.log('\nResult: FAIL');
    process.exit(1);
  }
  console.log('\nResult: PASS');
  process.exit(0);
}

main().catch((err) => {
  console.error(`Unhandled error: ${err.message}`);
  process.exit(1);
});
