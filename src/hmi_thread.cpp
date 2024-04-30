#include "hmi_thread.h"
#include "com_thread.h"
#include "foc_thread.h"
#include <Adafruit_TinyUSB.h>
#include "MIDI.h"
#include "audio/audio.h"

using namespace ace_button;

Adafruit_USBD_MIDI usb_midi(1);

MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, midiu);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, midi2)

enum {
  RID_KEYBOARD = 1,
  RID_MOUSE = 2,
  RID_GAMEPAD = 3,
};


uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD( HID_REPORT_ID(RID_KEYBOARD) ),
  TUD_HID_REPORT_DESC_MOUSE   ( HID_REPORT_ID(RID_MOUSE) ),
  TUD_HID_REPORT_DESC_GAMEPAD( HID_REPORT_ID(RID_GAMEPAD) )
};

// USB HID object
Adafruit_USBD_HID usb_hid;





// Hmi thread controls LED via FastLed and buttons via AceButton

HmiThread::HmiThread(const uint8_t task_core ) : Thread("HMI", 4096, 1, task_core) {
    _q_config_in = xQueueCreate(2, sizeof( ledConfig ));
    _q_hmi_config_in = xQueueCreate(2, sizeof( hmiConfig ));
    _q_settings_in = xQueueCreate(2, sizeof( DeviceSettings ));
    _q_keyevt_out = xQueueCreate(5, sizeof( KeyEvt ));
}

HmiThread::~HmiThread() {}


// init_usb() must be called before the thread is started
void HmiThread::init_usb() {
  usb_midi.setStringDescriptor("Nano_D MIDI");
  //usb_midi.setCableName(1, "Port1");
  //usb_midi.setCableName(2, "Port THRU");
  usb_midi.begin();
  midiu.begin();
//   midi1.setThruFilterMode(midi::Thru::Off);
//   midi2.setThruFilterMode(midi::Thru::Off);

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
    FastLED.setBrightness(led_config.led_brightness);
    midiUsbSettings = DeviceSettings::getInstance().midiUsb;
    midi2Settings = DeviceSettings::getInstance().midi2;
    Serial2.begin(31250, SERIAL_8N1, PIN_SERIAL2_RX, PIN_SERIAL2_TX);
    midi2.begin();

    audioPlayer.audio_init();
};


void HmiThread::put_led_config(ledConfig& new_config) {
    xQueueSend(_q_config_in, &new_config, (TickType_t)0);
};


void HmiThread::put_hmi_config(hmiConfig& new_config){
    xQueueSend(_q_hmi_config_in, &new_config, (TickType_t)0);
};


void HmiThread::put_settings(DeviceSettings& new_settings){
    xQueueSend(_q_settings_in, &new_settings, (TickType_t)0);
};


void HmiThread::handleConfig() {
    ledConfig newConfig;
    if (xQueueReceive(_q_config_in, &newConfig, (TickType_t)0)) {
        led_config = newConfig;
        FastLED.setBrightness(led_config.led_brightness);
        updateKeyLeds();
    }
    hmiConfig newHmiConfig;
    if (xQueueReceive(_q_hmi_config_in, &newHmiConfig, (TickType_t)0)) {
        hmi_config = newHmiConfig;
    }
};


void HmiThread::handleSettings() {
    DeviceSettings newSettings;
    if (xQueueReceive(_q_settings_in, &newSettings, (TickType_t)0)) {
        midiUsbSettings = newSettings.midiUsb;
        midi2Settings = newSettings.midi2;
        midiu.setThruFilterMode(midiUsbSettings.thru? midi::Thru::Full : midi::Thru::Off);
        midiu.setInputChannel(midiUsbSettings.in? MIDI_CHANNEL_OMNI : MIDI_CHANNEL_OFF);
        midi2.setThruFilterMode(midi2Settings.thru? midi::Thru::Full : midi::Thru::Off);
        midi2.setInputChannel(midi2Settings.in? MIDI_CHANNEL_OMNI : MIDI_CHANNEL_OFF);
        if (FastLED.getBrightness() > newSettings.ledMaxBrightness) {
            FastLED.setBrightness(newSettings.ledMaxBrightness);
            led_config.led_brightness = newSettings.ledMaxBrightness;
            updateKeyLeds();
        }
        Serial.println("Hmi settings updated from global settings");
    }
};


bool HmiThread::get_key_event(KeyEvt* keyEvt){
    return xQueueReceive(_q_keyevt_out, keyEvt, (TickType_t)0);
};



void HmiThread::run() {
    FastLED.addLeds<LED_CHIPSET, PIN_LED_A, RGB>(leds, NANO_LED_A_NUM);
    FastLED.addLeds<LED_CHIPSET, PIN_LED_B, LED_COL_ORDER>(ledsp, NANO_LED_B_NUM);
    FastLED.setBrightness( DEFAULT_LED_MAX_BRIGHTNESS );
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
    }
    ledsp[3] = CRGB(led_config.button_A_col_idle);
    ledsp[4] = CRGB(led_config.button_A_col_idle);
    ledsp[2] = CRGB(led_config.button_B_col_idle);
    ledsp[5] = CRGB(led_config.button_B_col_idle);
    ledsp[1] = CRGB(led_config.button_C_col_idle);
    ledsp[6] = CRGB(led_config.button_C_col_idle);
    ledsp[0] = CRGB(led_config.button_D_col_idle);
    ledsp[7] = CRGB(led_config.button_D_col_idle);

    unsigned long total = 0;
    unsigned long updates = 0;
    unsigned long ts = micros();

    while (1) {
        handleSettings();
        handleConfig();
        handleMidi();
        for (int i = 0; i < 4; i++)
            buttons[i]->check();
        updateValue();
        handleHid();
        updateLeds();
        
        unsigned long us = micros();
        FastLED.show();
        unsigned long now = micros();
        us = now - us;
        updates++;
        total += us;
        if (now - ts > 10000000) {
            float fps = 1000000.0 * updates / total;
            float ups = 1000000.0 * updates / (now - ts);
            StringMessage msg(new String("LED rate: " + String(ups) + " updates/s, "+ String(fps) +" frames/s raw (= " + String(fps * 64) + " pix/s = " + String(fps * 64 * 3 * 8 / 1000000) + "Mbps"));
            com_thread.put_string_message(msg);
            ts = now;
            total = 0;
            updates = 0;
        }

        audioPlayer.audio_loop();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
};




HmiThreadButtonHandler::HmiThreadButtonHandler(uint8_t _index) : index(_index) {};



void HmiThreadButtonHandler::handleEvent(AceButton* button, uint8_t eventType, uint8_t buttonState) {
    switch (eventType) {
        case AceButton::kEventPressed:
            hmi_thread.keyState |= (1<<index);
            for (int i=0; i<hmi_thread.hmi_config.keys[index].num_pressed_actions; i++) {
                hmi_thread.handleKeyAction(hmi_thread.hmi_config.keys[index].pressed[i], eventType);
            }
            if (audioPlayer.audio_config.key_audio_file!=nullptr)
                audioPlayer.play_audio(audioPlayer.audio_config.key_audio_file, audioPlayer.audio_config.audio_feedback_lvl);
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
    }
    KeyEvt keyEvt = { .type=eventType, .keyNum=(uint8_t)index, .keyState=hmi_thread.keyState };
    xQueueSend(hmi_thread._q_keyevt_out, &keyEvt, (TickType_t)0);
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
    }
};



void HmiThread::updateValue() {
    if (hmi_config.knob.num>0) {
        float angle = foc_thread.get_motor_angle();
        for (int i=0;i<hmi_config.knob.num;i++) {
            knobValue& v = hmi_config.knob.values[i];
            if (v.key_state==keyState) {
                float value = 0;
                if (v.angle_min<v.angle_max) {
                    _constrain(angle, v.angle_min, v.angle_max);
                }
                else {
                    _constrain(angle, v.angle_max, v.angle_min);
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
                currentValue = value;
                if (currentValue!=lastValue) {
                    if (v.type==knobValueType::KV_MIDI) {
                        uint8_t midi_value = (uint8_t)(value);
                        midi_value = _constrain(midi_value, 0, 127);
                        if (midiUsbSettings.nano)
                            midiu.sendControlChange(v.midi.cc, midi_value, v.midi.channel);
                        if (midi2Settings.nano)
                            midi2.sendControlChange(v.midi.cc, midi_value, v.midi.channel);
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

    if ( TinyUSBDevice.suspended() && (keys_changed||mouse_changed||pad_changed) ) {
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





void HmiThread::updateKeyLeds() {
    ledsp[3] = CRGB((keyState&0x1)?led_config.button_A_col_press:led_config.button_A_col_idle);
    ledsp[4] = CRGB((keyState&0x1)?led_config.button_A_col_press:led_config.button_A_col_idle);
    ledsp[2] = CRGB((keyState&0x2)?led_config.button_B_col_press:led_config.button_B_col_idle);
    ledsp[5] = CRGB((keyState&0x2)?led_config.button_B_col_press:led_config.button_B_col_idle);
    ledsp[1] = CRGB((keyState&0x4)?led_config.button_C_col_press:led_config.button_C_col_idle);
    ledsp[6] = CRGB((keyState&0x4)?led_config.button_C_col_press:led_config.button_C_col_idle);
    ledsp[0] = CRGB((keyState&0x8)?led_config.button_D_col_press:led_config.button_D_col_idle);
    ledsp[7] = CRGB((keyState&0x8)?led_config.button_D_col_press:led_config.button_D_col_idle);
};



void HmiThread::updateLeds() {
    uint16_t state = foc_thread.pass_cur_pos();
    uint16_t start = foc_thread.pass_start_pos();
    uint16_t end = foc_thread.pass_end_pos();
    bool at_limit = foc_thread.pass_at_limit();
    uint8_t pointer = (-foc_thread.get_motor_angle() / 6.283185307179586f) * 60; // TODO take device orientation into account
    uint16_t point = map(state, start, end, 0, NANO_LED_A_NUM - 1);
    halvesPointer(point, CRGB(led_config.pointer_col), CRGB(led_config.primary_col), CRGB(led_config.secondary_col));
    vTaskDelay(5);
}

    



// Standard Pointer with two halves
void HmiThread::halvesPointer(int indicator, const struct CRGB& pointerCol, const struct CRGB& preCol, const struct CRGB& postCol){ 
    for (int i = 0; i < NANO_LED_A_NUM; ++i) {
         if(i < indicator) {
             leds[i] = postCol;
         }
         if(i > indicator) {
             leds[i] = preCol;
         }
    }
    leds[indicator] = pointerCol;
};



// Quick Strobe Single Color
void HmiThread::strobe(int pps, const struct CRGB& strobeCol){
    fill_solid(leds, 60, strobeCol);
    FastLED.show();
    vTaskDelay(1000/pps / portTICK_PERIOD_MS);
    
    fill_solid(leds, 60, CRGB::Black);
    FastLED.show();
    vTaskDelay(1000/pps / portTICK_PERIOD_MS);
};




void HmiThread::breathing(int fps, const struct CRGB& fadeCol){
    for(int i = 0; i < NANO_LED_A_NUM; i++ ) {
        leds[i] = fadeCol;  // Set Color HERE!!!
        leds[i].fadeToBlackBy(brightness);
    }
    FastLED.show();
    brightness = brightness + fadeAmount;
    brightness = constrain(brightness, 0, led_config.led_brightness);
    // reverse the direction of the fading at the ends of the fade: 
    if(brightness <= 0 || brightness >= led_config.led_brightness) {
        fadeAmount = -fadeAmount ;
    }
    vTaskDelay(1000/fps / portTICK_PERIOD_MS); 
};

