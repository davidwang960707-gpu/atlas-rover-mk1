#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ATLAS_COMMON_OPUS_FRAME_MS 60u
#define ATLAS_COMMON_OPUS_SAMPLE_RATE 16000u
#define ATLAS_COMMON_OPUS_STREAM_PROTOCOL "atlas.audio.stream.v0"
#define ATLAS_COMMON_OPUS_STREAM_FRAME_MAGIC "AOP1"
#define ATLAS_COMMON_OPUS_STREAM_BINARY_HEADER_BYTES 32u

typedef struct {
    uint8_t level;
    uint32_t rms;
    uint32_t peak;
    uint32_t samples;
} atlas_common_opus_mic_level_t;

typedef struct {
    bool encoder_ready;
    uint16_t requested_ms;
    uint16_t frame_ms;
    uint16_t frame_samples;
    uint16_t frames_requested;
    uint16_t frames_encoded;
    uint32_t encoded_bytes;
    uint16_t min_packet_bytes;
    uint16_t max_packet_bytes;
    uint32_t avg_packet_bytes;
    uint8_t mic_level;
    uint32_t mic_rms;
    uint32_t mic_peak;
    esp_err_t last_error;
} atlas_common_opus_probe_result_t;

typedef struct {
    const char *url;
    uint32_t duration_ms;
} atlas_common_opus_stream_config_t;

typedef struct {
    bool running;
    bool connected;
    bool stop_requested;
    bool continuous;
    uint32_t duration_ms;
    uint16_t frame_ms;
    uint16_t frame_samples;
    uint16_t sample_rate;
    uint32_t started_ms;
    uint32_t ended_ms;
    uint32_t frames_encoded;
    uint32_t frames_sent;
    uint32_t bytes_sent;
    uint32_t send_failures;
    uint32_t muted_frames;
    uint32_t capture_failures;
    uint32_t encode_failures;
    uint32_t sequence;
    uint32_t free_internal_heap;
    uint32_t free_psram_heap;
    uint16_t last_packet_bytes;
    uint8_t mic_level;
    uint32_t mic_rms;
    uint32_t mic_peak;
    esp_err_t last_error;
    char stage[32];
    char url[192];
} atlas_common_opus_stream_status_t;

typedef esp_err_t (*atlas_common_opus_capture_pcm_fn_t)(int16_t *samples,
                                                        size_t sample_count,
                                                        atlas_common_opus_mic_level_t *level,
                                                        void *ctx);
typedef bool (*atlas_common_opus_is_muted_fn_t)(uint32_t *remaining_ms, void *ctx);
typedef void (*atlas_common_opus_note_stage_fn_t)(bool ok, const char *action, void *ctx);

typedef struct {
    atlas_common_opus_capture_pcm_fn_t capture_pcm_fn;
    atlas_common_opus_is_muted_fn_t is_muted_fn;
    atlas_common_opus_note_stage_fn_t note_stage_fn;
    const char *device_id;
    const char *stream_task_name;
    const char *websocket_task_name;
    void *ctx;
} atlas_common_opus_stream_backend_t;

void atlas_common_opus_stream_init(const atlas_common_opus_stream_backend_t *backend);
esp_err_t atlas_common_opus_probe_run(uint16_t duration_ms, atlas_common_opus_probe_result_t *result);
size_t atlas_common_opus_probe_write_json(const atlas_common_opus_probe_result_t *result, char *dst, size_t dst_size);
esp_err_t atlas_common_opus_stream_start(const atlas_common_opus_stream_config_t *config);
esp_err_t atlas_common_opus_stream_stop(void);
void atlas_common_opus_stream_get_status(atlas_common_opus_stream_status_t *status);
size_t atlas_common_opus_stream_write_status_json(const atlas_common_opus_stream_status_t *status, char *dst, size_t dst_size);
