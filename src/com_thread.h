#pragma once

#include <Arduino.h>
#include "thread_crtp.h"
#include <HardwareSerial.h>
#include <MIDI.h>
#include <ArduinoJSON.h>
#include "HapticProfileManager.h"
#include <atomic>      // FW3: global_sleep_flag must be std::atomic<bool>


enum StringMessageType {
    STRING_MESSAGE_DEBUG,
    STRING_MESSAGE_ERROR,
    STRING_MESSAGE_MOTOR,
    STRING_MESSAGE_PROFILE,
    STRING_MESSAGE_NEXT_PROFILE,
    STRING_MESSAGE_PREV_PROFILE
};

class StringMessage {
    public:
        StringMessage(String* message = nullptr, StringMessageType type = StringMessageType::STRING_MESSAGE_DEBUG) : message(message),  type(type) {};
        String* message;// = nullptr;
        StringMessageType type;
};



class ComThread : public Thread<ComThread> {
    friend class Thread<ComThread>; //Allow Base Thread to invoke protected run()
    public:
        ComThread(const uint8_t task_core);
        ~ComThread();

        void setCurrentProfile(String name);
        void put_string_message(const StringMessage& msg);
        bool isProfileNameOk(String& name);

        // FW3: std::atomic so HMI/LCD threads can read safely without a mutex
        std::atomic<bool> global_sleep_flag{false};
        unsigned long ts_last_activity;
        uint32_t global_idle_timeout = 5000;

        // FW6: network input — wifi_thread calls net_submit() from its task;
        // com_thread drains _q_net_in inside its own run() loop (single parser owner).
        // line must be heap-allocated (new String); com_thread deletes it after use.
        void net_submit(String* line);

        // FW6: wifi_thread registers its outbound queue so emit() can forward frames.
        // out queue items are heap-allocated String* — wifi_thread deletes after send.
        void net_attach_out(QueueHandle_t out);

    protected:
        void run();
        void handleProfileCommand(JsonVariant profile, JsonVariant updates);
        void handleSettingsCommand(JsonVariant s);
        void handleProfilesCommand(JsonVariant p);
        // FW6: shared command dispatcher (Serial + net-in paths). Static member so it
        // can reach the protected handlers on the passed-in instance.
        static void processCommand(JsonDocument& doc, ComThread& self);
        void handleMessages();
        void handleEvents();

        void dispatchLedConfig();
        void dispatchHapticConfig();
        void dispatchHmiConfig();
        void dispatchSettings();
        void dispatchAudioConfig();
        void dispatchLcdConfig();

        String generateDescription(HapticProfile& curr);

        void sendError(String& error, String* msg = nullptr);
        void sendError(String& error, String& msg);
        void sendError(const char* error, String& msg);
        void sendError(const char* error, const char* msg = nullptr);

        // FW3: emit {"ack":"<cmd>","ok":true/false,"error":"..."} for every mutating command
        void sendAck(const char* cmd, bool ok, const char* errMsg = nullptr);

        // FW6: single emit path — writes to Serial AND forwards to net-out queue if attached.
        // frame must already be a complete newline-terminated JSON string.
        void emit(const String& frame);

        QueueHandle_t _q_strings_in;

        // FW6: inter-task queues for network I/O (both nullptr until wifi attaches)
        QueueHandle_t _q_net_in  = nullptr;   // String* lines from wifi → com
        QueueHandle_t _q_net_out = nullptr;   // String* frames from com → wifi
};


extern ComThread com_thread;
