#include "atlas_opus_stream.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_audio_enc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_opus_enc.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "atlas_audio_service.h"

static const char *TAG = "atlas_opus";

#define ATLAS_OPUS_STREAM_TASK_STACK 24576u
#define ATLAS_OPUS_STREAM_TASK_PRIORITY 4u
#define ATLAS_OPUS_STREAM_CONNECT_TIMEOUT_MS 8000u
#define ATLAS_OPUS_STREAM_SEND_TIMEOUT_MS 1500u
#define ATLAS_OPUS_STREAM_MAX_DURATION_MS (5u * 60u * 1000u)

static TaskHandle_t s_stream_task;
static SemaphoreHandle_t s_stream_mutex;
static volatile bool s_stream_stop_requested;
static atlas_opus_stream_status_t s_stream_status = {
    .frame_ms = ATLAS_OPUS_PROBE_FRAME_MS,
    .sample_rate = ATLAS_OPUS_PROBE_SAMPLE_RATE,
    .last_error = ESP_OK,
    .stage = "idle",
};

static uint32_t stream_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ull);
}

static esp_err_t stream_ensure_mutex(void)
{
    if (s_stream_mutex == NULL) {
        s_stream_mutex = xSemaphoreCreateMutex();
    }
    return s_stream_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

static void stream_status_lock(void)
{
    if (stream_ensure_mutex() == ESP_OK) {
        (void)xSemaphoreTake(s_stream_mutex, portMAX_DELAY);
    }
}

static void stream_status_unlock(void)
{
    if (s_stream_mutex != NULL) {
        xSemaphoreGive(s_stream_mutex);
    }
}

static void stream_set_stage(const char *stage, esp_err_t err)
{
    stream_status_lock();
    strlcpy(s_stream_status.stage, stage == NULL ? "unknown" : stage, sizeof(s_stream_status.stage));
    s_stream_status.last_error = err;
    s_stream_status.free_internal_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_stream_status.free_psram_heap = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    stream_status_unlock();
}

static void write_be16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)((value >> 8) & 0xffu);
    dst[1] = (uint8_t)(value & 0xffu);
}

static void write_be32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)((value >> 24) & 0xffu);
    dst[1] = (uint8_t)((value >> 16) & 0xffu);
    dst[2] = (uint8_t)((value >> 8) & 0xffu);
    dst[3] = (uint8_t)(value & 0xffu);
}

static void write_stream_binary_header(uint8_t *dst,
                                       uint32_t seq,
                                       uint32_t timestamp_ms,
                                       uint16_t sample_rate,
                                       uint16_t frame_ms,
                                       uint16_t payload_len,
                                       const atlas_audio_mic_level_t *level)
{
    memcpy(dst, ATLAS_OPUS_STREAM_FRAME_MAGIC, 4);
    dst[4] = 1u;                                      // version
    dst[5] = ATLAS_OPUS_STREAM_BINARY_HEADER_BYTES;   // header length
    dst[6] = 0u;                                      // flags
    dst[7] = 1u;                                      // channels
    write_be32(dst + 8, seq);
    write_be32(dst + 12, timestamp_ms);
    write_be16(dst + 16, sample_rate);
    write_be16(dst + 18, frame_ms);
    write_be16(dst + 20, payload_len);
    dst[22] = level == NULL ? 0u : level->level;
    dst[23] = 0u;
    write_be32(dst + 24, level == NULL ? 0u : level->rms);
    write_be32(dst + 28, level == NULL ? 0u : level->peak);
}

static esp_err_t open_opus_encoder(void **encoder, uint16_t *frame_samples, int *frame_size_bytes, int *outbuf_size)
{
    if (encoder == NULL || frame_samples == NULL || frame_size_bytes == NULL || outbuf_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_opus_enc_config_t cfg = {
        .sample_rate = ESP_AUDIO_SAMPLE_RATE_16K,
        .channel = ESP_AUDIO_MONO,
        .bits_per_sample = ESP_AUDIO_BIT16,
        .bitrate = ESP_OPUS_BITRATE_AUTO,
        .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO,
        .complexity = 0,
        .enable_fec = false,
        .enable_dtx = true,
        .enable_vbr = true,
    };

    esp_audio_err_t enc_err = esp_opus_enc_open(&cfg, sizeof(cfg), encoder);
    if (enc_err != ESP_AUDIO_ERR_OK || *encoder == NULL) {
        ESP_LOGW(TAG, "opus encoder open failed: %d", (int)enc_err);
        return ESP_FAIL;
    }

    enc_err = esp_opus_enc_get_frame_size(*encoder, frame_size_bytes, outbuf_size);
    if (enc_err != ESP_AUDIO_ERR_OK || *frame_size_bytes <= 0 || *outbuf_size <= 0) {
        ESP_LOGW(TAG, "opus frame size failed: %d", (int)enc_err);
        esp_opus_enc_close(*encoder);
        *encoder = NULL;
        return ESP_FAIL;
    }
    *frame_samples = (uint16_t)(*frame_size_bytes / sizeof(int16_t));
    return ESP_OK;
}

static uint16_t clamp_probe_duration(uint16_t duration_ms)
{
    if (duration_ms < ATLAS_OPUS_PROBE_FRAME_MS) {
        return ATLAS_OPUS_PROBE_FRAME_MS;
    }
    if (duration_ms > 3000u) {
        return 3000u;
    }
    return duration_ms;
}

static uint32_t clamp_stream_duration(uint32_t duration_ms)
{
    if (duration_ms == 0u) {
        return 0u;
    }
    if (duration_ms < ATLAS_OPUS_PROBE_FRAME_MS) {
        return ATLAS_OPUS_PROBE_FRAME_MS;
    }
    if (duration_ms > ATLAS_OPUS_STREAM_MAX_DURATION_MS) {
        return ATLAS_OPUS_STREAM_MAX_DURATION_MS;
    }
    return duration_ms;
}

static void opus_stream_task(void *arg)
{
    (void)arg;

    char url[sizeof(s_stream_status.url)];
    uint32_t duration_ms = 0;
    stream_status_lock();
    strlcpy(url, s_stream_status.url, sizeof(url));
    duration_ms = s_stream_status.duration_ms;
    s_stream_status.running = true;
    s_stream_status.connected = false;
    s_stream_status.started_ms = stream_now_ms();
    s_stream_status.ended_ms = 0;
    s_stream_status.free_internal_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_stream_status.free_psram_heap = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    strlcpy(s_stream_status.stage, "connecting", sizeof(s_stream_status.stage));
    stream_status_unlock();

    esp_err_t final_err = ESP_OK;
    void *encoder = NULL;
    int frame_size_bytes = 0;
    int outbuf_size = 0;
    uint16_t frame_samples = 0;
    int16_t *pcm = NULL;
    uint8_t *opus = NULL;
    uint8_t *wire = NULL;
    esp_websocket_client_handle_t client = NULL;

    final_err = open_opus_encoder(&encoder, &frame_samples, &frame_size_bytes, &outbuf_size);
    if (final_err != ESP_OK) {
        stream_set_stage("encoder_failed", final_err);
        goto done;
    }

    pcm = (int16_t *)heap_caps_malloc((size_t)frame_size_bytes, MALLOC_CAP_8BIT);
    opus = (uint8_t *)heap_caps_malloc((size_t)outbuf_size, MALLOC_CAP_8BIT);
    wire = (uint8_t *)heap_caps_malloc((size_t)outbuf_size + ATLAS_OPUS_STREAM_BINARY_HEADER_BYTES, MALLOC_CAP_8BIT);
    if (pcm == NULL || opus == NULL || wire == NULL) {
        final_err = ESP_ERR_NO_MEM;
        stream_set_stage("no_mem", final_err);
        goto done;
    }

    esp_websocket_client_config_t ws_cfg = {
        .uri = url,
        .disable_auto_reconnect = true,
        .buffer_size = 2048,
        .network_timeout_ms = 5000,
        .reconnect_timeout_ms = 2000,
        .ping_interval_sec = 10,
        .task_name = "atlas_ws_audio",
        .task_stack = 6144,
        .task_prio = 4,
    };
    client = esp_websocket_client_init(&ws_cfg);
    if (client == NULL) {
        final_err = ESP_FAIL;
        stream_set_stage("ws_init_failed", final_err);
        goto done;
    }

    final_err = esp_websocket_client_start(client);
    if (final_err != ESP_OK) {
        stream_set_stage("ws_start_failed", final_err);
        goto done;
    }

    const uint32_t connect_started = stream_now_ms();
    while (!s_stream_stop_requested &&
           !esp_websocket_client_is_connected(client) &&
           stream_now_ms() - connect_started < ATLAS_OPUS_STREAM_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    if (!esp_websocket_client_is_connected(client)) {
        final_err = s_stream_stop_requested ? ESP_OK : ESP_ERR_TIMEOUT;
        stream_set_stage(s_stream_stop_requested ? "stopped" : "connect_timeout", final_err);
        goto done;
    }

    char start_json[360];
    snprintf(start_json,
             sizeof(start_json),
             "{\"type\":\"start\",\"protocol\":\"" ATLAS_OPUS_STREAM_PROTOCOL "\","
             "\"device_id\":\"dualeye\",\"codec\":\"opus\",\"sample_rate\":%u,"
             "\"channels\":1,\"frame_ms\":%u,\"header\":\"AOP1\",\"duration_ms\":%" PRIu32 "}",
             (unsigned)ATLAS_OPUS_PROBE_SAMPLE_RATE,
             (unsigned)ATLAS_OPUS_PROBE_FRAME_MS,
             duration_ms);
    if (esp_websocket_client_send_text(client, start_json, strlen(start_json), pdMS_TO_TICKS(ATLAS_OPUS_STREAM_SEND_TIMEOUT_MS)) < 0) {
        final_err = ESP_FAIL;
        stream_set_stage("start_send_failed", final_err);
        goto done;
    }

    stream_status_lock();
    s_stream_status.connected = true;
    s_stream_status.frame_samples = frame_samples;
    strlcpy(s_stream_status.stage, "streaming", sizeof(s_stream_status.stage));
    stream_status_unlock();
    atlas_audio_service_note_stage(ATLAS_AUDIO_SERVICE_MODE_RECORDING, "opus_streaming");

    const uint32_t stream_started = stream_now_ms();
    uint32_t seq = 0;
    while (!s_stream_stop_requested && esp_websocket_client_is_connected(client)) {
        if (duration_ms > 0u && stream_now_ms() - stream_started >= duration_ms) {
            break;
        }

        uint32_t mute_remaining_ms = 0;
        if (atlas_audio_service_is_muted(&mute_remaining_ms)) {
            stream_status_lock();
            s_stream_status.muted_frames++;
            s_stream_status.free_internal_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            s_stream_status.free_psram_heap = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            strlcpy(s_stream_status.stage, "muted", sizeof(s_stream_status.stage));
            stream_status_unlock();
            const uint32_t delay_ms = mute_remaining_ms > 0u && mute_remaining_ms < ATLAS_OPUS_PROBE_FRAME_MS ?
                                      mute_remaining_ms :
                                      ATLAS_OPUS_PROBE_FRAME_MS;
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            continue;
        }

        stream_status_lock();
        if (strcmp(s_stream_status.stage, "streaming") != 0) {
            strlcpy(s_stream_status.stage, "streaming", sizeof(s_stream_status.stage));
        }
        stream_status_unlock();

        atlas_audio_mic_level_t level = {0};
        final_err = atlas_audio_capture_pcm_mono(pcm, frame_samples, &level);
        if (final_err != ESP_OK) {
            stream_status_lock();
            s_stream_status.capture_failures++;
            stream_status_unlock();
            stream_set_stage("capture_failed", final_err);
            break;
        }

        esp_audio_enc_in_frame_t in = {
            .buffer = (uint8_t *)pcm,
            .len = (uint32_t)frame_size_bytes,
        };
        esp_audio_enc_out_frame_t out = {
            .buffer = opus,
            .len = (uint32_t)outbuf_size,
            .encoded_bytes = 0,
        };
        const esp_audio_err_t enc_err = esp_opus_enc_process(encoder, &in, &out);
        if (enc_err != ESP_AUDIO_ERR_OK || out.encoded_bytes == 0 || out.encoded_bytes > (uint32_t)outbuf_size) {
            final_err = ESP_FAIL;
            stream_status_lock();
            s_stream_status.encode_failures++;
            stream_status_unlock();
            stream_set_stage("encode_failed", final_err);
            break;
        }

        ++seq;
        write_stream_binary_header(wire,
                                   seq,
                                   stream_now_ms(),
                                   ATLAS_OPUS_PROBE_SAMPLE_RATE,
                                   ATLAS_OPUS_PROBE_FRAME_MS,
                                   (uint16_t)out.encoded_bytes,
                                   &level);
        memcpy(wire + ATLAS_OPUS_STREAM_BINARY_HEADER_BYTES, opus, out.encoded_bytes);
        const int wire_len = (int)(ATLAS_OPUS_STREAM_BINARY_HEADER_BYTES + out.encoded_bytes);
        const int sent = esp_websocket_client_send_bin(client, (const char *)wire, wire_len, pdMS_TO_TICKS(ATLAS_OPUS_STREAM_SEND_TIMEOUT_MS));

        stream_status_lock();
        s_stream_status.frames_encoded++;
        s_stream_status.sequence = seq;
        s_stream_status.last_packet_bytes = (uint16_t)out.encoded_bytes;
        s_stream_status.mic_level = level.level;
        s_stream_status.mic_rms = level.rms;
        s_stream_status.mic_peak = level.peak;
        if (sent > 0) {
            s_stream_status.frames_sent++;
            s_stream_status.bytes_sent += out.encoded_bytes;
        } else {
            s_stream_status.send_failures++;
        }
        s_stream_status.free_internal_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        s_stream_status.free_psram_heap = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        stream_status_unlock();

        if (sent < 0) {
            final_err = ESP_FAIL;
            stream_set_stage("send_failed", final_err);
            break;
        }
    }

    if (client != NULL && esp_websocket_client_is_connected(client)) {
        char end_json[360];
        atlas_opus_stream_status_t snapshot;
        atlas_opus_stream_get_status(&snapshot);
        snprintf(end_json,
                 sizeof(end_json),
                 "{\"type\":\"end\",\"protocol\":\"" ATLAS_OPUS_STREAM_PROTOCOL "\","
                 "\"device_id\":\"dualeye\",\"reason\":\"%s\",\"frames\":%" PRIu32 ","
                 "\"bytes\":%" PRIu32 ",\"duration_ms\":%" PRIu32 "}",
                 s_stream_stop_requested ? "stopped" : (final_err == ESP_OK ? "duration_done" : esp_err_to_name(final_err)),
                 snapshot.frames_sent,
                 snapshot.bytes_sent,
                 stream_now_ms() - snapshot.started_ms);
        (void)esp_websocket_client_send_text(client, end_json, strlen(end_json), pdMS_TO_TICKS(ATLAS_OPUS_STREAM_SEND_TIMEOUT_MS));
        (void)esp_websocket_client_close(client, pdMS_TO_TICKS(500));
    }

done:
    if (client != NULL) {
        (void)esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);
    }
    if (encoder != NULL) {
        esp_opus_enc_close(encoder);
    }
    free(pcm);
    free(opus);
    free(wire);

    stream_status_lock();
    s_stream_status.running = false;
    s_stream_status.connected = false;
    s_stream_status.stop_requested = false;
    s_stream_status.ended_ms = stream_now_ms();
    s_stream_status.last_error = final_err;
    s_stream_status.free_internal_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_stream_status.free_psram_heap = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_stream_status.stage[0] == '\0' ||
        strcmp(s_stream_status.stage, "streaming") == 0 ||
        (strcmp(s_stream_status.stage, "muted") == 0 && s_stream_status.frames_sent > 0)) {
        strlcpy(s_stream_status.stage, final_err == ESP_OK ? "done" : esp_err_to_name(final_err), sizeof(s_stream_status.stage));
    }
    stream_status_unlock();
    atlas_audio_service_note_stage(final_err == ESP_OK ? ATLAS_AUDIO_SERVICE_MODE_IDLE : ATLAS_AUDIO_SERVICE_MODE_ERROR,
                                   final_err == ESP_OK ? "opus_done" : "opus_error");

    s_stream_stop_requested = false;
    s_stream_task = NULL;
    vTaskDeleteWithCaps(NULL);
}

esp_err_t atlas_opus_stream_start(const atlas_opus_stream_config_t *config)
{
    if (config == NULL || config->url == NULL || config->url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(stream_ensure_mutex(), TAG, "stream mutex");

    stream_status_lock();
    const bool already_running = s_stream_status.running || s_stream_task != NULL;
    stream_status_unlock();
    if (already_running) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t duration_ms = clamp_stream_duration(config->duration_ms);
    s_stream_stop_requested = false;

    stream_status_lock();
    memset(&s_stream_status, 0, sizeof(s_stream_status));
    s_stream_status.running = true;
    s_stream_status.connected = false;
    s_stream_status.stop_requested = false;
    s_stream_status.continuous = duration_ms == 0u;
    s_stream_status.duration_ms = duration_ms;
    s_stream_status.frame_ms = ATLAS_OPUS_PROBE_FRAME_MS;
    s_stream_status.sample_rate = ATLAS_OPUS_PROBE_SAMPLE_RATE;
    s_stream_status.last_error = ESP_OK;
    s_stream_status.free_internal_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_stream_status.free_psram_heap = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    strlcpy(s_stream_status.stage, "queued", sizeof(s_stream_status.stage));
    strlcpy(s_stream_status.url, config->url, sizeof(s_stream_status.url));
    stream_status_unlock();

    BaseType_t ok = xTaskCreateWithCaps(opus_stream_task,
                                        "atlas_opus_ws",
                                        ATLAS_OPUS_STREAM_TASK_STACK,
                                        NULL,
                                        ATLAS_OPUS_STREAM_TASK_PRIORITY,
                                        &s_stream_task,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        stream_status_lock();
        s_stream_status.running = false;
        s_stream_status.last_error = ESP_ERR_NO_MEM;
        s_stream_status.free_internal_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        s_stream_status.free_psram_heap = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        strlcpy(s_stream_status.stage, "task_create_failed", sizeof(s_stream_status.stage));
        stream_status_unlock();
        s_stream_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t atlas_opus_stream_stop(void)
{
    ESP_RETURN_ON_ERROR(stream_ensure_mutex(), TAG, "stream mutex");

    stream_status_lock();
    const bool running = s_stream_status.running || s_stream_task != NULL;
    s_stream_status.stop_requested = running;
    if (running) {
        strlcpy(s_stream_status.stage, "stopping", sizeof(s_stream_status.stage));
    }
    stream_status_unlock();

    if (!running) {
        return ESP_ERR_INVALID_STATE;
    }
    s_stream_stop_requested = true;
    return ESP_OK;
}

void atlas_opus_stream_get_status(atlas_opus_stream_status_t *status)
{
    if (status == NULL) {
        return;
    }
    if (stream_ensure_mutex() != ESP_OK) {
        memset(status, 0, sizeof(*status));
        status->last_error = ESP_ERR_NO_MEM;
        strlcpy(status->stage, "mutex_failed", sizeof(status->stage));
        return;
    }
    stream_status_lock();
    *status = s_stream_status;
    stream_status_unlock();
    status->free_internal_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    status->free_psram_heap = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

size_t atlas_opus_stream_write_status_json(const atlas_opus_stream_status_t *status, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    if (status == NULL) {
        return snprintf(dst, dst_size, "{\"ok\":false,\"error\":\"missing status\"}");
    }

    char safe_stage[40];
    char safe_url[220];
    size_t off = 0;
    for (size_t i = 0; status->stage[i] != '\0' && off + 1 < sizeof(safe_stage); ++i) {
        const unsigned char c = (unsigned char)status->stage[i];
        safe_stage[off++] = (c < 0x20 || c == '"' || c == '\\') ? '_' : (char)c;
    }
    safe_stage[off] = '\0';
    off = 0;
    for (size_t i = 0; status->url[i] != '\0' && off + 1 < sizeof(safe_url); ++i) {
        const unsigned char c = (unsigned char)status->url[i];
        safe_url[off++] = (c < 0x20 || c == '"' || c == '\\') ? '_' : (char)c;
    }
    safe_url[off] = '\0';

    return snprintf(dst,
                    dst_size,
                    "{"
                    "\"running\":%s,\"connected\":%s,\"stop_requested\":%s,\"continuous\":%s,"
                    "\"stage\":\"%s\",\"url\":\"%s\",\"duration_ms\":%" PRIu32 ","
                    "\"frame_ms\":%" PRIu16 ",\"sample_rate\":%" PRIu16 ",\"frame_samples\":%" PRIu16 ","
                    "\"started_ms\":%" PRIu32 ",\"ended_ms\":%" PRIu32 ","
                    "\"frames_encoded\":%" PRIu32 ",\"frames_sent\":%" PRIu32 ",\"bytes_sent\":%" PRIu32 ","
                    "\"send_failures\":%" PRIu32 ",\"muted_frames\":%" PRIu32 ","
                    "\"capture_failures\":%" PRIu32 ",\"encode_failures\":%" PRIu32 ",\"sequence\":%" PRIu32 ","
                    "\"free_internal_heap\":%" PRIu32 ",\"free_psram_heap\":%" PRIu32 ","
                    "\"last_packet_bytes\":%" PRIu16 ","
                    "\"mic_level\":%" PRIu8 ",\"mic_rms\":%" PRIu32 ",\"mic_peak\":%" PRIu32 ","
                    "\"last_error\":\"%s\""
                    "}",
                    status->running ? "true" : "false",
                    status->connected ? "true" : "false",
                    status->stop_requested ? "true" : "false",
                    status->continuous ? "true" : "false",
                    safe_stage,
                    safe_url,
                    status->duration_ms,
                    status->frame_ms,
                    status->sample_rate,
                    status->frame_samples,
                    status->started_ms,
                    status->ended_ms,
                    status->frames_encoded,
                    status->frames_sent,
                    status->bytes_sent,
                    status->send_failures,
                    status->muted_frames,
                    status->capture_failures,
                    status->encode_failures,
                    status->sequence,
                    status->free_internal_heap,
                    status->free_psram_heap,
                    status->last_packet_bytes,
                    status->mic_level,
                    status->mic_rms,
                    status->mic_peak,
                    esp_err_to_name(status->last_error));
}

esp_err_t atlas_opus_probe_run(uint16_t duration_ms, atlas_opus_probe_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));
    result->requested_ms = clamp_probe_duration(duration_ms);
    result->frame_ms = ATLAS_OPUS_PROBE_FRAME_MS;
    result->frames_requested = (uint16_t)((result->requested_ms + ATLAS_OPUS_PROBE_FRAME_MS - 1u) /
                                          ATLAS_OPUS_PROBE_FRAME_MS);
    result->last_error = ESP_OK;

    esp_opus_enc_config_t cfg = {
        .sample_rate = ESP_AUDIO_SAMPLE_RATE_16K,
        .channel = ESP_AUDIO_MONO,
        .bits_per_sample = ESP_AUDIO_BIT16,
        .bitrate = ESP_OPUS_BITRATE_AUTO,
        .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO,
        .complexity = 0,
        .enable_fec = false,
        .enable_dtx = true,
        .enable_vbr = true,
    };

    void *encoder = NULL;
    esp_audio_err_t enc_err = esp_opus_enc_open(&cfg, sizeof(cfg), &encoder);
    if (enc_err != ESP_AUDIO_ERR_OK || encoder == NULL) {
        ESP_LOGW(TAG, "opus encoder open failed: %d", (int)enc_err);
        result->last_error = ESP_FAIL;
        return ESP_FAIL;
    }
    result->encoder_ready = true;

    int frame_size_bytes = 0;
    int outbuf_size = 0;
    enc_err = esp_opus_enc_get_frame_size(encoder, &frame_size_bytes, &outbuf_size);
    if (enc_err != ESP_AUDIO_ERR_OK || frame_size_bytes <= 0 || outbuf_size <= 0) {
        ESP_LOGW(TAG, "opus frame size failed: %d", (int)enc_err);
        esp_opus_enc_close(encoder);
        result->last_error = ESP_FAIL;
        return ESP_FAIL;
    }

    result->frame_samples = (uint16_t)(frame_size_bytes / sizeof(int16_t));
    int16_t *pcm = (int16_t *)heap_caps_malloc((size_t)frame_size_bytes, MALLOC_CAP_8BIT);
    uint8_t *opus = (uint8_t *)heap_caps_malloc((size_t)outbuf_size, MALLOC_CAP_8BIT);
    if (pcm == NULL || opus == NULL) {
        free(pcm);
        free(opus);
        esp_opus_enc_close(encoder);
        result->last_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    uint32_t rms_sum = 0;
    uint32_t peak_max = 0;
    uint8_t level_max = 0;
    uint16_t min_packet = UINT16_MAX;
    esp_err_t err = ESP_OK;

    for (uint16_t i = 0; i < result->frames_requested; ++i) {
        atlas_audio_mic_level_t level;
        err = atlas_audio_capture_pcm_mono(pcm, result->frame_samples, &level);
        if (err != ESP_OK) {
            break;
        }

        esp_audio_enc_in_frame_t in = {
            .buffer = (uint8_t *)pcm,
            .len = (uint32_t)frame_size_bytes,
        };
        esp_audio_enc_out_frame_t out = {
            .buffer = opus,
            .len = (uint32_t)outbuf_size,
            .encoded_bytes = 0,
        };
        enc_err = esp_opus_enc_process(encoder, &in, &out);
        if (enc_err != ESP_AUDIO_ERR_OK) {
            ESP_LOGW(TAG, "opus encode failed frame=%u err=%d", (unsigned)i, (int)enc_err);
            err = ESP_FAIL;
            break;
        }

        result->frames_encoded++;
        result->encoded_bytes += out.encoded_bytes;
        if (out.encoded_bytes < min_packet) {
            min_packet = (uint16_t)out.encoded_bytes;
        }
        if (out.encoded_bytes > result->max_packet_bytes) {
            result->max_packet_bytes = (uint16_t)out.encoded_bytes;
        }
        if (level.level > level_max) {
            level_max = level.level;
        }
        rms_sum += level.rms;
        if (level.peak > peak_max) {
            peak_max = level.peak;
        }
    }

    free(pcm);
    free(opus);
    esp_opus_enc_close(encoder);

    if (result->frames_encoded > 0) {
        result->min_packet_bytes = min_packet == UINT16_MAX ? 0u : min_packet;
        result->avg_packet_bytes = result->encoded_bytes / result->frames_encoded;
        result->mic_level = level_max;
        result->mic_rms = rms_sum / result->frames_encoded;
        result->mic_peak = peak_max;
    }
    result->last_error = err;
    return err;
}

size_t atlas_opus_probe_write_json(const atlas_opus_probe_result_t *result, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    if (result == NULL) {
        return snprintf(dst, dst_size, "{\"ok\":false,\"error\":\"missing result\"}");
    }
    return snprintf(dst,
                    dst_size,
                    "{"
                    "\"encoder_ready\":%s,\"requested_ms\":%" PRIu16 ",\"frame_ms\":%" PRIu16 ","
                    "\"sample_rate\":%u,\"frame_samples\":%" PRIu16 ","
                    "\"frames_requested\":%" PRIu16 ",\"frames_encoded\":%" PRIu16 ","
                    "\"encoded_bytes\":%" PRIu32 ",\"min_packet_bytes\":%" PRIu16 ","
                    "\"max_packet_bytes\":%" PRIu16 ",\"avg_packet_bytes\":%" PRIu32 ","
                    "\"mic_level\":%" PRIu8 ",\"mic_rms\":%" PRIu32 ",\"mic_peak\":%" PRIu32 ","
                    "\"last_error\":\"%s\""
                    "}",
                    result->encoder_ready ? "true" : "false",
                    result->requested_ms,
                    result->frame_ms,
                    (unsigned)ATLAS_OPUS_PROBE_SAMPLE_RATE,
                    result->frame_samples,
                    result->frames_requested,
                    result->frames_encoded,
                    result->encoded_bytes,
                    result->min_packet_bytes,
                    result->max_packet_bytes,
                    result->avg_packet_bytes,
                    result->mic_level,
                    result->mic_rms,
                    result->mic_peak,
                    esp_err_to_name(result->last_error));
}
