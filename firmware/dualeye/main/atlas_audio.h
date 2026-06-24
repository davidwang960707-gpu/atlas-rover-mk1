#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ATLAS_AUDIO_SAMPLE_RATE 16000u

typedef struct {
    bool initialized;
    bool i2c_ready;
    bool i2s_ready;
    bool output_ready;
    bool input_ready;
    uint16_t sample_rate;
    uint8_t volume;
    uint8_t last_mic_level;
    uint32_t last_mic_rms;
    uint32_t last_mic_peak;
    uint32_t speaker_tests;
    uint32_t mic_tests;
    esp_err_t last_error;
} atlas_audio_status_t;

typedef struct {
    uint8_t level;
    uint32_t rms;
    uint32_t peak;
    uint32_t samples;
} atlas_audio_mic_level_t;

esp_err_t atlas_audio_init(uint8_t volume);
esp_err_t atlas_audio_set_volume(uint8_t volume);
esp_err_t atlas_audio_play_beep(uint16_t frequency_hz, uint16_t duration_ms, uint8_t volume);
esp_err_t atlas_audio_measure_mic(uint16_t duration_ms, atlas_audio_mic_level_t *level);
esp_err_t atlas_audio_capture_pcm_mono(int16_t *samples,
                                       size_t sample_count,
                                       atlas_audio_mic_level_t *level);
esp_err_t atlas_audio_capture_wav(uint16_t duration_ms,
                                  uint8_t **wav_data,
                                  size_t *wav_size,
                                  atlas_audio_mic_level_t *level);
esp_err_t atlas_audio_play_wav_pcm(const uint8_t *wav_data, size_t wav_size, uint8_t volume);
void atlas_audio_get_status(atlas_audio_status_t *status);
