#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "atlas_audio.h"

typedef uint32_t (*atlas_audio_service_now_ms_fn_t)(void);

typedef enum {
    ATLAS_AUDIO_SERVICE_MODE_IDLE = 0,
    ATLAS_AUDIO_SERVICE_MODE_MONITORING,
    ATLAS_AUDIO_SERVICE_MODE_RECORDING,
    ATLAS_AUDIO_SERVICE_MODE_TRANSCRIBING,
    ATLAS_AUDIO_SERVICE_MODE_THINKING,
    ATLAS_AUDIO_SERVICE_MODE_PLAYING,
    ATLAS_AUDIO_SERVICE_MODE_COOLDOWN,
    ATLAS_AUDIO_SERVICE_MODE_ERROR,
} atlas_audio_service_mode_t;

typedef esp_err_t (*atlas_audio_service_job_fn_t)(void *ctx);

typedef struct {
    bool initialized;
    bool worker_started;
    bool psram_stack;
    bool busy;
    bool job_running;
    bool continuous_enabled;
    bool muted;
    uint32_t mute_remaining_ms;
    uint32_t muted_until_ms;
    uint32_t capture_count;
    uint32_t playback_count;
    uint32_t monitor_count;
    uint32_t turn_count;
    uint32_t job_count;
    uint32_t job_error_count;
    uint32_t consecutive_failures;
    uint32_t last_success_ms;
    uint32_t last_event_ms;
    uint32_t last_job_ms;
    atlas_audio_service_mode_t mode;
    esp_err_t last_error;
    char mute_reason[32];
    char last_action[32];
    char last_failure[80];
} atlas_audio_service_status_t;

void atlas_audio_service_init(atlas_audio_service_now_ms_fn_t now_ms_fn);
const char *atlas_audio_service_mode_name(atlas_audio_service_mode_t mode);
void atlas_audio_service_set_continuous_enabled(bool enabled);
void atlas_audio_service_note_stage(atlas_audio_service_mode_t mode, const char *action);
void atlas_audio_service_note_failure(const char *reason, esp_err_t err);
bool atlas_audio_service_is_muted(uint32_t *remaining_ms);
void atlas_audio_service_mute_for(uint32_t duration_ms, const char *reason);
void atlas_audio_service_note_turn(void);
esp_err_t atlas_audio_service_run_turn(atlas_audio_service_job_fn_t fn, void *ctx, uint32_t timeout_ms);
esp_err_t atlas_audio_service_submit_turn(atlas_audio_service_job_fn_t fn, void *ctx);
esp_err_t atlas_audio_service_measure_mic(uint16_t duration_ms, atlas_audio_mic_level_t *level);
esp_err_t atlas_audio_service_capture_wav(uint16_t duration_ms,
                                          uint8_t **wav_data,
                                          size_t *wav_size,
                                          atlas_audio_mic_level_t *level);
esp_err_t atlas_audio_service_play_wav_pcm(const uint8_t *wav_data, size_t wav_size, uint8_t volume);
void atlas_audio_service_get_status(atlas_audio_service_status_t *status);
size_t atlas_audio_service_write_json(char *dst, size_t dst_size);
