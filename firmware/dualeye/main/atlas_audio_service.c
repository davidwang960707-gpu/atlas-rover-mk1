#include "atlas_audio_service.h"

static void copy_mic_level_to_common(atlas_common_audio_mic_level_t *dst, const atlas_audio_mic_level_t *src)
{
    if (dst == NULL || src == NULL) {
        return;
    }
    dst->level = src->level;
    dst->rms = src->rms;
    dst->peak = src->peak;
    dst->samples = src->samples;
}

static void copy_mic_level_from_common(atlas_audio_mic_level_t *dst, const atlas_common_audio_mic_level_t *src)
{
    if (dst == NULL || src == NULL) {
        return;
    }
    dst->level = src->level;
    dst->rms = src->rms;
    dst->peak = src->peak;
    dst->samples = src->samples;
}

static esp_err_t dualeye_measure_mic(uint16_t duration_ms,
                                     atlas_common_audio_mic_level_t *level,
                                     void *ctx)
{
    (void)ctx;
    atlas_audio_mic_level_t native = {0};
    const esp_err_t err = atlas_audio_measure_mic(duration_ms, level == NULL ? NULL : &native);
    copy_mic_level_to_common(level, &native);
    return err;
}

static esp_err_t dualeye_capture_wav(uint16_t duration_ms,
                                     uint8_t **wav_data,
                                     size_t *wav_size,
                                     atlas_common_audio_mic_level_t *level,
                                     void *ctx)
{
    (void)ctx;
    atlas_audio_mic_level_t native = {0};
    const esp_err_t err = atlas_audio_capture_wav(duration_ms, wav_data, wav_size, level == NULL ? NULL : &native);
    copy_mic_level_to_common(level, &native);
    return err;
}

static esp_err_t dualeye_play_wav_pcm(const uint8_t *wav_data, size_t wav_size, uint8_t volume, void *ctx)
{
    (void)ctx;
    return atlas_audio_play_wav_pcm(wav_data, wav_size, volume);
}

void atlas_audio_service_init(atlas_audio_service_now_ms_fn_t now_ms_fn)
{
    const atlas_common_audio_service_config_t config = {
        .now_ms_fn = now_ms_fn,
        .measure_mic_fn = dualeye_measure_mic,
        .capture_wav_fn = dualeye_capture_wav,
        .play_wav_pcm_fn = dualeye_play_wav_pcm,
        .backend_ctx = NULL,
        .task_name = "atlas_audio_svc",
    };
    atlas_common_audio_service_init(&config);
}

const char *atlas_audio_service_mode_name(atlas_audio_service_mode_t mode)
{
    return atlas_common_audio_service_mode_name(mode);
}

void atlas_audio_service_set_continuous_enabled(bool enabled)
{
    atlas_common_audio_service_set_continuous_enabled(enabled);
}

void atlas_audio_service_note_stage(atlas_audio_service_mode_t mode, const char *action)
{
    atlas_common_audio_service_note_stage(mode, action);
}

void atlas_audio_service_note_failure(const char *reason, esp_err_t err)
{
    atlas_common_audio_service_note_failure(reason, err);
}

bool atlas_audio_service_is_muted(uint32_t *remaining_ms)
{
    return atlas_common_audio_service_is_muted(remaining_ms);
}

void atlas_audio_service_mute_for(uint32_t duration_ms, const char *reason)
{
    atlas_common_audio_service_mute_for(duration_ms, reason);
}

void atlas_audio_service_note_turn(void)
{
    atlas_common_audio_service_note_turn();
}

esp_err_t atlas_audio_service_run_turn(atlas_audio_service_job_fn_t fn, void *ctx, uint32_t timeout_ms)
{
    return atlas_common_audio_service_run_turn(fn, ctx, timeout_ms);
}

esp_err_t atlas_audio_service_submit_turn(atlas_audio_service_job_fn_t fn, void *ctx)
{
    return atlas_common_audio_service_submit_turn(fn, ctx);
}

esp_err_t atlas_audio_service_measure_mic(uint16_t duration_ms, atlas_audio_mic_level_t *level)
{
    atlas_common_audio_mic_level_t common = {0};
    const esp_err_t err = atlas_common_audio_service_measure_mic(duration_ms, level == NULL ? NULL : &common);
    copy_mic_level_from_common(level, &common);
    return err;
}

esp_err_t atlas_audio_service_capture_wav(uint16_t duration_ms,
                                          uint8_t **wav_data,
                                          size_t *wav_size,
                                          atlas_audio_mic_level_t *level)
{
    atlas_common_audio_mic_level_t common = {0};
    const esp_err_t err = atlas_common_audio_service_capture_wav(duration_ms,
                                                                wav_data,
                                                                wav_size,
                                                                level == NULL ? NULL : &common);
    copy_mic_level_from_common(level, &common);
    return err;
}

esp_err_t atlas_audio_service_play_wav_pcm(const uint8_t *wav_data, size_t wav_size, uint8_t volume)
{
    return atlas_common_audio_service_play_wav_pcm(wav_data, wav_size, volume);
}

void atlas_audio_service_get_status(atlas_audio_service_status_t *status)
{
    atlas_common_audio_service_get_status(status);
}

size_t atlas_audio_service_write_json(char *dst, size_t dst_size)
{
    return atlas_common_audio_service_write_json(dst, dst_size);
}
