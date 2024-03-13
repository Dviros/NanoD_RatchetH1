#pragma once

#include <Arduino.h>
#include <AceButton.h>
#include <FastLED.h>
#include "thread_crtp.h"
#include "nanofoc_d.h"
#include "./led_api.h"
#include "./hmi_api.h"
#include "./DeviceSettings.h"


class HmiThread : public Thread<HmiThread>, public ace_button::IEventHandler {
    friend class Thread<HmiThread>; //Allow Base Thread to invoke protected run()

    public:
        HmiThread(const uint8_t task_core);
        ~HmiThread();
       
        void init_usb();
        void init(ledConfig& initial_led_config, hmiConfig& initial_hmi_config);
    
        // queues
        void put_led_config(ledConfig& new_config);
        bool get_key_event(KeyEvt* keyEvt);
        void put_hmi_config(hmiConfig& new_config);
        void put_settings(DeviceSettings& new_settings);

        // Light Effects
        void halvesPointer(int indicator, const struct CRGB& pointerCol, const struct CRGB& preCol, const struct CRGB& postCol);
        void strobe(int pps, const struct CRGB& strobeCol);
        void breathing(int fps, const struct CRGB& fadeCol);

    protected:
        void run();
        
        QueueHandle_t _q_config_in;
        QueueHandle_t _q_hmi_config_in;
        QueueHandle_t _q_settings_in;
        QueueHandle_t _q_keyevt_out;
        // internal queue handler
        void handleConfig();
        void handleSettings();

        // LEDs
        ledConfig led_config;
        CRGB leds[NANO_LED_A_NUM];
        CRGB ledsp[NANO_LED_B_NUM];
        void updateKeyLeds();
        void updateLeds();

        // buttons
        hmiConfig hmi_config;
        ace_button::AceButton keyA;
        ace_button::AceButton keyB;
        ace_button::AceButton keyC;
        ace_button::AceButton keyD;
        uint8_t keyState = 0;
        // button handler
        void handleEvent(ace_button::AceButton* button, uint8_t eventType, uint8_t buttonState) override;
        void handleKeyAction(keyAction& action);

        // midi config
        void handleMidi();
        midiSettings midiUsbSettings;
        midiSettings midi2Settings;

        // animations
        bool gReverseDirection = false;
        int brightness = 0;
        int fadeAmount = 5;

};

extern HmiThread hmi_thread;
