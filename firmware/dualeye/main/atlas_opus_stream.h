#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "atlas_audio.h"

#define ATLAS_OPUS_PROBE_FRAME_MS 60u
#define ATLAS_OPUS_PROBE_SAMPLE_RATE ATLAS_AUDIO_SAMPLE_RATE

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
} atlas_opus_probe_result_t;

#define ATLAS_OPUS_STREAM_PROTOCOL "atlas.audio.stream.v0"
#define ATLAS_OPUS_STREAM_FRAME_MAGIC "AOP1"
#define ATLAS_OPUS_STREAM_BINARY_HEADER_BYTES 32u

typedef struct {
    const char *url;
    uint32_t duration_ms;
} atlas_opus_stream_config_t;

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
} atlas_opus_stream_status_t;

esp_err_t atlas_opus_probe_run(uint16_t duration_ms, atlas_opus_probe_result_t *result);
size_t atlas_opus_probe_write_json(const atlas_opus_probe_result_t *result, char *dst, size_t dst_size);
esp_err_t atlas_opus_stream_start(const atlas_opus_stream_config_t *config);
esp_err_t atlas_opus_stream_stop(void);
void atlas_opus_stream_get_status(atlas_opus_stream_status_t *status);
size_t atlas_opus_stream_write_status_json(const atlas_opus_stream_status_t *status, char *dst, size_t dst_size);
