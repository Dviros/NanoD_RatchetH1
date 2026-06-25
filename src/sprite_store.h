#pragma once
/*
 * SpriteStore — FW7
 *
 * Receives chunked sprite uploads over the serial JSON command channel,
 * stores them to LittleFS under /sprites/<name>, validates size + CRC,
 * and exposes helpers for the rest of the firmware.
 *
 * All public functions are safe to call from any single thread at a time
 * (the store itself is not re-entrant; the caller — com_thread — is
 * single-threaded by design).
 *
 * LVGL filesystem integration:
 *   SpriteStore::begin() registers a custom lv_fs driver on letter 'L'
 *   that maps "L:/sprites/<name>" to the corresponding LittleFS file.
 *   lv_conf.h must have LV_USE_FS_LITTLEFS (or a custom driver) enabled;
 *   see integrationHooks in the agent manifest.
 */

#include <Arduino.h>
#include <ArduinoJson.h>

namespace SpriteStore {

    /* --- Lifecycle -------------------------------------------------------- */

    /**
     * Mount LittleFS (if not already mounted), create /sprites directory,
     * and register the LVGL 'L' filesystem driver.
     * Call once from main before any thread starts.
     */
    void begin();

    /* --- Command dispatcher ----------------------------------------------- */

    /**
     * Dispatch an inbound JSON sprite command.
     * Returns true on success; on failure sets err and returns false.
     *
     * Recognised sub-commands (field "op"):
     *   "begin"  – { "op":"begin",  "name":"<n>", "size":<bytes> }
     *   "data"   – { "op":"data",   "seq":<int>,  "data":"<base64>" }
     *   "end"    – { "op":"end",    "crc32":<uint32> }
     *   "delete" – { "op":"delete", "name":"<n>" }
     *   "list"   – { "op":"list" }                  (returns names via err)
     *   "select" – { "op":"select", "name":"<n>" }  (sets DeviceSettings.activeSprite)
     */
    bool handleCommand(JsonObjectConst cmd, String& err);

    /* --- Helpers ---------------------------------------------------------- */

    /** Absolute LittleFS path for a sprite: "/sprites/<name>". */
    String pathFor(const String& name);

    /** True if a file exists for the given sprite name. */
    bool exists(const String& name);

    /**
     * Fix 6: populate arr with {name, size} JSON objects for every stored sprite.
     * Call this from the "list" command handler in com_thread to build the
     * {"sprites":[{"name":"..","size":N},...]} response frame.
     */
    void listJson(JsonArray arr);

} // namespace SpriteStore
