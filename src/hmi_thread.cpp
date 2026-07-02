#include "hmi_thread.h"
#include "com_thread.h"
#include "foc_thread.h"
#include <Adafruit_TinyUSB.h>
#include "MIDI.h"
#include "audio/audio_api.h"   // audio_play() / audio_click() stubs (no-ops if NANO_AUDIO=0)
#include "audio/audio.h"       // BinarisAudioPlayer – guarded by NANO_AUDIO inside
#include <SparkFun_STUSB4500.h>

using namespace ace_button;

Adafruit_USBD_MIDI usb_midi(1);

MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, midiu);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, midi2)

enum {
  RID_KEYBOARD = 1,
  RID_MOUSE = 2,
  RID_GAMEPAD = 3,
  RID_CONSUMER = 4,  // USB HID Consumer Control (page 0x0C)
};


uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD( HID_REPORT_ID(RID_KEYBOARD) ),
  TUD_HID_REPORT_DESC_MOUSE   ( HID_REPORT_ID(RID_MOUSE) ),
  TUD_HID_REPORT_DESC_GAMEPAD ( HID_REPORT_ID(RID_GAMEPAD) ),
  TUD_HID_REPORT_DESC_CONSUMER( HID_REPORT_ID(RID_CONSUMER) ),
};

// USB HID object
Adafruit_USBD_HID usb_hid;

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// Hmi thread controls LED via FastLed and buttons via AceButton

HmiThread::HmiThread(const uint8_t task_core ) : Thread("HMI", 4608, 1, task_core) {
    _q_config_in = xQueueCreate(2, sizeof( ledConfig ));
    _q_hmi_config_in = xQueueCreate(2, sizeof( hmiConfig ));
    _q_settings_in = xQueueCreate(2, sizeof( HmiDeviceSettings ));
    _q_keyevt_out = xQueueCreate(5, sizeof( KeyEvt ));
}

HmiThread::~HmiThread() {}



void midi_sysex_handler(byte* array, unsigned size) {
    hmi_thread.handleSysex(array, size);
};


// init_usb() must be called before the thread is started
void HmiThread::init_usb() {
  usb_midi.setStringDescriptor("Nano_D MIDI");
  midiu.setHandleSystemExclusive(midi_sysex_handler);
  usb_midi.begin();
  midiu.begin();

  usb_hid.setBootProtocol(HID_ITF_PROTOCOL_NONE);
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.setStringDescriptor("Nano_D HID");
  usb_hid.begin();
};


// init must be called before the thread is started
void HmiThread::init(ledConfig& initial_led_config, hmiConfig& initial_hmi_config) {
    led_config = initial_led_config;
    hmi_config = initial_hmi_config;
    led_max_brightness =  DeviceSettings::getInstance().ledMaxBrightness;
    uint8_t b = min(led_max_brightness, led_config.led_brightness);
    FastLED.setBrightness(b);
    midi_sysex_id = DeviceSettings::getInstance().midi_sysex_id;
    midiUsbSettings = DeviceSettings::getInstance().midiUsb;
    midi2Settings = DeviceSettings::getInstance().midi2;
    Serial2.begin(31250, SERIAL_8N1, PIN_SERIAL2_RX, PIN_SERIAL2_TX);
    midi2.setHandleSystemExclusive(midi_sysex_handler);  
    midi2.begin();
#if NANO_AUDIO
    audioPlayer.audio_init();
#endif
};



void HmiThread::put_led_config(ledConfig& new_config) {
    xQueueSend(_q_config_in, &new_config, (TickType_t)0);
};


void HmiThread::put_hmi_config(hmiConfig& new_config){
    xQueueSend(_q_hmi_config_in, &new_config, (TickType_t)0);
};


void HmiThread::put_settings(HmiDeviceSettings& new_settings){
    xQueueSend(_q_settings_in, &new_settings, (TickType_t)0);
};


void HmiThread::handleConfig() {
    ledConfig newConfig;
    if (xQueueReceive(_q_config_in, &newConfig, (TickType_t)0)) {
        led_config = newConfig;
        uint8_t newBrightness = min(led_max_brightness, led_config.led_brightness);
        if (FastLED.getBrightness() != newBrightness) {
            FastLED.setBrightness(newBrightness);
        }
        updateKeyLeds();
    }
    hmiConfig newHmiConfig;
    if (xQueueReceive(_q_hmi_config_in, &newHmiConfig, (TickType_t)0)) {
        hmi_config = newHmiConfig;
    }
};


void HmiThread::handleSettings() {
    HmiDeviceSettings newSettings;
    if (xQueueReceive(_q_settings_in, &newSettings, (TickType_t)0)) {
        midiUsbSettings = newSettings.midiUsb;
        midi2Settings = newSettings.midi2;
        midiu.setThruFilterMode(midiUsbSettings.thru? midi::Thru::Full : midi::Thru::Off);
        midiu.setInputChannel(midiUsbSettings.in? MIDI_CHANNEL_OMNI : MIDI_CHANNEL_OFF);
        midi2.setThruFilterMode(midi2Settings.thru? midi::Thru::Full : midi::Thru::Off);
        midi2.setInputChannel(midi2Settings.in? MIDI_CHANNEL_OMNI : MIDI_CHANNEL_OFF);
        led_max_brightness = newSettings.ledMaxBrightness;
        uint8_t newBrightness = min(newSettings.ledMaxBrightness, led_config.led_brightness);
        if (FastLED.getBrightness() != newBrightness) {
            FastLED.setBrightness(newBrightness);
            updateKeyLeds();
        }
        midi_sysex_id = newSettings.midi_sysex_id;
        Serial.println("Hmi settings updated from global settings");
    }
};


bool HmiThread::get_key_event(KeyEvt* keyEvt){
    return xQueueReceive(_q_keyevt_out, keyEvt, (TickType_t)0);
};




// Liveness heartbeat: the HMI loop stamps this every iteration. The com thread watches
// it and force-restarts if the LED render loop ever stalls (e.g. a FastLED RMT hang)
// for >5 s, so the device self-recovers instead of freezing forever.
volatile uint32_t g_hmi_beat = 0;
// PD diagnostic: raw RDO bytes + both byte-order decodes, surfaced via {"pd":"status"}
// so we can see the *actual* negotiated PDO instead of trusting one unverified byte order.
volatile uint8_t g_pd_rdo[4] = {0, 0, 0, 0};
volatile uint8_t g_pd_sel_hi = 0, g_pd_sel_lo = 0;
volatile uint8_t g_pd_cc_adv = 0;   // Type-C CC advertisement: 0=default 1=1.5A 2=3.0A

void HmiThread::run() {
    // Fix 3: recursive mutex guards keyState, currentValue and any shared HMI state.
    // Created here (before ISR callbacks can fire) so it is valid for the full thread lifetime.
    _hmi_mutex = xSemaphoreCreateRecursiveMutex();
    configASSERT(_hmi_mutex);

    FastLED.addLeds<LED_CHIPSET, PIN_LED_A, RGB>(leds, NANO_LED_A_NUM);
    FastLED.addLeds<LED_CHIPSET, PIN_LED_B, LED_COL_ORDER>(ledsp, NANO_LED_B_NUM);
    FastLED.setBrightness( DEFAULT_LED_MAX_BRIGHTNESS );
    // Power-budget the LEDs. 60+8 WS2812 unbounded can pull >2A — more than the motor —
    // on a rail with only 1uF VBUS bulk (schematic). Reserve ~2W system (ESP+LCD) and
    // ~3W motor from the negotiated supply budget; the LEDs get the rest, floored at
    // 250mA (still clearly visible) and capped at 1200mA. FastLED scales output
    // dynamically to hold the cap, so this is invisible until the budget is exceeded.
    {
        uint32_t supply_ma = DeviceSettings::getInstance().pdBudgetMa;
        float    pdv       = DeviceSettings::getInstance().getPdVoltage();
        int32_t  led_mw    = (int32_t)(supply_ma * pdv) - 2000 - 3000;
        uint16_t led_ma    = (uint16_t)constrain(led_mw / 5, (int32_t)250, (int32_t)1200);
        FastLED.setMaxPowerInVoltsAndMilliamps(5, led_ma);
        Serial.printf("LED power cap: %umA @5V (supply budget %lumA @%.1fV)\n",
                      led_ma, (unsigned long)supply_ma, pdv);
    }
    pinMode(PIN_BTN_A, INPUT_PULLUP);
    pinMode(PIN_BTN_B, INPUT_PULLUP);
    pinMode(PIN_BTN_C, INPUT_PULLUP);
    pinMode(PIN_BTN_D, INPUT_PULLUP);
    buttons[0] = new AceButton(new ButtonConfig(), PIN_BTN_A);
    buttons[1] = new AceButton(new ButtonConfig(), PIN_BTN_B);
    buttons[2] = new AceButton(new ButtonConfig(), PIN_BTN_C);
    buttons[3] = new AceButton(new ButtonConfig(), PIN_BTN_D);
    for (int i = 0; i < 4; i++) {
        button_handler[i] = HmiThreadButtonHandler(i);
        buttons[i]->getButtonConfig()->setIEventHandler(&button_handler[i]);
        buttons[i]->getButtonConfig()->setClickDelay(50);
        buttons[i]->getButtonConfig()->clearFeature(ButtonConfig::kFeatureDoubleClick);
        // Fix 4: enable LongPress events so held-key actions are dispatched (kEventLongPressed).
        buttons[i]->getButtonConfig()->setFeature(ButtonConfig::kFeatureLongPress);
        buttons[i]->getButtonConfig()->setLongPressDelay(500); // 500 ms hold threshold
    }
    int keys[4] = {0x1, 0x2, 0x4, 0x8};
    int leds[4][2] = {{3, 4}, {2, 5}, {1, 6}, {0, 7}};
    CRGB colors[4][2] = {
        {led_config.button_A_col_press, led_config.button_A_col_idle},
        {led_config.button_B_col_press, led_config.button_B_col_idle},
        {led_config.button_C_col_press, led_config.button_C_col_idle},
        {led_config.button_D_col_press, led_config.button_D_col_idle}
    };

    for (int i = 0; i < 4; i++) {
        CRGB color = (keyState & keys[i]) ? colors[i][0] : colors[i][1];
        ledsp[leds[i][0]] = color;
        ledsp[leds[i][1]] = color;
    }

    unsigned long total = 0;
    unsigned long updates = 0;
    unsigned long ts = micros();

    audio_play(SOUND_CHIME); // startup chime – no-op if NANO_AUDIO=0
    while (1) {
        g_hmi_beat = millis();   // liveness stamp (before the LED show that can hang)
        handleSettings();
        handleConfig();
        handleMidi();
        for (int i = 0; i < 4; i++)
            buttons[i]->check();
        updateValue();
        handleHid();       
        updateLeds();
        unsigned long currentMillis = millis();
        static unsigned long previousMillis = 0;
        if (currentMillis - previousMillis >= 16) {
            // Limit Leds to ~60fps
            FastLED.show();
            previousMillis = currentMillis;
        }
#if NANO_AUDIO
        audioPlayer.audio_loop();
#endif
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
};




HmiThreadButtonHandler::HmiThreadButtonHandler(uint8_t _index) : index(_index) {};



void HmiThreadButtonHandler::handleEvent(AceButton* button, uint8_t eventType, uint8_t buttonState) {
    // Fix 3: guard keyState and all shared HMI state with the recursive mutex.
    SemaphoreGuard guard(hmi_thread._hmi_mutex);

    switch (eventType) {
        case AceButton::kEventPressed:
            hmi_thread.keyState |= (1<<index);
            for (int i=0; i<hmi_thread.hmi_config.keys[index].num_pressed_actions; i++) {
                hmi_thread.handleKeyAction(hmi_thread.hmi_config.keys[index].pressed[i], eventType);
            }
            audio_click(); // key-press haptic click – no-op if NANO_AUDIO=0
        break;
        case AceButton::kEventReleased:
            hmi_thread.keyState &= ~(1<<index);
            for (int i=0; i<hmi_thread.hmi_config.keys[index].num_pressed_actions; i++) {
                hmi_thread.handleKeyAction(hmi_thread.hmi_config.keys[index].pressed[i], eventType);
            }
            for (int i=0; i<hmi_thread.hmi_config.keys[index].num_released_actions; i++) {
                hmi_thread.handleKeyAction(hmi_thread.hmi_config.keys[index].released[i], eventType);
            }
        break;
        // Fix 4: dispatch held actions (were parsed/stored but never fired).
        case AceButton::kEventLongPressed:
            for (int i=0; i<hmi_thread.hmi_config.keys[index].num_held_actions; i++) {
                hmi_thread.handleKeyAction(hmi_thread.hmi_config.keys[index].held[i], eventType);
            }
        break;
    }
    KeyEvt keyEvt = { .type=eventType, .keyNum=(uint8_t)index, .keyState=hmi_thread.keyState };
    xQueueSend(hmi_thread._q_keyevt_out, &keyEvt, (TickType_t)0);
    hmi_thread.lastCheck = millis();
    hmi_thread.isIdle = false;
    hmi_thread.last_pos = -1;
    hmi_thread.updateKeyLeds();
};




void HmiThread::handleKeyAction(keyAction& action, uint8_t eventType) {
    StringMessage msg;
    switch (action.type) {
        case keyActionType::KA_MIDI:
            if (eventType==AceButton::kEventPressed) {
                if (midiUsbSettings.nano)
                    midiu.sendControlChange(action.midi.cc, action.midi.val, action.midi.channel);
                if (midi2Settings.nano)
                    midi2.sendControlChange(action.midi.cc, action.midi.val, action.midi.channel);
            }
        break;
        case keyActionType::KA_KEY:
            if (num_key_codes<6 && eventType==AceButton::kEventPressed)
                current_key_codes[num_key_codes++] = action.hid.key_codes[0];
            else if (num_key_codes>0 && eventType==AceButton::kEventReleased) {
                for (int i=0; i<num_key_codes; i++) {
                    if (current_key_codes[i]==action.hid.key_codes[0]) {
                        for (int j=i; j<num_key_codes-1; j++)
                            current_key_codes[j] = current_key_codes[j+1];
                        num_key_codes--;
                        current_key_codes[num_key_codes] = 0;
                        break;
                    }
                }
            }
        break;
        case keyActionType::KA_MOUSE:
            if (eventType==AceButton::kEventPressed)
                current_mouse_buttons |= action.mouse.buttons;
            else
                current_mouse_buttons &= ~action.mouse.buttons;
        break;
        case keyActionType::KA_GAMEPAD:
            if (eventType==AceButton::kEventPressed)
                current_pad_buttons |= action.pad.buttons;
            else
                current_pad_buttons &= ~action.pad.buttons;
        break;
        case keyActionType::KA_PROFILE_CHANGE:
            if (action.profile!="" && eventType==AceButton::kEventPressed) {
                StringMessage msg(new String(action.profile), STRING_MESSAGE_PROFILE);
                com_thread.put_string_message(msg);
            }
        break;
        case keyActionType::KA_PROFILE_NEXT:
            msg = StringMessage(nullptr, STRING_MESSAGE_NEXT_PROFILE);
            if (eventType==AceButton::kEventPressed)
                com_thread.put_string_message(msg);
        break;
        case keyActionType::KA_PROFILE_PREV:
            msg = StringMessage(nullptr, STRING_MESSAGE_PREV_PROFILE);
            if (eventType==AceButton::kEventPressed)
                com_thread.put_string_message(msg);
        break;
        case keyActionType::KA_CONSUMER:
            if (eventType==AceButton::kEventPressed)
                current_consumer_usage = action.consumer.usage;
            else if (eventType==AceButton::kEventReleased)
                current_consumer_usage = 0;
        break;
    }
};



void HmiThread::updateValue() {
    if (hmi_config.knob.num>0) {
        float angle = foc_thread.get_motor_angle();
        for (int i=0;i<hmi_config.knob.num;i++) {
            knobValue& v = hmi_config.knob.values[i];
            if (v.key_state==keyState) {
                float value = 0;
                // Fix 5: assign result of _constrain back to angle so the clamp actually takes effect.
                if (v.angle_min<v.angle_max) {
                    angle = _constrain(angle, v.angle_min, v.angle_max);
                }
                else {
                    angle = _constrain(angle, v.angle_max, v.angle_min);
                }
                if (v.angle_max == v.angle_min) {
                    value = v.value_min;
                }
                else {
                    value = (angle - v.angle_min) * (v.value_max - v.value_min) / (v.angle_max - v.angle_min) + v.value_min;
                }
                if (v.step!=0) {
                    value = round(value / v.step) * v.step;
                }
                // Fix 6: removed the line that overwrote `currentValue` with raw encoder position,
                // which killed the knob mapping.  The computed mapped value is correct.
                currentValue = value;
                if (currentValue!=lastValue) {
                    if (v.type==knobValueType::KV_MIDI) {
                        uint8_t midi_value = (uint8_t)(currentValue);
                        midi_value = _constrain(midi_value, 0, 127);
                        if (midiUsbSettings.nano)
                            midiu.sendControlChange(v.midi.cc, midi_value, v.midi.channel);
                        if (midi2Settings.nano)
                            midi2.sendControlChange(v.midi.cc, midi_value, v.midi.channel);
                    }
                    else if (v.type==knobValueType::KV_VOLUME) {
                        // Queue one Consumer VolumeUp/VolumeDown pulse per detent crossing.
                        // handleHid() drains the queue in two successive HID-ready windows:
                        // first sends the usage, then sends 0 (release) on the next iteration.
                        // Direction is determined by sign of value change.
                        pending_volume_usage = (currentValue > lastValue)
                            ? (uint16_t)HID_USAGE_CONSUMER_VOLUME_INCREMENT   // 0xE9
                            : (uint16_t)HID_USAGE_CONSUMER_VOLUME_DECREMENT;  // 0xEA
                        pending_volume_release = false;
                    }
                    lastValue = currentValue;
                }
            }
        } // for over values
    }
};



void HmiThread::handleHid() {

    bool keys_changed = (num_key_codes!=last_num_key_codes);
    bool mouse_changed = (current_mouse_buttons!=last_mouse_buttons);
    bool pad_changed = (current_pad_buttons!=last_pad_buttons);
    bool consumer_changed = (current_consumer_usage!=last_consumer_usage);

    if ( TinyUSBDevice.suspended() && (keys_changed||mouse_changed||pad_changed||consumer_changed) ) {
        TinyUSBDevice.remoteWakeup();
    }

    if (usb_hid.ready()) {
        if (keys_changed) {
            if (num_key_codes>0)
                usb_hid.keyboardReport(RID_KEYBOARD, 0, current_key_codes);
            else
                usb_hid.keyboardRelease(RID_KEYBOARD);
            last_num_key_codes = num_key_codes;
        }
        if (mouse_changed) {
            usb_hid.mouseButtonPress(RID_MOUSE, current_mouse_buttons);
            last_mouse_buttons = current_mouse_buttons;
        }
        if (pad_changed) {
            hid_gamepad_report_t report = {
                .x = 0,
                .y = 0,
                .z = 0,
                .rz = 0,
                .rx = 0,
                .ry = 0,
                .hat = 0,
                .buttons = current_pad_buttons
            };
            usb_hid.sendReport(RID_GAMEPAD, &report, sizeof(report));
            last_pad_buttons = current_pad_buttons;
        }
        if (consumer_changed) {
            usb_hid.sendReport(RID_CONSUMER, &current_consumer_usage, sizeof(current_consumer_usage));
            last_consumer_usage = current_consumer_usage;
        }
        // KV_VOLUME: drain one-shot volume pulse (press then release in successive handleHid() calls)
        if (pending_volume_usage != 0 && !pending_volume_release) {
            uint16_t usage_snap = pending_volume_usage;
            if (usb_hid.sendReport(RID_CONSUMER, &usage_snap, sizeof(usage_snap))) {
                pending_volume_release = true;  // next call sends the release
            }
        } else if (pending_volume_release) {
            uint16_t release = 0;
            if (usb_hid.sendReport(RID_CONSUMER, &release, sizeof(release))) {
                pending_volume_usage = 0;
                pending_volume_release = false;
            }
        }
    }
};



void HmiThread::handleMidi() {
    if (midiu.read()) {
        midi::MidiType t = midiu.getType();
        uint8_t d1 = midiu.getData1();
        uint8_t d2 = midiu.getData2();
        uint8_t c = midiu.getChannel();
        if (midiUsbSettings.route && midi2Settings.out) {
            midi2.send(t, d1, d2, c);        
        }
    }
    if (midi2.read()) {
        midi::MidiType t = midi2.getType();
        uint8_t d1 = midi2.getData1();
        uint8_t d2 = midi2.getData2();
        uint8_t c = midi2.getChannel();
        if (midi2Settings.route && midiUsbSettings.out) {
            midiu.send(t, d1, d2, c);        
        }
    }
};



void HmiThread::handleSysex(byte* array, unsigned size){
    if (array[0]==SYSEX_BINARIS_ID && array[1]==SYSEX_NANO_ID && array[2]==hmi_thread.midi_sysex_id) {
        Serial.println("Received a sysex message");
        // TODO handle sysex messages
    }
};




void HmiThread::updateKeyLeds() {
    int keys[4] = {0x1, 0x2, 0x4, 0x8};
    int leds[4][2] = {{3, 4}, {2, 5}, {1, 6}, {0, 7}};
    CRGB colors[4][2] = {
        {led_config.button_A_col_press, led_config.button_A_col_idle},
        {led_config.button_B_col_press, led_config.button_B_col_idle},
        {led_config.button_C_col_press, led_config.button_C_col_idle},
        {led_config.button_D_col_press, led_config.button_D_col_idle}
    };

    for (int i = 0; i < 4; i++) {
        CRGB color = (keyState & keys[i]) ? colors[i][0] : colors[i][1];
        ledsp[leds[i][0]] = color;
        ledsp[leds[i][1]] = color;
    }
    
};


// Define a variable to store the last time cur_pos was updated

void HmiThread::updateLeds() {
    // TODO: optimise this
    uint16_t cur_pos = foc_thread.pass_cur_pos();
    uint16_t start_pos = foc_thread.pass_start_pos();
    uint16_t end_pos = foc_thread.pass_end_pos();
    uint8_t device_orientation = DeviceSettings::getInstance().deviceOrientation;
    uint8_t led_orientation = map(device_orientation, 0, 3, 0, 135);
    uint16_t point, start, end;
    if (start_pos == end_pos) {
        // Degenerate/free-spin profile: map()'s divisor (in_max-in_min) would be 0 —
        // integer divide-by-zero panics the ESP32. Render a full ring instead.
        point = 0; start = 0; end = NANO_LED_A_NUM - 1;
    } else {
        point = map(cur_pos, end_pos, start_pos, 0, NANO_LED_A_NUM - 1);
        start = map(start_pos, end_pos, start_pos, 0, NANO_LED_A_NUM - 1);
        end   = map(end_pos, end_pos, start_pos, 0, NANO_LED_A_NUM - 1);
    }


    // Knob-turning detector: while moving show volume (pointer); when still and a
    // cover is up, show song progress on the ring instead.
    static uint16_t led_last_pos = 0;
    static unsigned long led_pos_ts = 0;
    if (cur_pos != led_last_pos) { led_pos_ts = millis(); led_last_pos = cur_pos; }
    bool turning = (millis() - led_pos_ts < 1000);
    bool cover   = DeviceSettings::getInstance().getActiveSprite().endsWith(".rgb565");

    if (cover && turning) {
        // Music + turning the knob: show the VOLUME level as a bright partial arc with
        // a dim remainder (a real volume meter), so it's clearly distinct from the full
        // idle album glow. Same ring geometry as the seek arc; derived from `point`.
        int vol_permille = constrain(1000 - (point * 1000) / (NANO_LED_A_NUM - 1), 0, 1000);
        seekRing(vol_permille, CRGB(led_config.primary_col), led_orientation);
        updateKeyLeds();
        FastLED.setBrightness(min(led_max_brightness, led_config.led_brightness));
    } else if (cover && !turning) {
        // Music idle: song-progress bar on the ring, album color (no R/G/B cycle).
        seekRing(DeviceSettings::getInstance().seekPermille, CRGB(led_config.primary_col), led_orientation);
        updateKeyLeds();
        FastLED.setBrightness(min(led_max_brightness, led_config.led_brightness));
    } else if (com_thread.global_sleep_flag && !cover) {
        hmi_thread.IdleLeds(25, CRGB::Red, CRGB::Green, CRGB::Blue);
        FastLED.setBrightness(25);
    } else {
        halvesPointer(point, start, end, led_orientation, (led_config.pointer_col), CRGB(led_config.primary_col), CRGB(led_config.secondary_col));
        updateKeyLeds();
        FastLED.setBrightness(led_config.led_brightness);
    }
};

    



// Standard Pointer with two halves
void HmiThread::halvesPointer(int indicator, int startpos, int endpos, int orientation, const struct CRGB& pointerCol, const struct CRGB& postCol, const struct CRGB& preCol){ 
    
    for (int i = NANO_LED_A_NUM - 1; i >= 0; i--) {
         if(i > indicator) {
            int index = ( i + orientation) % NANO_LED_A_NUM ;
             leds[index] = postCol;
         }
         if(i < indicator) {
            int index = ( i + orientation) % NANO_LED_A_NUM;
             leds[index] = preCol;
         }
    }
    int index = ( indicator + orientation) % NANO_LED_A_NUM;
    leds[index] = pointerCol;
    return;
};

// Song-progress bar on the ring, drawn with the SAME geometry as the volume
// pointer (halvesPointer): same `(i + orientation)` mapping, and progress grows in
// the same direction the volume increases (toward the volume-max end of the ring).
// permille < 0 (no seek yet) → full album ring.
void HmiThread::seekRing(int permille, const struct CRGB& col, int orientation){
    CRGB lit = col;
    CRGB dim = col; dim.nscale8(28);
    int N = NANO_LED_A_NUM;
    // thresh: N-1 at 0% (one lit LED at the origin) → 0 at 100% (full ring), mirroring
    // the volume's map(cur_pos, end_pos, start_pos, 0, N-1). Lit = i >= thresh.
    int thresh = (permille < 0) ? 0 : ((N - 1) * (1000 - permille)) / 1000;
    for (int i = 0; i < N; i++) {
        int index = (i + orientation) % N;
        leds[index] = (i >= thresh) ? lit : dim;
    }
    for (int i = 0; i < NANO_LED_B_NUM; i++) ledsp[i] = lit;
};

/*
    IdleLed animation
    Animates the LEDs with a color gradient
*/

static uint8_t colorIndex = 0;
void HmiThread::IdleLeds(int fps, const struct CRGB& idleColStart, const struct CRGB& idleColMid, const struct CRGB& idleColEnd){

    CRGB colors[] = {idleColStart ,idleColMid, idleColEnd};
    static unsigned long lastUpdateTime = 0;
    static bool increasing = false;
    static uint8_t darkness = 255;
    static uint8_t progress = 0;
    CRGB beginColor = colors[colorIndex];
    CRGB endColor = colors[(colorIndex + 1) % ARRAY_SIZE(colors)];
    CRGB currentColor = blend(beginColor, endColor, progress);

    // Fix 1: original loop ran to NANO_LED_A_NUM+8 (68), writing past end of leds[60].
    // Ring LEDs (leds[]) and button LEDs (ledsp[]) are separate arrays — fill each bounded loop.
    for (int i = 0; i < NANO_LED_A_NUM; i++) {
        leds[i] = currentColor;
    }
    for (int i = 0; i < NANO_LED_B_NUM; i++) {
        ledsp[i] = currentColor;
    }

    progress++;
    if (progress == 0) {  // Overflow, time to move to the next color
        colorIndex = (colorIndex + 1) % ARRAY_SIZE(colors);
    }
    if(!com_thread.global_sleep_flag)
        return;
}

STUSB4500 usb_pd;

PowerType HmiThread::init_pd() {
    Wire.begin(PIN_NANO_I2C_SDA, PIN_NANO_I2C_SCL);

    // Fix 2a: guard — if STUSB4500 is absent do not proceed; boot continues safely on 5V USB.
    if (!usb_pd.begin()) {
        Serial.println("STUSB4500 not found; defaulting to 5V USB");
        DeviceSettings::getInstance().setPdVoltage(5.0f); // thread-safe setter from FW5
        _pd_negotiated_current = 0.9f;  // USB 2.0 default 900 mA
        return POWER_5V_USB;
    }
    Serial.println("STUSB4500 found");

    // Only write NVM when the profile is not already correct, avoiding unnecessary flash wear.
    // Guard checks PDO count AND the key PDO2 parameters so a previously-programmed NVM
    // with wrong voltage-limit percentages is detected and reprogrammed.
    bool needsProgram = (usb_pd.getPdoNumber() != 2)
                     || (usb_pd.getVoltage(2) != 9.0f)
                     || (usb_pd.getCurrent(2) != 3.0f)
                     || (usb_pd.getVoltage(1) != 5.0f);
    if (needsProgram) {
        Serial.println("Programming STUSB4500 NVM with USB-PD profiles");
        usb_pd.setUsbCommCapable(true);
        // PDO1 — 5 V fallback (always present per USB-PD spec)
        usb_pd.setVoltage(1, 5.0);
        usb_pd.setCurrent(1, 3.0);
        usb_pd.setLowerVoltageLimit(1, 20);
        usb_pd.setUpperVoltageLimit(1, 20);
        // PDO2 — 9 V high-power profile (board maximum, clamped externally)
        usb_pd.setVoltage(2, 9.0);
        usb_pd.setCurrent(2, 3.0);
        usb_pd.setLowerVoltageLimit(2, 20);
        usb_pd.setUpperVoltageLimit(2, 20);  // 20 % tolerance — standard for STUSB4500
        // Only two PDOs active; PDO3 slot is left at NVM default (unused).
        usb_pd.setPdoNumber(2);
        usb_pd.write();

        // Fix 3: use the SparkFun library's softReset() instead of a raw Wire
        // register write that was prepared but never transmitted (endTransmission
        // was called but the frame was never actually sent as a complete I2C write
        // with data). softReset() handles the full register sequence internally.
        // 1000 ms settle delay gives the PD re-negotiation window enough time.
        usb_pd.softReset();
        delay(1000); // allow re-negotiation to complete

        // Datasheet-correct NVM apply: hard-reset the STUSB4500 so it reloads the
        // freshly-written NVM and re-runs negotiation from it (softReset() renegotiates
        // but does NOT reload NVM). Schematic: PD_RESET (active-high, R17 10k pulldown)
        // <- ESP32 GPIO15. CAUTION: while the STUSB resets it releases SNK_VBUS_EN, so
        // Q2 un-gates Vmot and the ESP32 (powered from Vmot via U9) may power-cycle
        // itself — same effect as the datasheet's "re-plug after NVM flash". Runs at
        // most once per NVM change (needsProgram guards it), so no boot loop.
        pinMode(PIN_PD_RESET, OUTPUT);
        digitalWrite(PIN_PD_RESET, HIGH);
        delay(15);
        digitalWrite(PIN_PD_RESET, LOW);
        delay(1000); // NVM reload + renegotiation window (if we kept power)
    }

    // Fix 2d: read the selected PDO voltage; PDO1=5V is always the fallback per our NVM config.
    // The SparkFun library exposes getVoltage(pdoNum) for the NVM-configured values.
    // We read the active PDO number by querying the STATUS register bit TYPEC_FSM_STATE.
    // Simpler: read both configured PDO voltages and pick based on which was negotiated.
    // STUSB4500 RDO_REG_STATUS (0x91) bits [30:28] = selected object number (1-indexed).
    uint8_t selected_pdo = 1; // default to PDO1 (5V)
    Wire.beginTransmission(0x28);
    Wire.write(0x91); // RDO_REG_STATUS register (selected PDO object position)
    if (Wire.endTransmission(false) == 0) {
        if (Wire.requestFrom((uint8_t)0x28, (uint8_t)4) == 4) {
            uint8_t b0 = Wire.read();
            uint8_t b1 = Wire.read();
            uint8_t b2 = Wire.read();
            uint8_t b3 = Wire.read();
            // Stash the raw RDO + both byte-order decodes for the {"pd":"status"}
            // diagnostic so the actual negotiated PDO can be confirmed empirically.
            g_pd_rdo[0] = b0; g_pd_rdo[1] = b1; g_pd_rdo[2] = b2; g_pd_rdo[3] = b3;
            g_pd_sel_hi = (b3 >> 4) & 0x07;
            g_pd_sel_lo = (b0 >> 4) & 0x07;
            // RDO object position is in bits [30:28] of the 32-bit register (big-endian).
            // See docs/KNOWN_RESIDUALS.md — byte ordering unverified on hardware; use
            // -DPD_RDO_ALT_BYTE to test big-endian first byte (b0) instead of b3.
#ifdef PD_RDO_ALT_BYTE
            selected_pdo = (b0 >> 4) & 0x07;
#else
            selected_pdo = (b3 >> 4) & 0x07;
#endif
            if (selected_pdo == 0) selected_pdo = 1; // 0 means no contract yet
        }
    }

    // Type-C CC current advertisement — works WITHOUT a PD contract (e.g. through a
    // non-PD hub, RDO stays 0 but the source still advertises via its CC pull-up).
    // CC_STATUS reg 0x11: bits[1:0]=CC1, bits[3:2]=CC2 → 0=default(500/900mA),
    // 1=1.5A, 2=3.0A. This is the honest supply budget when no contract formed.
    uint8_t cc_adv = 0;
    Wire.beginTransmission(0x28);
    Wire.write(0x11);
    if (Wire.endTransmission(false) == 0 &&
        Wire.requestFrom((uint8_t)0x28, (uint8_t)1) == 1) {
        uint8_t cc = Wire.read();
        uint8_t a = cc & 0x03, b = (cc >> 2) & 0x03;
        cc_adv = (a > b) ? a : b;
        if (cc_adv > 2) cc_adv = 0; // 3 = reserved
    }
    g_pd_cc_adv = cc_adv;
    bool have_contract = (g_pd_rdo[0] | g_pd_rdo[1] | g_pd_rdo[2] | g_pd_rdo[3]) != 0;

    float negotiated_v = usb_pd.getVoltage(selected_pdo);
    float negotiated_i = usb_pd.getCurrent(selected_pdo);

    // Clamp to board-safe [5.0, 9.0] V regardless of what the charger offered.
    if (negotiated_v < 5.0f || negotiated_v > 9.0f) {
        Serial.printf("PD voltage %.1fV out of range, clamping to 5.0V\n", negotiated_v);
        negotiated_v = 5.0f;
        negotiated_i = 0.9f; // USB default 900 mA when no PD contract
    }
    if (negotiated_i <= 0.0f) {
        negotiated_i = 0.9f; // guard: library returns 0 for PDO1 on some NVM states
    }

    // Supply budget (mA at VBUS): contract current when a PD contract exists, else the
    // CC advertisement. FOC (motor limit) and HMI (LED power cap) budget from this.
    uint32_t budget_ma;
    if (have_contract) {
        budget_ma = (uint32_t)(negotiated_i * 1000.0f);
    } else {
        budget_ma = (cc_adv == 2) ? 3000 : (cc_adv == 1) ? 1500 : 900;
        negotiated_i = budget_ma / 1000.0f; // report the advertised budget, not NVM wishes
    }

    // Store into DeviceSettings so foc_thread can consume voltage before motor init.
    DeviceSettings::getInstance().setPdVoltage(negotiated_v);
    DeviceSettings::getInstance().pdBudgetMa = budget_ma;
    // Store current so com_thread can serve {"pd":"?"} queries.
    _pd_negotiated_current = negotiated_i;
    Serial.printf("PD: contract=%d PDO%d %.1fV cc_adv=%d -> budget %lumA\n",
                  (int)have_contract, selected_pdo, negotiated_v, cc_adv,
                  (unsigned long)budget_ma);

    if (negotiated_v >= 9.0f) return POWER_9V_PD;
    if (negotiated_v > 5.0f)  return POWER_5V_PD; // 6/7/8V variants possible
    return POWER_5V_USB;
}