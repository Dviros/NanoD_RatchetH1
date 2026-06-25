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

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <lvgl.h>

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
static constexpr size_t  MAX_SPRITE_SIZE   = 64  * 1024;  // 64 KB per sprite
static constexpr size_t  MAX_SPRITES_BYTES = 512 * 1024;  // 512 KB total
static constexpr uint8_t MAX_SPRITES       = 16;
static const char*       SPRITE_DIR        = "/sprites";

// ---------------------------------------------------------------------------
// Upload state machine
// ---------------------------------------------------------------------------
namespace {

struct UploadCtx {
    bool     active   = false;
    String   name;
    size_t   expected = 0;   // declared size in "begin"
    size_t   written  = 0;   // bytes written so far
    int      next_seq = 0;   // expected sequence number
    uint32_t crc_run  = 0;   // running CRC-32 accumulator
    File     file;
};

UploadCtx g_upload;

// Simple CRC-32 (IEEE 802.3 poly, no table — small flash footprint).
// For a 64 KB sprite this is fast enough in the com_thread context.
static uint32_t crc32_update(uint32_t crc, const uint8_t* buf, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
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

static bool handle_begin(JsonObjectConst cmd, String& err) {
    // Abort any prior stale upload.
    abort_upload();

    const char* name = cmd["name"];
    size_t size = cmd["size"] | 0u;

    if (!name || strlen(name) == 0 || strlen(name) > 32) {
        err = "invalid name"; return false;
    }
    if (size == 0 || size > MAX_SPRITE_SIZE) {
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
    g_upload.name     = String(name);
    g_upload.expected = size;
    g_upload.written  = 0;
    g_upload.next_seq = 0;
    g_upload.crc_run  = 0;
    return true;
}

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

    if (g_upload.written + (size_t)dec > MAX_SPRITE_SIZE) {
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
    if (!LittleFS.begin(true)) {
        Serial.println("[SpriteStore] LittleFS mount failed");
        return;
    }
    if (!LittleFS.exists(SPRITE_DIR)) {
        LittleFS.mkdir(SPRITE_DIR);
    }

    // Register the LVGL filesystem driver on letter 'L'.
    lv_fs_drv_init(&s_lvfs_drv);
    s_lvfs_drv.letter   = 'L';
    s_lvfs_drv.open_cb  = lvfs_open;
    s_lvfs_drv.close_cb = lvfs_close;
    s_lvfs_drv.read_cb  = lvfs_read;
    s_lvfs_drv.seek_cb  = lvfs_seek;
    s_lvfs_drv.tell_cb  = lvfs_tell;
    // write_cb left null — read-only driver sufficient for image display
    lv_fs_drv_register(&s_lvfs_drv);

    Serial.println("[SpriteStore] ready");
}

bool handleCommand(JsonObjectConst cmd, String& err) {
    const char* op = cmd["op"] | "";

    if (strcmp(op, "begin") == 0)  return handle_begin(cmd, err);
    if (strcmp(op, "data")  == 0)  return handle_data(cmd, err);
    if (strcmp(op, "end")   == 0)  return handle_end(cmd, err);
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

} // namespace SpriteStore
