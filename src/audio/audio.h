
#pragma once

#include <inttypes.h>
#include <Arduino.h>
#include "./audio_api.h"

/* All internals are compiled only when audio hardware is present.
 * The public audio_play() / audio_click() stubs in audio_api.h are
 * always visible but become empty inlines when NANO_AUDIO == 0. */

#if NANO_AUDIO

#include <driver/i2s.h>

typedef enum {
    AUDIO_CMD_NONE     = 0x00,
    AUDIO_CMD_CONFIG   = 0x01,
    AUDIO_CMD_HAPTIC   = 0x02,
    AUDIO_CMD_PLAY_WAV = 0x03
} AudioCommandType;

struct AudioCommand {
    AudioCommandType type;
    union {
        audioConfig  config;
        uint8_t*     audio_file;
    };
};

class BinarisAudioPlayer {
    friend class HmiThreadButtonHandler;
    friend class HmiThread;
public:
    BinarisAudioPlayer();
    ~BinarisAudioPlayer();
    void audio_init();
    void play_audio(uint8_t* audio_file, uint16_t volume);
    void put_audio_config(audioConfig& config);
    void play_haptic_audio();
    void audio_loop();
    bool check_file(String fName, uint8_t* audio_file);
    // public read of the active feedback level (free helpers play at this volume)
    uint16_t feedback_lvl() const { return audio_config.audio_feedback_lvl; }
protected:
    void handle_audio_commands();
    void start_play(uint8_t* audio_file);

    i2s_driver_config_t i2s_config;
    i2s_pin_config_t    pin_config;

    QueueHandle_t _q_audio_in;
    audioConfig   audio_config; // active haptic-click config

    size_t   num_bytes_remaining = 0;
    uint8_t* data_ptr            = nullptr;
};

extern BinarisAudioPlayer audioPlayer;

#endif /* NANO_AUDIO */
