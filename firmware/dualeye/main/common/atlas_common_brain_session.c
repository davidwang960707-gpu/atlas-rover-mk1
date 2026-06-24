#include "common/atlas_common_brain_session.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"


static const char *TAG = "atlas_brain_session";

#define ATLAS_COMMON_BRAIN_SESSION_TASK_STACK 8192u
#define ATLAS_COMMON_BRAIN_SESSION_TASK_PRIORITY 4u
#define ATLAS_COMMON_BRAIN_SESSION_CONNECT_WAIT_MS 9000u
#define ATLAS_COMMON_BRAIN_SESSION_RECONNECT_MS 3000u
#define ATLAS_COMMON_BRAIN_SESSION_HEALTH_RETRY_MS 3500u
#define ATLAS_COMMON_BRAIN_SESSION_PING_MS 10000u
#define ATLAS_COMMON_BRAIN_SESSION_SEND_TIMEOUT_MS 1200u
#define ATLAS_COMMON_BRAIN_SESSION_TURN_RESPONSE_MAX 4096u
#define ATLAS_COMMON_BRAIN_SESSION_TTS_MAX (512u * 1024u)
#define ATLAS_COMMON_BRAIN_SESSION_TURN_TIMEOUT_MS 90000u

static TaskHandle_t s_task;
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_request_mutex;
static SemaphoreHandle_t s_turn_sem;
static volatile bool s_stop_requested;
static atlas_common_brain_session_config_t s_config_snapshot;
static atlas_common_brain_session_status_t s_status;
static esp_websocket_client_handle_t s_active_client;
static char s_base_url[ATLAS_COMMON_BRAIN_SESSION_URL_MAX];
static char s_device_id[32];
static char s_protocol[48];
static char s_hello_detail[80];
static char s_task_name[32];
static char s_websocket_task_name[32];

static uint32_t s_turn_request_id;
static bool s_turn_waiting;
static char s_turn_response[ATLAS_COMMON_BRAIN_SESSION_TURN_RESPONSE_MAX];
static size_t s_turn_response_len;
static char s_turn_text_rx[ATLAS_COMMON_BRAIN_SESSION_TURN_RESPONSE_MAX];
static size_t s_turn_text_rx_len;
static uint8_t *s_turn_tts_data;
static size_t s_turn_tts_expected;
static size_t s_turn_tts_received;

static uint32_t brain_session_now_ms(void)
{
    if (s_config_snapshot.now_ms_fn != NULL) {
        return s_config_snapshot.now_ms_fn();
    }
    return 0;
}

static bool brain_session_ready(void)
{
    if (s_config_snapshot.ready_fn == NULL) {
        return true;
    }
    return s_config_snapshot.ready_fn(s_config_snapshot.ctx);
}

static const char *brain_session_runtime_name(void)
{
    if (s_config_snapshot.runtime_fn == NULL) {
        return "unknown";
    }
    const char *name = s_config_snapshot.runtime_fn(s_config_snapshot.ctx);
    return name == NULL || name[0] == '\0' ? "unknown" : name;
}

static const char *brain_session_device_id(void)
{
    return s_config_snapshot.device_id == NULL || s_config_snapshot.device_id[0] == '\0' ?
           "device" :
           s_config_snapshot.device_id;
}

static const char *brain_session_protocol(void)
{
    return s_config_snapshot.protocol == NULL || s_config_snapshot.protocol[0] == '\0' ?
           "atlas.brain.session.v1" :
           s_config_snapshot.protocol;
}

static const char *brain_session_hello_detail(void)
{
    return s_config_snapshot.hello_detail == NULL || s_config_snapshot.hello_detail[0] == '\0' ?
           "boot/session" :
           s_config_snapshot.hello_detail;
}

static esp_err_t brain_session_ensure_mutex(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    return s_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

static void brain_session_lock(void)
{
    if (brain_session_ensure_mutex() == ESP_OK) {
        (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void brain_session_unlock(void)
{
    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}

static void brain_session_set_stage(const char *stage, esp_err_t err)
{
    brain_session_lock();
    strlcpy(s_status.stage, stage == NULL ? "unknown" : stage, sizeof(s_status.stage));
    s_status.last_error = err;
    if (err != ESP_OK) {
        s_status.last_error_ms = brain_session_now_ms();
    }
    brain_session_unlock();
}

static bool brain_session_build_url(const char *base_url, char *out, size_t out_size)
{
    if (base_url == NULL || base_url[0] == '\0' || out == NULL || out_size == 0) {
        return false;
    }

    char tmp[ATLAS_COMMON_BRAIN_SESSION_URL_MAX] = "";
    strlcpy(tmp, base_url, sizeof(tmp));
    while (strlen(tmp) > 0 && tmp[strlen(tmp) - 1] == '/') {
        tmp[strlen(tmp) - 1] = '\0';
    }

    if (strncmp(tmp, "ws://", 5) == 0 || strncmp(tmp, "wss://", 6) == 0) {
        if (strstr(tmp, "/ws/brain") != NULL) {
            strlcpy(out, tmp, out_size);
        } else {
            snprintf(out, out_size, "%s/ws/brain", tmp);
        }
        return true;
    }
    if (strncmp(tmp, "http://", 7) == 0) {
        snprintf(out, out_size, "ws://%s/ws/brain", tmp + 7);
        return true;
    }
    if (strncmp(tmp, "https://", 8) == 0) {
        snprintf(out, out_size, "wss://%s/ws/brain", tmp + 8);
        return true;
    }
    snprintf(out, out_size, "ws://%s/ws/brain", tmp);
    return true;
}

static bool brain_session_build_http_url(const char *base_url, const char *path, char *out, size_t out_size)
{
    if (base_url == NULL || base_url[0] == '\0' || path == NULL || out == NULL || out_size == 0) {
        return false;
    }

    char tmp[ATLAS_COMMON_BRAIN_SESSION_URL_MAX] = "";
    strlcpy(tmp, base_url, sizeof(tmp));
    while (strlen(tmp) > 0 && tmp[strlen(tmp) - 1] == '/') {
        tmp[strlen(tmp) - 1] = '\0';
    }

    char base[ATLAS_COMMON_BRAIN_SESSION_URL_MAX] = "";
    if (strncmp(tmp, "ws://", 5) == 0) {
        strlcpy(base, "http://", sizeof(base));
        strlcat(base, tmp + 5, sizeof(base));
    } else if (strncmp(tmp, "wss://", 6) == 0) {
        strlcpy(base, "https://", sizeof(base));
        strlcat(base, tmp + 6, sizeof(base));
    } else if (strncmp(tmp, "http://", 7) == 0 || strncmp(tmp, "https://", 8) == 0) {
        strlcpy(base, tmp, sizeof(base));
    } else {
        strlcpy(base, "http://", sizeof(base));
        strlcat(base, tmp, sizeof(base));
    }

    const bool path_has_slash = path[0] == '/';
    snprintf(out, out_size, "%s%s%s", base, path_has_slash ? "" : "/", path);
    return true;
}

static bool brain_session_config_enabled(const atlas_common_brain_session_config_t *config, char *url, size_t url_size)
{
    if (config == NULL || !config->enabled) {
        return false;
    }
    return brain_session_build_url(config->base_url, url, url_size);
}

static esp_err_t brain_session_probe_health(const atlas_common_brain_session_config_t *config, int *status_code)
{
    if (status_code != NULL) {
        *status_code = 0;
    }
    if (config == NULL || config->base_url == NULL || config->base_url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char health_url[ATLAS_COMMON_BRAIN_SESSION_URL_MAX] = "";
    if (!brain_session_build_http_url(config->base_url, "/health", health_url, sizeof(health_url))) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t cfg = {
        .url = health_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 1500,
        .buffer_size = 512,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t err = esp_http_client_perform(client);
    const int http_status = esp_http_client_get_status_code(client);
    if (status_code != NULL) {
        *status_code = http_status;
    }
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        return err;
    }
    return http_status >= 200 && http_status < 300 ? ESP_OK : ESP_FAIL;
}

static esp_err_t brain_session_send_json(esp_websocket_client_handle_t client, const char *json)
{
    if (client == NULL || json == NULL || !esp_websocket_client_is_connected(client)) {
        return ESP_ERR_INVALID_STATE;
    }
    const int sent = esp_websocket_client_send_text(client, json, strlen(json), pdMS_TO_TICKS(ATLAS_COMMON_BRAIN_SESSION_SEND_TIMEOUT_MS));
    if (sent < 0) {
        return ESP_FAIL;
    }
    brain_session_lock();
    s_status.messages_sent++;
    brain_session_unlock();
    return ESP_OK;
}

static esp_err_t brain_session_send_event(esp_websocket_client_handle_t client, const char *event, const char *detail)
{
    char payload[512];
    snprintf(payload,
             sizeof(payload),
             "{\"type\":\"%s\",\"event\":\"%s\",\"protocol\":\"%s\","
             "\"device_id\":\"%s\",\"runtime\":\"%s\",\"detail\":\"%s\","
             "\"free_heap\":%u,\"ts_ms\":%" PRIu32 "}",
             event,
             event,
             brain_session_protocol(),
             brain_session_device_id(),
             brain_session_runtime_name(),
             detail == NULL ? "" : detail,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             brain_session_now_ms());
    return brain_session_send_json(client, payload);
}

static esp_err_t brain_session_ensure_request_sync(void)
{
    if (s_request_mutex == NULL) {
        s_request_mutex = xSemaphoreCreateMutex();
    }
    if (s_turn_sem == NULL) {
        s_turn_sem = xSemaphoreCreateBinary();
    }
    return (s_request_mutex == NULL || s_turn_sem == NULL) ? ESP_ERR_NO_MEM : ESP_OK;
}

static bool json_text_has_request_id(const char *json, uint32_t request_id)
{
    if (json == NULL || request_id == 0) {
        return false;
    }
    char compact[40];
    char spaced[40];
    snprintf(compact, sizeof(compact), "\"request_id\":%" PRIu32, request_id);
    snprintf(spaced, sizeof(spaced), "\"request_id\": %" PRIu32, request_id);
    return strstr(json, compact) != NULL || strstr(json, spaced) != NULL;
}

static size_t json_text_size_value(const char *json, const char *key)
{
    if (json == NULL || key == NULL) {
        return 0;
    }
    char needle[32];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *pos = strstr(json, needle);
    if (pos == NULL) {
        return 0;
    }
    pos = strchr(pos, ':');
    if (pos == NULL) {
        return 0;
    }
    ++pos;
    while (*pos == ' ' || *pos == '\t') {
        ++pos;
    }
    return (size_t)strtoul(pos, NULL, 10);
}

static void brain_session_reset_turn_buffers_locked(void)
{
    s_turn_response[0] = '\0';
    s_turn_response_len = 0;
    s_turn_text_rx[0] = '\0';
    s_turn_text_rx_len = 0;
    free(s_turn_tts_data);
    s_turn_tts_data = NULL;
    s_turn_tts_expected = 0;
    s_turn_tts_received = 0;
}

static void brain_session_handle_text_payload(const char *json)
{
    if (json == NULL) {
        return;
    }

    brain_session_lock();
    s_status.messages_received++;
    const bool waiting = s_turn_waiting && json_text_has_request_id(json, s_turn_request_id);
    if (waiting && strstr(json, "turn.tts.begin") != NULL) {
        const size_t bytes = json_text_size_value(json, "bytes");
        if (bytes > 0 && bytes <= ATLAS_COMMON_BRAIN_SESSION_TTS_MAX) {
            free(s_turn_tts_data);
            s_turn_tts_data = (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (s_turn_tts_data == NULL) {
                s_turn_tts_data = (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
            }
            s_turn_tts_expected = s_turn_tts_data == NULL ? 0 : bytes;
            s_turn_tts_received = 0;
        }
    } else if (waiting &&
               (strstr(json, "turn.result") != NULL || strstr(json, "turn.error") != NULL)) {
        strlcpy(s_turn_response, json, sizeof(s_turn_response));
        s_turn_response_len = strlen(s_turn_response);
        if (s_turn_sem != NULL) {
            xSemaphoreGive(s_turn_sem);
        }
    }
    brain_session_unlock();
}

static void brain_session_handle_binary_payload(const uint8_t *data, size_t len, size_t offset, size_t payload_len)
{
    if (data == NULL || len == 0) {
        return;
    }
    brain_session_lock();
    s_status.messages_received++;
    if (s_turn_waiting && s_turn_tts_data != NULL && s_turn_tts_expected > 0 &&
        payload_len <= s_turn_tts_expected && offset + len <= s_turn_tts_expected) {
        memcpy(s_turn_tts_data + offset, data, len);
        const size_t end = offset + len;
        if (end > s_turn_tts_received) {
            s_turn_tts_received = end;
        }
    }
    brain_session_unlock();
}

static void brain_session_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    if (event_id == WEBSOCKET_EVENT_ERROR) {
        brain_session_set_stage("socket_error", ESP_FAIL);
        return;
    }
    if (event_id != WEBSOCKET_EVENT_DATA || data == NULL || data->data_ptr == NULL || data->data_len <= 0) {
        return;
    }

    const size_t data_len = (size_t)data->data_len;
    const size_t payload_len = data->payload_len > 0 ? (size_t)data->payload_len : data_len;
    const size_t offset = data->payload_offset >= 0 ? (size_t)data->payload_offset : 0;
    if (data->op_code == 0x1) {
        brain_session_lock();
        if (offset == 0) {
            s_turn_text_rx_len = 0;
            s_turn_text_rx[0] = '\0';
        }
        if (payload_len < sizeof(s_turn_text_rx) && s_turn_text_rx_len + data_len < sizeof(s_turn_text_rx)) {
            memcpy(s_turn_text_rx + s_turn_text_rx_len, data->data_ptr, data_len);
            s_turn_text_rx_len += data_len;
            s_turn_text_rx[s_turn_text_rx_len] = '\0';
        }
        const bool complete = (offset + data_len) >= payload_len;
        char text[ATLAS_COMMON_BRAIN_SESSION_TURN_RESPONSE_MAX];
        text[0] = '\0';
        if (complete && s_turn_text_rx_len > 0) {
            strlcpy(text, s_turn_text_rx, sizeof(text));
            s_turn_text_rx_len = 0;
            s_turn_text_rx[0] = '\0';
        }
        brain_session_unlock();
        if (text[0] != '\0') {
            brain_session_handle_text_payload(text);
        }
    } else if (data->op_code == 0x2) {
        brain_session_handle_binary_payload((const uint8_t *)data->data_ptr, data_len, offset, payload_len);
    }
}

static void brain_session_task(void *arg)
{
    (void)arg;
    char url[sizeof(s_status.url)] = "";
    if (!brain_session_config_enabled(&s_config_snapshot, url, sizeof(url))) {
        brain_session_set_stage("disabled", ESP_OK);
        brain_session_lock();
        s_status.running = false;
        s_status.enabled = false;
        brain_session_unlock();
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    brain_session_lock();
    s_status.enabled = true;
    s_status.running = true;
    strlcpy(s_status.url, url, sizeof(s_status.url));
    brain_session_unlock();

    while (!s_stop_requested) {
        if (!brain_session_ready()) {
            brain_session_set_stage("waiting_wifi", ESP_OK);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int health_status = 0;
        const esp_err_t health_err = brain_session_probe_health(&s_config_snapshot, &health_status);
        if (health_err != ESP_OK) {
            brain_session_lock();
            s_status.connected = false;
            s_active_client = NULL;
            s_status.last_error = health_err;
            s_status.last_error_ms = brain_session_now_ms();
            strlcpy(s_status.stage, "brain_offline", sizeof(s_status.stage));
            brain_session_unlock();
            ESP_LOGW(TAG,
                     "Atlas Brain offline, local UI fallback active: health_err=%s http=%d url=%s",
                     esp_err_to_name(health_err),
                     health_status,
                     s_status.url);
            vTaskDelay(pdMS_TO_TICKS(ATLAS_COMMON_BRAIN_SESSION_HEALTH_RETRY_MS));
            continue;
        }

        brain_session_set_stage("connecting", ESP_OK);
        esp_websocket_client_config_t ws_cfg = {
            .uri = url,
            .disable_auto_reconnect = true,
            .buffer_size = 2048,
            .network_timeout_ms = 5000,
            .reconnect_timeout_ms = 2000,
            .ping_interval_sec = 15,
            .task_name = s_config_snapshot.websocket_task_name == NULL ? "atlas_session_ws" : s_config_snapshot.websocket_task_name,
            .task_stack = 8192,
            .task_prio = 4,
        };
        esp_websocket_client_handle_t client = esp_websocket_client_init(&ws_cfg);
        if (client == NULL) {
            brain_session_set_stage("init_failed", ESP_FAIL);
            vTaskDelay(pdMS_TO_TICKS(ATLAS_COMMON_BRAIN_SESSION_RECONNECT_MS));
            continue;
        }
        esp_err_t err = esp_websocket_register_events(client,
                                                      WEBSOCKET_EVENT_ANY,
                                                      brain_session_event_handler,
                                                      NULL);
        if (err != ESP_OK) {
            brain_session_set_stage("event_failed", err);
            esp_websocket_client_destroy(client);
            vTaskDelay(pdMS_TO_TICKS(ATLAS_COMMON_BRAIN_SESSION_RECONNECT_MS));
            continue;
        }

        err = esp_websocket_client_start(client);
        if (err != ESP_OK) {
            brain_session_set_stage("start_failed", err);
            esp_websocket_client_destroy(client);
            vTaskDelay(pdMS_TO_TICKS(ATLAS_COMMON_BRAIN_SESSION_RECONNECT_MS));
            continue;
        }

        const uint32_t started = brain_session_now_ms();
        while (!s_stop_requested &&
               !esp_websocket_client_is_connected(client) &&
               brain_session_now_ms() - started < ATLAS_COMMON_BRAIN_SESSION_CONNECT_WAIT_MS) {
            vTaskDelay(pdMS_TO_TICKS(80));
        }

        if (!esp_websocket_client_is_connected(client)) {
            brain_session_set_stage("connect_timeout", ESP_ERR_TIMEOUT);
            (void)esp_websocket_client_close(client, pdMS_TO_TICKS(250));
            esp_websocket_client_stop(client);
            esp_websocket_client_destroy(client);
            vTaskDelay(pdMS_TO_TICKS(ATLAS_COMMON_BRAIN_SESSION_RECONNECT_MS));
            continue;
        }

        brain_session_lock();
        s_status.connected = true;
        s_status.connects++;
        s_status.last_connected_ms = brain_session_now_ms();
        s_active_client = client;
        brain_session_unlock();
        brain_session_set_stage("connected", ESP_OK);
        (void)brain_session_send_event(client, "hello", brain_session_hello_detail());

        uint32_t last_ping = brain_session_now_ms();
        while (!s_stop_requested && esp_websocket_client_is_connected(client)) {
            const uint32_t now = brain_session_now_ms();
            if (now - last_ping >= ATLAS_COMMON_BRAIN_SESSION_PING_MS) {
                (void)brain_session_send_event(client, "ping", "periodic_state");
                last_ping = now;
            }
            vTaskDelay(pdMS_TO_TICKS(250));
        }

        brain_session_lock();
        if (s_status.connected) {
            s_status.disconnects++;
        }
        s_status.connected = false;
        s_active_client = NULL;
        if (s_turn_waiting && s_turn_sem != NULL) {
            strlcpy(s_turn_response,
                    "{\"ok\":false,\"type\":\"turn.error\",\"error\":\"brain websocket disconnected\"}",
                    sizeof(s_turn_response));
            s_turn_response_len = strlen(s_turn_response);
            xSemaphoreGive(s_turn_sem);
        }
        brain_session_unlock();
        brain_session_set_stage(s_stop_requested ? "stopped" : "disconnected", ESP_OK);
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);

        if (!s_stop_requested) {
            vTaskDelay(pdMS_TO_TICKS(ATLAS_COMMON_BRAIN_SESSION_RECONNECT_MS));
        }
    }

    brain_session_lock();
    s_status.running = false;
    s_status.connected = false;
    brain_session_unlock();
    s_task = NULL;
    vTaskDelete(NULL);
}

static void brain_session_copy_config(const atlas_common_brain_session_config_t *config)
{
    memset(&s_config_snapshot, 0, sizeof(s_config_snapshot));
    if (config == NULL) {
        return;
    }
    strlcpy(s_base_url, config->base_url == NULL ? "" : config->base_url, sizeof(s_base_url));
    strlcpy(s_device_id, config->device_id == NULL ? "device" : config->device_id, sizeof(s_device_id));
    strlcpy(s_protocol, config->protocol == NULL ? "atlas.brain.session.v1" : config->protocol, sizeof(s_protocol));
    strlcpy(s_hello_detail, config->hello_detail == NULL ? "boot/session" : config->hello_detail, sizeof(s_hello_detail));
    strlcpy(s_task_name, config->task_name == NULL ? "atlas_brain_session" : config->task_name, sizeof(s_task_name));
    strlcpy(s_websocket_task_name,
            config->websocket_task_name == NULL ? "atlas_session_ws" : config->websocket_task_name,
            sizeof(s_websocket_task_name));
    s_config_snapshot = *config;
    s_config_snapshot.base_url = s_base_url;
    s_config_snapshot.device_id = s_device_id;
    s_config_snapshot.protocol = s_protocol;
    s_config_snapshot.hello_detail = s_hello_detail;
    s_config_snapshot.task_name = s_task_name;
    s_config_snapshot.websocket_task_name = s_websocket_task_name;
}

esp_err_t atlas_common_brain_session_start(const atlas_common_brain_session_config_t *config)
{
    ESP_RETURN_ON_ERROR(brain_session_ensure_mutex(), TAG, "mutex");
    ESP_RETURN_ON_ERROR(brain_session_ensure_request_sync(), TAG, "request sync");
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char url[sizeof(s_status.url)] = "";
    brain_session_copy_config(config);
    const bool enabled = brain_session_config_enabled(&s_config_snapshot, url, sizeof(url));

    brain_session_lock();
    memset(&s_status, 0, sizeof(s_status));
    s_status.enabled = enabled;
    s_status.last_error = ESP_OK;
    strlcpy(s_status.stage, enabled ? "starting" : "disabled", sizeof(s_status.stage));
    strlcpy(s_status.url, url, sizeof(s_status.url));
    s_active_client = NULL;
    s_turn_waiting = false;
    brain_session_reset_turn_buffers_locked();
    s_stop_requested = false;
    brain_session_unlock();

    if (!enabled) {
        ESP_LOGI(TAG, "Atlas Brain session disabled; llm.mode must be host and base_url must be set");
        return ESP_OK;
    }

    const BaseType_t ok = xTaskCreate(brain_session_task,
                                      s_config_snapshot.task_name == NULL ? "atlas_brain_session" : s_config_snapshot.task_name,
                                      ATLAS_COMMON_BRAIN_SESSION_TASK_STACK,
                                      NULL,
                                      ATLAS_COMMON_BRAIN_SESSION_TASK_PRIORITY,
                                      &s_task);
    if (ok != pdPASS) {
        brain_session_set_stage("task_failed", ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Atlas Brain session client starting: %s", url);
    return ESP_OK;
}

void atlas_common_brain_session_stop(void)
{
    s_stop_requested = true;
}

void atlas_common_brain_session_get_status(atlas_common_brain_session_status_t *status)
{
    if (status == NULL) {
        return;
    }
    brain_session_lock();
    *status = s_status;
    brain_session_unlock();
}

esp_err_t atlas_common_brain_session_turn_wav(const uint8_t *wav,
                                         size_t wav_len,
                                         char **response,
                                         size_t *response_len,
                                         uint8_t **tts_wav,
                                         size_t *tts_wav_len,
                                         uint32_t timeout_ms)
{
    if (wav == NULL || wav_len == 0 || response == NULL || response_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *response = NULL;
    *response_len = 0;
    if (tts_wav != NULL) {
        *tts_wav = NULL;
    }
    if (tts_wav_len != NULL) {
        *tts_wav_len = 0;
    }

    ESP_RETURN_ON_ERROR(brain_session_ensure_mutex(), TAG, "mutex");
    ESP_RETURN_ON_ERROR(brain_session_ensure_request_sync(), TAG, "request sync");
    if (xSemaphoreTake(s_request_mutex, pdMS_TO_TICKS(1)) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    while (xSemaphoreTake(s_turn_sem, 0) == pdTRUE) {
    }

    brain_session_lock();
    esp_websocket_client_handle_t client = s_active_client;
    const bool connected = s_status.connected &&
                           client != NULL &&
                           esp_websocket_client_is_connected(client);
    if (!connected) {
        brain_session_unlock();
        xSemaphoreGive(s_request_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    s_turn_waiting = true;
    s_turn_request_id++;
    if (s_turn_request_id == 0) {
        s_turn_request_id = 1;
    }
    const uint32_t request_id = s_turn_request_id;
    brain_session_reset_turn_buffers_locked();
    brain_session_unlock();

    char begin[320];
    snprintf(begin,
             sizeof(begin),
             "{\"type\":\"turn.audio.begin\",\"protocol\":\"%s\","
             "\"request_id\":%" PRIu32 ",\"device_id\":\"%s\","
             "\"format\":\"wav\",\"bytes\":%u,\"language\":\"auto\",\"speak\":true,"
             "\"tts_transport\":\"ws_binary\",\"ts_ms\":%" PRIu32 "}",
             brain_session_protocol(),
             request_id,
             brain_session_device_id(),
             (unsigned)wav_len,
             brain_session_now_ms());

    esp_err_t err = brain_session_send_json(client, begin);
    if (err == ESP_OK) {
        const int sent = esp_websocket_client_send_bin(client,
                                                       (const char *)wav,
                                                       (int)wav_len,
                                                       pdMS_TO_TICKS(ATLAS_COMMON_BRAIN_SESSION_SEND_TIMEOUT_MS * 4u));
        err = sent < 0 ? ESP_FAIL : ESP_OK;
        if (err == ESP_OK) {
            brain_session_lock();
            s_status.messages_sent++;
            brain_session_unlock();
        }
    }

    if (err != ESP_OK) {
        brain_session_lock();
        s_turn_waiting = false;
        brain_session_reset_turn_buffers_locked();
        brain_session_unlock();
        xSemaphoreGive(s_request_mutex);
        return err;
    }

    const uint32_t wait_ms = timeout_ms == 0 ? ATLAS_COMMON_BRAIN_SESSION_TURN_TIMEOUT_MS : timeout_ms;
    if (xSemaphoreTake(s_turn_sem, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        brain_session_lock();
        s_turn_waiting = false;
        brain_session_reset_turn_buffers_locked();
        brain_session_unlock();
        xSemaphoreGive(s_request_mutex);
        return ESP_ERR_TIMEOUT;
    }

    brain_session_lock();
    s_turn_waiting = false;
    if (s_turn_response_len > 0) {
        char *copy = (char *)malloc(s_turn_response_len + 1);
        if (copy != NULL) {
            memcpy(copy, s_turn_response, s_turn_response_len + 1);
            *response = copy;
            *response_len = s_turn_response_len;
        } else {
            err = ESP_ERR_NO_MEM;
        }
    } else {
        err = ESP_FAIL;
    }

    if (err == ESP_OK && tts_wav != NULL && tts_wav_len != NULL &&
        s_turn_tts_data != NULL && s_turn_tts_received >= 44 &&
        s_turn_tts_received <= s_turn_tts_expected &&
        memcmp(s_turn_tts_data, "RIFF", 4) == 0) {
        *tts_wav = s_turn_tts_data;
        *tts_wav_len = s_turn_tts_received;
        s_turn_tts_data = NULL;
        s_turn_tts_expected = 0;
        s_turn_tts_received = 0;
    }
    brain_session_reset_turn_buffers_locked();
    brain_session_unlock();

    xSemaphoreGive(s_request_mutex);
    return err;
}

size_t atlas_common_brain_session_write_json(char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    atlas_common_brain_session_status_t status;
    atlas_common_brain_session_get_status(&status);
    const int written = snprintf(dst,
                                 dst_size,
                                 "{\"enabled\":%s,\"running\":%s,\"connected\":%s,"
                                 "\"stage\":\"%s\",\"url\":\"%s\",\"connects\":%" PRIu32 ","
                                 "\"disconnects\":%" PRIu32 ",\"messages_sent\":%" PRIu32 ","
                                 "\"messages_received\":%" PRIu32 ","
                                 "\"last_error\":\"%s\",\"last_connected_ms\":%" PRIu32 "}",
                                 status.enabled ? "true" : "false",
                                 status.running ? "true" : "false",
                                 status.connected ? "true" : "false",
                                 status.stage,
                                 status.url,
                                 status.connects,
                                 status.disconnects,
                                 status.messages_sent,
                                 status.messages_received,
                                 esp_err_to_name(status.last_error),
                                 status.last_connected_ms);
    if (written < 0) {
        return 0;
    }
    return (size_t)written >= dst_size ? dst_size - 1 : (size_t)written;
}
