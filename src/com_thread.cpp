
#include "./com_thread.h"
#include "./foc_thread.h"
#include "./hmi_thread.h"
#include "./lcd_thread.h"
#include <esp_task_wdt.h>
#include "./DeviceSettings.h"
#include "audio/audio.h"
// FW3: route "wifi" and "sprite" commands per shared contract
#include "./wifi_thread.h"
#include "./sprite_store.h"
// FW7: reboot + bootloader-download-mode support
#include <esp_system.h>   // esp_restart(), esp_reset_reason()
#include "esp32-hal-tinyusb.h"  // usb_persist_restart(RESTART_BOOTLOADER) — native-USB download entry

// Map esp_reset_reason() to a short label for the {"boot"} diagnostic frame.
static const char* boot_reason_str(int r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_SW:        return "SW";        // esp_restart() — our reboot / flash
        case ESP_RST_PANIC:     return "PANIC";     // crash / exception (firmware bug)
        case ESP_RST_INT_WDT:   return "INT_WDT";   // interrupt watchdog
        case ESP_RST_TASK_WDT:  return "TASK_WDT";  // a task blocked too long
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";  // supply dipped — e.g. motor torque spike
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_EXT:       return "EXT";
        default:                return "OTHER";
    }
}



// Priority 3: raised from 1 → 3 (FW7: prevent command processing starvation by
// LVGL/HMI at priority 1; still below WIFI task at 5 and FOC real-time task).
ComThread::ComThread(const uint8_t task_core) : Thread("COM", 12000, 3, task_core) {
    _q_strings_in = xQueueCreate(5, sizeof( StringMessage ));
};

ComThread::~ComThread() {

};


void ComThread::put_string_message(const StringMessage& msg){
    xQueueSend(_q_strings_in, &msg, (TickType_t)0);
};


// FW6: enqueue a heap-allocated line from wifi_thread into the net-in queue.
// Called from wifi_thread's task — must be ISR/task safe (xQueueSend is).
// wifi_thread retains ownership until this returns; on failure it must delete.
void ComThread::net_submit(String* line) {
    if (_q_net_in == nullptr || line == nullptr) {
        delete line;
        return;
    }
    if (xQueueSend(_q_net_in, &line, 0) != pdTRUE) {
        // Queue full — drop and free to avoid leak.
        delete line;
    }
}

// FW6: wifi_thread registers its outbound queue once at startup.
// After this call, emit() will also xQueueSend String* copies to out.
void ComThread::net_attach_out(QueueHandle_t out) {
    _q_net_out = out;
}

// FW6: single emit point — all outgoing JSON frames pass through here.
// Writes to Serial (owned by com_thread) and, if a net-out queue is
// attached, enqueues a heap copy for wifi_thread to forward to clients.
void ComThread::emit(const String& frame) {
    Serial.print(frame);
    if (!frame.endsWith("\n")) Serial.print('\n');

    if (_q_net_out != nullptr) {
        String* copy = new String(frame);
        if (!copy->endsWith("\n")) copy->concat('\n');
        if (xQueueSend(_q_net_out, &copy, 0) != pdTRUE) {
            delete copy; // drop if queue full; do not block com_thread
        }
    }
}


String title = "";
String data1 = "";
String data2 = "";
String data3 = "";
String data4 = "";

// FW3: message command storage (title/text/duration) for LCD_LAYOUT_MESSAGE dispatch
String msg_title = "";
String msg_text  = "";

// FW6: remote-screen LCD command — declared at file scope so processCommand()
// (a free function) can reference it alongside title/data1..4 above.
// Pointers into title/data1..4 are set once in run() before the loop.
static LcdCommand remoteLcdCommand;

// FW6: process one parsed JSON command document (static member ComThread::processCommand,
// declared in com_thread.h). Extracted so the Serial path and net-in path share one handler.

void ComThread::run() {
    // FW6: create net-in queue (String* pointers, depth 8)
    _q_net_in = xQueueCreate(8, sizeof(String*));

    // FW3: shorten readline timeout so partial frames don't block for 1 s
    Serial.setTimeout(50);

    // serial is initialized in main.cpp, but subsequently used only here
    sendAck("boot", true); // non-JSON guard: replaced bare println with JSON
    unsigned long ts = millis();
    ts_last_activity = ts;
    JsonDocument idleDoc;
    remoteLcdCommand.type = LCD_LAYOUT_DEFAULT;
    remoteLcdCommand.title = &title;
    remoteLcdCommand.data1 = &data1;
    remoteLcdCommand.data2 = &data2;
    remoteLcdCommand.data3 = &data3;
    remoteLcdCommand.data4 = &data4;
    dispatchSettings();
    dispatchLcdConfig();
    while (true) {
        // ── Serial input path (line / JSON) ──────────────────────────────────
        // Binary sprite upload is chunk-acked: {"sprite":{"op":"binchunk","len":L}}
        // is followed by exactly L raw bytes. We read them into RAM (fast, no flash
        // → the 256 B CDC FIFO can't overflow), commit the whole chunk to flash in
        // one write while the host waits, then ack. Reads and flash writes never
        // overlap, and the host has no USB OUT in flight during a flash op — so a
        // slow LittleFS write can't make macOS time out and silently drop bytes
        // (a single s.write flood lost ~5% exactly that way).
        if (Serial.available()) {
            JsonDocument doc;
            String input = Serial.readStringUntil('\n');
            DeserializationError error = deserializeJson(doc, input);
            if (error) {
                doc.clear();
                sendError("JSON parse error", error.c_str());
            } else {
                ts_last_activity = millis();
                JsonObjectConst sp = doc["sprite"];
                if (!sp.isNull() && strcmp(sp["op"] | "", "binchunk") == 0) {
                    size_t len = (size_t)(sp["len"] | 0);
                    static uint8_t cbuf[8192];
                    bool   rcv = SpriteStore::binActive();   // ram OR flash upload
                    size_t got = 0;
                    bool   ok  = false;
                    if (rcv && len > 0 && len <= sizeof(cbuf)) {
                        // Drain exactly len raw bytes into RAM (no flash here → the
                        // 256 B FIFO can't overflow); batched avail-reads keep up,
                        // yield only while waiting so a stalled host can't spin core 0.
                        unsigned long t0 = millis();
                        while (got < len && millis() - t0 < 800) {
                            size_t n = Serial.read(cbuf + got, len - got);  // bulk queue read
                            if (n) { got += n; t0 = millis(); }
                            else taskYIELD();
                        }
                        ok = (got == len) && SpriteStore::binFeed(cbuf, len);  // 1 flash write, host idle
                    }
                    if (!ok) SpriteStore::binAbort();
                    JsonDocument a;
                    a["binack"]["rd"]  = (uint32_t)(ok ? SpriteStore::binWritten() : 0);
                    a["binack"]["ok"]  = ok;
                    a["binack"]["got"] = (uint32_t)got;
                    a["binack"]["len"] = (uint32_t)len;
                    a["binack"]["rcv"] = rcv;
                    String f; serializeJson(a, f); emit(f);
                } else {
                    processCommand(doc, *this);
                }
            }
        }

        // ── Network input path (FW6) ─────────────────────────────────────────
        // Drain all pending lines submitted by wifi_thread via net_submit().
        // Parsing and handling stay in this task — no cross-task parser calls.
        if (_q_net_in != nullptr) {
            String* netLine = nullptr;
            while (xQueueReceive(_q_net_in, &netLine, 0) == pdTRUE && netLine != nullptr) {
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, *netLine);
                delete netLine;
                netLine = nullptr;
                if (error) {
                    sendError("JSON parse error", error.c_str());
                } else {
                    ts_last_activity = millis();
                    processCommand(doc, *this);
                }
            }
        }

        // Self-heal an abandoned sprite upload (e.g. the bridge restarted mid-transfer):
        // without this, binReceiving() stays true forever, parking the motor and
        // freezing the haptic loop / LED ring. Aborts after a 2 s stall.
        SpriteStore::binWatchdog();

        // HMI/LED-thread liveness telemetry (DIAGNOSTIC ONLY — does NOT restart, so it can
        // never crash-loop). The HMI loop stamps g_hmi_beat each iteration; if the LED render
        // stalls >5 s we emit one {"fault":"hmi_stall"} line so it's visible in the log. The
        // real anti-hang fix is the bounded-wait patch in the FastLED RMT driver (show() no
        // longer blocks forever), so this should never actually fire.
        {
            extern volatile uint32_t g_hmi_beat;
            static bool hmi_seen = false, fault_sent = false;
            uint32_t beat = g_hmi_beat;
            if (beat != 0) hmi_seen = true;
            if (hmi_seen && (millis() - beat) > 5000) {
                if (!fault_sent) {
                    fault_sent = true;
                    JsonDocument fd; fd["fault"] = "hmi_stall"; fd["stall_ms"] = (uint32_t)(millis() - beat);
                    String f; serializeJson(fd, f); emit(f);
                }
            } else {
                fault_sent = false;
            }
        }

        // Boot diagnostic: report why we last (re)started + the negotiated PD voltage.
        // Re-emitted for ~12 s so a host that reconnects after a crash still catches it.
        {
            static int boot_reason = -1;
            static unsigned long boot_t0 = 0, boot_last = 0;
            if (boot_reason < 0) { boot_reason = (int)esp_reset_reason(); boot_t0 = millis(); }
            if (millis() - boot_t0 < 12000 && millis() - boot_last > 2000) {
                boot_last = millis();
                JsonDocument bd; JsonObject o = bd["boot"].to<JsonObject>();
                o["reset"]  = boot_reason;
                o["reason"] = boot_reason_str(boot_reason);
                o["pd_v"]   = DeviceSettings::getInstance().getPdVoltage();
                o["budget_ma"] = DeviceSettings::getInstance().pdBudgetMa;
                String f; serializeJson(bd, f); emit(f);
            }
        }

        // send any outgoing messages
        handleMessages();

        // send key events
        handleEvents();

        // Reliable button input. The HMI/AceButton polling on the shared core 0
        // intermittently misses presses (the GPIOs respond fine — confirmed by
        // scope — but events don't fire). The com thread runs reliably (the knob
        // telemetry proves it), so edge-detect the 4 button pins here and emit
        // {"kd"}/{"ku"} directly. Pins are already INPUT_PULLUP (set by the HMI).
        {
            static const int bpins[4] = {PIN_BTN_A, PIN_BTN_B, PIN_BTN_C, PIN_BTN_D};
            static int stable[4]  = {1, 1, 1, 1};   // committed (emitted) level
            static int pend[4]    = {1, 1, 1, 1};   // candidate level being confirmed
            static unsigned long pendts[4] = {0, 0, 0, 0};
            static uint8_t bstate = 0;
            // Confirm-before-emit: a change must HOLD for 30ms before it's committed.
            // Sub-30ms excursions (contact bounce, supply-dip glitches on these
            // pullup-only pins — 40/41 JTAG, 45/46 strapping) never emit at all.
            int commit[4]; int ncommit = 0;
            for (int i = 0; i < 4; i++) {
                int v = digitalRead(bpins[i]);
                if (v != pend[i]) { pend[i] = v; pendts[i] = millis(); }
                if (pend[i] != stable[i] && millis() - pendts[i] >= 30)
                    commit[ncommit++] = i;
            }
            // Power-glitch veto: a rail sag flips several pins in the same tick —
            // real fingers don't hit 3+ keys within 10ms. Absorb silently.
            if (ncommit >= 3) {
                for (int n = 0; n < ncommit; n++) stable[commit[n]] = pend[commit[n]];
            } else {
                for (int n = 0; n < ncommit; n++) {
                    int i = commit[n];
                    stable[i] = pend[i];
                    JsonDocument ed;
                    if (stable[i] == 0) { bstate |= (1 << i);  ed["ks"] = bstate; ed["kd"] = i; }
                    else                { bstate &= ~(1 << i); ed["ks"] = bstate; ed["ku"] = i; }
                    String f; serializeJson(ed, f); emit(f);
                    ts_last_activity = millis();
                }
            }
        }

        // send idle message
        unsigned long now = millis();
        if (now-ts>1000 && now-ts_last_activity>global_idle_timeout && global_idle_timeout>0) {
          ts = now;
          idleDoc["idle"] = now-ts_last_activity;
          String frame;
          serializeJson(idleDoc, frame);
          emit(frame);
        }
        if (now-ts_last_activity<=global_idle_timeout || global_idle_timeout==0)
          global_sleep_flag = false;
        else
          global_sleep_flag = true;

        vTaskDelay(10); // give other threads a chance to run...
    }

};


// ── Command dispatcher ────────────────────────────────────────────────────────
// All JSON handling lives here; called for both Serial and network lines.
void ComThread::processCommand(JsonDocument& doc, ComThread& self) {
    JsonVariant profile = doc["profile"];
    JsonVariant v = doc["updates"];
    if (profile.is<String>() || v!=nullptr) { // haptic command
      self.handleProfileCommand(profile, v);
    }
    if (!doc["current"].isNull()) { // set current profile
      String cur = doc["current"].as<String>();
      self.setCurrentProfile(cur);
      // FW3: emit ACK for set-current-profile (was silently missing)
      self.sendAck("current", true);
    }
    if (!doc["R"].isNull()) { // motor command
      // FW3: allocate only if queue has space to avoid a heap leak when full.
      const char* cmd = doc["R"];
      String* cmdstr = new String(cmd);
      foc_thread.put_motor_command(cmdstr);
      // NOTE: if foc_thread queue is full cmdstr is silently dropped (FW1 to fix)
    }
    // FW3: "message" command — protocol sends an object {title,text,duration}
    v = doc["message"];
    if (v.is<JsonObject>()) {
      JsonObject mo = v.as<JsonObject>();
      msg_title = mo["title"].is<String>() ? mo["title"].as<String>() : "";
      msg_text  = mo["text"].is<String>()  ? mo["text"].as<String>()  : "";
      // duration field reserved for future timed-dismiss support
      LcdCommand msgCmd;
      msgCmd.type  = LCD_LAYOUT_MESSAGE;
      msgCmd.title = &msg_title;
      msgCmd.data1 = &msg_text;
      msgCmd.data2 = nullptr;
      msgCmd.data3 = nullptr;
      msgCmd.data4 = nullptr;
      lcd_thread.put_lcd_command(msgCmd);
      self.sendAck("message", true);
    }
    v = doc["screen"];
    if (v!=nullptr) {
      if (v["title"].is<String>()) title = v["title"].as<String>(); else title = "";
      if (v["data1"].is<String>()) data1 = v["data1"].as<String>(); else data1 = "";
      if (v["data2"].is<String>()) data2 = v["data2"].as<String>(); else data2 = "";
      if (v["data3"].is<String>()) data3 = v["data3"].as<String>(); else data3 = "";
      if (v["data4"].is<String>()) data4 = v["data4"].as<String>(); else data4 = "";
      lcd_thread.put_lcd_command(remoteLcdCommand);
    }
    v = doc["recalibrate"];
    if (v.is<bool>()) { // recalibrate motor
      if (v.as<bool>()) {
        // FW3: replaced bare Serial.println() with JSON debug message
        StringMessage dbg(new String("Recalibrating motor"), STRING_MESSAGE_DEBUG);
        self.put_string_message(dbg);
        foc_thread.put_motor_command(new String("129=1"));
        self.sendAck("recalibrate", true);
      }
    }
    v = doc["profiles"];
    if (v!=nullptr) { // list or reorder profiles
      self.handleProfilesCommand(v);
    }
    v = doc["settings"];
    if (v!=nullptr) { // get or set settings
      self.handleSettingsCommand(v);
    }
    if (doc["save"]) { // save settings and profiles to SPIFFS
      if (doc["save"].as<bool>()==true) {
        DeviceSettings::getInstance().toSPIFFS();
        HapticProfileManager::getInstance().toSPIFFS();
        DeviceSettings::getInstance().storeCurrentProfile(HapticProfileManager::getInstance().getCurrentProfile()->profile_name);
        // FW3: emit proper JSON {"saved":true} and ACK
        JsonDocument reply;
        reply["saved"] = true;
        String frame;
        serializeJson(reply, frame);
        self.emit(frame);
        self.sendAck("save", true);
      }
    }
    if (doc["load"]) { // load settings and profiles from SPIFFS
      if (doc["load"].as<bool>()==true) {
        // first nuke existing profiles
        for (int i=0; i<MAX_PROFILES; i++) {
          HapticProfile* p = HapticProfileManager::getInstance()[i];
          if (p!=nullptr) {
            String name = p->profile_name;
            HapticProfileManager::getInstance().remove(name);
          }
        }
        DeviceSettings::getInstance().fromSPIFFS();
        HapticProfileManager::getInstance().fromSPIFFS();
        HapticProfileManager::getInstance().setCurrentProfile(DeviceSettings::getInstance().loadCurrentProfile());
        self.dispatchSettings();
        self.dispatchHapticConfig();
        self.dispatchAudioConfig();
        self.dispatchLedConfig();
        self.dispatchHmiConfig();
        self.dispatchLcdConfig();
        self.sendAck("load", true);
      }
    }

    // FW3: route "wifi" command — update DeviceSettings then apply to wifi_thread.
    // Use thread-safe NVS-persisting setters (Fix 1: was bare public field writes).
    v = doc["wifi"];
    if (v.is<JsonObject>()) {
      JsonObject wobj = v.as<JsonObject>();
      DeviceSettings& ds = DeviceSettings::getInstance();
      if (wobj["ssid"].is<String>())    ds.setWifiSsid(wobj["ssid"].as<String>());
      if (wobj["password"].is<String>()) ds.setWifiPassword(wobj["password"].as<String>());
      if (wobj["enabled"].is<bool>())    ds.setWifiEnabled(wobj["enabled"].as<bool>());
      wifi_thread.apply_settings();
      self.sendAck("wifi", true);
    }

    // FW3: route "sprite" command — delegate to SpriteStore per contract.
    // Fix 6: special-case op=="list" to emit {"sprites":[...]} JSON frame
    v = doc["sprite"];
    if (v.is<JsonObject>()) {
      JsonObjectConst scmd = v.as<JsonObjectConst>();
      const char* spOp = scmd["op"] | "";
      if (strcmp(spOp, "list") == 0) {
        String listResult;
        bool ok = SpriteStore::handleCommand(scmd, listResult);
        if (ok) {
          JsonDocument listDoc;
          SpriteStore::listJson(listDoc["sprites"].to<JsonArray>());
          String frame;
          serializeJson(listDoc, frame);
          self.emit(frame);
        }
        self.sendAck("sprite", ok, ok ? nullptr : listResult.c_str());
      } else {
        String spriteErr;
        bool ok = SpriteStore::handleCommand(scmd, spriteErr);
        self.sendAck("sprite", ok, ok ? nullptr : spriteErr.c_str());
      }
    }

    // PD power status query — {"pd":"?"} or {"pd":"status"}
    // Reply: {"pd":{"voltage":<V>,"current":<A>,"power":<W>,"source":"5V"|"PD"}}
    // voltage = last negotiated/clamped value from DeviceSettings.pdVoltage
    // current = value read from STUSB4500 for the active PDO during init_pd()
    // power   = voltage * current (W)
    // source  = "PD" when voltage > 5.0 V, else "5V"
    v = doc["pd"];
    if (v.is<String>()) {
      String pdCmd = v.as<String>();
      if (pdCmd == "?" || pdCmd == "status") {
        float voltage = DeviceSettings::getInstance().getPdVoltage();
        float current = hmi_thread.getPdCurrent();
        float power   = voltage * current;
        const char* source = (voltage > 5.05f) ? "PD" : "5V";
        JsonDocument pdDoc;
        JsonObject pdObj = pdDoc["pd"].to<JsonObject>();
        pdObj["voltage"] = voltage;
        pdObj["current"] = current;
        pdObj["power"]   = power;
        pdObj["source"]  = source;
        // Raw RDO diagnostic — verify the ACTUAL negotiated PDO instead of trusting one
        // unverified byte order. rdo = the 4 raw bytes of reg 0x91; sel_hi=(b3>>4)&7 (LE
        // MSB), sel_lo=(b0>>4)&7 (alt). The real object position is 1 (=5V) or 2 (=9V) —
        // whichever decode lands in [1,2] is correct; pdo_sel/v_real are that best guess.
        extern volatile uint8_t g_pd_rdo[4];
        extern volatile uint8_t g_pd_sel_hi, g_pd_sel_lo;
        extern volatile uint8_t g_pd_cc_adv;
        char rdohex[12];
        snprintf(rdohex, sizeof(rdohex), "%02X%02X%02X%02X",
                 g_pd_rdo[0], g_pd_rdo[1], g_pd_rdo[2], g_pd_rdo[3]);
        uint8_t real_sel = (g_pd_sel_hi >= 1 && g_pd_sel_hi <= 2) ? g_pd_sel_hi
                         : (g_pd_sel_lo >= 1 && g_pd_sel_lo <= 2) ? g_pd_sel_lo : 1;
        pdObj["rdo"]     = rdohex;
        pdObj["sel_hi"]  = g_pd_sel_hi;
        pdObj["sel_lo"]  = g_pd_sel_lo;
        pdObj["pdo_sel"] = real_sel;
        pdObj["v_real"]  = (real_sel == 2) ? 9.0 : 5.0;
        pdObj["cc_adv"]  = g_pd_cc_adv;   // Type-C advert: 0=default 1=1.5A 2=3.0A
        pdObj["budget_ma"] = DeviceSettings::getInstance().pdBudgetMa;
        String frame;
        serializeJson(pdDoc, frame);
        self.emit(frame);
      }
    }

    // FW7: net diagnostics — {"net":"?"} or {"net":"status"}
    // Reply: {"net":{"rssi":<dBm>,"ip":"<ip>","ps":"NONE|MIN|MAX","heap":<bytes>,"uptime":<ms>,"clients":<n>}}
    // In no-WiFi builds wifi_thread.netStatusJson() returns {"net":{"enabled":false}}.
    v = doc["net"];
    if (v.is<String>()) {
      String netCmd = v.as<String>();
      if (netCmd == "?" || netCmd == "status") {
        String frame = wifi_thread.netStatusJson();
        self.emit(frame);
      }
    }

    // Live motor phase-current-limit override — {"climit":0.9}. Runtime-only (reverts
    // to the supply-derived tier on reboot). For binary-searching the stable 5V ceiling
    // under real hammering without a flash cycle per guess.
    v = doc["climit"];
    if (v.is<float>() || v.is<int>()) {
      foc_thread.set_current_limit(v.as<float>());
      JsonDocument cd; cd["climit"] = foc_thread.get_current_limit();
      String f; serializeJson(cd, f); self.emit(f);
    }

    // FW7: reboot commands.
    // {"reboot":true}           → ACK then normal warm restart via esp_restart().
    // {"reboot":"bootloader"}   → ACK then ROM download-mode restart (ESP32-S3).
    //   The RTC_CNTL_OPTION1_REG bit 0 (RTC_CNTL_FORCE_DOWNLOAD_BOOT) tells the
    //   ROM to enter UART download mode instead of booting the flash image.
    //   This is the same mechanism used by esptool.py after the DTR/RTS dance;
    //   doing it in firmware means the host only needs a plain USB-SERIAL open
    //   (no control-line toggling required).
    v = doc["reboot"];
    if (!v.isNull()) {
      if (v.is<bool>() && v.as<bool>() == true) {
        self.sendAck("reboot", true);
        vTaskDelay(pdMS_TO_TICKS(20)); // let ACK flush over Serial/TCP before reset
        esp_restart();
      } else if (v.is<String>() && v.as<String>() == "bootloader") {
        self.sendAck("reboot", true);
        vTaskDelay(pdMS_TO_TICKS(20)); // let ACK flush before reset
        // Restart into ROM download mode with USB kept enumerated. This is the
        // exact call the Arduino USB-CDC stack makes on esptool's DTR/RTS dance
        // (USBCDC.cpp). It sets the USB persist flags so the native USB survives
        // the reset and re-enumerates as the ROM downloader — REG_WRITE +
        // esp_restart drops USB and wedges the device on native-USB S3 boards.
        usb_persist_restart(RESTART_BOOTLOADER);
      }
      // Any other value for "reboot" is silently ignored — avoids accidental
      // reboots from future protocol extensions that happen to use the same key.
    }

    // FW8: {"ring":{"primary":<u32>,"secondary":<u32>,"mode":<0-3>}} — transient
    // album-color LED glow. Pushes a one-off led_config to the HMI thread without
    // touching the saved profile (it reasserts on the next profile/settings change).
    v = doc["ring"];
    if (!v.isNull()) {
      ledConfig cfg = HapticProfileManager::getInstance().getCurrentProfile()->led_config;
      cfg.primary_col   = v["primary"]   | cfg.primary_col;
      cfg.secondary_col = v["secondary"] | cfg.secondary_col;
      cfg.led_mode      = v["mode"]      | cfg.led_mode;
      hmi_thread.put_led_config(cfg);
      DeviceSettings::getInstance().musicColor = cfg.primary_col;  // album color for LCD overlays
      self.sendAck("ring", true);
    }

    // {"seek":{"pos":<0.0..1.0>}} — song progress for the on-screen seek arc
    // (drawn over the cover by the LCD thread in the album color). pos<0 hides it.
    v = doc["seek"];
    if (!v.isNull()) {
      float pos = v["pos"] | -1.0f;
      DeviceSettings::getInstance().seekPermille =
          pos < 0 ? -1 : (int32_t)(constrain(pos, 0.0f, 1.0f) * 1000.0f);
      self.sendAck("seek", true);
    }
}


// FW3: helper — emit {"ack":"<cmd>","ok":true/false} (and optional "error") for every mutating command
void ComThread::sendAck(const char* cmd, bool ok, const char* errMsg) {
    JsonDocument doc;
    doc["ack"] = cmd;
    doc["ok"]  = ok;
    if (!ok && errMsg != nullptr && errMsg[0] != '\0')
      doc["error"] = errMsg;
    String frame;
    serializeJson(doc, frame);
    emit(frame);
}




void ComThread::handleEvents() {
    // Buttons are now emitted by the com thread's reliable edge-detect (see run());
    // just drain the HMI key queue so it doesn't back up — don't double-emit here.
    { KeyEvt drainEvt; while (hmi_thread.get_key_event(&drainEvt)) {} }

    // COALESCE + THROTTLE the position telemetry. The old code emitted one JSON frame per
    // FOC angle event — hundreds per second while spinning fast. Each frame is a USB-CDC
    // write whose ISR runs on core 0 and preempts the FastLED RMT refill ISR; under that
    // storm the RMT done-interrupt is missed and the LED thread hangs in show() (knob-turn
    // freeze) or the stuck ISR trips the interrupt watchdog (INT_WDT reset). Draining the
    // whole queue but emitting only the NEWEST position at <=50 Hz is lossless for volume
    // and removes the storm at the source.
    AngleEvt latest; bool hadEvent = false;
    { AngleEvt e; while (foc_thread.get_angle_event(&e)) { latest = e; hadEvent = true; } }
    if (!hadEvent) return;
    ts_last_activity = millis();   // count knob motion as activity even on throttled ticks
    static unsigned long last_emit = 0;
    if (millis() - last_emit < 20) return;   // <=50 Hz
    last_emit = millis();
    JsonDocument eventDoc;
    float a = foc_thread.get_motor_angle();
    float v_rad = foc_thread.get_motor_velocity();
    int32_t turns = (int32_t)(a / (2.0f * PI));
    eventDoc["p"] = latest.cur_pos;   // legacy uint16 position (host maps this to volume)
    eventDoc["a"] = a;
    eventDoc["t"] = turns;
    eventDoc["v"] = v_rad;
    String frame;
    serializeJson(eventDoc, frame);
    emit(frame);
};




void ComThread::handleSettingsCommand(JsonVariant s) {
  if (s.isNull()) return;
  if (s.is<String>()) {
    // send the settings
    JsonDocument doc;
    JsonObject obj = doc["settings"].to<JsonObject>();
    DeviceSettings::getInstance().toJSON(obj);
    String frame;
    serializeJson(doc, frame);
    emit(frame);
  }
  if (s.is<JsonObject>()) {
    JsonObject obj = s.as<JsonObject>();
    DeviceSettings::getInstance() = obj;
    dispatchSettings();
    sendAck("settings", true);
  }
};



void ComThread::handleMessages() {
  StringMessage incoming;
  JsonDocument doc;
  String pName = "";
  if (xQueueReceive(_q_strings_in, &incoming, (TickType_t)0)) {
    HapticProfileManager& pm = HapticProfileManager::getInstance();
    bool sendDoc = false;
    switch(incoming.type) {
      case STRING_MESSAGE_DEBUG:
        if (incoming.message!=nullptr) {
          doc["debug"] = *incoming.message;
          sendDoc = true;
        }
        break;
      case STRING_MESSAGE_ERROR:
        if (incoming.message!=nullptr) {
          doc["error"] = *incoming.message;
          sendDoc = true;
        }
        break;
      case STRING_MESSAGE_MOTOR:
        if (incoming.message!=nullptr) {
          doc["r"] = *incoming.message;
          sendDoc = true;
        }
        break;
      case STRING_MESSAGE_PROFILE:
        if (incoming.message!=nullptr) {
          String s = *incoming.message;
          setCurrentProfile(s);
          doc["current"] = s;
          sendDoc = true;
        }
        break;
      case STRING_MESSAGE_NEXT_PROFILE:
        pName = pm.getNextProfileName();
        if (pName!="") {
          setCurrentProfile(pName);
          doc["current"] = pName;
          sendDoc = true;
        }
        break;
      case STRING_MESSAGE_PREV_PROFILE:
        pName = pm.getPrevProfileName();
        if (pName!="")  {
          setCurrentProfile(pName);
          doc["current"] = pName;
          sendDoc = true;
        }
        break;
      default:
        if (incoming.message!=nullptr) {
          // FW3: replaced bare Serial.println() to avoid injecting non-JSON; wrap in debug
          doc["debug"] = *incoming.message;
          sendDoc = true;
        }
        break;
    }
    if (sendDoc) {
      String frame;
      serializeJson(doc, frame);
      emit(frame);
    }
    if (incoming.message!=nullptr) {
      delete incoming.message;
    }
  }
};




void ComThread::handleProfilesCommand(JsonVariant p) {
  if (p.isNull()) return;
  HapticProfileManager& pm = HapticProfileManager::getInstance();
  if (p.is<String>()) {
    String s = p.as<String>();
    if (s=="#all") {
      // send the list of all profile names
      JsonDocument doc;
      JsonArray arr = doc["profiles"].to<JsonArray>();
      for (int i=0; i<pm.size(); i++) {
        arr.add(pm[i]->profile_name);
      }
      doc["current"] = pm.getCurrentProfile()->profile_name;
      String frame;
      serializeJson(doc, frame);
      emit(frame);
    }
  }
  if (p.is<JsonArray>()) {
    JsonArray arr = p.as<JsonArray>();

    // FW3: real reorder — delete profiles absent from the incoming list, then
    // rearrange profiles[] to match the requested order using swap-sort.

    // Pass 1: delete profiles not in the new list
    for (int i=0; i<MAX_PROFILES; i++) {
      HapticProfile* prof = pm[i];
      if (prof==nullptr) continue;
      bool found = false;
      for (JsonVariant item : arr) {
        if (item.is<String>() && item.as<String>()==prof->profile_name) {
          found = true;
          break;
        }
      }
      if (!found) {
        pm.remove(prof->profile_name);
      }
    }

    // Pass 2: sort profiles[] to match the requested order using selection-sort.
    int targetIdx = 0;
    for (int arrIdx = 0; arrIdx < (int)arr.size() && targetIdx < MAX_PROFILES; arrIdx++) {
      if (!arr[arrIdx].is<String>()) continue;
      String wantedName = arr[arrIdx].as<String>();
      for (int scanIdx = targetIdx; scanIdx < MAX_PROFILES; scanIdx++) {
        HapticProfile* scanProf = pm[scanIdx]; // nullptr if empty
        if (scanProf != nullptr && scanProf->profile_name == wantedName) {
          if (scanIdx != targetIdx) {
            HapticProfile* tgtProf = pm[targetIdx]; // may be nullptr (empty slot)
            if (tgtProf != nullptr) {
              HapticProfile tmp = *tgtProf;
              *tgtProf          = *scanProf;
              *scanProf         = tmp;
            }
          }
          targetIdx++;
          break;
        }
      }
    }

    // FW3: re-anchor manager's current_profile pointer after struct-level swap
    String curName = pm.getCurrentProfile() ? pm.getCurrentProfile()->profile_name : "";
    if (curName != "") pm.setCurrentProfile(curName);

    sendAck("profiles", true);
  }
};




bool ComThread::isProfileNameOk(String& name){
  if (name==nullptr)
    return false;
  if (name.length()<1 || name.length()>20)
    return false;
  // TODO check for invalid characters
  return true;
};




void ComThread::sendError(String& error, String* msg){
      JsonDocument doc;
      doc["error"] = error;
      if (msg!=nullptr)
        doc["msg"] = *msg;
      String frame;
      serializeJson(doc, frame);
      emit(frame);
};
void ComThread::sendError(String& error, String& msg){
  sendError(error, &msg);
};
void ComThread::sendError(const char* error, String& msg) {
  String e = error;
  sendError(e, &msg);
};
void ComThread::sendError(const char* error, const char* msg) {
  String e = error;
  if (msg==nullptr) {
    sendError(e);
  }
  else {
    String m = msg;
    sendError(e, m);
  }
};



void ComThread::handleProfileCommand(JsonVariant profile, JsonVariant updates) {
  if (profile.isNull()&&updates.isNull()) return;
  HapticProfileManager& pm = HapticProfileManager::getInstance();
  HapticProfile* p;
  if (profile.is<String>()) {
    String pname = profile.as<String>();
    if (!isProfileNameOk(pname)) {
      sendError("Invalid profile name", pname);
      sendAck("profile", false, "Invalid profile name");
      return;
    }
    p = pm[pname];
    if (p==nullptr)
      p = pm.add(pname);
    if (p==nullptr) {
      sendError("Cannot add another profile");
      sendAck("profile", false, "Cannot add another profile");
      return;
    }
  }
  else
    p = pm.getCurrentProfile();

  if (updates.isNull()) {
    JsonDocument doc, profileDoc;
    // send the selected profile
    JsonObject obj = doc["profile"].to<JsonObject>();
    p->toJSON(obj);
    String frame;
    serializeJson(doc, frame);
    emit(frame);
    // read-only query — no ACK required
  }
  else if (updates.is<JsonObject>()) {
    JsonObject obj = updates.as<JsonObject>();
    if (obj["name"].is<String>() && obj["name"].as<String>()!=p->profile_name) {
      String new_name = obj["name"].as<String>();
      if (pm[new_name]!=nullptr) {
        sendError("Profile name already exists");
        sendAck("profile", false, "Profile name already exists");
        return;
      }
    }
    // update the profile
    *p = obj; // assigning the JSON object to the profile will update the profile's fields
    if (p==pm.getCurrentProfile()) {
      dispatchHapticConfig();
      dispatchAudioConfig();
      dispatchLedConfig();
      dispatchHmiConfig();
      dispatchLcdConfig();
    }
    sendAck("profile", true);
  }
};


void ComThread::setCurrentProfile(String name){
  HapticProfile* profile = HapticProfileManager::getInstance().setCurrentProfile(name);
  if (profile!=nullptr) { // if we changed profile, send the new haptic config to the FOC thread
    dispatchHapticConfig();
    dispatchLedConfig();
    dispatchHmiConfig();
    dispatchAudioConfig();
    dispatchLcdConfig();
  }
};


void ComThread::dispatchLedConfig() {
    ledConfig copy = HapticProfileManager::getInstance().getCurrentProfile()->led_config;
    if (copy.led_brightness>DeviceSettings::getInstance().ledMaxBrightness)
      copy.led_brightness = DeviceSettings::getInstance().ledMaxBrightness;
    hmi_thread.put_led_config(copy);
    //hmi_thread.put_key_config(HapticProfileManager::getInstance().getCurrentProfile()->key_config);
};


void ComThread::dispatchHmiConfig() {
    hmi_thread.put_hmi_config(HapticProfileManager::getInstance().getCurrentProfile()->hmi_config);
};

void ComThread::dispatchHapticConfig() {
  if (HapticProfileManager::getInstance().getCurrentProfile()->hmi_config.knob.num>0)
    foc_thread.put_haptic_config(HapticProfileManager::getInstance().getCurrentProfile()->hmi_config.knob.values[0].haptic);
};

void ComThread::dispatchSettings() {
    DeviceSettings& ds = DeviceSettings::getInstance();
    HmiDeviceSettings hmiSettings{
      .ledMaxBrightness = ds.ledMaxBrightness,
      .deviceOrientation = ds.deviceOrientation,
      .midiUsb = ds.midiUsb,
      .midi2 = ds.midi2,
      .midi_sysex_id = ds.midi_sysex_id
    };
    hmi_thread.put_settings(hmiSettings);
    global_idle_timeout = ds.idleTimeout;
};


void ComThread::dispatchAudioConfig() {
#if NANO_AUDIO
    audioPlayer.put_audio_config(HapticProfileManager::getInstance().getCurrentProfile()->audio_config);
#endif
};


String autoDescription = "";

String ComThread::generateDescription(HapticProfile& curr) {
  String desc = "";
  if (curr.hmi_config.knob.num>0) {
    switch (curr.hmi_config.knob.values[0].type) {
      case knobValueType::KV_MIDI:
        desc = "MIDI CC ";
        desc += curr.hmi_config.knob.values[0].midi.cc;
        break;
      case knobValueType::KV_GAMEPAD:
        desc = "Gamepad";
        break;
      case knobValueType::KV_MOUSE:
        desc = "Mouse";
        break;
      case knobValueType::KV_ACTIONS:
        desc = "Actions";
        break;
      case knobValueType::KV_DEVICE_PROFILES:
        desc = "Profiles";
        break;
      default:
        desc = "?";
        break;
    }
  }
  else {
    desc = "No Mapping";
  }
  return desc;
};

void ComThread::dispatchLcdConfig() {
    HapticProfile* curr = HapticProfileManager::getInstance().getCurrentProfile();
    LcdCommand cmd;
    cmd.type = LCD_LAYOUT_DEFAULT;
    cmd.title = &curr->profile_name;
    if (curr->profile_desc.length()>0)
      cmd.data1 = &curr->profile_desc;
    else {
      autoDescription = generateDescription(*curr);
      cmd.data1 = &autoDescription;
    }
    cmd.data2 = nullptr;
    cmd.data3 = nullptr;
    cmd.data4 = nullptr;
    lcd_thread.put_lcd_command(cmd);
};
