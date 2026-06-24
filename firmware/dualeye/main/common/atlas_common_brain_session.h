#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ATLAS_COMMON_BRAIN_SESSION_URL_MAX 192

typedef uint32_t (*atlas_common_brain_session_now_ms_fn_t)(void);
typedef bool (*atlas_common_brain_session_ready_fn_t)(void *ctx);
typedef const char *(*atlas_common_brain_session_runtime_fn_t)(void *ctx);

typedef struct {
    bool enabled;
    const char *base_url;
    const char *device_id;
    const char *protocol;
    const char *hello_detail;
    const char *task_name;
    const char *websocket_task_name;
    atlas_common_brain_session_now_ms_fn_t now_ms_fn;
    atlas_common_brain_session_ready_fn_t ready_fn;
    atlas_common_brain_session_runtime_fn_t runtime_fn;
    void *ctx;
} atlas_common_brain_session_config_t;

typedef struct {
    bool enabled;
    bool running;
    bool connected;
    uint32_t connects;
    uint32_t disconnects;
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t last_connected_ms;
    uint32_t last_error_ms;
    esp_err_t last_error;
    char stage[32];
    char url[ATLAS_COMMON_BRAIN_SESSION_URL_MAX];
} atlas_common_brain_session_status_t;

esp_err_t atlas_common_brain_session_start(const atlas_common_brain_session_config_t *config);
void atlas_common_brain_session_stop(void);
void atlas_common_brain_session_get_status(atlas_common_brain_session_status_t *status);
size_t atlas_common_brain_session_write_json(char *dst, size_t dst_size);
esp_err_t atlas_common_brain_session_turn_wav(const uint8_t *wav,
                                              size_t wav_len,
                                              char **response,
                                              size_t *response_len,
                                              uint8_t **tts_wav,
                                              size_t *tts_wav_len,
                                              uint32_t timeout_ms);
