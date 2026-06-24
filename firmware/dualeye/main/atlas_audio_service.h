#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "atlas_audio.h"
#include "common/atlas_common_audio_service.h"

typedef atlas_common_audio_service_now_ms_fn_t atlas_audio_service_now_ms_fn_t;
typedef atlas_common_audio_service_mode_t atlas_audio_service_mode_t;
typedef atlas_common_audio_service_job_fn_t atlas_audio_service_job_fn_t;
typedef atlas_common_audio_service_status_t atlas_audio_service_status_t;

#define ATLAS_AUDIO_SERVICE_MODE_IDLE ATLAS_COMMON_AUDIO_SERVICE_MODE_IDLE
#define ATLAS_AUDIO_SERVICE_MODE_MONITORING ATLAS_COMMON_AUDIO_SERVICE_MODE_MONITORING
#define ATLAS_AUDIO_SERVICE_MODE_RECORDING ATLAS_COMMON_AUDIO_SERVICE_MODE_RECORDING
#define ATLAS_AUDIO_SERVICE_MODE_TRANSCRIBING ATLAS_COMMON_AUDIO_SERVICE_MODE_TRANSCRIBING
#define ATLAS_AUDIO_SERVICE_MODE_THINKING ATLAS_COMMON_AUDIO_SERVICE_MODE_THINKING
#define ATLAS_AUDIO_SERVICE_MODE_PLAYING ATLAS_COMMON_AUDIO_SERVICE_MODE_PLAYING
#define ATLAS_AUDIO_SERVICE_MODE_COOLDOWN ATLAS_COMMON_AUDIO_SERVICE_MODE_COOLDOWN
#define ATLAS_AUDIO_SERVICE_MODE_ERROR ATLAS_COMMON_AUDIO_SERVICE_MODE_ERROR

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
