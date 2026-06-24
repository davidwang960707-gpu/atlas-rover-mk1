#include "common/atlas_common_audio_service.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define ATLAS_COMMON_AUDIO_SERVICE_QUEUE_LEN 1u
#define ATLAS_COMMON_AUDIO_SERVICE_TASK_STACK 32768u
#define ATLAS_COMMON_AUDIO_SERVICE_TASK_PRIORITY 3u
#define ATLAS_COMMON_AUDIO_SERVICE_DEFAULT_TASK_NAME "atlas_audio_svc"

static const char *TAG = "atlas_common_audio_service";

typedef struct {
    atlas_common_audio_service_job_fn_t fn;
    void *ctx;
    SemaphoreHandle_t done;
    esp_err_t result;
} atlas_common_audio_service_job_t;

static atlas_common_audio_service_status_t s_status;
static atlas_common_audio_service_config_t s_config;
static QueueHandle_t s_job_queue;
static TaskHandle_t s_worker_task;
static bool s_operation_busy;
static bool s_job_running;
static SemaphoreHandle_t s_status_mutex;

static uint32_t service_now_ms(void)
{
    return s_config.now_ms_fn == NULL ? 0u : s_config.now_ms_fn();
}

static uint32_t remaining_ms(uint32_t now, uint32_t until)
{
    return until > now ? until - now : 0u;
}

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strlcpy(dst, src, dst_size);
}

static void json_escape(char *dst, size_t dst_size, const char *src)
{
    size_t out = 0;
    if (dst == NULL || dst_size == 0) {
        return;
    }
    const char *text = src == NULL ? "" : src;
    for (size_t i = 0; text[i] != '\0' && out + 1 < dst_size; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if ((c == '"' || c == '\\') && out + 2 < dst_size) {
            dst[out++] = '\\';
            dst[out++] = (char)c;
        } else if (c >= 0x20) {
            dst[out++] = (char)c;
        }
    }
    dst[out] = '\0';
}

static esp_err_t ensure_status_mutex(void)
{
    if (s_status_mutex == NULL) {
        s_status_mutex = xSemaphoreCreateMutex();
    }
    return s_status_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

static bool status_lock(void)
{
    return ensure_status_mutex() == ESP_OK &&
           xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE;
}

static void status_unlock(void)
{
    if (s_status_mutex != NULL) {
        xSemaphoreGive(s_status_mutex);
    }
}

static void refresh_busy_locked(void)
{
    s_status.job_running = s_job_running;
    s_status.busy = s_operation_busy || s_job_running;
}

static void note_mode_locked(atlas_common_audio_service_mode_t mode, const char *action)
{
    s_status.mode = mode;
    s_status.last_event_ms = service_now_ms();
    copy_text(s_status.last_action, sizeof(s_status.last_action), action);
    refresh_busy_locked();
}

static void note_mode(atlas_common_audio_service_mode_t mode, const char *action)
{
    if (status_lock()) {
        note_mode_locked(mode, action);
        status_unlock();
    }
}

static void set_operation_busy(bool busy)
{
    if (status_lock()) {
        s_operation_busy = busy;
        refresh_busy_locked();
        status_unlock();
    }
}

static void set_job_running_locked(bool running)
{
    s_job_running = running;
    refresh_busy_locked();
}

static void finish_job_mode_locked(esp_err_t err)
{
    if (err == ESP_OK) {
        s_status.consecutive_failures = 0;
        s_status.last_success_ms = service_now_ms();
        note_mode_locked(s_status.continuous_enabled ? ATLAS_COMMON_AUDIO_SERVICE_MODE_MONITORING : ATLAS_COMMON_AUDIO_SERVICE_MODE_IDLE,
                         s_status.continuous_enabled ? "turn_done_monitoring" : "turn_done_idle");
    } else {
        s_status.consecutive_failures++;
        note_mode_locked(s_status.continuous_enabled ? ATLAS_COMMON_AUDIO_SERVICE_MODE_MONITORING : ATLAS_COMMON_AUDIO_SERVICE_MODE_ERROR,
                         s_status.continuous_enabled ? "turn_error_monitoring" : "turn_error");
    }
}

static void audio_service_worker(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "audio service worker started");
    for (;;) {
        atlas_common_audio_service_job_t *job = NULL;
        if (xQueueReceive(s_job_queue, &job, portMAX_DELAY) != pdTRUE || job == NULL) {
            continue;
        }

        if (status_lock()) {
            set_job_running_locked(true);
            s_status.job_count++;
            s_status.last_job_ms = service_now_ms();
            s_status.last_error = ESP_OK;
            copy_text(s_status.last_failure, sizeof(s_status.last_failure), "");
            note_mode_locked(s_status.continuous_enabled ? ATLAS_COMMON_AUDIO_SERVICE_MODE_MONITORING : ATLAS_COMMON_AUDIO_SERVICE_MODE_IDLE,
                             "turn_worker_start");
            status_unlock();
        }

        const esp_err_t err = job->fn == NULL ? ESP_ERR_INVALID_ARG : job->fn(job->ctx);
        job->result = err;
        if (status_lock()) {
            s_status.last_error = err;
            if (err != ESP_OK) {
                s_status.job_error_count++;
                if (s_status.last_failure[0] == '\0') {
                    copy_text(s_status.last_failure, sizeof(s_status.last_failure), esp_err_to_name(err));
                }
            }
            set_job_running_locked(false);
            finish_job_mode_locked(err);
            status_unlock();
        }

        if (job->done != NULL) {
            xSemaphoreGive(job->done);
        } else {
            free(job);
        }
    }
}

void atlas_common_audio_service_init(const atlas_common_audio_service_config_t *config)
{
    (void)ensure_status_mutex();
    if (config != NULL) {
        s_config = *config;
    } else {
        memset(&s_config, 0, sizeof(s_config));
    }
    if (s_config.task_name == NULL || s_config.task_name[0] == '\0') {
        s_config.task_name = ATLAS_COMMON_AUDIO_SERVICE_DEFAULT_TASK_NAME;
    }

    if (status_lock()) {
        memset(&s_status, 0, sizeof(s_status));
        s_status.initialized = true;
        s_status.mode = ATLAS_COMMON_AUDIO_SERVICE_MODE_IDLE;
        s_status.last_error = ESP_OK;
        copy_text(s_status.last_action, sizeof(s_status.last_action), "init");
        s_operation_busy = false;
        s_job_running = false;
        refresh_busy_locked();
        status_unlock();
    } else {
        memset(&s_status, 0, sizeof(s_status));
        s_status.initialized = true;
        s_status.mode = ATLAS_COMMON_AUDIO_SERVICE_MODE_IDLE;
        s_status.last_error = ESP_ERR_NO_MEM;
        copy_text(s_status.last_action, sizeof(s_status.last_action), "init_no_lock");
    }

    if (s_job_queue == NULL) {
        s_job_queue = xQueueCreate(ATLAS_COMMON_AUDIO_SERVICE_QUEUE_LEN, sizeof(atlas_common_audio_service_job_t *));
    }
    if (s_job_queue != NULL && s_worker_task == NULL) {
        BaseType_t ok = xTaskCreateWithCaps(audio_service_worker,
                                            s_config.task_name,
                                            ATLAS_COMMON_AUDIO_SERVICE_TASK_STACK,
                                            NULL,
                                            ATLAS_COMMON_AUDIO_SERVICE_TASK_PRIORITY,
                                            &s_worker_task,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ok == pdPASS) {
            if (status_lock()) {
                s_status.worker_started = true;
                s_status.psram_stack = true;
                status_unlock();
            }
        } else {
            ok = xTaskCreate(audio_service_worker,
                             s_config.task_name,
                             ATLAS_COMMON_AUDIO_SERVICE_TASK_STACK,
                             NULL,
                             ATLAS_COMMON_AUDIO_SERVICE_TASK_PRIORITY,
                             &s_worker_task);
            if (ok == pdPASS) {
                if (status_lock()) {
                    s_status.worker_started = true;
                    s_status.psram_stack = false;
                    copy_text(s_status.last_failure,
                              sizeof(s_status.last_failure),
                              "worker stack fallback internal");
                    status_unlock();
                }
                ESP_LOGW(TAG, "audio service worker uses internal stack fallback");
            } else {
                if (status_lock()) {
                    s_status.worker_started = false;
                    s_status.psram_stack = false;
                    s_status.last_error = ESP_ERR_NO_MEM;
                    copy_text(s_status.last_failure, sizeof(s_status.last_failure), "worker create failed");
                    status_unlock();
                }
                ESP_LOGE(TAG, "worker create failed");
            }
        }
    } else if (s_worker_task != NULL) {
        if (status_lock()) {
            s_status.worker_started = true;
            status_unlock();
        }
    }
}

const char *atlas_common_audio_service_mode_name(atlas_common_audio_service_mode_t mode)
{
    switch (mode) {
    case ATLAS_COMMON_AUDIO_SERVICE_MODE_IDLE:
        return "idle";
    case ATLAS_COMMON_AUDIO_SERVICE_MODE_MONITORING:
        return "monitoring";
    case ATLAS_COMMON_AUDIO_SERVICE_MODE_RECORDING:
        return "recording";
    case ATLAS_COMMON_AUDIO_SERVICE_MODE_TRANSCRIBING:
        return "transcribing";
    case ATLAS_COMMON_AUDIO_SERVICE_MODE_THINKING:
        return "thinking";
    case ATLAS_COMMON_AUDIO_SERVICE_MODE_PLAYING:
        return "playing";
    case ATLAS_COMMON_AUDIO_SERVICE_MODE_COOLDOWN:
        return "cooldown";
    case ATLAS_COMMON_AUDIO_SERVICE_MODE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

void atlas_common_audio_service_set_continuous_enabled(bool enabled)
{
    if (status_lock()) {
        s_status.continuous_enabled = enabled;
        note_mode_locked(enabled ? ATLAS_COMMON_AUDIO_SERVICE_MODE_MONITORING : ATLAS_COMMON_AUDIO_SERVICE_MODE_IDLE,
                         enabled ? "continuous_on" : "continuous_off");
        status_unlock();
    }
}

void atlas_common_audio_service_note_stage(atlas_common_audio_service_mode_t mode, const char *action)
{
    note_mode(mode, action == NULL ? "stage" : action);
}

void atlas_common_audio_service_note_failure(const char *reason, esp_err_t err)
{
    if (status_lock()) {
        s_status.last_error = err;
        copy_text(s_status.last_failure, sizeof(s_status.last_failure), reason == NULL ? esp_err_to_name(err) : reason);
        note_mode_locked(ATLAS_COMMON_AUDIO_SERVICE_MODE_ERROR, reason == NULL ? "error" : reason);
        status_unlock();
    }
}

bool atlas_common_audio_service_is_muted(uint32_t *remaining)
{
    uint32_t left = 0;
    bool muted = false;
    if (status_lock()) {
        left = remaining_ms(service_now_ms(), s_status.muted_until_ms);
        s_status.muted = left > 0u;
        s_status.mute_remaining_ms = left;
        muted = s_status.muted;
        status_unlock();
    }
    if (remaining != NULL) {
        *remaining = left;
    }
    return muted;
}

void atlas_common_audio_service_mute_for(uint32_t duration_ms, const char *reason)
{
    const uint32_t now = service_now_ms();
    if (status_lock()) {
        s_status.muted_until_ms = now + duration_ms;
        s_status.muted = duration_ms > 0u;
        s_status.mute_remaining_ms = duration_ms;
        copy_text(s_status.mute_reason, sizeof(s_status.mute_reason), reason == NULL ? "mute" : reason);
        note_mode_locked(s_status.mode, "mute");
        status_unlock();
    }
}

void atlas_common_audio_service_note_turn(void)
{
    if (status_lock()) {
        s_status.turn_count++;
        s_status.last_event_ms = service_now_ms();
        status_unlock();
    }
}

esp_err_t atlas_common_audio_service_run_turn(atlas_common_audio_service_job_fn_t fn, void *ctx, uint32_t timeout_ms)
{
    if (fn == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (timeout_ms != 0u) {
        atlas_common_audio_service_note_failure("finite timeout not supported", ESP_ERR_INVALID_ARG);
        return ESP_ERR_INVALID_ARG;
    }
    if (s_job_queue == NULL || s_worker_task == NULL) {
        atlas_common_audio_service_note_failure("worker not started", ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    bool busy = false;
    if (status_lock()) {
        busy = s_status.busy || s_job_running;
        status_unlock();
    }
    if (busy) {
        atlas_common_audio_service_note_failure("audio service busy", ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    atlas_common_audio_service_job_t job = {
        .fn = fn,
        .ctx = ctx,
        .done = xSemaphoreCreateBinary(),
        .result = ESP_ERR_INVALID_STATE,
    };
    if (job.done == NULL) {
        atlas_common_audio_service_note_failure("job semaphore failed", ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }

    atlas_common_audio_service_job_t *job_ptr = &job;
    if (xQueueSend(s_job_queue, &job_ptr, 0) != pdTRUE) {
        vSemaphoreDelete(job.done);
        atlas_common_audio_service_note_failure("turn queue full", ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(job.done, portMAX_DELAY) != pdTRUE) {
        vSemaphoreDelete(job.done);
        atlas_common_audio_service_note_failure("turn timeout", ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }

    vSemaphoreDelete(job.done);
    return job.result;
}

esp_err_t atlas_common_audio_service_submit_turn(atlas_common_audio_service_job_fn_t fn, void *ctx)
{
    if (fn == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_job_queue == NULL || s_worker_task == NULL) {
        atlas_common_audio_service_note_failure("worker not started", ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    bool busy = false;
    if (status_lock()) {
        busy = s_status.busy || s_job_running;
        status_unlock();
    }
    if (busy) {
        atlas_common_audio_service_note_failure("audio service busy", ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    atlas_common_audio_service_job_t *job = (atlas_common_audio_service_job_t *)calloc(1, sizeof(*job));
    if (job == NULL) {
        atlas_common_audio_service_note_failure("async job alloc failed", ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    job->fn = fn;
    job->ctx = ctx;
    job->done = NULL;
    job->result = ESP_ERR_INVALID_STATE;

    if (xQueueSend(s_job_queue, &job, 0) != pdTRUE) {
        free(job);
        atlas_common_audio_service_note_failure("async queue full", ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t atlas_common_audio_service_measure_mic(uint16_t duration_ms, atlas_common_audio_mic_level_t *level)
{
    if (s_config.measure_mic_fn == NULL) {
        atlas_common_audio_service_note_failure("measure backend missing", ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_lock()) {
        s_status.monitor_count++;
        status_unlock();
    }
    note_mode(ATLAS_COMMON_AUDIO_SERVICE_MODE_MONITORING, "measure_mic");
    const esp_err_t err = s_config.measure_mic_fn(duration_ms, level, s_config.backend_ctx);
    if (status_lock()) {
        s_status.last_error = err;
        status_unlock();
    }
    if (err != ESP_OK) {
        note_mode(ATLAS_COMMON_AUDIO_SERVICE_MODE_ERROR, "measure_error");
    } else {
        bool continuous_enabled = false;
        if (status_lock()) {
            continuous_enabled = s_status.continuous_enabled;
            status_unlock();
        }
        if (!continuous_enabled) {
            note_mode(ATLAS_COMMON_AUDIO_SERVICE_MODE_IDLE, "measure_done");
        }
    }
    return err;
}

esp_err_t atlas_common_audio_service_capture_wav(uint16_t duration_ms,
                                                 uint8_t **wav_data,
                                                 size_t *wav_size,
                                                 atlas_common_audio_mic_level_t *level)
{
    if (s_config.capture_wav_fn == NULL) {
        atlas_common_audio_service_note_failure("capture backend missing", ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    set_operation_busy(true);
    if (status_lock()) {
        s_status.capture_count++;
        status_unlock();
    }
    note_mode(ATLAS_COMMON_AUDIO_SERVICE_MODE_RECORDING, "capture_wav");
    const esp_err_t err = s_config.capture_wav_fn(duration_ms, wav_data, wav_size, level, s_config.backend_ctx);
    bool job_running = false;
    if (status_lock()) {
        s_status.last_error = err;
        job_running = s_job_running;
        status_unlock();
    }
    set_operation_busy(false);
    if (err == ESP_OK) {
        note_mode(job_running ? ATLAS_COMMON_AUDIO_SERVICE_MODE_THINKING : ATLAS_COMMON_AUDIO_SERVICE_MODE_IDLE, "capture_done");
    } else {
        atlas_common_audio_service_note_failure("capture_error", err);
    }
    return err;
}

esp_err_t atlas_common_audio_service_play_wav_pcm(const uint8_t *wav_data, size_t wav_size, uint8_t volume)
{
    if (s_config.play_wav_pcm_fn == NULL) {
        atlas_common_audio_service_note_failure("play backend missing", ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    set_operation_busy(true);
    if (status_lock()) {
        s_status.playback_count++;
        status_unlock();
    }
    note_mode(ATLAS_COMMON_AUDIO_SERVICE_MODE_PLAYING, "play_wav");
    const esp_err_t err = s_config.play_wav_pcm_fn(wav_data, wav_size, volume, s_config.backend_ctx);
    bool job_running = false;
    if (status_lock()) {
        s_status.last_error = err;
        job_running = s_job_running;
        status_unlock();
    }
    set_operation_busy(false);
    if (err == ESP_OK) {
        note_mode(job_running ? ATLAS_COMMON_AUDIO_SERVICE_MODE_COOLDOWN : ATLAS_COMMON_AUDIO_SERVICE_MODE_IDLE, "play_done");
    } else {
        atlas_common_audio_service_note_failure("play_error", err);
    }
    return err;
}

void atlas_common_audio_service_get_status(atlas_common_audio_service_status_t *status)
{
    if (status == NULL) {
        return;
    }
    (void)atlas_common_audio_service_is_muted(NULL);
    if (status_lock()) {
        *status = s_status;
        status_unlock();
    } else {
        memset(status, 0, sizeof(*status));
        status->last_error = ESP_ERR_NO_MEM;
        copy_text(status->last_failure, sizeof(status->last_failure), "status lock failed");
    }
}

size_t atlas_common_audio_service_write_json(char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    atlas_common_audio_service_status_t status;
    atlas_common_audio_service_get_status(&status);
    char mute_reason[sizeof(status.mute_reason) * 2];
    char last_action[sizeof(status.last_action) * 2];
    char last_failure[sizeof(status.last_failure) * 2];
    json_escape(mute_reason, sizeof(mute_reason), status.mute_reason);
    json_escape(last_action, sizeof(last_action), status.last_action);
    json_escape(last_failure, sizeof(last_failure), status.last_failure);
    return snprintf(dst,
                    dst_size,
                    "{\"initialized\":%s,\"worker_started\":%s,\"psram_stack\":%s,\"mode\":\"%s\",\"busy\":%s,\"job_running\":%s,\"continuous_enabled\":%s,"
                    "\"muted\":%s,\"mute_remaining_ms\":%" PRIu32 ",\"mute_reason\":\"%s\","
                    "\"capture_count\":%" PRIu32 ",\"playback_count\":%" PRIu32 ","
                    "\"monitor_count\":%" PRIu32 ",\"turn_count\":%" PRIu32 ","
                    "\"job_count\":%" PRIu32 ",\"job_error_count\":%" PRIu32 ","
                    "\"consecutive_failures\":%" PRIu32 ",\"last_success_ms\":%" PRIu32 ","
                    "\"last_event_ms\":%" PRIu32 ",\"last_job_ms\":%" PRIu32 ","
                    "\"last_action\":\"%s\",\"last_error\":\"%s\",\"last_failure\":\"%s\"}",
                    status.initialized ? "true" : "false",
                    status.worker_started ? "true" : "false",
                    status.psram_stack ? "true" : "false",
                    atlas_common_audio_service_mode_name(status.mode),
                    status.busy ? "true" : "false",
                    status.job_running ? "true" : "false",
                    status.continuous_enabled ? "true" : "false",
                    status.muted ? "true" : "false",
                    status.mute_remaining_ms,
                    mute_reason,
                    status.capture_count,
                    status.playback_count,
                    status.monitor_count,
                    status.turn_count,
                    status.job_count,
                    status.job_error_count,
                    status.consecutive_failures,
                    status.last_success_ms,
                    status.last_event_ms,
                    status.last_job_ms,
                    last_action,
                    esp_err_to_name(status.last_error),
                    last_failure);
}
