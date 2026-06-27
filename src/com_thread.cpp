
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
#include <esp_system.h>   // esp_restart()
#include "esp32-hal-tinyusb.h"  // usb_persist_restart(RESTART_BOOTLOADER) — native-USB download entry



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
        // ── Binary sprite receive (fast path) ────────────────────────────────
        // After {"sprite":{"op":"binbegin",..}} the host streams raw bytes (no
        // base64/JSON). Drain them straight to flash in big blocks; a normal
        // {"sprite":{"op":"end",..}} line follows once binRemaining() hits 0.
        if (SpriteStore::binReceiving()) {
            static uint8_t binbuf[512];
            static unsigned long bin_last = 0;
            if (bin_last == 0) bin_last = millis();
            // Drain only what's already buffered, yielding between empties so the
            // TinyUSB task can move incoming USB bytes into the CDC FIFO. A plain
            // blocking readBytes(want) starves that task → the bytes never arrive.
            for (int k = 0; k < 200 && SpriteStore::binReceiving(); ++k) {
                int avail = Serial.available();
                if (avail <= 0) { vTaskDelay(1); continue; }
                size_t want = SpriteStore::binRemaining();
                if ((size_t)avail < want) want = (size_t)avail;
                if (want > sizeof(binbuf)) want = sizeof(binbuf);
                size_t n = Serial.readBytes(binbuf, want);   // bytes are present → no block
                if (n) { SpriteStore::binFeed(binbuf, n); ts_last_activity = millis(); bin_last = millis(); }
            }
            if (SpriteStore::binReceiving() && millis() - bin_last > 3000) {
                SpriteStore::binAbort();                     // stalled host → recover
                sendError("binary upload timeout", "aborted");
            }
            if (!SpriteStore::binReceiving()) bin_last = 0;  // reset window for next upload
        }
        // ── Serial input path (line / JSON) ──────────────────────────────────
        else if (Serial.available()) {
            JsonDocument doc;
            String input = Serial.readStringUntil('\n');
            DeserializationError error = deserializeJson(doc, input);
            if (error) {
                doc.clear();
                sendError("JSON parse error", error.c_str());
            } else {
                ts_last_activity = millis();
                processCommand(doc, *this);
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

        // send any outgoing messages
        handleMessages();

        // send key events
        handleEvents();

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
      self.sendAck("ring", true);
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
    JsonDocument eventDoc;
    bool hadEvent = false;
    do {
      KeyEvt keyEvt;
      hadEvent = hmi_thread.get_key_event(&keyEvt);
      if (hadEvent) {
        eventDoc.clear();
        eventDoc["ks"] = keyEvt.keyState;
        if (keyEvt.type==0) // AceButton::kEventPressed
          eventDoc["kd"] = keyEvt.keyNum;
        else if (keyEvt.type==1) // AceButton::kEventReleased
          eventDoc["ku"] = keyEvt.keyNum;
        String frame;
        serializeJson(eventDoc, frame);
        emit(frame);
        ts_last_activity = millis();
      }
    } while (hadEvent);
    do {
      AngleEvt angleEvt;
      hadEvent = foc_thread.get_angle_event(&angleEvt);
      if (hadEvent) {
        eventDoc.clear();
        // FW3: emit richer telemetry per communications.md {a,t,v}
        // "p" is kept for back-compat with older hosts; new hosts should use a/t/v.
        // a = shaft_angle (rad), t = integer turns, v = velocity (rad/s).
        float a = foc_thread.get_motor_angle();
        float v_rad = foc_thread.get_motor_velocity(); // requires FW1 addition
        int32_t turns = (int32_t)(a / (2.0f * PI));    // integer floor turns from angle
        eventDoc["p"] = angleEvt.cur_pos;               // legacy uint16 position
        eventDoc["a"] = a;
        eventDoc["t"] = turns;
        eventDoc["v"] = v_rad;
        String frame;
        serializeJson(eventDoc, frame);
        emit(frame);
        ts_last_activity = millis();
      }
    } while (hadEvent);
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
