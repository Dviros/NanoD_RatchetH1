/*
 * sprite_store.cpp — FW7
 *
 * Chunked sprite upload, LittleFS storage, CRC validation, and LVGL
 * filesystem driver registration.
 *
 * Storage layout:
 *   /sprites/<name>   — raw image bytes (BMP or PNG depending on decoder)
 *
 * Limits:
 *   MAX_SPRITE_SIZE   — reject any single upload larger than this
 *   MAX_SPRITES_BYTES — reject an upload that would push total beyond this
 *   SPRITE_DIR        — LittleFS directory
 *
 * LVGL FS driver letter: 'L'
 *   Paths visible to LVGL: "L:/sprites/<name>"
 */

#include "sprite_store.h"
#include "DeviceSettings.h"
#include "crc32_util.h"   // nano_crc32_update — shared with native test suite

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <lvgl.h>

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
static constexpr size_t  MAX_SPRITE_SIZE   = 64  * 1024;  // 64 KB per static sprite (bmp/png)
static constexpr size_t  MAX_GIF_SIZE      = 200 * 1024;  // 200 KB per GIF sprite
static constexpr size_t  MAX_RGB565_SIZE   = 128 * 1024;  // 240x240x2 = 115 KB raw frame + margin
static constexpr size_t  MAX_SPRITES_BYTES = 768 * 1024;  // 768 KB total (raised from 512 KB for GIF)
static constexpr uint8_t MAX_SPRITES       = 16;
// GIF logical-screen cap. gifdec allocates one contiguous 5*w*h block; 240x240
// (the panel size) = 281 KB, which fits the 2 MB LVGL pool with margin. Anything
// larger is rejected at upload so it can never OOM-hang the LCD. Bytes-size alone
// is insufficient: a tiny highly-compressed GIF can still declare huge dimensions.
static constexpr uint16_t MAX_GIF_DIM      = 240;
static const char*       SPRITE_DIR        = "/sprites";

// ---------------------------------------------------------------------------
// Upload state machine
// ---------------------------------------------------------------------------
namespace {

struct UploadCtx {
    bool     active   = false;
    bool     binary   = false; // raw-byte fast path (binbegin/binend) vs base64 data
    String   name;
    size_t   expected = 0;   // declared size in "begin"
    size_t   written  = 0;   // bytes written so far
    int      next_seq = 0;   // expected sequence number
    uint32_t crc_run  = 0;   // running CRC-32 accumulator
    File     file;
};

UploadCtx g_upload;

// CRC-32 is now provided by include/crc32_util.h (nano_crc32_update).
// The alias keeps the call-sites below unchanged.
static inline uint32_t crc32_update(uint32_t crc, const uint8_t* buf, size_t len) {
    return nano_crc32_update(crc, buf, len);
}

// ---------------------------------------------------------------------------
// Total bytes currently stored under SPRITE_DIR.
// ---------------------------------------------------------------------------
static size_t total_stored_bytes() {
    size_t total = 0;
    File dir = LittleFS.open(SPRITE_DIR, "r");
    if (!dir || !dir.isDirectory()) return 0;
    File f = dir.openNextFile();
    while (f) {
        if (!f.isDirectory()) total += f.size();
        f.close();
        f = dir.openNextFile();
    }
    dir.close();
    return total;
}

// ---------------------------------------------------------------------------
// Count sprites already stored.
// ---------------------------------------------------------------------------
static size_t count_sprites() {
    size_t n = 0;
    File dir = LittleFS.open(SPRITE_DIR, "r");
    if (!dir || !dir.isDirectory()) return 0;
    File f = dir.openNextFile();
    while (f) {
        if (!f.isDirectory()) n++;
        f.close();
        f = dir.openNextFile();
    }
    dir.close();
    return n;
}

// ---------------------------------------------------------------------------
// Abort any in-progress upload, closing + removing the partial file.
// ---------------------------------------------------------------------------
static void abort_upload() {
    if (g_upload.active) {
        if (g_upload.file) {
            g_upload.file.close();
            LittleFS.remove(SpriteStore::pathFor(g_upload.name));
        }
        g_upload.active = false;
    }
}

// ---------------------------------------------------------------------------
// Minimal base64 decoder — no external dep.
// Returns decoded length, or -1 on error.
// ---------------------------------------------------------------------------
static const int8_t B64_TBL[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

static int b64_decode(const char* src, size_t src_len, uint8_t* dst, size_t dst_cap) {
    size_t out = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < src_len; i++) {
        uint8_t c = (uint8_t)src[i];
        if (c == '=') break;
        int8_t v = B64_TBL[c];
        if (v < 0) continue; // skip whitespace
        acc = (acc << 6) | (uint8_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out >= dst_cap) return -1; // overflow
            dst[out++] = (uint8_t)(acc >> bits);
        }
    }
    return (int)out;
}

// ---------------------------------------------------------------------------
// Sub-command handlers
// ---------------------------------------------------------------------------

// Returns true if the filename ends with ".gif" (case-insensitive).
static bool is_gif_name(const char* name) {
    size_t n = strlen(name);
    if (n < 4) return false;
    const char* ext = name + n - 4;
    return (ext[0] == '.' &&
            (ext[1] == 'g' || ext[1] == 'G') &&
            (ext[2] == 'i' || ext[2] == 'I') &&
            (ext[3] == 'f' || ext[3] == 'F'));
}

// Raw full-screen RGB565 frame (.rgb565): pre-rendered by the host, streamed
// straight to the LCD (no decode, no RAM frame) — the no-PSRAM artwork path.
static bool is_raw565_name(const char* name) {
    size_t n = strlen(name);
    return n >= 7 && strcasecmp(name + n - 7, ".rgb565") == 0;
}

// Per-type upload size cap.
static size_t effective_max(const char* name) {
    if (is_gif_name(name))    return MAX_GIF_SIZE;
    if (is_raw565_name(name)) return MAX_RGB565_SIZE;
    return MAX_SPRITE_SIZE;
}

static bool handle_begin(JsonObjectConst cmd, String& err) {
    // Abort any prior stale upload.
    abort_upload();

    const char* name = cmd["name"];
    size_t size = cmd["size"] | 0u;

    if (!name || strlen(name) == 0 || strlen(name) > 32) {
        err = "invalid name"; return false;
    }

    // Choose size limit based on file type: GIFs and raw RGB565 frames may be larger.
    if (size == 0 || size > effective_max(name)) {
        err = "invalid size"; return false;
    }
    // Reject if adding would overflow total budget.
    // (Existing file with same name counts as replaced — subtract its size.)
    String path = SpriteStore::pathFor(String(name));
    size_t existing = 0;
    if (LittleFS.exists(path.c_str())) {
        File ex = LittleFS.open(path.c_str(), "r");
        if (ex) { existing = ex.size(); ex.close(); }
    }
    if (count_sprites() >= MAX_SPRITES && existing == 0) {
        err = "sprite slot limit reached"; return false;
    }
    if (total_stored_bytes() - existing + size > MAX_SPRITES_BYTES) {
        err = "storage quota exceeded"; return false;
    }

    g_upload.file = LittleFS.open(path.c_str(), "w");
    if (!g_upload.file) { err = "cannot open file"; return false; }

    g_upload.active   = true;
    g_upload.binary   = false;
    g_upload.name     = String(name);
    g_upload.expected = size;
    g_upload.written  = 0;
    g_upload.next_seq = 0;
    g_upload.crc_run  = 0;
    return true;
}

// ── Binary fast path: binbegin opens the file (reusing handle_begin), then raw
//    bytes stream straight to flash via binFeed (no base64/JSON per chunk), then
//    binend validates via handle_end. ~10x faster than the base64 data path.

static bool handle_binbegin(JsonObjectConst cmd, String& err) {
    if (!handle_begin(cmd, err)) return false;
    g_upload.binary = true;
    return true;
}

// binReceiving / binRemaining / binFeed are defined in the SpriteStore namespace
// block below (they're public; the helpers above are file-local).

static bool handle_data(JsonObjectConst cmd, String& err) {
    if (!g_upload.active) { err = "no active upload"; return false; }

    int seq = cmd["seq"] | -1;
    if (seq != g_upload.next_seq) {
        abort_upload();
        err = "sequence error"; return false;
    }

    const char* b64 = cmd["data"] | "";
    size_t b64_len = strlen(b64);

    // Worst-case decoded length.
    size_t max_decoded = (b64_len / 4 + 1) * 3 + 4;
    uint8_t* buf = (uint8_t*)malloc(max_decoded);
    if (!buf) { abort_upload(); err = "OOM"; return false; }

    int dec = b64_decode(b64, b64_len, buf, max_decoded);
    if (dec < 0) {
        free(buf); abort_upload(); err = "base64 decode error"; return false;
    }

    size_t upload_max = effective_max(g_upload.name.c_str());
    if (g_upload.written + (size_t)dec > upload_max) {
        free(buf); abort_upload(); err = "size overflow"; return false;
    }

    // Fix 5: check File::write() return; if fewer bytes written than decoded,
    // the disk is full — abort and report rather than silently corrupt the file.
    size_t wrote = g_upload.file.write(buf, (size_t)dec);
    if (wrote != (size_t)dec) {
        free(buf);
        abort_upload();
        err = "write failed (disk full?)";
        return false;
    }
    g_upload.crc_run = crc32_update(g_upload.crc_run, buf, (size_t)dec);
    g_upload.written += (size_t)dec;
    g_upload.next_seq++;

    free(buf);
    return true;
}

static bool handle_end(JsonObjectConst cmd, String& err) {
    if (!g_upload.active) { err = "no active upload"; return false; }

    uint32_t expected_crc = cmd["crc32"] | 0u;

    g_upload.file.flush();
    g_upload.file.close();

    if (g_upload.written != g_upload.expected) {
        LittleFS.remove(SpriteStore::pathFor(g_upload.name).c_str());
        g_upload.active = false;
        err = "size mismatch (got " + String(g_upload.written) +
              ", expected " + String(g_upload.expected) + ")";
        return false;
    }
    if (g_upload.crc_run != expected_crc) {
        LittleFS.remove(SpriteStore::pathFor(g_upload.name).c_str());
        g_upload.active = false;
        err = "CRC mismatch";
        return false;
    }

    // GIF safety gate: verify magic + cap dimensions so the decoder's single
    // 5*w*h allocation can never OOM-hang the LCD. Reject + delete unsafe GIFs.
    if (is_gif_name(g_upload.name.c_str())) {
        String gerr;
        if (!SpriteStore::gifRenderable(g_upload.name, gerr)) {
            LittleFS.remove(SpriteStore::pathFor(g_upload.name).c_str());
            g_upload.active = false;
            err = gerr;
            return false;
        }
    }

    g_upload.active = false;
    return true;
}

static bool handle_delete(JsonObjectConst cmd, String& err) {
    const char* name = cmd["name"];
    if (!name || strlen(name) == 0) { err = "invalid name"; return false; }
    String path = SpriteStore::pathFor(String(name));
    if (!LittleFS.exists(path.c_str())) { err = "not found"; return false; }
    LittleFS.remove(path.c_str());
    // Clear activeSprite if it was the deleted one.
    if (DeviceSettings::getInstance().activeSprite == String(name))
        DeviceSettings::getInstance().activeSprite = "";
    return true;
}

static bool handle_list(String& result) {
    // Fix 6: retained for SpriteStore::handleCommand() dispatch compatibility.
    // Actual JSON emission is done by SpriteStore::listJson() called from com_thread.
    result = "";
    return true;
}

static bool handle_select(JsonObjectConst cmd, String& err) {
    const char* name = cmd["name"];
    if (!name) {
        // Empty string = deselect
        DeviceSettings::getInstance().activeSprite = "";
        return true;
    }
    if (strlen(name) > 0 && !SpriteStore::exists(String(name))) {
        err = "sprite not found"; return false;
    }
    DeviceSettings::getInstance().activeSprite = String(name);
    return true;
}

// ---------------------------------------------------------------------------
// LVGL custom filesystem driver for LittleFS ('L' drive letter).
// Maps  lv_fs paths of the form "L:/sprites/foo.bmp" to LittleFS.
// ---------------------------------------------------------------------------

static void* lvfs_open(lv_fs_drv_t* /*drv*/, const char* path, lv_fs_mode_t mode) {
    const char* lv_mode = (mode == LV_FS_MODE_WR) ? "w" : "r";
    // 'path' arrives without the drive letter, e.g. "/sprites/foo.bmp"
    File* fp = new File(LittleFS.open(path, lv_mode));
    if (!*fp) { delete fp; return nullptr; }
    return fp;
}

static lv_fs_res_t lvfs_close(lv_fs_drv_t* /*drv*/, void* file_p) {
    File* fp = (File*)file_p;
    fp->close();
    delete fp;
    return LV_FS_RES_OK;
}

static lv_fs_res_t lvfs_read(lv_fs_drv_t* /*drv*/, void* file_p,
                              void* buf, uint32_t btr, uint32_t* br) {
    File* fp = (File*)file_p;
    *br = fp->read((uint8_t*)buf, btr);
    return LV_FS_RES_OK;
}

static lv_fs_res_t lvfs_seek(lv_fs_drv_t* /*drv*/, void* file_p,
                              uint32_t pos, lv_fs_whence_t whence) {
    File* fp = (File*)file_p;
    SeekMode sm = SeekSet;
    if (whence == LV_FS_SEEK_CUR) sm = SeekCur;
    else if (whence == LV_FS_SEEK_END) sm = SeekEnd;
    fp->seek(pos, sm);
    return LV_FS_RES_OK;
}

static lv_fs_res_t lvfs_tell(lv_fs_drv_t* /*drv*/, void* file_p, uint32_t* pos_p) {
    *pos_p = (uint32_t)((File*)file_p)->position();
    return LV_FS_RES_OK;
}

// Static driver instance — must outlive all lv_img objects.
static lv_fs_drv_t s_lvfs_drv;

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace SpriteStore {

void begin() {
    // Filesystem ONLY. Safe to call from setup() before lv_init(). Must NOT touch LVGL.
    if (!LittleFS.begin(true)) {
        Serial.println("[SpriteStore] LittleFS mount failed");
        return;
    }
    if (!LittleFS.exists(SPRITE_DIR)) {
        LittleFS.mkdir(SPRITE_DIR);
    }
    Serial.println("[SpriteStore] storage ready");
}

// MUST be called AFTER lv_init() (from LcdThread::run), never from setup().
// lv_fs_drv_register() allocates via the LVGL heap; before lv_init() the TLSF
// state is NULL and lv_malloc dereferences it -> hard fault / boot crash loop.
void registerLvglDriver() {
    lv_fs_drv_init(&s_lvfs_drv);
    s_lvfs_drv.letter   = 'L';
    s_lvfs_drv.open_cb  = lvfs_open;
    s_lvfs_drv.close_cb = lvfs_close;
    s_lvfs_drv.read_cb  = lvfs_read;
    s_lvfs_drv.seek_cb  = lvfs_seek;
    s_lvfs_drv.tell_cb  = lvfs_tell;
    // write_cb left null — read-only driver sufficient for image display
    lv_fs_drv_register(&s_lvfs_drv);
    Serial.println("[SpriteStore] LVGL 'L:' driver registered");
}

// GIF safety gate. gifdec does ONE contiguous lv_malloc of 5*w*h bytes; an
// oversized GIF would exceed the LVGL pool and OOM-hang the LCD task (the
// LV_USE_ASSERT_MALLOC handler is a while(1)). Verify the magic and cap the
// logical-screen dimensions so the decode can never exceed the pool. Used both
// at upload (reject bad GIFs) and before render (last line of defense).
bool gifRenderable(const String& name, String& err) {
    File f = LittleFS.open(pathFor(name).c_str(), "r");
    if (!f) { err = "cannot open gif"; return false; }
    uint8_t hdr[10];
    size_t n = f.read(hdr, sizeof(hdr));
    f.close();
    if (n < sizeof(hdr)) { err = "gif too short"; return false; }
    if (memcmp(hdr, "GIF8", 4) != 0 ||
        (hdr[4] != '7' && hdr[4] != '9') || hdr[5] != 'a') {
        err = "not a valid GIF (bad magic)"; return false;
    }
    uint16_t w = (uint16_t)hdr[6] | ((uint16_t)hdr[7] << 8);  // logical screen w, LE
    uint16_t h = (uint16_t)hdr[8] | ((uint16_t)hdr[9] << 8);  // logical screen h, LE
    if (w == 0 || h == 0 || w > MAX_GIF_DIM || h > MAX_GIF_DIM) {
        err = "GIF " + String(w) + "x" + String(h) + " exceeds " +
              String(MAX_GIF_DIM) + "x" + String(MAX_GIF_DIM) + " limit";
        return false;
    }
    return true;
}

bool handleCommand(JsonObjectConst cmd, String& err) {
    const char* op = cmd["op"] | "";

    if (strcmp(op, "begin") == 0)  return handle_begin(cmd, err);
    if (strcmp(op, "binbegin")==0) return handle_binbegin(cmd, err);
    if (strcmp(op, "data")  == 0)  return handle_data(cmd, err);
    if (strcmp(op, "end")   == 0)  return handle_end(cmd, err);  // also finalizes binary uploads
    if (strcmp(op, "delete")== 0)  return handle_delete(cmd, err);
    if (strcmp(op, "list")  == 0)  return handle_list(err);   // names in err
    if (strcmp(op, "select")== 0)  return handle_select(cmd, err);

    err = "unknown op: ";
    err += op;
    return false;
}

// Fix 6: populate a JsonArray with {name, size} objects for all stored sprites.
// Called from com_thread when op=="list" to build the {"sprites":[...]} frame.
void listJson(JsonArray arr) {
    File dir = LittleFS.open(SPRITE_DIR, "r");
    if (!dir || !dir.isDirectory()) return;
    File f = dir.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            JsonObject entry = arr.add<JsonObject>();
            entry["name"] = String(f.name());
            entry["size"] = (uint32_t)f.size();
        }
        f.close();
        f = dir.openNextFile();
    }
    dir.close();
}

String pathFor(const String& name) {
    return String(SPRITE_DIR) + "/" + name;
}

bool exists(const String& name) {
    return LittleFS.exists(pathFor(name).c_str());
}

bool binReceiving() { return g_upload.active && g_upload.binary; }

void binAbort() { abort_upload(); }   // recover from a stalled/interrupted binary upload

size_t binRemaining() {
    return (g_upload.active && g_upload.expected > g_upload.written)
           ? (g_upload.expected - g_upload.written) : 0;
}

bool binFeed(const uint8_t* buf, size_t len) {
    if (!g_upload.active || !g_upload.binary) return false;
    if (g_upload.written + len > g_upload.expected)
        len = g_upload.expected - g_upload.written;     // never overrun declared size
    if (len == 0) return true;
    size_t wrote = g_upload.file.write(buf, len);
    if (wrote != len) { abort_upload(); return false; } // disk full → abort
    g_upload.crc_run = crc32_update(g_upload.crc_run, buf, len);
    g_upload.written += len;
    return true;
}

} // namespace SpriteStore
