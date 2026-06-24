#include "atlas_opus_stream.h"

#include <string.h>

#include "atlas_audio_service.h"

static void copy_level_to_common(atlas_common_opus_mic_level_t *dst, const atlas_audio_mic_level_t *src)
{
    if (dst == NULL || src == NULL) {
        return;
    }
    dst->level = src->level;
    dst->rms = src->rms;
    dst->peak = src->peak;
    dst->samples = src->samples;
}

static esp_err_t dualeye_opus_capture_pcm(int16_t *samples,
                                          size_t sample_count,
                                          atlas_common_opus_mic_level_t *level,
                                          void *ctx)
{
    (void)ctx;
    atlas_audio_mic_level_t native = {0};
    const esp_err_t err = atlas_audio_capture_pcm_mono(samples, sample_count, level == NULL ? NULL : &native);
    copy_level_to_common(level, &native);
    return err;
}

static bool dualeye_opus_is_muted(uint32_t *remaining_ms, void *ctx)
{
    (void)ctx;
    return atlas_audio_service_is_muted(remaining_ms);
}

static void dualeye_opus_note_stage(bool ok, const char *action, void *ctx)
{
    (void)ctx;
    if (action != NULL && strcmp(action, "opus_streaming") == 0) {
        atlas_audio_service_note_stage(ATLAS_AUDIO_SERVICE_MODE_RECORDING, action);
        return;
    }
    atlas_audio_service_note_stage(ok ? ATLAS_AUDIO_SERVICE_MODE_IDLE : ATLAS_AUDIO_SERVICE_MODE_ERROR,
                                   action == NULL ? (ok ? "opus_done" : "opus_error") : action);
}

static void ensure_common_opus_initialized(void)
{
    const atlas_common_opus_stream_backend_t backend = {
        .capture_pcm_fn = dualeye_opus_capture_pcm,
        .is_muted_fn = dualeye_opus_is_muted,
        .note_stage_fn = dualeye_opus_note_stage,
        .device_id = "dualeye",
        .stream_task_name = "atlas_opus_ws",
        .websocket_task_name = "atlas_ws_audio",
        .ctx = NULL,
    };
    atlas_common_opus_stream_init(&backend);
}

esp_err_t atlas_opus_probe_run(uint16_t duration_ms, atlas_opus_probe_result_t *result)
{
    ensure_common_opus_initialized();
    return atlas_common_opus_probe_run(duration_ms, result);
}

size_t atlas_opus_probe_write_json(const atlas_opus_probe_result_t *result, char *dst, size_t dst_size)
{
    return atlas_common_opus_probe_write_json(result, dst, dst_size);
}

esp_err_t atlas_opus_stream_start(const atlas_opus_stream_config_t *config)
{
    ensure_common_opus_initialized();
    return atlas_common_opus_stream_start(config);
}

esp_err_t atlas_opus_stream_stop(void)
{
    ensure_common_opus_initialized();
    return atlas_common_opus_stream_stop();
}

void atlas_opus_stream_get_status(atlas_opus_stream_status_t *status)
{
    ensure_common_opus_initialized();
    atlas_common_opus_stream_get_status(status);
}

size_t atlas_opus_stream_write_status_json(const atlas_opus_stream_status_t *status, char *dst, size_t dst_size)
{
    return atlas_common_opus_stream_write_status_json(status, dst, dst_size);
}
