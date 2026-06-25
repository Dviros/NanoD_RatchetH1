
#pragma once

#include <inttypes.h>
#include <Arduino.h>

/* Sound IDs for the play() convenience API */
typedef enum : uint8_t {
    SOUND_NONE  = 0,
    SOUND_SOFT  = 1,
    SOUND_HARD  = 2,
    SOUND_LOUD  = 3,
    SOUND_CLACK = 4,
    SOUND_CHIME = 5,
} SoundId;

typedef struct {
    uint8_t audio_feedback_lvl;

    /*
        Audio File (UI: Audio Magnitude) – pre-baked PROGMEM WAV samples.
        Files: 'faint'[0] 'soft'[1] 'default'[2] 'medium'[3] 'hard'[4]
        Mono 16-bit 22 050 Hz by default; stereo variant guarded by AUDIO_FILES_STEREO.
    */
    uint8_t* audio_file;     // haptic detent click sample
    uint8_t* key_audio_file; // key-press click sample

} audioConfig;


/* WAV data arrays – always declared so profile/settings code compiles cleanly.
 * When NANO_AUDIO=0 these resolve to 1-byte sentinel stubs (see audio.cpp).
 * Only the NANO_AUDIO build actually plays them. */
extern uint8_t soft_wav[];
extern uint8_t hard_wav[];
extern uint8_t loud_wav[];
extern uint8_t clack_wav[];
extern uint8_t chime_wav[];

uint8_t*  get_audio_file(String fName);
String    get_audio_filename(uint8_t* audio_file);

/* ------------------------------------------------------------------
 * High-level convenience API – callable from haptic / HMI layer.
 * Guards are inside the implementation so callers need no #if NANO_AUDIO.
 * ------------------------------------------------------------------ */

/** Play a named sound at the default volume. No-op when NANO_AUDIO=0. */
void audio_play(SoundId id);

/** Trigger the currently-configured haptic click sound. No-op when NANO_AUDIO=0.
 *  Call this from detent callbacks and button handlers. */
void audio_click();
