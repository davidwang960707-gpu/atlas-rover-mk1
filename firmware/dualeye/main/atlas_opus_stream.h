#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "atlas_audio.h"
#include "common/atlas_common_opus_stream.h"

#define ATLAS_OPUS_PROBE_FRAME_MS ATLAS_COMMON_OPUS_FRAME_MS
#define ATLAS_OPUS_PROBE_SAMPLE_RATE ATLAS_COMMON_OPUS_SAMPLE_RATE

typedef atlas_common_opus_probe_result_t atlas_opus_probe_result_t;

#define ATLAS_OPUS_STREAM_PROTOCOL ATLAS_COMMON_OPUS_STREAM_PROTOCOL
#define ATLAS_OPUS_STREAM_FRAME_MAGIC ATLAS_COMMON_OPUS_STREAM_FRAME_MAGIC
#define ATLAS_OPUS_STREAM_BINARY_HEADER_BYTES ATLAS_COMMON_OPUS_STREAM_BINARY_HEADER_BYTES

typedef atlas_common_opus_stream_config_t atlas_opus_stream_config_t;
typedef atlas_common_opus_stream_status_t atlas_opus_stream_status_t;

esp_err_t atlas_opus_probe_run(uint16_t duration_ms, atlas_opus_probe_result_t *result);
size_t atlas_opus_probe_write_json(const atlas_opus_probe_result_t *result, char *dst, size_t dst_size);
esp_err_t atlas_opus_stream_start(const atlas_opus_stream_config_t *config);
esp_err_t atlas_opus_stream_stop(void);
void atlas_opus_stream_get_status(atlas_opus_stream_status_t *status);
size_t atlas_opus_stream_write_status_json(const atlas_opus_stream_status_t *status, char *dst, size_t dst_size);
