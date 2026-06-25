
#include "audio.h"
#include "nanofoc_d.h"
#include "../haptic.h"

/* -----------------------------------------------------------------------
 * Convenience API stubs – always compiled, no-ops when audio is off.
 * These are the only symbols called by hmi_thread / haptic layer so
 * those callers need no #if NANO_AUDIO guards of their own.
 * --------------------------------------------------------------------- */

void audio_play(SoundId id) {
#if NANO_AUDIO
    uint8_t* wav = nullptr;
    switch (id) {
        case SOUND_SOFT:  wav = soft_wav;  break;
        case SOUND_HARD:  wav = hard_wav;  break;
        case SOUND_LOUD:  wav = loud_wav;  break;
        case SOUND_CLACK: wav = clack_wav; break;
        case SOUND_CHIME: wav = chime_wav; break;
        default: break;
    }
    if (wav) audioPlayer.play_audio(wav, audioPlayer.audio_config.audio_feedback_lvl);
#else
    (void)id;
#endif
}

void audio_click() {
#if NANO_AUDIO
    audioPlayer.play_haptic_audio();
#endif
}

/* -----------------------------------------------------------------------
 * Everything below is compiled only when NANO_AUDIO is enabled.
 * --------------------------------------------------------------------- */

#if NANO_AUDIO

#ifdef USE_AUDIO_LIB
#include "XT_I2S_Audio.h"
static XT_I2S_Class*          xt_player        = nullptr;
static XT_PlayListItem_Class* clack_wav_sample  = nullptr;
static XT_PlayListItem_Class* loud_wav_sample   = nullptr;
static XT_PlayListItem_Class* soft_wav_sample   = nullptr;
static XT_PlayListItem_Class* hard_wav_sample   = nullptr;
static XT_PlayListItem_Class* chime_wav_sample  = nullptr;
#endif /* USE_AUDIO_LIB */

#ifndef USE_AUDIO_LIB
#define SAMPLES_PER_SEC 22050
#endif
#define DEFAULT_VOLUME 100


BinarisAudioPlayer audioPlayer; // global instance


/* --- WAV name <-> pointer helpers -------------------------------------- */

uint8_t* get_audio_file(String fName) {
    if (fName == "loud")  return loud_wav;
    if (fName == "soft")  return soft_wav;
    if (fName == "hard")  return hard_wav;
    if (fName == "clack") return clack_wav;
    if (fName == "chime") return chime_wav;
    return nullptr;
}

String get_audio_filename(uint8_t* audio_file) {
    if (audio_file == loud_wav)  return "loud";
    if (audio_file == soft_wav)  return "soft";
    if (audio_file == hard_wav)  return "hard";
    if (audio_file == clack_wav) return "clack";
    if (audio_file == chime_wav) return "chime";
    return "none";
}


/* --- BinarisAudioPlayer ------------------------------------------------ */

BinarisAudioPlayer::BinarisAudioPlayer() {
    _q_audio_in = xQueueCreate(2, sizeof(AudioCommand));
    assert(_q_audio_in != NULL);
}

BinarisAudioPlayer::~BinarisAudioPlayer() {}


void BinarisAudioPlayer::audio_init() {
    // Validate embedded WAV headers at startup so mismatches are caught early.
    check_file("hard.wav",  hard_wav);
    check_file("soft.wav",  soft_wav);
    check_file("clack.wav", clack_wav);
    check_file("loud.wav",  loud_wav);
    check_file("chime.wav", chime_wav);

    audio_config.audio_feedback_lvl = DEFAULT_VOLUME;
    audio_config.audio_file         = hard_wav;
    audio_config.key_audio_file     = nullptr;

#ifdef USE_AUDIO_LIB
    xt_player       = new XT_I2S_Class(PIN_I2S_LRC, PIN_I2S_BCLK, PIN_I2S_DOUT, I2S_NUM_0);
    xt_player->Volume = audio_config.audio_feedback_lvl;
    clack_wav_sample = new XT_Wav_Class((const unsigned char*)clack_wav);
    loud_wav_sample  = new XT_Wav_Class((const unsigned char*)loud_wav);
    soft_wav_sample  = new XT_Wav_Class((const unsigned char*)soft_wav);
    hard_wav_sample  = new XT_Wav_Class((const unsigned char*)hard_wav);
    chime_wav_sample = new XT_Wav_Class((const unsigned char*)chime_wav);
    data_ptr = nullptr; // not used in XT_I2S path
    Serial.println("[audio] XT_I2S lib ready");
#else
    /* Raw I2S driver path (default, no extra library). */
    i2s_config.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_config.sample_rate          = SAMPLES_PER_SEC;
    i2s_config.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
#if defined(AUDIO_FILES_STEREO)
    i2s_config.chan_mask            = I2S_CHANNEL_STEREO;
    i2s_config.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
#else
    i2s_config.chan_mask            = I2S_CHANNEL_MONO;
    i2s_config.channel_format       = I2S_CHANNEL_FMT_ALL_LEFT;
#endif
    i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_config.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count        = 8;
    i2s_config.dma_buf_len          = 256;
    i2s_config.use_apll             = 0;
    i2s_config.tx_desc_auto_clear   = true;
    i2s_config.fixed_mclk           = -1;

    pin_config.bck_io_num    = PIN_I2S_BCLK;
    pin_config.ws_io_num     = PIN_I2S_LRC;
    pin_config.data_out_num  = PIN_I2S_DOUT;
    pin_config.data_in_num   = I2S_PIN_NO_CHANGE;

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_set_sample_rates(I2S_NUM_0, SAMPLES_PER_SEC);
    Serial.println("[audio] raw I2S ready");
#endif /* USE_AUDIO_LIB */
}


/* Queue a raw WAV pointer for playback. Call from any thread. */
void BinarisAudioPlayer::play_audio(uint8_t* audio_file, uint16_t /*volume*/) {
    if (audio_file == nullptr) return;
    AudioCommand cmd{ .type = AUDIO_CMD_PLAY_WAV, .audio_file = audio_file };
    xQueueSend(_q_audio_in, &cmd, (TickType_t)0);
}

/* Queue the current haptic-click sample. Call from any thread. */
void BinarisAudioPlayer::play_haptic_audio() {
    AudioCommand cmd{ .type = AUDIO_CMD_HAPTIC };
    xQueueSend(_q_audio_in, &cmd, (TickType_t)0);
}

/* Update the active audio config (sample + level). */
void BinarisAudioPlayer::put_audio_config(audioConfig& config) {
    AudioCommand cmd{ .type = AUDIO_CMD_CONFIG, .config = config };
    xQueueSend(_q_audio_in, &cmd, (TickType_t)0);
}


void BinarisAudioPlayer::start_play(uint8_t* audio_file) {
#ifdef USE_AUDIO_LIB
    XT_PlayListItem_Class* sample = nullptr;
    if      (audio_file == clack_wav) sample = clack_wav_sample;
    else if (audio_file == loud_wav)  sample = loud_wav_sample;
    else if (audio_file == soft_wav)  sample = soft_wav_sample;
    else if (audio_file == hard_wav)  sample = hard_wav_sample;
    else if (audio_file == chime_wav) sample = chime_wav_sample;

    if (sample == nullptr || xt_player == nullptr) {
        Serial.println("[audio] start_play: missing sample or player");
        return;
    }
    xt_player->Play(sample);
#else
    /* Point data_ptr past the 44-byte WAV header; read data-chunk size. */
    data_ptr = &audio_file[44];
    num_bytes_remaining = (uint32_t)audio_file[40]
                        | ((uint32_t)audio_file[41] << 8)
                        | ((uint32_t)audio_file[42] << 16)
                        | ((uint32_t)audio_file[43] << 24);
#endif
}


/* Validate a WAV header against the expected format.
 * Returns true if the file is valid for the current build config. */
bool BinarisAudioPlayer::check_file(String fName, uint8_t* audio_file) {
    // Check RIFF/WAVE/fmt /data magic bytes
    if (!(audio_file[0]  == 'R' && audio_file[1]  == 'I' &&
          audio_file[2]  == 'F' && audio_file[3]  == 'F' &&
          audio_file[8]  == 'W' && audio_file[9]  == 'A' &&
          audio_file[10] == 'V' && audio_file[11] == 'E' &&
          audio_file[12] == 'f' && audio_file[13] == 'm' &&
          audio_file[14] == 't' && audio_file[15] == ' ' &&
          audio_file[36] == 'd' && audio_file[37] == 'a' &&
          audio_file[38] == 't' && audio_file[39] == 'a')) {
        Serial.println(fName + ": not a valid WAV file");
        return false;
    }

    // Channel count (bytes 22-23, little-endian uint16)
#if defined(AUDIO_FILES_STEREO)
    const uint16_t expected_channels = 2;
#else
    const uint16_t expected_channels = 1;
#endif
    uint16_t channels = (uint16_t)audio_file[22] | ((uint16_t)audio_file[23] << 8);
    if (channels != expected_channels) {
        Serial.println(fName + ": expected " + expected_channels + " ch, got " + channels);
        return false;
    }

    // PCM format (bytes 20-21 == 0x0001)
    if (!(audio_file[20] == 0x01 && audio_file[21] == 0x00)) {
        Serial.println(fName + ": not PCM");
        return false;
    }

    // Sample rate (bytes 24-27, little-endian uint32)
    uint32_t sr = (uint32_t)audio_file[24]
                | ((uint32_t)audio_file[25] << 8)
                | ((uint32_t)audio_file[26] << 16)
                | ((uint32_t)audio_file[27] << 24);
    if (sr != SAMPLES_PER_SEC) {
        Serial.println(fName + ": expected " + SAMPLES_PER_SEC + " Hz, got " + sr);
        return false;
    }

    // Bit depth (bytes 34-35 == 16)
    if (!(audio_file[34] == 0x10 && audio_file[35] == 0x00)) {
        Serial.println(fName + ": not 16-bit");
        return false;
    }

    // Block align = channels * (bits/8). Mono=2, Stereo=4.
    const uint16_t expected_block_align = expected_channels * 2;
    uint16_t block_align = (uint16_t)audio_file[32] | ((uint16_t)audio_file[33] << 8);
    if (block_align != expected_block_align) {
        Serial.println(fName + ": block_align " + block_align + " != " + expected_block_align);
        return false;
    }

    // Data chunk size must be a multiple of block_align
    uint32_t data_size = (uint32_t)audio_file[40]
                       | ((uint32_t)audio_file[41] << 8)
                       | ((uint32_t)audio_file[42] << 16)
                       | ((uint32_t)audio_file[43] << 24);
    if (data_size % expected_block_align != 0) {
        Serial.println(fName + ": data size not aligned to block_align");
        return false;
    }
    return true;
}


void BinarisAudioPlayer::handle_audio_commands() {
    AudioCommand cmd;
    if (xQueueReceive(_q_audio_in, &cmd, (TickType_t)0) != pdTRUE) return;

    switch (cmd.type) {
        case AUDIO_CMD_PLAY_WAV:
            if (data_ptr == nullptr) {
                start_play(cmd.audio_file);
            }
            // drop if already playing (queue is shallow; transient clicks are disposable)
            break;
        case AUDIO_CMD_HAPTIC:
            if (data_ptr == nullptr) {
                start_play(audio_config.audio_file);
            }
            break;
        case AUDIO_CMD_CONFIG:
            audio_config = cmd.config;
#ifdef USE_AUDIO_LIB
            if (xt_player != nullptr)
                xt_player->Volume = audio_config.audio_feedback_lvl;
#endif
            break;
        default:
            break;
    }
}


/* Must be called from a single thread (HMI thread). */
void BinarisAudioPlayer::audio_loop() {
    handle_audio_commands();

#ifdef USE_AUDIO_LIB
    if (xt_player != nullptr)
        xt_player->FillBuffer();
#else
    if (data_ptr == nullptr) return;

    size_t written = 0;
    i2s_write(I2S_NUM_0, data_ptr, num_bytes_remaining, &written, 0);
    if (written >= num_bytes_remaining) {
        // Playback complete
        data_ptr            = nullptr;
        num_bytes_remaining = 0;
    } else {
        data_ptr            += written;
        num_bytes_remaining -= written;
    }
#endif
}


/* Haptic detent callback – fires on every INCREASE/DECREASE event.
 * Defined here so the audio subsystem owns its own reaction to detents. */
extern "C" void HapticInterface::UserHapticEventCallback(
        HapticEvt event, float /*currentAngle*/, uint16_t /*currentPos*/) {
    switch (event) {
        case HapticEvt::INCREASE:
        case HapticEvt::DECREASE:
            audioPlayer.play_haptic_audio();
            break;
        default:
            break;
    }
}

#else /* !NANO_AUDIO --------------------------------------------------- */

/* 1-byte sentinel stubs so any code that stores or compares wav pointers
 * (e.g. HapticProfileManager defaults) still links without flash cost. */
uint8_t soft_wav[1]  = {0};
uint8_t hard_wav[1]  = {0};
uint8_t loud_wav[1]  = {0};
uint8_t clack_wav[1] = {0};
uint8_t chime_wav[1] = {0};

uint8_t* get_audio_file(String /*fName*/)            { return nullptr; }
String   get_audio_filename(uint8_t* /*audio_file*/) { return "none"; }

/* No-op haptic callback when audio is disabled. */
extern "C" void HapticInterface::UserHapticEventCallback(
        HapticEvt /*event*/, float /*angle*/, uint16_t /*pos*/) {}

#endif /* NANO_AUDIO */
