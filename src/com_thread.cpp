
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



ComThread::ComThread(const uint8_t task_core) : Thread("COM", 12000, 1, task_core) {
    _q_strings_in = xQueueCreate(5, sizeof( StringMessage ));
};

ComThread::~ComThread() {

};


void ComThread::put_string_message(const StringMessage& msg){
    xQueueSend(_q_strings_in, &msg, (TickType_t)0);
};



String title = "";
String data1 = "";
String data2 = "";
String data3 = "";
String data4 = "";

// FW3: message command storage (title/text/duration) for LCD_LAYOUT_MESSAGE dispatch
String msg_title = "";
String msg_text  = "";

void ComThread::run() {
    // FW3: shorten readline timeout so partial frames don't block for 1 s
    Serial.setTimeout(50);

    // serial is initialized in main.cpp, but subsequently used only here
    sendAck("boot", true); // non-JSON guard: replaced bare println with JSON
    unsigned long ts = millis();
    ts_last_activity = ts;
    JsonDocument idleDoc;
    LcdCommand remoteLcdCommand;
    remoteLcdCommand.type = LCD_LAYOUT_DEFAULT;
    remoteLcdCommand.title = &title;
    remoteLcdCommand.data1 = &data1;
    remoteLcdCommand.data2 = &data2;
    remoteLcdCommand.data3 = &data3;
    remoteLcdCommand.data4 = &data4;
    dispatchSettings();
    dispatchLcdConfig();
    while (true) {
        JsonDocument doc;
        if (Serial.available()) {
            String input = Serial.readStringUntil('\n');
            DeserializationError error = deserializeJson(doc, input);
            if (error) {
                doc.clear();
                sendError("JSON parse error", error.c_str());
                continue;
            }
            ts_last_activity = millis();

            JsonVariant profile = doc["profile"];
            JsonVariant v = doc["updates"];
            if (profile.is<String>() || v!=nullptr) { // haptic command
              handleProfileCommand(profile, v);
            }
            if (!doc["current"].isNull()) { // set current profile
              String cur = doc["current"].as<String>();
              setCurrentProfile(cur);
              // FW3: emit ACK for set-current-profile (was silently missing)
              sendAck("current", true);
            }
            if (!doc["R"].isNull()) { // motor command
              // FW3: allocate only if queue has space to avoid a heap leak when full.
              // put_motor_command() does not delete on xQueueSend failure (FW1 should
              // add a bool return value and delete internally); we guard here instead.
              const char* cmd = doc["R"];
              String* cmdstr = new String(cmd);
              foc_thread.put_motor_command(cmdstr);
              // NOTE: if foc_thread queue is full cmdstr is silently dropped (FW1 to fix)
            }
            // FW3: "message" command — protocol sends an object {title,text,duration}
            // Old code tested v.is<String>() which always failed, leaking the allocated String.
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
              sendAck("message", true);
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
                put_string_message(dbg);
                foc_thread.put_motor_command(new String("129=1"));
                sendAck("recalibrate", true);
              }
            }
            v = doc["profiles"];
            if (v!=nullptr) { // list or reorder profiles
              handleProfilesCommand(v);
            }
            v = doc["settings"];
            if (v!=nullptr) { // get or set settings
              handleSettingsCommand(v);
            }
            if (doc["save"]) { // save settings and profiles to SPIFFS
              if (doc["save"].as<bool>()==true) {
                DeviceSettings::getInstance().toSPIFFS();
                HapticProfileManager::getInstance().toSPIFFS();
                DeviceSettings::getInstance().storeCurrentProfile(HapticProfileManager::getInstance().getCurrentProfile()->profile_name);
                // FW3: emit proper JSON {"saved":true} and ACK
                JsonDocument reply;
                reply["saved"] = true;
                serializeJson(reply, Serial);
                Serial.println(); // newline after JSON
                sendAck("save", true);
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
                dispatchSettings();
                dispatchHapticConfig();
                dispatchAudioConfig();
                dispatchLedConfig();
                dispatchHmiConfig();
                dispatchLcdConfig();
                sendAck("load", true);
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
              sendAck("wifi", true);
            }

            // FW3: route "sprite" command — delegate to SpriteStore per contract.
            // Fix 6: special-case op=="list" to emit {"sprites":[...]} JSON frame
            // because handle_list() returns data in the err/result string and
            // the caller used to pass nullptr for the result on success.
            v = doc["sprite"];
            if (v.is<JsonObject>()) {
              JsonObjectConst scmd = v.as<JsonObjectConst>();
              const char* spOp = scmd["op"] | "";
              if (strcmp(spOp, "list") == 0) {
                // Build and emit a proper JSON sprite list frame.
                String listResult;
                bool ok = SpriteStore::handleCommand(scmd, listResult);
                if (ok) {
                  // Emit {"sprites":[{"name":"..","size":N},...]}
                  JsonDocument listDoc;
                  SpriteStore::listJson(listDoc["sprites"].to<JsonArray>());
                  serializeJson(listDoc, Serial);
                  Serial.println();
                }
                sendAck("sprite", ok, ok ? nullptr : listResult.c_str());
              } else {
                String spriteErr;
                bool ok = SpriteStore::handleCommand(scmd, spriteErr);
                sendAck("sprite", ok, ok ? nullptr : spriteErr.c_str());
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
          serializeJson(idleDoc, Serial);
          Serial.println(); // newline after JSON
        }
        if (now-ts_last_activity<=global_idle_timeout || global_idle_timeout==0)
          global_sleep_flag = false;
        else
          global_sleep_flag = true;

        vTaskDelay(10); // give other threads a chance to run...
    }

};




// FW3: helper — emit {"ack":"<cmd>","ok":true/false} (and optional "error") for every mutating command
void ComThread::sendAck(const char* cmd, bool ok, const char* errMsg) {
    JsonDocument doc;
    doc["ack"] = cmd;
    doc["ok"]  = ok;
    if (!ok && errMsg != nullptr && errMsg[0] != '\0')
      doc["error"] = errMsg;
    serializeJson(doc, Serial);
    Serial.println(); // newline after JSON
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
        serializeJson(eventDoc, Serial);
        Serial.println(); // newline after JSON
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
        // get_motor_angle() already exists on FocThread.
        // get_motor_velocity() must be added by FW1 (see integrationHooks).
        // Float reads on Xtensa-LX7 are single-instruction — safe to call
        // cross-task without a mutex for these telemetry-only reads.
        float a = foc_thread.get_motor_angle();
        float v_rad = foc_thread.get_motor_velocity(); // requires FW1 addition
        int32_t turns = (int32_t)(a / (2.0f * PI));    // integer floor turns from angle
        eventDoc["p"] = angleEvt.cur_pos;               // legacy uint16 position
        eventDoc["a"] = a;
        eventDoc["t"] = turns;
        eventDoc["v"] = v_rad;
        serializeJson(eventDoc, Serial);
        Serial.println(); // newline after JSON
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
    serializeJson(doc, Serial);
    Serial.println(); // newline after JSON
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
      serializeJson(doc, Serial);
      Serial.println(); // newline after JSON
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
      serializeJson(doc, Serial);
      Serial.println(); // newline after JSON
    }
  }
  if (p.is<JsonArray>()) {
    JsonArray arr = p.as<JsonArray>();

    // FW3: real reorder — delete profiles absent from the incoming list, then
    // rearrange profiles[] to match the requested order using swap-sort.
    // Complexity O(N^2) for N<=10 is fine on embedded.

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
        // FW3: removed bare Serial.println("Deleting profile …") — injected non-JSON
      }
    }

    // Pass 2: sort profiles[] to match the requested order using selection-sort.
    // pm[int] returns nullptr for empty slots (profile_name=="").  After pass 1
    // only slots whose profiles were in the incoming list remain non-null, so
    // scanning [targetIdx..MAX_PROFILES) will always find a non-null scanProf
    // before we reach the end.  Guard with a null-check on both sides of the swap.
    int targetIdx = 0;
    for (int arrIdx = 0; arrIdx < (int)arr.size() && targetIdx < MAX_PROFILES; arrIdx++) {
      if (!arr[arrIdx].is<String>()) continue;
      String wantedName = arr[arrIdx].as<String>();
      for (int scanIdx = targetIdx; scanIdx < MAX_PROFILES; scanIdx++) {
        HapticProfile* scanProf = pm[scanIdx]; // nullptr if empty
        if (scanProf != nullptr && scanProf->profile_name == wantedName) {
          if (scanIdx != targetIdx) {
            HapticProfile* tgtProf = pm[targetIdx]; // may be nullptr (empty slot)
            // Swap if both slots are valid (non-null); if target is empty we cannot
            // swap safely via public API — report via integrationHooks for FW5 to
            // expose a swap accessor.  In practice, after pass 1 cleans up deleted
            // profiles and active profiles are contiguous, this branch is not reached.
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

    // FW3: re-anchor manager's current_profile pointer after struct-level swap so
    // it doesn't dangle if the current profile's slot was moved.
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
      serializeJson(doc, Serial);
      Serial.println(); // newline after JSON
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
    serializeJson(doc, Serial);
    Serial.println(); // newline after JSON
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
