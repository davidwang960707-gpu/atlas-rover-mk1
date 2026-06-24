#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "atlas_config.h"

typedef uint32_t (*atlas_brain_ws_now_ms_fn_t)(void);

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
    char url[ATLAS_LLM_BASE_URL_MAX + 16];
} atlas_brain_ws_status_t;

esp_err_t atlas_brain_ws_client_start(const atlas_config_t *config, atlas_brain_ws_now_ms_fn_t now_ms_fn);
void atlas_brain_ws_client_stop(void);
void atlas_brain_ws_client_get_status(atlas_brain_ws_status_t *status);
size_t atlas_brain_ws_client_write_json(char *dst, size_t dst_size);
esp_err_t atlas_brain_ws_client_turn_wav(const uint8_t *wav,
                                         size_t wav_len,
                                         char **response,
                                         size_t *response_len,
                                         uint8_t **tts_wav,
                                         size_t *tts_wav_len,
                                         uint32_t timeout_ms);
