#include "atlas_brain_ws_client.h"

#include <string.h>

#include "common/atlas_common_brain_session.h"

#include "atlas_runtime.h"
#include "atlas_wifi.h"

#define ATLAS_BRAIN_SESSION_PROTOCOL "atlas.brain.session.v1"

static bool dualeye_brain_session_ready(void *ctx)
{
    (void)ctx;
    atlas_wifi_status_t wifi = {0};
    atlas_wifi_get_status(&wifi);
    return wifi.sta_connected;
}

static const char *dualeye_brain_session_runtime(void *ctx)
{
    (void)ctx;
    return atlas_runtime_state_name(atlas_runtime_get_state());
}

static void copy_status_from_common(atlas_brain_ws_status_t *dst, const atlas_common_brain_session_status_t *src)
{
    if (dst == NULL || src == NULL) {
        return;
    }
    dst->enabled = src->enabled;
    dst->running = src->running;
    dst->connected = src->connected;
    dst->connects = src->connects;
    dst->disconnects = src->disconnects;
    dst->messages_sent = src->messages_sent;
    dst->messages_received = src->messages_received;
    dst->last_connected_ms = src->last_connected_ms;
    dst->last_error_ms = src->last_error_ms;
    dst->last_error = src->last_error;
    strlcpy(dst->stage, src->stage, sizeof(dst->stage));
    strlcpy(dst->url, src->url, sizeof(dst->url));
}

esp_err_t atlas_brain_ws_client_start(const atlas_config_t *config, atlas_brain_ws_now_ms_fn_t now_ms_fn)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool enabled = strcmp(config->llm.mode, "host") == 0 &&
                         config->llm.base_url[0] != '\0';
    const atlas_common_brain_session_config_t session_config = {
        .enabled = enabled,
        .base_url = config->llm.base_url,
        .device_id = "dualeye",
        .protocol = ATLAS_BRAIN_SESSION_PROTOCOL,
        .hello_detail = "dualeye boot/session",
        .task_name = "atlas_brain_ws",
        .websocket_task_name = "atlas_ws_brain",
        .now_ms_fn = now_ms_fn,
        .ready_fn = dualeye_brain_session_ready,
        .runtime_fn = dualeye_brain_session_runtime,
        .ctx = NULL,
    };
    return atlas_common_brain_session_start(&session_config);
}

void atlas_brain_ws_client_stop(void)
{
    atlas_common_brain_session_stop();
}

void atlas_brain_ws_client_get_status(atlas_brain_ws_status_t *status)
{
    if (status == NULL) {
        return;
    }
    atlas_common_brain_session_status_t common = {0};
    atlas_common_brain_session_get_status(&common);
    copy_status_from_common(status, &common);
}

size_t atlas_brain_ws_client_write_json(char *dst, size_t dst_size)
{
    return atlas_common_brain_session_write_json(dst, dst_size);
}

esp_err_t atlas_brain_ws_client_turn_wav(const uint8_t *wav,
                                         size_t wav_len,
                                         char **response,
                                         size_t *response_len,
                                         uint8_t **tts_wav,
                                         size_t *tts_wav_len,
                                         uint32_t timeout_ms)
{
    return atlas_common_brain_session_turn_wav(wav,
                                               wav_len,
                                               response,
                                               response_len,
                                               tts_wav,
                                               tts_wav_len,
                                               timeout_ms);
}
