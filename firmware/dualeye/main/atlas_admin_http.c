#include "atlas_admin_http.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "atlas_audio.h"
#include "atlas_audio_service.h"
#include "atlas_brain_ws_client.h"
#include "atlas_expression.h"
#include "atlas_brain_intent.h"
#include "atlas_llm_client.h"
#include "atlas_brain_adapter.h"
#include "atlas_opus_stream.h"
#include "atlas_pairing.h"
#include "atlas_runtime.h"
#include "atlas_scene.h"
#include "atlas_sr_probe.h"
#include "atlas_wifi.h"
#include "common/atlas_common_assets.h"
#include "common/atlas_common_config.h"
#include "common/atlas_common_device_status.h"

static const char *TAG = "atlas_admin";

#define ATLAS_FIRMWARE_VERSION "0.14.8-pet-head-yaw"
#define ATLAS_FIRMWARE_CHANNEL "dev"
#define ATLAS_RESOURCE_VERSION ATLAS_COMMON_RESOURCE_VERSION
#define ATLAS_FONT_VERSION ATLAS_COMMON_FONT_VERSION
#define ATLAS_TOOL_SCHEMA_VERSION "atlas.tools.v0.desk_apps"
#define ATLAS_BRAIN_SESSION_PROTOCOL "atlas.brain.session.v1"
#define ATLAS_BRAIN_SESSION_STAGE "P1_dualeye_persistent_ws"
#define ATLAS_HTTP_JSON_CHUNK_THRESHOLD 1400
#define ATLAS_HTTP_JSON_CHUNK_SIZE 512
#define ATLAS_OPUS_PROBE_VERSION "opus-60ms-probe-v0"
#define ATLAS_SR_PROBE_STAGE "P3_resource_probe"
#define ATLAS_BUILD_FINGERPRINT_JSON \
    "\"fingerprint\":{\"firmware_version\":\"" ATLAS_FIRMWARE_VERSION "\"," \
    "\"build_tag\":\"%s\",\"channel\":\"" ATLAS_FIRMWARE_CHANNEL "\"," \
    "\"build_date\":\"" __DATE__ "\",\"build_time\":\"" __TIME__ "\"," \
    "\"resource_version\":\"" ATLAS_RESOURCE_VERSION "\"," \
    "\"font_version\":\"" ATLAS_FONT_VERSION "\"," \
    "\"tool_schema_version\":\"" ATLAS_TOOL_SCHEMA_VERSION "\"," \
    "\"brain_session_protocol\":\"" ATLAS_BRAIN_SESSION_PROTOCOL "\"," \
    "\"opus_probe_version\":\"" ATLAS_OPUS_PROBE_VERSION "\"," \
    "\"sr_probe_stage\":\"" ATLAS_SR_PROBE_STAGE "\"}"

typedef struct {
    atlas_config_t *config;
    atlas_ui_state_t *ui_state;
    atlas_admin_now_ms_fn_t now_ms;
    httpd_handle_t server;
} atlas_admin_ctx_t;

static atlas_admin_ctx_t s_ctx;
static TaskHandle_t s_voice_wake_task;
static volatile bool s_voice_wake_enabled;
static volatile bool s_voice_wake_busy;
static volatile bool s_voice_wake_psram_stack;
static uint8_t s_voice_wake_threshold = 36;
static uint8_t s_voice_wake_hits_required = 1;
static uint16_t s_voice_wake_duration_ms = 3500;
static uint32_t s_voice_wake_triggers;
static uint32_t s_voice_wake_last_ms;
static uint32_t s_voice_wake_last_level;
static uint32_t s_voice_wake_last_rms;
static uint32_t s_voice_wake_last_peak;
static uint32_t s_voice_wake_noise_rms;
static uint32_t s_voice_wake_noise_peak;
static uint8_t s_voice_wake_hit_count;
static uint32_t s_voice_wake_mute_until_ms;
static char s_voice_wake_last_reason[24] = "boot";

#define ATLAS_BRAIN_EVENT_HISTORY 10u
#define ATLAS_VOICE_WAKE_START_DELAY_MS 1200u
#define ATLAS_VOICE_WAKE_MEASURE_MS 160u
#define ATLAS_VOICE_WAKE_IDLE_MS 120u
#define ATLAS_VOICE_WAKE_REARM_MS 900u
#define ATLAS_VOICE_WAKE_TASK_STACK_BYTES 12288u
#define ATLAS_VOICE_WAKE_TASK_PRIORITY 2u
#define ATLAS_VOICE_WAKE_MIN_RMS 45u
#define ATLAS_VOICE_WAKE_MIN_PEAK 650u
#define ATLAS_VOICE_WAKE_NOISE_MARGIN_RMS 32u
#define ATLAS_VOICE_WAKE_NOISE_MARGIN_PEAK 260u
#define ATLAS_VOICE_WAKE_SPIKE_RMS 120u
#define ATLAS_VOICE_WAKE_SPIKE_PEAK 1000u
#define ATLAS_VOICE_WAKE_PLAY_MUTE_MS 2200u
#define ATLAS_MANUAL_UI_MUTE_MS 5000u

typedef struct {
    uint16_t duration_ms;
    size_t wav_len;
    uint8_t mic_level;
    uint32_t mic_rms;
    uint32_t mic_peak;
    int bridge_status;
    bool bridge_ok;
    bool tts_ready;
    char asr_text[160];
    char reply[180];
    char bridge_error[120];
    esp_err_t play_err;
    size_t tts_wav_len;
} atlas_voice_turn_result_t;

typedef struct {
    uint16_t duration_ms;
    atlas_voice_turn_result_t *result;
} atlas_voice_turn_job_ctx_t;

typedef struct {
    uint16_t duration_ms;
    atlas_voice_turn_result_t result;
} atlas_voice_turn_async_ctx_t;

typedef struct {
    uint16_t duration_ms;
    atlas_opus_probe_result_t *result;
} atlas_opus_probe_job_ctx_t;

typedef struct {
    uint32_t seq;
    uint32_t ms;
    char type[32];
    char detail[96];
} atlas_brain_event_t;

static atlas_brain_event_t s_brain_events[ATLAS_BRAIN_EVENT_HISTORY];
static uint32_t s_brain_event_seq;
#if CONFIG_HTTPD_WS_SUPPORT
static int s_brain_ws_fd = -1;
static uint32_t s_brain_ws_session_seq;
#endif

static void brain_ws_emit_event(const char *type, const char *detail);

static bool is_motion_event(atlas_voice_event_t event)
{
    return event == ATLAS_VOICE_EVENT_MOVE_FORWARD ||
           event == ATLAS_VOICE_EVENT_MOVE_BACKWARD ||
           event == ATLAS_VOICE_EVENT_TURN_LEFT ||
           event == ATLAS_VOICE_EVENT_TURN_RIGHT;
}

static bool is_supported_page(atlas_page_t page)
{
    switch (page) {
    case ATLAS_PAGE_EYES:
    case ATLAS_PAGE_CLOCK:
    case ATLAS_PAGE_STATUS:
    case ATLAS_PAGE_VOICE:
    case ATLAS_PAGE_MUSIC:
    case ATLAS_PAGE_STORY:
    case ATLAS_PAGE_CHAT:
    case ATLAS_PAGE_CALENDAR:
    case ATLAS_PAGE_POMODORO:
    case ATLAS_PAGE_PHOTO:
        return true;
    default:
        return false;
    }
}

static bool is_supported_action(const char *action)
{
    return action != NULL &&
           (strcmp(action, "music") == 0 ||
            strcmp(action, "story") == 0 ||
            strcmp(action, "chat") == 0 ||
            strcmp(action, "clock") == 0 ||
            strcmp(action, "clock.show") == 0 ||
            strcmp(action, "clock.status") == 0 ||
            strcmp(action, "clock.sync") == 0 ||
            strcmp(action, "calendar") == 0 ||
            strcmp(action, "calendar.show") == 0 ||
            strcmp(action, "calendar.today") == 0 ||
            strcmp(action, "calendar.set_note") == 0 ||
            strcmp(action, "pomodoro") == 0 ||
            strcmp(action, "pomodoro.show") == 0 ||
            strcmp(action, "pomodoro.status") == 0 ||
            strcmp(action, "pomodoro.start") == 0 ||
            strcmp(action, "pomodoro.stop") == 0 ||
            strcmp(action, "pomodoro.reset") == 0 ||
            strcmp(action, "alarm") == 0);
}

static bool parse_bool_flag(const char *value, bool *out)
{
    if (value == NULL || out == NULL) {
        return false;
    }
    if (strcasecmp(value, "1") == 0 || strcasecmp(value, "true") == 0) {
        *out = true;
        return true;
    }
    if (strcasecmp(value, "0") == 0 || strcasecmp(value, "false") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool set_clock_from_epoch_ms(const char *epoch_ms_s)
{
    if (epoch_ms_s == NULL || epoch_ms_s[0] == '\0') {
        return false;
    }

    char *endptr = NULL;
    long long epoch_value = strtoll(epoch_ms_s, &endptr, 10);
    if (endptr == epoch_ms_s || epoch_value <= 0) {
        return false;
    }

    if (epoch_value < 10000000000LL) {
        epoch_value *= 1000LL;
    }
    if (epoch_value < 1700000000000LL) {
        return false;
    }

    setenv("TZ", "CST-8", 1);
    tzset();
    struct timeval tv = {
        .tv_sec = (time_t)(epoch_value / 1000LL),
        .tv_usec = (suseconds_t)((epoch_value % 1000LL) * 1000LL),
    };
    return settimeofday(&tv, NULL) == 0;
}

static const char *weekday_zh(int weekday)
{
    static const char *const names[] = {
        "星期日",
        "星期一",
        "星期二",
        "星期三",
        "星期四",
        "星期五",
        "星期六",
    };
    if (weekday < 0 || weekday > 6) {
        return "星期?";
    }
    return names[weekday];
}

static bool format_clock_snapshot(char *time_text,
                                  size_t time_size,
                                  char *date_text,
                                  size_t date_size,
                                  char *weekday_text,
                                  size_t weekday_size)
{
    if (time_text == NULL || date_text == NULL || weekday_text == NULL) {
        return false;
    }

    time_t unix_now = time(NULL);
    struct tm tm_now;
    if (unix_now > 1700000000 && localtime_r(&unix_now, &tm_now) != NULL) {
        snprintf(time_text, time_size, "%02d:%02d:%02d", tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
        snprintf(date_text,
                 date_size,
                 "%04d-%02d-%02d",
                 tm_now.tm_year + 1900,
                 tm_now.tm_mon + 1,
                 tm_now.tm_mday);
        strlcpy(weekday_text, weekday_zh(tm_now.tm_wday), weekday_size);
        return true;
    }

    strlcpy(time_text, "not_synced", time_size);
    strlcpy(date_text, "", date_size);
    strlcpy(weekday_text, "", weekday_size);
    return false;
}

static const char *atlas_firmware_build_tag(void)
{
    return "atlas-dualeye-" __DATE__ " " __TIME__;
}

static uint32_t now_ms(void)
{
    return s_ctx.now_ms == NULL ? 0 : s_ctx.now_ms();
}

static uint32_t remaining_ms(uint32_t now, uint32_t until)
{
    return until > now ? until - now : 0u;
}

static void voice_wake_mute_for(uint32_t duration_ms)
{
    const uint32_t ts = now_ms();
    s_voice_wake_mute_until_ms = ts + duration_ms;
    s_voice_wake_hit_count = 0;
    atlas_audio_service_mute_for(duration_ms, "voice_wake");
}

static void manual_ui_override(uint32_t ts, const char *reason)
{
    atlas_audio_service_status_t service_status;
    atlas_audio_service_get_status(&service_status);
    atlas_runtime_set_state(ATLAS_RUNTIME_STATE_IDLE, reason == NULL ? "manual ui" : reason, ts);
    atlas_audio_service_note_stage(service_status.continuous_enabled ?
                                       ATLAS_AUDIO_SERVICE_MODE_MONITORING :
                                       ATLAS_AUDIO_SERVICE_MODE_IDLE,
                                   reason == NULL ? "manual_ui" : reason);
    voice_wake_mute_for(ATLAS_MANUAL_UI_MUTE_MS);
    brain_ws_emit_event("ui.manual_override", reason == NULL ? "manual_ui" : reason);
}

static void ui_set_page_state(atlas_page_t page,
                              atlas_expression_t expression,
                              uint8_t audio_level,
                              uint32_t ts,
                              atlas_pet_event_t event,
                              bool apply_pet_event)
{
    atlas_ui_lock();
    s_ctx.ui_state->page = page;
    s_ctx.ui_state->expression = expression;
    s_ctx.ui_state->audio_level = audio_level;
    s_ctx.ui_state->last_event_ms = ts;
    if (apply_pet_event) {
        atlas_pet_handle_event(&s_ctx.ui_state->pet, event, ts);
    }
    atlas_ui_unlock();
}

static void ui_apply_page(atlas_page_t page, uint32_t ts)
{
    atlas_ui_lock();
    s_ctx.ui_state->page = page;
    if (page == ATLAS_PAGE_VOICE) {
        s_ctx.ui_state->expression = ATLAS_EXPR_LISTEN;
        s_ctx.ui_state->audio_level = 24;
        atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_VOICE_LISTEN, ts);
    } else if (page == ATLAS_PAGE_MUSIC || page == ATLAS_PAGE_STORY || page == ATLAS_PAGE_CHAT) {
        s_ctx.ui_state->expression = page == ATLAS_PAGE_CHAT ? ATLAS_EXPR_LISTEN : ATLAS_EXPR_SPEAKING;
        s_ctx.ui_state->audio_level = page == ATLAS_PAGE_CHAT ? 28 : 58;
        atlas_pet_handle_event(&s_ctx.ui_state->pet,
                               page == ATLAS_PAGE_MUSIC ? ATLAS_PET_EVENT_MUSIC :
                               page == ATLAS_PAGE_STORY ? ATLAS_PET_EVENT_STORY :
                                                          ATLAS_PET_EVENT_CHAT,
                               ts);
    } else if (page == ATLAS_PAGE_CLOCK ||
               page == ATLAS_PAGE_CALENDAR || page == ATLAS_PAGE_POMODORO ||
               page == ATLAS_PAGE_ALARM || page == ATLAS_PAGE_PHOTO) {
        s_ctx.ui_state->expression = ATLAS_EXPR_CURIOUS;
        s_ctx.ui_state->audio_level = 0;
        atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_THINK, ts);
    } else if (!s_ctx.ui_state->moving) {
        s_ctx.ui_state->expression = ATLAS_EXPR_IDLE;
        s_ctx.ui_state->audio_level = 0;
    }
    s_ctx.ui_state->last_event_ms = ts;
    atlas_ui_unlock();
}

static void ui_set_expression_state(atlas_expression_t expression, uint8_t audio_level, uint32_t ts)
{
    atlas_ui_lock();
    s_ctx.ui_state->expression = expression;
    s_ctx.ui_state->audio_level = audio_level;
    s_ctx.ui_state->last_event_ms = ts;
    atlas_ui_unlock();
}

static void ui_finish_audio_state(atlas_expression_t expression, uint32_t ts)
{
    atlas_ui_lock();
    s_ctx.ui_state->audio_level = 0;
    s_ctx.ui_state->last_event_ms = ts;
    if (!s_ctx.ui_state->moving) {
        s_ctx.ui_state->expression = expression;
    }
    atlas_ui_unlock();
}

static bool page_survives_brain_offline(atlas_page_t page)
{
    return page == ATLAS_PAGE_EYES ||
           page == ATLAS_PAGE_CLOCK ||
           page == ATLAS_PAGE_STATUS ||
           page == ATLAS_PAGE_CALENDAR ||
           page == ATLAS_PAGE_POMODORO;
}

static void ui_show_brain_offline(const char *reason, uint32_t ts)
{
    char text[ATLAS_CHAT_TEXT_MAX];
    snprintf(text,
             sizeof(text),
             "Brain 离线\n%s",
             reason == NULL || reason[0] == '\0' ? "请检查 Mac Atlas Brain 与 Wi-Fi" : reason);
    atlas_ui_set_chat_text(s_ctx.ui_state, text);

    atlas_ui_lock();
    if (!page_survives_brain_offline(s_ctx.ui_state->page)) {
        s_ctx.ui_state->page = ATLAS_PAGE_CHAT;
    }
    if (!s_ctx.ui_state->moving) {
        s_ctx.ui_state->expression = ATLAS_EXPR_CURIOUS;
        s_ctx.ui_state->audio_level = 0;
    }
    s_ctx.ui_state->last_event_ms = ts;
    atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_THINK, ts);
    atlas_ui_unlock();
}

static void voice_wake_note_sample(const atlas_audio_mic_level_t *level, bool hit)
{
    if (level == NULL) {
        return;
    }
    s_voice_wake_last_level = level->level;
    s_voice_wake_last_rms = level->rms;
    s_voice_wake_last_peak = level->peak;

    if (s_voice_wake_noise_rms == 0u) {
        s_voice_wake_noise_rms = level->rms;
    }
    if (s_voice_wake_noise_peak == 0u) {
        s_voice_wake_noise_peak = level->peak;
    }
    if (!hit) {
        s_voice_wake_noise_rms = ((s_voice_wake_noise_rms * 7u) + level->rms) / 8u;
        s_voice_wake_noise_peak = ((s_voice_wake_noise_peak * 7u) + level->peak) / 8u;
    }
}

static bool voice_wake_sample_hit(const atlas_audio_mic_level_t *level)
{
    if (level == NULL) {
        return false;
    }
    const bool level_hit = level->level >= s_voice_wake_threshold;
    const uint32_t noise_rms = s_voice_wake_noise_rms == 0u ? level->rms : s_voice_wake_noise_rms;
    const uint32_t noise_peak = s_voice_wake_noise_peak == 0u ? level->peak : s_voice_wake_noise_peak;
    const bool rms_hit = level->rms >= ATLAS_VOICE_WAKE_MIN_RMS &&
                         level->rms >= noise_rms + ATLAS_VOICE_WAKE_NOISE_MARGIN_RMS;
    const bool peak_hit = level->peak >= ATLAS_VOICE_WAKE_MIN_PEAK &&
                          level->peak >= noise_peak + ATLAS_VOICE_WAKE_NOISE_MARGIN_PEAK;
    const bool dynamic_hit = rms_hit && peak_hit;
    const bool spike_hit = level->rms >= ATLAS_VOICE_WAKE_SPIKE_RMS &&
                           level->peak >= ATLAS_VOICE_WAKE_SPIKE_PEAK;
    if (level_hit) {
        strlcpy(s_voice_wake_last_reason, "level", sizeof(s_voice_wake_last_reason));
    } else if (dynamic_hit) {
        strlcpy(s_voice_wake_last_reason, "dynamic", sizeof(s_voice_wake_last_reason));
    } else if (spike_hit) {
        strlcpy(s_voice_wake_last_reason, "spike", sizeof(s_voice_wake_last_reason));
    } else {
        strlcpy(s_voice_wake_last_reason, "idle", sizeof(s_voice_wake_last_reason));
    }
    return level_hit || dynamic_hit || spike_hit;
}

static uint16_t clamp_voice_duration(uint16_t duration)
{
    if (duration < 800) {
        return 800;
    }
    if (duration > 6000) {
        return 6000;
    }
    return duration;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static const char *json_bool(bool value)
{
    return value ? "true" : "false";
}

static void url_decode(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    size_t out = 0;
    for (size_t i = 0; i < src_len && out + 1 < dst_size; ++i) {
        if (src[i] == '+') {
            dst[out++] = ' ';
        } else if (src[i] == '%' && i + 2 < src_len) {
            const int hi = hex_value(src[i + 1]);
            const int lo = hex_value(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[out++] = (char)((hi << 4) | lo);
                i += 2;
            }
        } else {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
}

static void trim_assign(char *s)
{
    atlas_common_config_trim(s);
}

static bool form_get_value(const char *body, const char *key, char *out, size_t out_size)
{
    if (body == NULL || key == NULL || out == NULL || out_size == 0) {
        return false;
    }

    const size_t key_len = strlen(key);
    const char *cursor = body;
    while (*cursor != '\0') {
        const char *pair_end = strchr(cursor, '&');
        if (pair_end == NULL) {
            pair_end = cursor + strlen(cursor);
        }
        const char *equals = memchr(cursor, '=', (size_t)(pair_end - cursor));
        if (equals != NULL && (size_t)(equals - cursor) == key_len && strncmp(cursor, key, key_len) == 0) {
            url_decode(out, out_size, equals + 1, (size_t)(pair_end - equals - 1));
            return true;
        }
        cursor = *pair_end == '&' ? pair_end + 1 : pair_end;
    }

    out[0] = '\0';
    return false;
}

static bool query_get_value(httpd_req_t *req, const char *key, char *out, size_t out_size)
{
    if (req == NULL || key == NULL || out == NULL || out_size == 0) {
        return false;
    }
    char query[160];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        out[0] = '\0';
        return false;
    }
    if (httpd_query_key_value(query, key, out, out_size) != ESP_OK) {
        out[0] = '\0';
        return false;
    }
    trim_assign(out);
    return true;
}

static esp_err_t read_body(httpd_req_t *req, char *body, size_t body_size)
{
    if (body == NULL || body_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t received = 0;
    const size_t target = req->content_len < body_size - 1 ? req->content_len : body_size - 1;
    while (received < target) {
        const int ret = httpd_req_recv(req, body + received, target - received);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }
    body[received] = '\0';
    return ESP_OK;
}

static bool authorize_body(const char *body)
{
    char pin[16];
    if (!form_get_value(body, "pin", pin, sizeof(pin))) {
        return false;
    }
    trim_assign(pin);
    return atlas_pairing_authorize_pin(pin);
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    if (json == NULL) {
        json = "{}";
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const size_t len = strlen(json);
    if (len <= ATLAS_HTTP_JSON_CHUNK_THRESHOLD) {
        return httpd_resp_send(req, json, len);
    }

    for (size_t off = 0; off < len; off += ATLAS_HTTP_JSON_CHUNK_SIZE) {
        const size_t chunk_len = (len - off) > ATLAS_HTTP_JSON_CHUNK_SIZE ? ATLAS_HTTP_JSON_CHUNK_SIZE : (len - off);
        const esp_err_t err = httpd_resp_send_chunk(req, json + off, chunk_len);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "send_json chunk failed len=%u off=%u err=%s",
                     (unsigned)len,
                     (unsigned)off,
                     esp_err_to_name(err));
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{
    char safe[112];
    size_t out = 0;
    const char *src = message == NULL ? "error" : message;
    for (size_t i = 0; src[i] != '\0' && out + 1 < sizeof(safe); ++i) {
        const unsigned char c = (unsigned char)src[i];
        safe[out++] = (c < 0x20 || c == '"' || c == '\\') ? ' ' : (char)c;
    }
    safe[out] = '\0';

    char json[160];
    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"%s\"}", safe);
    httpd_resp_set_status(req, status);
    return send_json(req, json);
}

static void json_escape(char *dst, size_t dst_size, const char *src)
{
    size_t out = 0;
    if (dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    for (size_t i = 0; src[i] != '\0' && out + 1 < dst_size; ++i) {
        if (src[i] == '"' || src[i] == '\\') {
            if (out + 2 >= dst_size) {
                break;
            }
            dst[out++] = '\\';
            dst[out++] = src[i];
        } else if ((unsigned char)src[i] >= 0x20) {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
}

static bool json_fragment_complete(size_t written, size_t capacity)
{
    return capacity > 1 && written < capacity - 1;
}

static bool json_snprintf_complete(int written, size_t capacity)
{
    return written >= 0 && (size_t)written < capacity;
}

static uint32_t brain_ws_remember_event(const char *type, const char *detail)
{
    s_brain_event_seq++;
    atlas_brain_event_t *event = &s_brain_events[(s_brain_event_seq - 1u) % ATLAS_BRAIN_EVENT_HISTORY];
    event->seq = s_brain_event_seq;
    event->ms = now_ms();
    strlcpy(event->type, type == NULL || type[0] == '\0' ? "event" : type, sizeof(event->type));
    strlcpy(event->detail, detail == NULL ? "" : detail, sizeof(event->detail));
    return event->seq;
}

static size_t brain_ws_write_recent_events(char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    size_t off = 0;
    off += snprintf(dst + off, dst_size - off, "[");
    bool first = true;
    const uint32_t total = s_brain_event_seq < ATLAS_BRAIN_EVENT_HISTORY ? s_brain_event_seq : ATLAS_BRAIN_EVENT_HISTORY;
    for (uint32_t i = 0; i < total && off < dst_size; ++i) {
        const uint32_t seq = s_brain_event_seq - i;
        const atlas_brain_event_t *event = &s_brain_events[(seq - 1u) % ATLAS_BRAIN_EVENT_HISTORY];
        if (event->seq == 0u) {
            continue;
        }
        char type[72];
        char detail[180];
        json_escape(type, sizeof(type), event->type);
        json_escape(detail, sizeof(detail), event->detail);
        off += snprintf(dst + off,
                        dst_size - off,
                        "%s{\"seq\":%" PRIu32 ",\"ms\":%" PRIu32 ",\"type\":\"%s\",\"detail\":\"%s\"}",
                        first ? "" : ",",
                        event->seq,
                        event->ms,
                        type,
                        detail);
        first = false;
    }
    if (off < dst_size) {
        off += snprintf(dst + off, dst_size - off, "]");
    }
    return off;
}

static void brain_ws_build_state_json(char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    atlas_wifi_status_t wifi;
    atlas_wifi_get_status(&wifi);
    atlas_audio_status_t audio;
    atlas_audio_get_status(&audio);
    atlas_audio_service_status_t audio_service_status;
    atlas_audio_service_get_status(&audio_service_status);

    char runtime_json[2200];
    atlas_runtime_write_json(runtime_json, sizeof(runtime_json));
    char audio_service_json[1400];
    atlas_audio_service_write_json(audio_service_json, sizeof(audio_service_json));
    atlas_scene_snapshot_t scene;
    atlas_scene_resolve(s_ctx.ui_state,
                        s_ctx.config,
                        &wifi,
                        &audio,
                        &audio_service_status,
                        atlas_runtime_get_state(),
                        atlas_runtime_get_reason(),
                        now_ms(),
                        &scene);
    char scene_json[1100];
    atlas_scene_write_json(&scene, scene_json, sizeof(scene_json));
    char events_json[1200];
    brain_ws_write_recent_events(events_json, sizeof(events_json));

    snprintf(dst,
             dst_size,
             "{\"scene\":%s,\"runtime\":%s,\"audio_service\":%s,\"recent_events\":%s}",
             scene_json,
             runtime_json,
             audio_service_json,
             events_json);
}

static void brain_ws_emit_event(const char *type, const char *detail)
{
    const uint32_t seq = brain_ws_remember_event(type, detail);
#if CONFIG_HTTPD_WS_SUPPORT
    if (s_ctx.server == NULL || s_brain_ws_fd < 0) {
        return;
    }
    if (httpd_ws_get_fd_info(s_ctx.server, s_brain_ws_fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
        s_brain_ws_fd = -1;
        return;
    }
    char safe_type[72];
    char safe_detail[180];
    json_escape(safe_type, sizeof(safe_type), type);
    json_escape(safe_detail, sizeof(safe_detail), detail);
    char json[560];
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"protocol\":\"" ATLAS_BRAIN_SESSION_PROTOCOL "\",\"type\":\"event\","
             "\"stage\":\"" ATLAS_BRAIN_SESSION_STAGE "\",\"session_id\":\"dualeye-ws-%" PRIu32 "\","
             "\"event\":{\"seq\":%" PRIu32 ",\"ms\":%" PRIu32 ",\"type\":\"%s\",\"detail\":\"%s\"}}",
             s_brain_ws_session_seq,
             seq,
             now_ms(),
             safe_type,
             safe_detail);
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };
    if (httpd_ws_send_frame_async(s_ctx.server, s_brain_ws_fd, &frame) != ESP_OK) {
        s_brain_ws_fd = -1;
    }
#else
    (void)seq;
#endif
}

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    size_t max;
} atlas_http_buffer_t;

static esp_err_t http_buffer_append(atlas_http_buffer_t *buf, const void *data, size_t len)
{
    if (buf == NULL || data == NULL || len == 0) {
        return ESP_OK;
    }
    if (buf->len + len + 1 > buf->max) {
        return ESP_ERR_NO_MEM;
    }
    const size_t needed = buf->len + len + 1;
    if (needed > buf->cap) {
        size_t next_cap = buf->cap == 0 ? 1024 : buf->cap * 2;
        while (next_cap < needed) {
            next_cap *= 2;
        }
        if (next_cap > buf->max) {
            next_cap = buf->max;
        }
        void *next = heap_caps_malloc(next_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (next == NULL) {
            next = heap_caps_malloc(next_cap, MALLOC_CAP_8BIT);
        }
        if (next == NULL) {
            return ESP_ERR_NO_MEM;
        }
        if (buf->data != NULL && buf->len > 0) {
            memcpy(next, buf->data, buf->len);
        }
        free(buf->data);
        buf->data = (uint8_t *)next;
        buf->cap = next_cap;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return ESP_OK;
}

static esp_err_t http_capture_event(esp_http_client_event_t *evt)
{
    if (evt == NULL || evt->user_data == NULL) {
        return ESP_OK;
    }
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != NULL && evt->data_len > 0) {
        return http_buffer_append((atlas_http_buffer_t *)evt->user_data, evt->data, (size_t)evt->data_len);
    }
    return ESP_OK;
}

static void build_bridge_url(char *dst, size_t dst_size, const char *base_url, const char *path)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (base_url == NULL || base_url[0] == '\0' || path == NULL) {
        return;
    }
    const size_t base_len = strlen(base_url);
    const bool has_slash = base_len > 0 && base_url[base_len - 1] == '/';
    snprintf(dst, dst_size, "%s%s%s", base_url, has_slash || path[0] == '/' ? "" : "/", path[0] == '/' && has_slash ? path + 1 : path);
}

static void build_bridge_ws_url(char *dst, size_t dst_size, const char *base_url, const char *path)
{
    build_bridge_url(dst, dst_size, base_url, path);
    if (dst == NULL || dst_size == 0 || dst[0] == '\0') {
        return;
    }
    if (strncmp(dst, "http://", 7) == 0) {
        memmove(dst + 5, dst + 7, strlen(dst + 7) + 1);
        memcpy(dst, "ws://", 5);
    } else if (strncmp(dst, "https://", 8) == 0) {
        memmove(dst + 6, dst + 8, strlen(dst + 8) + 1);
        memcpy(dst, "wss://", 6);
    }
}

static esp_err_t http_get_binary(const char *url, uint8_t **data, size_t *data_len, int *status_code)
{
    if (url == NULL || url[0] == '\0' || data == NULL || data_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *data = NULL;
    *data_len = 0;
    if (status_code != NULL) {
        *status_code = 0;
    }

    atlas_http_buffer_t buffer = {.max = 512u * 1024u};
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 45000,
        .event_handler = http_capture_event,
        .user_data = &buffer,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t err = esp_http_client_perform(client);
    if (status_code != NULL) {
        *status_code = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        free(buffer.data);
        return err;
    }
    *data = buffer.data;
    *data_len = buffer.len;
    return ESP_OK;
}

static esp_err_t play_wav_from_url(const char *url, uint8_t volume, size_t *wav_len, int *status_code)
{
    if (url == NULL || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (wav_len != NULL) {
        *wav_len = 0;
    }
    if (status_code != NULL) {
        *status_code = 0;
    }

    uint8_t *wav = NULL;
    size_t len = 0;
    int http_status = 0;
    esp_err_t err = http_get_binary(url, &wav, &len, &http_status);
    if (status_code != NULL) {
        *status_code = http_status;
    }
    if (err == ESP_OK && (http_status < 200 || http_status >= 300)) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK && len > 0) {
        err = atlas_audio_service_play_wav_pcm(wav, len, volume);
    }
    if (wav_len != NULL) {
        *wav_len = len;
    }
    free(wav);
    return err;
}

static void cjson_string(cJSON *root, const char *key, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strlcpy(out, item->valuestring, out_size);
    }
}

static const char *wifi_auth_name(int authmode)
{
    switch (authmode) {
    case 0:
        return "OPEN";
    case 1:
        return "WEP";
    case 2:
        return "WPA";
    case 3:
        return "WPA2";
    case 4:
        return "WPA/WPA2";
    case 5:
        return "WPA2-ENT";
    case 6:
        return "WPA3";
    case 7:
        return "WPA2/WPA3";
    default:
        return "SECURE";
    }
}

static esp_err_t app_handler(httpd_req_t *req)
{
    char requested_page_name[24] = "";
    if (query_get_value(req, "page", requested_page_name, sizeof(requested_page_name))) {
        atlas_page_t page = ATLAS_PAGE_EYES;
        if (atlas_page_from_name(requested_page_name, &page) && is_supported_page(page)) {
            const uint32_t ts = now_ms();
            manual_ui_override(ts, "mobile nav page");
            ui_apply_page(page, ts);
            ESP_LOGI(TAG, "app nav page requested: %s", requested_page_name);
        } else {
            ESP_LOGW(TAG, "bad app nav page: %s", requested_page_name);
        }
    }

    static const char *html =
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<meta http-equiv=\"Cache-Control\" content=\"no-store, no-cache, must-revalidate, max-age=0\">"
        "<meta http-equiv=\"Pragma\" content=\"no-cache\">"
        "<meta http-equiv=\"Expires\" content=\"0\">"
        "<title>Atlas Robot 手机控制台 0.14.8</title>"
        "<style>"
        ":root{--bg:#f8f5ee;--panel:#fffdf8;--line:#d8ddd5;--text:#16211d;--muted:#66736d;--acc:#167b5f;--warn:#a66b00;--ok:#167b5f;--radius:8px}"
        "*{box-sizing:border-box}"
        "body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:linear-gradient(180deg,#fbf7ef 0%,#eef5ee 100%);color:var(--text);}"
        "main{max-width:980px;margin:0 auto;padding:12px 14px 20px;}"
        ".hero{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:14px;border:1px solid var(--line);background:rgba(255,253,248,.94);border-radius:var(--radius);box-shadow:0 14px 38px rgba(36,45,40,.12);}"
        ".hero a{color:#167b5f;text-decoration:none;font-weight:600;}h1{margin:0;font-size:22px;letter-spacing:0}"
        "section{margin-top:12px;padding:12px;border:1px solid var(--line);background:var(--panel);border-radius:8px;box-sizing:border-box;box-shadow:0 8px 26px rgba(36,45,40,.08);}"
        "h2{margin:0 0 10px;font-size:16px;}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(108px,1fr));gap:8px;}"
        ".pad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:340px;margin:0 auto;}.pad .wide{grid-column:1/4}"
        "input,select,button{font:inherit;box-sizing:border-box;padding:10px 12px;border:1px solid var(--line);border-radius:8px;background:white;color:var(--text);}"
        "button{cursor:pointer;min-height:40px;transition:.15s ease;border-color:#cfd7cd;background:#19231f;color:white}.primary{border-color:var(--acc);background:#167b5f;color:white}.danger{border-color:#b43d2a;background:#42201b;color:#ffe6dc;}"
        ".navbtn{display:flex;align-items:center;justify-content:center;text-decoration:none;cursor:pointer;min-height:40px;padding:10px 12px;border:1px solid #cfd7cd;border-radius:8px;background:#19231f;color:white;text-align:center;}"
        "button:hover:enabled{transform:translateY(-1px);filter:brightness(1.04)}button:disabled{opacity:.48;cursor:not-allowed;border-color:#d8ddd5;background:#eef1eb;color:#66736d;}"
        ".pill{border-radius:99px;background:#f1f5ef;border:1px solid var(--line);padding:2px 8px;color:#34413b;display:inline-flex;font-size:12px;}"
        ".row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;} .chip{display:inline-flex;padding:4px 8px;border-radius:99px;border:1px solid #3f5772;font-size:12px;margin-right:8px;margin-bottom:8px;}"
        ".muted{color:var(--muted);font-size:13px;line-height:1.45}.tagline{font-size:12px;color:#cdd7e4;display:inline-block;}"
        ".tagline{color:var(--muted)}.status{margin:8px 0;font-size:12px;line-height:1.45;white-space:pre-wrap;padding:10px;border-radius:6px;background:#f6f8f4;border:1px solid var(--line);color:#41504a;max-height:160px;overflow:auto;}"
        ".statusTitle{font-weight:600}.msg{margin-top:4px;padding:6px 8px;border-radius:6px;background:#0f1720;border:1px solid #24364e;color:#d5e2f2;font-size:12px;line-height:1.5;min-height:30px;}"
        ".msg{background:#f6f8f4;border-color:var(--line);color:#34413b}"
        ".petline{display:grid;grid-template-columns:72px 1fr;gap:8px;align-items:center;margin:7px 0;font-size:13px}"
        ".bar{height:9px;background:#eef1eb;border:1px solid #d8ddd5}.bar span{display:block;height:100%;background:#167b5f}"
        "</style></head>"
        "<body><main>"
        "<div class='hero'><div><h1>Atlas Robot 手机控制台</h1><span class='tagline'>0.14.8 · pet_head 透明土拨鼠头、拟 3D 转头、用户界面契约加固、Brain 离线安全降级、三种对话界面、Atlas Brain、双眼、语音、番茄、日历和 OPUS 真流；底盘本版暂停。</span></div><a href='/admin'>管理</a></div>"
        "<section><h2>连接 & 配对</h2><div class='grid'><input id='pin' inputmode='numeric' placeholder='6 位配对码'><button onclick='savePin()'>保存配对码</button><button class='danger' onclick='stopNow()'>STOP</button></div><p class='muted'>STOP 不需要配对码；其余动作与配置建议配对。</p></section>"
        "<section><h2>能力说明（真实接入 vs 页面占位）</h2><div id='capGrid' class='row'></div><p class='muted'>标记说明：✅：有真实状态机/控制链；🧪：页面占位或预留位，先验证流程后接入实际服务。</p></section>"
        "<section><h2>运行状态</h2><div id='status' class='status'>加载中...</div><div id='msg' class='msg'>等待操作反馈...</div><div id='mode' class='row'><span class='chip'>当前模式：未知</span><span class='chip'>当前页面：未知</span><span class='chip'>当前表情：未知</span></div></section>"
        "<section><h2>模式切换</h2><div class='row'><button class='primary' onclick='setMode(&apos;manual&apos;)'>手动模式</button><button class='primary' onclick='setMode(&apos;ai&apos;)'>AI 模式</button></div></section>"
        "<section><h2>双眼主题</h2><div class='row'><label class='row'><select id='theme'><option value='raptor'>猛禽眼</option><option value='mecha'>机械电子眼</option><option value='goggle'>护目镜眼</option><option value='pet'>电子宠物巡游</option><option value='blue_pupil'>蓝色瞳孔</option><option value='no_smoking'>禁烟禁电子烟</option><option value='tomoe_spin'>红色旋纹</option><option value='classic'>经典蓝眼</option><option value='amber'>琥珀巡航</option><option value='mint'>薄荷友好</option><option value='alert'>红色警戒</option><option value='night'>低亮夜航</option></select><button onclick='saveTheme()'>保存主题</button><span class='muted'>主题已内置到固件（无 SD 卡也可直接运行）</span></label><label class='row'>对话界面<select id='chat_mode'><option value='pet_head'>土拨鼠头 + 文字</option><option value='text'>双屏文字</option><option value='eyes_only'>纯眼睛表情</option></select></label></div></section>"
        "<section><h2>双眼表情</h2><div class='grid'><button onclick='expr(&apos;happy&apos;)'>开心</button><button onclick='expr(&apos;thinking&apos;)'>思考</button><button onclick='expr(&apos;listen&apos;)'>聆听</button><button onclick='expr(&apos;speaking&apos;)'>说话</button><button onclick='expr(&apos;sleepy&apos;)'>困倦</button><button onclick='expr(&apos;wink&apos;)'>眨眼</button></div></section>"
        "<section><h2>电子宠物</h2><div id='pet' class='status'>加载中...</div><div class='grid'><button onclick='wakePet()'>唤醒</button><button onclick='petEvent(&apos;touch&apos;)'>摸摸</button><button onclick='petEvent(&apos;play&apos;)'>玩一下</button><button onclick='petEvent(&apos;feed&apos;)'>补能</button><button onclick='petEvent(&apos;rest&apos;)'>休息</button><button onclick='petEvent(&apos;patrol&apos;)'>巡游状态</button><button onclick='petEvent(&apos;music&apos;)'>音乐状态</button><button onclick='petEvent(&apos;story&apos;)'>故事状态</button><button onclick='petEvent(&apos;chat&apos;)'>对话状态</button></div></section>"
        "<section><h2>显示页</h2><div class='grid'>"
        "<a id='page-eyes' class='navbtn' data-cap='eyes' href='/app?page=eyes&v=140'>双眼</a><a id='page-clock' class='navbtn' data-cap='clock_page' href='/app?page=clock&v=140'>时钟</a><button id='clock-sync' data-cap='clock_page' onclick=\"syncClock()\">校准时钟</button>"
        "<a id='page-status' class='navbtn' data-cap='status_page' href='/app?page=status&v=140'>状态</a><a id='page-voice' class='navbtn' data-cap='voice_ui' href='/app?page=voice&v=140'>语音</a>"
        "<a id='page-music' class='navbtn' data-cap='music_ui' href='/app?page=music&v=140'>音乐</a><a id='page-story' class='navbtn' data-cap='story_ui' href='/app?page=story&v=140'>故事</a>"
        "<a id='page-chat' class='navbtn' data-cap='chat_ui' href='/app?page=chat&v=140'>对话</a><a id='page-calendar' class='navbtn' data-cap='calendar_ui' href='/app?page=calendar&v=140'>日历</a>"
        "<a id='page-pomodoro' class='navbtn' data-cap='pomodoro_ui' href='/app?page=pomodoro&v=140'>番茄</a>"
        "</div><p class='muted'>照片、闹钟是下一阶段应用，不再放在主切换区假装可用。</p></section>"
        "<section><h2>板载音频</h2><div class='grid'><button class='primary' data-cap='audio_hw' onclick='speakerTest()'>喇叭测试</button><button class='primary' data-cap='audio_hw' onclick='micTest()'>麦克风测试</button><button class='primary' data-cap='voice_ui' onclick='voiceTurn()'>板载语音对话</button><button class='primary' data-cap='voice_ui' onclick='voiceWake(1)'>开启连续对话</button><button data-cap='voice_ui' onclick='voiceWake(0)'>关闭连续对话</button><button class='primary' data-cap='audio_hw' onclick='playBridgeTts()'>播放最近回复</button><button onclick='audioStatus()'>音频状态</button></div>"
        "<p class='muted'>使用 DualEye 板载 ES7210 麦克风和 ES8311/功放/Speaker header。连续对话开启后会在回答完自动回到聆听状态；当前是音量门限触发，不是关键词 WakeNet。</p></section>"
        "<section><h2>Atlas Brain / Mac 桥接</h2><p class='muted'>推荐网络：DualEye、Mac、手机都在同一个家用 Wi-Fi。首次配网才连 DualEye 热点；配网后用 /api/status 里的 STA IP 作为 DualEye 地址。手机访问 DualEye 时，桥接地址要填 Mac 的局域网 IP，不要填 127.0.0.1。</p>"
        "<div class='grid'><input id='bridge_url' placeholder='http://Mac局域网IP:8787'><button onclick='saveBridgeUrl()'>保存桥接地址</button><button onclick='testBridge()'>测试桥接</button></div>"
        "<label>自然语言输入</label><textarea id='bridge_text' rows='3' style='padding:10px;height:auto;min-height:90px;color:#eff2f7;background:#1c2938;border:1px solid #3f5772;border-radius:8px' placeholder='例如：你好，介绍一下你自己 / 切换到开心表情 / 开始 25 分钟番茄，任务是固件测试'></textarea><div class='grid' style='margin-top:8px'><button class='primary' onclick='sendBridgeText()'>发送给机器人</button><button onclick='openBridgeConsole()'>打开语音对话台</button></div>"
        "<p class='muted'>语音输入和语音合成先在 Mac 桥接页运行：打开桥接地址即可录音、转文字、朗读。救援模式：Mac 也连 DualEye 热点时，桥接地址填 Mac 在 192.168.4.x 网段的 IP；但 Mac 可能没有互联网。</p></section>"
        "<section><h2>对话文本（Atlas Brain）</h2><label>展示文本</label><div class='grid'><input id='chat_text_input' placeholder='写下对话界面要展示的文字'><button onclick='sendChatText()'>更新文本</button></div></section>"
        "<section><h2>日历</h2><label>标题</label><input id='calendar_title' placeholder='今天的主题'><label>便签</label><textarea id='calendar_note' rows='3' style='padding:10px;height:auto;min-height:84px;color:#eff2f7;background:#1c2938;border:1px solid #3f5772;border-radius:8px'></textarea><div class='grid'><button onclick='saveCalendar()'>保存日历内容</button><button onclick=\"act('calendar')\">打开日历页</button></div></section>"
        "<section><h2>番茄钟</h2><label>任务名称</label><input id='pomodoro_task' placeholder='番茄任务名'><label>专注分钟</label><input id='pomodoro_focus' type='number' min='1' max='120' value='25'>"
        "<label>休息分钟</label><input id='pomodoro_break' type='number' min='1' max='30' value='5'><div class='grid'>"
        "<button onclick='savePomodoro()'>保存番茄配置</button><button onclick='startPomodoro()'>开始番茄</button><button onclick='stopPomodoro()'>停止番茄</button></div></section>"
        "<section><h2>应用动作</h2><div class='grid'><button id='act-music' data-cap='music_ui' onclick=\"act('music')\">听音乐</button><button id='act-story' data-cap='story_ui' onclick=\"act('story')\">讲故事</button>"
        "<button id='act-chat' data-cap='chat_ui' onclick=\"sendChatText()\">陪我说话</button><button id='act-pomodoro' data-cap='pomodoro_ui' onclick=\"act('pomodoro')\">番茄专注</button><button id='act-alarm' data-cap='alarm_ui' onclick=\"act('alarm')\">设置闹钟</button></div></section>"
        "<section><h2>移动（本版暂停）</h2><div class='pad'><button></button><button data-cap='rover_motion' onclick=\"move('F')\">前进</button><button></button><button data-cap='rover_motion' onclick=\"move('L')\">左转</button><button class='danger' onclick=\"stopNow()\">停止</button><button data-cap='rover_motion' onclick=\"move('R')\">右转</button><button class='wide' data-cap='rover_motion' onclick=\"move('B')\">后退</button></div><p class='muted'>这一版实体先不做动态底盘，STOP 保留为后续联调安全入口。</p></section>"
        "</main><script>"
        "const enc=encodeURIComponent,$=id=>document.getElementById(id);let st=null;"
        "const capDefs=["
        "{k:'eyes',name:'双眼'},"
        "{k:'clock_page',name:'时钟'},"
        "{k:'status_page',name:'状态'},"
        "{k:'audio_hw',name:'板载音频'},"
        "{k:'voice_ui',name:'语音'},"
        "{k:'music_ui',name:'音乐'},"
        "{k:'story_ui',name:'故事'},"
        "{k:'chat_ui',name:'对话'},"
        "{k:'calendar_ui',name:'日历'},"
        "{k:'pomodoro_ui',name:'番茄'},"
        "{k:'photo_ui',name:'照片'},"
        "{k:'alarm_ui',name:'闹钟'},"
        "{k:'pet',name:'电子宠物'},"
        "{k:'desk_apps',name:'桌面应用'},"
        "{k:'rover_motion',name:'动态底盘'},"
        "];"
        "const capNotes={eyes:'已打通',clock_page:'已接入 桌面时钟',audio_hw:'板载麦克风/外放自检',voice_ui:'依赖板载音频+ASR/TTS',music_ui:'DualEye 后端可展示状态切换',story_ui:'DualEye 后端可展示状态切换',chat_ui:'已接入 Atlas Brain 对话文本',calendar_ui:'已接入 日历配置+展示',pomodoro_ui:'已接入 番茄配置+计时',photo_ui:'本地相册占位页，图片资源待接入',alarm_ui:'待接入闹钟',pet:'已打通',desk_apps:'时钟/日历/番茄已按工具化应用补强',rover_motion:'本版暂停，桌面伴侣形态优先'};"
        "function badgeByFeature(name,ok,cap){const state=ok?'✅':'🧪';return `${state} ${name}${cap ? '：'+cap : ''}`;}"
        "function setMsg(msg){const m=$('msg');if(m){m.textContent=msg;}else{console.log(msg);}}"
        "function parseJsonText(raw){const t=((raw||'').replace(/^\\uFEFF/,'')).trim();if(!t){throw new Error('状态响应为空');}const marker='{\"ok\"';const start=t.indexOf(marker)>=0?t.indexOf(marker):t.indexOf('{');if(start<0){throw new Error('响应不是 JSON：未找到开头');}const end=t.lastIndexOf('}');if(end<=start){throw new Error('响应不是 JSON：结尾缺失');}return JSON.parse(t.slice(start,end+1));}"
        "function updateCapButtons(){const feats=(st&&st.features)||{};document.querySelectorAll('[data-cap]').forEach(btn=>{const key=btn.getAttribute('data-cap');const ok=!!feats[key];if(btn.tagName==='A'){if(!ok){btn.dataset.href=btn.href;btn.removeAttribute('href')}else if(btn.dataset.href){btn.href=btn.dataset.href}}else{btn.disabled=!ok;}btn.title=ok?'':'当前按钮为页面入口，功能待接入真实服务';btn.classList.toggle('pill',!ok);if(ok){btn.classList.remove('pill')}else{btn.classList.add('pill')}})}"
        "function initPin(){const q=new URLSearchParams(location.search).get('pin');if(q){$('pin').value=q.trim();localStorage.setItem('atlas_pin',$('pin').value)}if(!$('pin').value){$('pin').value=(localStorage.getItem('atlas_pin')||'').trim()}}"
        "function initBridge(){const saved=localStorage.getItem('atlas_bridge_url')||'http://127.0.0.1:8787';const el=$('bridge_url');if(el){el.value=saved}}"
        "function bridgeUrl(){const el=$('bridge_url');const raw=((el&&el.value)||localStorage.getItem('atlas_bridge_url')||'http://127.0.0.1:8787').trim();return raw.replace(/\\/$/,'')}"
        "function saveBridgeUrl(){const url=bridgeUrl();localStorage.setItem('atlas_bridge_url',url);if($('bridge_url')){$('bridge_url').value=url}setMsg(`已保存 Atlas Brain 地址：${url}`)}"
        "function maybeFillBridgeFromStatus(){const u=st&&st.llm&&st.llm.mode==='host'&&st.llm.base_url?st.llm.base_url:'';if(!u)return;const saved=localStorage.getItem('atlas_bridge_url')||'';if(!saved||saved==='http://127.0.0.1:8787'){localStorage.setItem('atlas_bridge_url',u);if($('bridge_url'))$('bridge_url').value=u;}}"
        "async function testBridge(){saveBridgeUrl();try{const r=await fetch(`${bridgeUrl()}/health`);const t=await r.text();setMsg(`桥接测试：${r.status} ${t.slice(0,160)}`)}catch(e){setMsg(`桥接不可达：${e.message||e}。确认 Mac 已运行脚本，且手机/电脑能访问 Mac 局域网 IP。`)}}"
        "async function sendBridgeText(){saveBridgeUrl();const text=($('bridge_text')&&$('bridge_text').value)||'';if(!text.trim()){setMsg('先输入要发给 Atlas Brain 的文本');return;}try{const r=await fetch(`${bridgeUrl()}/text`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({text,speak:true})});const t=await r.text();setMsg(`桥接返回(${r.status}): ${t.slice(0,220)}`);await refresh();}catch(e){setMsg(`桥接发送失败：${e.message||e}`)}}"
        "function openBridgeConsole(){window.open(bridgeUrl(),'_blank')}"
        "function savePin(){const v=$('pin').value.trim();$('pin').value=v;localStorage.setItem('atlas_pin',v);alert('已保存配对码') }"
        "function fillCaps(){const box=$('capGrid');const feats=(st&&st.features)||{};let h='';for(const c of capDefs){const ok=!!feats[c.k];h+=`<span class=\\\"chip\\\">${badgeByFeature(c.name,ok,capNotes[c.k])}</span>`;}box.innerHTML=h;}"
        "function statusCards(){if(!st) return;const sc=st.scene||{};const sev=sc.severity==='error'?'异常':(sc.severity==='warn'?'注意':'正常');const cm={pet_head:'土拨鼠头',text:'双屏文字',eyes_only:'纯眼睛'}[st.ui.chat_mode]||st.ui.chat_mode||'土拨鼠头';return `<span class='chip'>场景：${sc.label||sc.state||'未知'}</span><span class='chip'>状态：${sev}</span><span class='chip'>模式：${st.safety.control_mode==='ai'?'AI':'手动'}</span><span class='chip'>页面：${st.ui.page}</span><span class='chip'>表情：${st.ui.expression}</span><span class='chip'>主题：${st.ui.theme}</span><span class='chip'>界面：${cm}</span>`;}"
        "function petHtml(p){if(!p)return '加载中...';const b=(n,v)=>`<div class=\"petline\"><span>${n}</span><div class=\"bar\"><span style=\"width:${v}%\"></span></div></div>`;return `${p.label} · ${p.phase} · 资源 ${p.asset_id}<br>`+b('心情',p.mood)+b('能量',p.energy)+b('好奇心',p.curiosity)}"
        "async function post(u,b=''){try{const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});const t=await r.text();if(!r.ok){setMsg(`请求失败(${r.status}): ${t||r.statusText}`);return null;}try{return parseJsonText(t)}catch(e){setMsg(`响应不是JSON：${e.message || 'parse failed'}；原始内容：${(t||'').slice(0,120)}`);return {raw:t,error:e.message||'parse failed'}}}catch(e){setMsg(`网络异常：${e.message||e}`);return null;}}"
        "function pinBody(){const next=st&&st.pairing_code?st.pairing_code:'';const cur=($('pin').value||'').trim();if(next){if(cur!==next){$('pin').value=next;localStorage.setItem('atlas_pin',next);setMsg(`已同步配对码：${next}`);}}if(!$('pin').value){setMsg('请先保存/输入6位配对码');return '';}return `pin=${enc($('pin').value)}`;}"
        "function optionalPinBody(){const v=($('pin')&&$('pin').value||'').trim();return v?`pin=${enc(v)}`:'';}"
        "async function doPost(u,b=''){const r=await post(u,b);if(!r){return false;}if(r.ok===false){setMsg(`操作失败：${r.error||r.raw||'unknown error'}`);return false;}setMsg('操作已提交');return true;}"
        "async function refresh(){try{const stReq=await fetch('/api/status/lite');const stText=await stReq.text();if(!stReq.ok){setMsg(`读取状态失败(${stReq.status}): ${stText}`);return;}try{st=parseJsonText(stText);}catch(e){setMsg(`状态响应解析失败：${e.message||e}；原始内容：${(stText||'').slice(0,160)}`);return;}if(st && st.ok){const nextPin=st.pairing_code||'';if(nextPin){if(($('pin').value||'').trim()!==nextPin){localStorage.setItem('atlas_pin',nextPin);setMsg(`已同步配对码：${nextPin}`)}$('pin').value=nextPin;}maybeFillBridgeFromStatus();$('status').textContent=JSON.stringify(st,null,2);$('mode').innerHTML=statusCards();$('pet').innerHTML=petHtml(st.pet);$('theme').value=st.ui.theme||'classic';if($('chat_mode'))$('chat_mode').value=st.ui.chat_mode||'pet_head';if(st.chat && st.chat.text!==undefined){$('chat_text_input').value=st.chat.text;}if(st.calendar){$('calendar_title').value=st.calendar.title||'';$('calendar_note').value=st.calendar.note||'';}if(st.pomodoro){$('pomodoro_task').value=st.pomodoro.task||'';$('pomodoro_focus').value=st.pomodoro.focus_minutes||25;$('pomodoro_break').value=st.pomodoro.break_minutes||5;}fillCaps();updateCapButtons();setMsg(`固件 ${st.firmware || 'unknown'}`);}}catch(e){setMsg(`状态读取异常：${e.message||e}`);}}"
        "async function setMode(m){const speed=st&&st.safety?st.safety.max_speed_percent:40;const dur=st&&st.safety?st.safety.max_duration_ms:700;if(await doPost('/api/config/safety',`${pinBody()}&motion_enabled=0&control_mode=${m}&max_speed=${speed}&max_duration=${dur}`)){await refresh();}}"
        "async function saveTheme(){const v=st&&st.ui&&st.ui.volume!==undefined?st.ui.volume:90;const cm=$('chat_mode')?$('chat_mode').value:'pet_head';if(await doPost('/api/config/ui',`${pinBody()}&theme=${enc($('theme').value)}&chat_mode=${enc(cm)}&brightness=70&volume=${enc(v)}`)){await refresh();}}"
        "async function stopNow(){if(await doPost('/api/rover/stop')){await refresh();}}"
        "async function move(d){if(await doPost('/api/rover/move',`${pinBody()}&dir=${d}&speed=30&duration=500`)){await refresh();}}"
        "async function expr(e){setMsg(`正在切换表情：${e}`);if(await doPost('/api/app/expression',`${optionalPinBody()}&expression=${e}`)){await refresh();}}"
        "async function page(p){setMsg(`正在切换页面：${p}`);if(await doPost('/api/app/page',`${optionalPinBody()}&page=${p}`)){await refresh();}}"
        "async function act(a){setMsg(`正在执行应用动作：${a}`);if(await doPost('/api/app/action',`${optionalPinBody()}&action=${a}`)){await refresh();}}"
        "async function syncClock(){if(await doPost('/api/app/action',`${optionalPinBody()}&action=clock.sync&epoch_ms=${Date.now()}`)){await refresh();}}"
        "async function audioStatus(){try{const r=await fetch('/api/audio/status');const t=await r.text();setMsg(`音频状态：${t.slice(0,220)}`)}catch(e){setMsg(`音频状态读取失败：${e.message||e}`)}}"
        "async function speakerTest(){if(await doPost('/api/audio/beep',`${pinBody()}&freq=880&duration=320&volume=90`)){setMsg('已播放高音量喇叭测试音');await refresh();}}"
        "async function micTest(){const r=await post('/api/audio/mic-level',`${pinBody()}&duration=500`);if(r&&r.ok){setMsg(`麦克风电平 ${r.level}/100，RMS=${r.rms}，Peak=${r.peak}`);await refresh();}}"
        "async function voiceTurn(){setMsg('开始板载录音，请对着 DualEye 说话...');const r=await post('/api/voice/turn',`${pinBody()}&duration=2800`);if(r&&r.ok){setMsg(`识别：${r.asr_text||'空'}\\n回复：${r.reply||'空'}\\n朗读：${r.played?'已播放':'未播放 '+(r.play_error||'')}`);await refresh();}}"
        "async function voiceWake(on){const r=await post('/api/voice/wake',`${pinBody()}&enabled=${on?1:0}&threshold=36&hits=1&duration=3500`);if(r&&r.ok){setMsg(`${on?'已开启':'已关闭'}连续对话：阈值 ${r.voice_wake.threshold}，连续 ${r.voice_wake.hits_required} 次触发，已触发 ${r.voice_wake.triggers} 次`);await refresh();}}"
        "async function playBridgeTts(){const r=await post('/api/audio/play-url',`${pinBody()}`);if(r&&r.ok){setMsg(`已通过 DualEye 播放最近回复，${r.wav_bytes||0} bytes`);await refresh();}}"
        "async function sendChatText(){const text=$('chat_text_input').value||'';await doPost('/api/app/action',`${optionalPinBody()}&action=chat&chat_text=${enc(text)}`);await refresh();}"
        "async function saveCalendar(){if(await doPost('/api/config/calendar',`${pinBody()}&title=${enc($('calendar_title').value||'')}&note=${enc($('calendar_note').value||'')}`)){await refresh();}}"
        "async function savePomodoro(){if(await doPost('/api/config/pomodoro',`${pinBody()}&task_name=${enc($('pomodoro_task').value||'')}&focus_minutes=${enc($('pomodoro_focus').value||'25')}&break_minutes=${enc($('pomodoro_break').value||'5')}&enabled=1`)){await refresh();}}"
        "async function startPomodoro(){if(await doPost('/api/app/action',`${optionalPinBody()}&action=pomodoro.start&task_name=${enc($('pomodoro_task').value||'')}&focus_minutes=${enc($('pomodoro_focus').value||'25')}&break_minutes=${enc($('pomodoro_break').value||'5')}`)){await refresh();}}"
        "async function stopPomodoro(){if(await doPost('/api/app/action',`${optionalPinBody()}&action=pomodoro.stop&running=0`)){await refresh();}}"
        "async function wakePet(){if(await doPost('/api/pet/wake')){await refresh();}}"
        "async function petEvent(e){if(await doPost('/api/pet/event',`${optionalPinBody()}&event=${e}`)){await refresh();}}"
        "initPin();initBridge();setMsg('点击操作后若失败会显示提示');refresh();setInterval(refresh,5000);</script></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    return httpd_resp_sendstr(req, html);
}

static esp_err_t admin_handler(httpd_req_t *req)
{
    static const char *html =
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<meta http-equiv=\"Cache-Control\" content=\"no-store, no-cache, must-revalidate, max-age=0\">"
        "<meta http-equiv=\"Pragma\" content=\"no-cache\">"
        "<meta http-equiv=\"Expires\" content=\"0\">"
        "<title>Atlas Rover 管理台</title>"
        "<style>:root{--bg:#080d12;--panel:#11151b;--line:#6f5b32;--text:#efe9df;--muted:#bdb6ad}"
        "*{box-sizing:border-box}"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;background:radial-gradient(circle at 10% 0%, #1b2733, var(--bg));color:var(--text)}"
        "main{max-width:1020px;margin:0 auto;padding:18px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:12px}"
        ".hero{padding:14px;border:1px solid var(--line);background:linear-gradient(90deg,#22180e,#17120d);margin-bottom:12px;border-radius:10px}"
        "section{border:1px solid var(--line);padding:14px;background:var(--panel);border-radius:10px;box-shadow:0 6px 20px rgba(2,6,10,.22)}"
        "button,input,select,textarea{font:inherit;padding:9px;border-radius:8px;border:1px solid var(--line);background:#151922;color:#efe9df}"
        "button{cursor:pointer;transition:.15s ease}button:hover{transform:translateY(-1px)}button.stop{background:#7a261f;border-color:#ff6b4b}button.primary{border-color:#3fc9ff}.row{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0}"
        "label{display:grid;gap:4px;margin:8px 0;color:#bdb6ad}code{display:block;white-space:pre-wrap;background:#07080a;padding:10px;border:1px solid #26313d;color:#5fe1b4}.chip{display:inline-flex;padding:4px 8px;border-radius:99px;border:1px solid #8a632f;margin-right:8px;margin-bottom:8px;font-size:12px;color:#d4cfbf;}"
        ".wifiList{display:grid;gap:7px;margin-top:8px}.wifiList button{text-align:left;display:flex;justify-content:space-between;gap:8px;align-items:center}.wifiMeta{color:#bdb6ad;font-size:12px;white-space:nowrap}"
        "textarea{min-height:110px}"
        "</style></head>"
        "<body><main><div class='hero'><h1>Atlas Rover Mk.1 管理台</h1><p><a href=\"/app\" style=\"color:#f5dc96\">进入应用页</a> · 本版为桌面伴侣构建，动态底盘默认暂停</p></div>"
        "<section><h2>功能能力（当前状态）</h2><div id='adminCaps'></div></section>"
        "<section><h2>状态</h2><code id=\"status\">加载中...</code><div class=\"row\"><button class=\"stop\" onclick=\"stopNow()\">STOP</button><button onclick=\"refresh()\">刷新</button><button class=\"primary\" onclick=\"selfTest()\">一键自检</button><button onclick=\"systemInfo()\">系统信息</button></div></section>"
        "<div class=\"grid\"><section><h2>移动调试（本版暂停）</h2><label>配对码<input id=\"pin\" inputmode=\"numeric\" placeholder=\"6 位配对码\"></label>"
        "<label>速度 %<input id=\"speed\" type=\"number\" min=\"1\" max=\"80\" value=\"30\"></label><label>时长 ms<input id=\"duration\" type=\"number\" min=\"100\" max=\"2000\" value=\"500\"></label>"
        "<div class=\"row\"><button onclick=\"move('F')\">前进</button><button onclick=\"move('B')\">后退</button><button onclick=\"move('L')\">左转</button><button onclick=\"move('R')\">右转</button></div></section>"
        "<section><h2>Wi-Fi 配网</h2><label>SSID<input id=\"ssid\" placeholder=\"可扫描后点选\"></label><label>密码<input id=\"wifi_pass\" type=\"password\"></label><div class=\"row\"><button onclick=\"scanWifi()\">扫描附近 Wi-Fi</button><button class=\"primary\" onclick=\"saveWifi()\">保存 Wi-Fi</button></div><div id=\"wifiScan\" class=\"wifiList\"></div><p style=\"color:#bdb6ad;font-size:13px;line-height:1.45\">扫描会短暂占用 Wi-Fi；点选网络后只需要输入密码，再保存并重启。</p></section>"
        "<section><h2>大模型/API</h2><label>模式<select id=\"llm_mode\"><option value=\"off\">关闭</option><option value=\"host\">Atlas Brain / Mac 桥接</option><option value=\"cloud\">云端大模型</option><option value=\"embedded\">端侧 Agent（后续）</option></select></label>"
        "<p style=\"color:#bdb6ad;font-size:13px;line-height:1.45\">推荐：Mac 运行 tools/atlas_brain_server.py。DualEye/Mac/手机同一 Wi-Fi，模式选 host，Base URL 填 http://Mac局域网IP:8787。手机连 DualEye 热点时不要填 127.0.0.1；救援模式下 Mac 也要连同一个 DualEye 热点。</p>"
        "<label>Provider<input id=\"provider\" value=\"atlas_brain_mac\"></label><label>Base URL<input id=\"base_url\" placeholder=\"http://Mac局域网IP:8787\"></label><label>Model<input id=\"model\"></label><label>API Key<input id=\"api_key\" type=\"password\" placeholder=\"留空则不更新\"></label>"
        "<button class=\"primary\" onclick=\"saveLlm()\">保存 API 设置</button></section>"
        "<section><h2>安全</h2><label><input id=\"motion_enabled\" type=\"checkbox\"> 允许运动</label><label>控制模式<select id=\"control_mode\"><option value=\"manual\">手动模式：Web 控制</option><option value=\"ai\">AI 模式：语音/Atlas Brain</option></select></label><label>最大速度 %<input id=\"max_speed\" type=\"number\" min=\"1\" max=\"80\" value=\"40\"></label>"
        "<label>最大时长 ms<input id=\"max_duration\" type=\"number\" min=\"100\" max=\"2000\" value=\"700\"></label><button class=\"primary\" onclick=\"saveSafety()\">保存安全设置</button></section>"
        "<section><h2>界面/主题</h2><label>主题<select id=\"ui_theme\"><option value=\"raptor\">猛禽眼</option><option value=\"mecha\">机械电子眼</option><option value=\"goggle\">护目镜眼</option><option value=\"pet\">电子宠物巡游</option><option value=\"blue_pupil\">蓝色瞳孔</option><option value=\"no_smoking\">禁烟禁电子烟</option><option value=\"tomoe_spin\">红色旋纹</option><option value=\"classic\">经典蓝眼</option><option value=\"amber\">琥珀巡航</option><option value=\"mint\">薄荷友好</option><option value=\"alert\">红色警戒</option><option value=\"night\">低亮夜航</option></select></label>"
        "<label>对话界面<select id=\"chat_mode\"><option value=\"pet_head\">土拨鼠头 + 文字</option><option value=\"text\">双屏文字</option><option value=\"eyes_only\">纯眼睛表情</option></select></label><label>屏幕亮度 %<input id=\"brightness\" type=\"number\" min=\"0\" max=\"100\" value=\"70\"></label><label>音量 %<input id=\"volume\" type=\"number\" min=\"0\" max=\"100\" value=\"90\"></label><button class=\"primary\" onclick=\"saveUi()\">保存界面设置</button></section>"
        "<section><h2>板载音频/语音</h2><p style=\"color:#bdb6ad;font-size:13px;line-height:1.45\">DualEye 板载 ES7210 麦克风，ES8311 + 功放 + Speaker header 外放。板载语音会录音 2.8 秒，发给 Mac 桥接做 MiMo ASR/LLM/TTS，再拉回 WAV 播放。</p>"
        "<div class=\"row\"><button class=\"primary\" onclick=\"speakerTest()\">喇叭测试</button><button class=\"primary\" onclick=\"micTest()\">麦克风测试</button><button class=\"primary\" onclick=\"voiceTurn()\">板载语音对话</button><button class=\"primary\" onclick=\"voiceWake(1)\">开启连续对话</button><button onclick=\"voiceWake(0)\">关闭连续对话</button><button class=\"primary\" onclick=\"playBridgeTts()\">播放最近回复</button><button onclick=\"audioStatus()\">音频状态</button></div></section>"
        "<section><h2>文本意图测试</h2><label>文本<input id=\"voice_text\" placeholder=\"forward / stop / left\"></label><button onclick=\"sendText()\">发送到意图层</button></section>"
        "<section><h2>Atlas Brain 结构化意图</h2><label>Intent<textarea id=\"brain_intent\" rows=\"9\" style=\"font:inherit;padding:9px;border-radius:6px;border:1px solid #8a632f;background:#151922;color:#efe9df\">{\\\"tool\\\":\\\"atlas_set_expression\\\",\\\"input\\\":{\\\"expression\\\":\\\"happy\\\"}}</textarea></label><button onclick=\"sendBrainIntent()\">发送结构化意图</button></section>"
        "<section><h2>系统</h2><div class=\"row\"><button onclick=\"syncClock()\">用浏览器时间校准</button><button onclick=\"resetCfg()\">清除 Wi-Fi/API</button><button onclick=\"reboot()\">重启</button></div></section></div></main>"
        "<script>"
        "const enc=encodeURIComponent;const capDefs=["
        "{k:'eyes',name:'双眼'},"
        "{k:'clock_page',name:'时钟'},"
        "{k:'status_page',name:'状态'},"
        "{k:'audio_hw',name:'板载音频'},"
        "{k:'voice_ui',name:'语音'},"
        "{k:'music_ui',name:'音乐'},"
        "{k:'story_ui',name:'故事'},"
        "{k:'chat_ui',name:'对话'},"
        "{k:'calendar_ui',name:'日历'},"
        "{k:'pomodoro_ui',name:'番茄'},"
        "{k:'photo_ui',name:'照片'},"
        "{k:'alarm_ui',name:'闹钟'},"
        "{k:'pet',name:'电子宠物'},"
        "{k:'desk_apps',name:'桌面应用'},"
        "{k:'rover_motion',name:'动态底盘'},"
        "];"
        "const capNotes={clock_page:'已接入 桌面时钟',audio_hw:'ES7210/ES8311 自检',voice_ui:'依赖板载音频+ASR/TTS',music_ui:'DualEye 后端已接入状态切换',story_ui:'DualEye 后端已接入状态切换',chat_ui:'DualEye 后端可展示对话文本',calendar_ui:'DualEye 后端可展示与配置日历文案',pomodoro_ui:'番茄功能已接入（可配置/开始/停止）',photo_ui:'本地占位，资源待接入',alarm_ui:'待接入',desk_apps:'时钟/日历/番茄工具化应用',rover_motion:'本版暂停'};"
        "function badge(name,ok,note){const s=ok?'✅':'🧪';return `<span class='chip'>${s} ${name}${note?`：${note}`:''}</span>`;}"
        "function refreshCaps(s){const box=document.getElementById('adminCaps');const feats=(s&&s.features)||{};let h='';for(const c of capDefs){h += badge(c.name,feats[c.k],capNotes[c.k]);}box.innerHTML=h;}"
        "function parseJsonText(raw){const t=((raw||'').replace(/^\\uFEFF/,'')).trim();if(!t){throw new Error('状态响应为空');}const marker='{\"ok\"';const start=t.indexOf(marker)>=0?t.indexOf(marker):t.indexOf('{');if(start<0){throw new Error('响应不是 JSON：未找到开头');}const end=t.lastIndexOf('}');if(end<=start){throw new Error('响应不是 JSON：结尾缺失');}return JSON.parse(t.slice(start,end+1));}"
        "function pin(){return document.getElementById('pin').value}"
        "function h(s){return String(s||'').replace(/[&<>\"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[m]))}"
        "function initPinInput(){const q=new URLSearchParams(location.search).get('pin');const saved=localStorage.getItem('atlas_pin')||'';const p=document.getElementById('pin');if(!p){return;}if(q){p.value=q.trim();localStorage.setItem('atlas_pin',p.value);return;}if(!p.value&&saved){p.value=saved;}}"
        "async function post(u,b=''){try{const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});const t=await r.text();if(!r.ok){return {ok:false,error:`请求失败(${r.status})`,raw:t};}try{return parseJsonText(t)}catch(e){return {ok:false,error:(e&&e.message)?e.message:'响应不是 JSON',raw:t};}}catch(e){return {ok:false,error:e.message||String(e),raw:''}}}"
        "async function refresh(){try{const stReq=await fetch('/api/status');const stText=await stReq.text();if(!stReq.ok){document.getElementById('status').textContent=stText;return;}let s;try{s=parseJsonText(stText);}catch(e){document.getElementById('status').textContent=stText;alert(`状态读取失败：${e.message}`);return;}document.getElementById('status').textContent=JSON.stringify(s,null,2);refreshCaps(s);const nextPin=s&&s.pairing_code?s.pairing_code:'';if(nextPin){localStorage.setItem('atlas_pin',nextPin);const p=document.getElementById('pin');if(p && !p.value){p.value=nextPin;}}"
        "document.getElementById('max_speed').value=s.safety.max_speed_percent;document.getElementById('max_duration').value=s.safety.max_duration_ms;document.getElementById('motion_enabled').checked=s.safety.motion_enabled;document.getElementById('control_mode').value=s.safety.control_mode||'manual';"
        "document.getElementById('ui_theme').value=s.ui.theme||'classic';if(document.getElementById('chat_mode'))document.getElementById('chat_mode').value=s.ui.chat_mode||'pet_head';document.getElementById('brightness').value=s.ui.brightness;document.getElementById('volume').value=s.ui.volume;"
        "document.getElementById('llm_mode').value=s.llm.mode||'off';document.getElementById('provider').value=s.llm.provider||'';document.getElementById('base_url').value=s.llm.base_url||'';document.getElementById('model').value=s.llm.model||''}"
        "async function stopNow(){alert(JSON.stringify(await post('/api/rover/stop')));refresh()}"
        "async function getJson(u){const r=await fetch(u,{cache:'no-store'});const t=await r.text();try{return parseJsonText(t)}catch(e){return {ok:false,error:e.message||String(e),raw:t}}}"
        "async function selfTest(){const data=await getJson('/api/selftest');document.getElementById('status').textContent=JSON.stringify(data,null,2);alert(`自检：pass=${data.summary&&data.summary.pass} warn=${data.summary&&data.summary.warn} fail=${data.summary&&data.summary.fail}`)}"
        "async function systemInfo(){const data=await getJson('/api/system/info');document.getElementById('status').textContent=JSON.stringify(data,null,2)}"
        "async function move(d){const b=`pin=${enc(pin())}&dir=${d}&speed=${enc(speed.value)}&duration=${enc(duration.value)}`;alert(JSON.stringify(await post('/api/rover/move',b)));refresh()}"
        "function chooseWifi(ssid){document.getElementById('ssid').value=ssid;document.getElementById('wifi_pass').focus()}"
        "async function scanWifi(){const box=document.getElementById('wifiScan');box.textContent='正在扫描附近 Wi-Fi...';try{const r=await fetch('/api/wifi/scan',{cache:'no-store'});const t=await r.text();if(!r.ok){box.textContent=`扫描失败(${r.status}): ${t}`;return;}const data=parseJsonText(t);const nets=data.networks||[];if(!nets.length){box.textContent='没有扫到可用 Wi-Fi，靠近路由器后再试。';return;}box.innerHTML=nets.map((n,i)=>`<button id=\"wifi_${i}\" type=\"button\"><span>${h(n.ssid)}</span><span class=\"wifiMeta\">${n.rssi}dBm · CH${n.channel} · ${n.secure?'加密':'开放'}</span></button>`).join('');nets.forEach((n,i)=>{const el=document.getElementById(`wifi_${i}`);if(el){el.onclick=()=>chooseWifi(n.ssid);}})}catch(e){box.textContent=`扫描异常：${e.message||e}`}}"
        "async function saveWifi(){const b=`pin=${enc(pin())}&ssid=${enc(ssid.value)}&password=${enc(wifi_pass.value)}`;alert(JSON.stringify(await post('/api/config/wifi',b)));refresh()}"
        "async function saveLlm(){const b=`pin=${enc(pin())}&mode=${enc(llm_mode.value)}&provider=${enc(provider.value)}&base_url=${enc(base_url.value)}&model=${enc(model.value)}&api_key=${enc(api_key.value)}`;alert(JSON.stringify(await post('/api/config/llm',b)));api_key.value='';refresh()}"
        "async function saveSafety(){const b=`pin=${enc(pin())}&motion_enabled=${motion_enabled.checked?1:0}&control_mode=${enc(control_mode.value)}&max_speed=${enc(max_speed.value)}&max_duration=${enc(max_duration.value)}`;alert(JSON.stringify(await post('/api/config/safety',b)));refresh()}"
        "async function saveUi(){const cm=document.getElementById('chat_mode')?document.getElementById('chat_mode').value:'pet_head';const b=`pin=${enc(pin())}&theme=${enc(ui_theme.value)}&chat_mode=${enc(cm)}&brightness=${enc(brightness.value)}&volume=${enc(volume.value)}`;alert(JSON.stringify(await post('/api/config/ui',b)));refresh()}"
        "async function syncClock(){alert(JSON.stringify(await post('/api/app/action',`pin=${enc(pin())}&action=clock.sync&epoch_ms=${Date.now()}`)));refresh()}"
        "async function audioStatus(){alert(JSON.stringify(await (await fetch('/api/audio/status')).json()))}"
        "async function speakerTest(){alert(JSON.stringify(await post('/api/audio/beep',`pin=${enc(pin())}&freq=880&duration=320&volume=90`)));refresh()}"
        "async function micTest(){alert(JSON.stringify(await post('/api/audio/mic-level',`pin=${enc(pin())}&duration=500`)));refresh()}"
        "async function voiceTurn(){alert('开始板载录音，请对着 DualEye 说话');alert(JSON.stringify(await post('/api/voice/turn',`pin=${enc(pin())}&duration=2800`)));refresh()}"
        "async function voiceWake(on){alert(JSON.stringify(await post('/api/voice/wake',`pin=${enc(pin())}&enabled=${on?1:0}&threshold=36&hits=1&duration=3500`)));refresh()}"
        "async function playBridgeTts(){alert(JSON.stringify(await post('/api/audio/play-url',`pin=${enc(pin())}`)));refresh()}"
        "async function sendText(){const b=`pin=${enc(pin())}&text=${enc(voice_text.value)}`;alert(JSON.stringify(await post('/api/voice/text',b)));refresh()}"
        "async function sendBrainIntent(){const b=`pin=${enc(pin())}&intent=${enc(brain_intent.value)}`;alert(JSON.stringify(await post('/api/intent',b)));refresh()}"
        "async function resetCfg(){if(confirm('清除 Wi-Fi/API 配置？'))alert(JSON.stringify(await post('/api/config/reset',`pin=${enc(pin())}`)))}"
        "async function reboot(){if(confirm('重启设备？'))alert(JSON.stringify(await post('/api/system/reboot',`pin=${enc(pin())}`)))}initPinInput();refresh();setInterval(refresh,4000);</script></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    return httpd_resp_sendstr(req, html);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    atlas_wifi_status_t wifi;
    atlas_wifi_get_status(&wifi);

    atlas_llm_status_t llm;
    atlas_llm_client_get_status(s_ctx.config, &llm);

    atlas_audio_status_t audio;
    atlas_audio_get_status(&audio);

    atlas_ui_state_t ui_snapshot;
    atlas_ui_lock();
    ui_snapshot = *s_ctx.ui_state;
    atlas_ui_unlock();
    const atlas_ui_state_t *ui = &ui_snapshot;

    char base_url[ATLAS_LLM_BASE_URL_MAX * 2];
    char mode[ATLAS_LLM_MODE_MAX * 2];
    char model[ATLAS_LLM_MODEL_MAX * 2];
    char provider[ATLAS_LLM_PROVIDER_MAX * 2];
    char ui_theme[ATLAS_UI_THEME_MAX * 2];
    char chat_mode[ATLAS_CHAT_MODE_MAX * 2];
    char chat_text[ATLAS_BRAIN_INTENT_SPEECH_MAX * 2];
    char cal_title[ATLAS_CALENDAR_TITLE_MAX * 2];
    char cal_note[ATLAS_CALENDAR_NOTE_MAX * 2];
    char pom_task[ATLAS_POMODORO_TASK_MAX * 2];
    char pet_asset[sizeof(ui->pet.asset_id) * 2];
    char wifi_mode[32];
    char control_mode[ATLAS_CONTROL_MODE_MAX * 2];
    char wifi_sta_ip[ATLAS_WIFI_IP_MAX * 2];
    char wifi_ap_ip[ATLAS_WIFI_IP_MAX * 2];
    char wifi_ap_ssid[ATLAS_WIFI_AP_SSID_MAX * 2];
    char audio_error[32];
    char voice_reason[48];
    char pairing_code[8];
    char firmware[64];
    char clock_time[16];
    char clock_date[16];
    char clock_weekday[24];
    char clock_weekday_json[48];
    json_escape(base_url, sizeof(base_url), llm.base_url);
    json_escape(mode, sizeof(mode), llm.mode);
    json_escape(model, sizeof(model), llm.model);
    json_escape(provider, sizeof(provider), llm.provider);
    json_escape(ui_theme, sizeof(ui_theme), s_ctx.config->ui.theme);
    json_escape(chat_mode,
                sizeof(chat_mode),
                atlas_config_chat_mode_is_valid(ui->chat_mode) ? ui->chat_mode : s_ctx.config->ui.chat_mode);
    json_escape(chat_text, sizeof(chat_text), ui->chat_text);
    json_escape(cal_title, sizeof(cal_title), ui->calendar_title);
    json_escape(cal_note, sizeof(cal_note), ui->calendar_note);
    json_escape(pom_task, sizeof(pom_task), ui->pomodoro_task_name);
    json_escape(pet_asset, sizeof(pet_asset), ui->pet.asset_id);
    json_escape(wifi_mode, sizeof(wifi_mode), atlas_wifi_mode_name(wifi.mode));
    json_escape(control_mode, sizeof(control_mode), s_ctx.config->safety.control_mode);
    json_escape(wifi_sta_ip, sizeof(wifi_sta_ip), wifi.sta_ip);
    json_escape(wifi_ap_ip, sizeof(wifi_ap_ip), wifi.ap_ip);
    json_escape(wifi_ap_ssid, sizeof(wifi_ap_ssid), wifi.ap_ssid);
    json_escape(audio_error, sizeof(audio_error), esp_err_to_name(audio.last_error));
    json_escape(voice_reason, sizeof(voice_reason), s_voice_wake_last_reason);
    const bool clock_synced = format_clock_snapshot(clock_time,
                                                    sizeof(clock_time),
                                                    clock_date,
                                                    sizeof(clock_date),
                                                    clock_weekday,
                                                    sizeof(clock_weekday));
    json_escape(clock_weekday_json, sizeof(clock_weekday_json), clock_weekday);

    const uint32_t ts = now_ms();
    const uint32_t idle_ms = ts - ui->pet.last_interaction_ms;
    const uint32_t interval_ms = (ui->pomodoro_interval_ms == 0u)
                                    ? (((ui->pomodoro_in_break && ui->pomodoro_break_minutes > 0u) ?
                                            ((uint32_t)ui->pomodoro_break_minutes * 60u * 1000u) :
                                            ((uint32_t)(ui->pomodoro_focus_minutes > 0u ? ui->pomodoro_focus_minutes : 25u) *
                                             60u *
                                             1000u)))
                                    : ui->pomodoro_interval_ms;
    const uint32_t elapsed = (ui->pomodoro_running && ts >= ui->pomodoro_interval_started_ms) ?
                                (ts - ui->pomodoro_interval_started_ms) : 0u;
    const uint32_t capped_elapsed = (interval_ms > 0u && elapsed >= interval_ms) ? interval_ms : elapsed;
    const uint8_t pomodoro_progress_percent = (interval_ms == 0u) ? 0u : (uint8_t)((capped_elapsed * 100u) / interval_ms);
    const uint32_t pomodoro_remaining_ms = (interval_ms == 0u) ? 0u : (interval_ms - capped_elapsed);
    char runtime_json[1900];
    const size_t runtime_len = atlas_runtime_write_json(runtime_json, sizeof(runtime_json));
    if (!json_fragment_complete(runtime_len, sizeof(runtime_json))) {
        return send_error(req, "500 Internal Server Error", "runtime status json overflow");
    }
    atlas_audio_service_status_t audio_service_status;
    atlas_audio_service_get_status(&audio_service_status);
    char audio_service_json[1400];
    const size_t audio_service_len = atlas_audio_service_write_json(audio_service_json, sizeof(audio_service_json));
    if (!json_fragment_complete(audio_service_len, sizeof(audio_service_json))) {
        return send_error(req, "500 Internal Server Error", "audio service status json overflow");
    }
    atlas_opus_stream_status_t opus_stream_status;
    atlas_opus_stream_get_status(&opus_stream_status);
    char opus_stream_json[1200];
    const size_t opus_stream_len = atlas_opus_stream_write_status_json(&opus_stream_status, opus_stream_json, sizeof(opus_stream_json));
    if (!json_fragment_complete(opus_stream_len, sizeof(opus_stream_json))) {
        return send_error(req, "500 Internal Server Error", "opus stream status json overflow");
    }
    char brain_ws_json[620];
    atlas_brain_ws_status_t brain_ws_status;
    atlas_brain_ws_client_get_status(&brain_ws_status);
    char brain_offline_reason[72];
    json_escape(brain_offline_reason,
                sizeof(brain_offline_reason),
                brain_ws_status.connected ? "" :
                (!brain_ws_status.enabled ? "disabled" :
                 (brain_ws_status.stage[0] == '\0' ? "not_connected" : brain_ws_status.stage)));
    const size_t brain_ws_len = atlas_brain_ws_client_write_json(brain_ws_json, sizeof(brain_ws_json));
    if (!json_fragment_complete(brain_ws_len, sizeof(brain_ws_json))) {
        return send_error(req, "500 Internal Server Error", "brain ws status json overflow");
    }
    atlas_scene_snapshot_t scene;
    atlas_scene_resolve(ui,
                        s_ctx.config,
                        &wifi,
                        &audio,
                        &audio_service_status,
                        atlas_runtime_get_state(),
                        atlas_runtime_get_reason(),
                        ts,
                        &scene);
    char scene_json[1100];
    const size_t scene_len = atlas_scene_write_json(&scene, scene_json, sizeof(scene_json));
    if (!json_fragment_complete(scene_len, sizeof(scene_json))) {
        return send_error(req, "500 Internal Server Error", "scene status json overflow");
    }
    strlcpy(pairing_code, atlas_pairing_code(), sizeof(pairing_code));
    strlcpy(firmware, atlas_firmware_build_tag(), sizeof(firmware));

    const size_t json_size = 18000;
    char *json = (char *)heap_caps_calloc(1, json_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (json == NULL) {
        json = (char *)heap_caps_calloc(1, json_size, MALLOC_CAP_8BIT);
    }
    if (json == NULL) {
        return send_error(req, "500 Internal Server Error", "no memory");
    }
    const bool feature_eyes = true;
    const bool feature_clock_page = true;
    const bool feature_status_page = true;
    const bool feature_audio_hw = audio.input_ready && audio.output_ready;
    const bool feature_voice_ui = feature_audio_hw;
    const bool feature_music_ui = true;
    const bool feature_story_ui = true;
    const bool feature_chat_ui = true;
    const bool feature_calendar_ui = true;
    const bool feature_pomodoro_ui = true;
    const bool feature_photo_ui = true;
    const bool feature_alarm_ui = false;
    const bool feature_pet = true;
    const bool feature_desk_apps = true;
    const bool feature_rover_motion = atlas_config_motion_supported();
    strlcpy(pairing_code, atlas_pairing_code(), sizeof(pairing_code));
    strlcpy(firmware, atlas_firmware_build_tag(), sizeof(firmware));

    const int json_written = snprintf(json,
                                      json_size,
                                      "{"
             "\"ok\":true,"
             "\"pairing_code\":\"%s\","
             "\"pairing_hint\":\"see DualEye screen or serial log\","
             "\"firmware\":\"%s\","
             ATLAS_BUILD_FINGERPRINT_JSON ","
             "\"features\":{\"eyes\":%s,\"clock_page\":%s,\"status_page\":%s,\"audio_hw\":%s,\"voice_ui\":%s,\"music_ui\":%s,\"story_ui\":%s,\"chat_ui\":%s,\"calendar_ui\":%s,\"pomodoro_ui\":%s,\"photo_ui\":%s,\"alarm_ui\":%s,\"pet\":%s,\"pet_head\":true,\"desk_apps\":%s,\"tools\":true,\"rover_motion\":%s},"
             "\"apps\":{\"protocol\":\"atlas.desk_apps.v0\","
             "\"clock\":{\"enabled\":true,\"synced\":%s,\"time\":\"%s\",\"date\":\"%s\",\"weekday\":\"%s\"},"
             "\"calendar\":{\"enabled\":%s,\"title\":\"%s\",\"note\":\"%s\",\"source\":\"local_note\"},"
             "\"pomodoro\":{\"enabled\":%s,\"running\":%s,\"in_break\":%s,\"task\":\"%s\",\"focus_minutes\":%" PRIu16 ",\"break_minutes\":%" PRIu16 ",\"progress_percent\":%" PRIu8 ",\"remaining_ms\":%" PRIu32 "}},"
             "\"ui\":{\"page\":\"%s\",\"expression\":\"%s\",\"motion\":\"%s\",\"moving\":%s,\"last_ack\":%d,"
             "\"theme\":\"%s\",\"chat_mode\":\"%s\",\"brightness\":%u,\"volume\":%u,\"chat_text\":\"%s\"},"
             "\"scene\":%s,"
             "\"audio\":{\"initialized\":%s,\"i2c_ready\":%s,\"i2s_ready\":%s,\"input_ready\":%s,\"output_ready\":%s,"
             "\"sample_rate\":%" PRIu16 ",\"volume\":%" PRIu8 ",\"mic_level\":%" PRIu8 ",\"mic_rms\":%" PRIu32 ",\"mic_peak\":%" PRIu32 ","
             "\"speaker_tests\":%" PRIu32 ",\"mic_tests\":%" PRIu32 ",\"last_error\":\"%s\"},"
             "\"audio_service\":%s,"
             "\"audio_stream\":%s,"
             "\"brain_ws\":%s,"
             "\"voice\":{\"available\":%s,\"brain_online\":%s,\"turn_transport\":\"brain_ws_binary\",\"offline_reason\":\"%s\"},"
             "\"voice_wake\":{\"enabled\":%s,\"busy\":%s,\"psram_stack\":%s,\"continuous\":true,\"threshold\":%u,\"hits_required\":%u,\"duration_ms\":%u,"
             "\"triggers\":%" PRIu32 ",\"last_ms\":%" PRIu32 ",\"last_level\":%" PRIu32 ",\"last_rms\":%" PRIu32 ",\"last_peak\":%" PRIu32 ","
             "\"noise_rms\":%" PRIu32 ",\"noise_peak\":%" PRIu32 ",\"hit_count\":%u,\"mute_ms\":%" PRIu32 ",\"reason\":\"%s\"},"
             "\"chat\":{\"text\":\"%s\"},"
             "\"calendar\":{\"enabled\":%s,\"title\":\"%s\",\"note\":\"%s\"},"
             "\"pomodoro\":{\"enabled\":%s,\"running\":%s,\"in_break\":%s,\"task\":\"%s\",\"focus_minutes\":%" PRIu16 ",\"break_minutes\":%" PRIu16 ",\"progress_percent\":%" PRIu8 ",\"remaining_ms\":%" PRIu32 "},"
             "\"pet\":{\"phase\":\"%s\",\"label\":\"%s\",\"mood\":%" PRIu8 ",\"energy\":%" PRIu8 ",\"curiosity\":%" PRIu8 ","
             "\"asleep\":%s,\"asset_id\":\"%s\",\"idle_ms\":%" PRIu32 "},"
             "\"wifi\":{\"mode\":\"%s\",\"sta_connected\":%s,\"sta_ip\":\"%s\",\"ap_started\":%s,\"ap_ip\":\"%s\",\"ap_ssid\":\"%s\"},"
             "\"llm\":{\"mode\":\"%s\",\"label\":\"%s\",\"provider\":\"%s\",\"base_url\":\"%s\",\"model\":\"%s\",\"configured\":%s,\"api_key_set\":%s},"
             "\"runtime\":%s,"
             "\"safety\":{\"motion_enabled\":%s,\"control_mode\":\"%s\",\"max_speed_percent\":%u,\"max_duration_ms\":%u}"
             "}",
             pairing_code,
             firmware,
             firmware,
             json_bool(feature_eyes),
             json_bool(feature_clock_page),
             json_bool(feature_status_page),
             json_bool(feature_audio_hw),
             json_bool(feature_voice_ui),
             json_bool(feature_music_ui),
             json_bool(feature_story_ui),
             json_bool(feature_chat_ui),
             json_bool(feature_calendar_ui),
             json_bool(feature_pomodoro_ui),
             json_bool(feature_photo_ui),
             json_bool(feature_alarm_ui),
             json_bool(feature_pet),
             json_bool(feature_desk_apps),
             json_bool(feature_rover_motion),
             json_bool(clock_synced),
             clock_time,
             clock_date,
             clock_weekday_json,
             json_bool(s_ctx.config->calendar.enabled),
             cal_title,
             cal_note,
             json_bool(s_ctx.config->pomodoro.enabled),
             json_bool(ui->pomodoro_running),
             json_bool(ui->pomodoro_in_break),
             pom_task,
             ui->pomodoro_focus_minutes,
             ui->pomodoro_break_minutes,
             pomodoro_progress_percent,
             pomodoro_remaining_ms,
             atlas_page_name(ui->page),
             atlas_expression_name(ui->expression),
             atlas_motion_name(ui->motion),
             ui->moving ? "true" : "false",
             (int)ui->last_ack,
             ui_theme,
             chat_mode,
             s_ctx.config->ui.brightness,
             s_ctx.config->ui.volume,
             chat_text,
             scene_json,
             json_bool(audio.initialized),
             json_bool(audio.i2c_ready),
             json_bool(audio.i2s_ready),
             json_bool(audio.input_ready),
             json_bool(audio.output_ready),
             audio.sample_rate,
             audio.volume,
             audio.last_mic_level,
             audio.last_mic_rms,
             audio.last_mic_peak,
             audio.speaker_tests,
             audio.mic_tests,
             audio_error,
             audio_service_json,
             opus_stream_json,
             brain_ws_json,
             json_bool(feature_audio_hw && brain_ws_status.connected),
             json_bool(brain_ws_status.connected),
             brain_offline_reason,
             json_bool(s_voice_wake_enabled),
             json_bool(s_voice_wake_busy),
             json_bool(s_voice_wake_psram_stack),
             s_voice_wake_threshold,
             s_voice_wake_hits_required,
             s_voice_wake_duration_ms,
             s_voice_wake_triggers,
             s_voice_wake_last_ms,
             s_voice_wake_last_level,
             s_voice_wake_last_rms,
             s_voice_wake_last_peak,
             s_voice_wake_noise_rms,
             s_voice_wake_noise_peak,
             s_voice_wake_hit_count,
             remaining_ms(ts, s_voice_wake_mute_until_ms),
             voice_reason,
             chat_text,
             json_bool(s_ctx.config->calendar.enabled),
             cal_title,
             cal_note,
             json_bool(s_ctx.config->pomodoro.enabled),
             json_bool(ui->pomodoro_running),
             json_bool(ui->pomodoro_in_break),
             pom_task,
             ui->pomodoro_focus_minutes,
             ui->pomodoro_break_minutes,
             pomodoro_progress_percent,
             pomodoro_remaining_ms,
             atlas_pet_phase_name(ui->pet.phase),
             atlas_pet_phase_label_zh(ui->pet.phase),
             ui->pet.mood,
             ui->pet.energy,
             ui->pet.curiosity,
             ui->pet.asleep ? "true" : "false",
             pet_asset,
             (unsigned long)idle_ms,
             wifi_mode,
             wifi.sta_connected ? "true" : "false",
             wifi_sta_ip,
             wifi.ap_started ? "true" : "false",
             wifi_ap_ip,
             wifi_ap_ssid,
             mode,
             atlas_llm_client_mode_label(llm.mode),
             provider,
             base_url,
             model,
             llm.configured ? "true" : "false",
             llm.api_key_set ? "true" : "false",
             runtime_json,
             s_ctx.config->safety.motion_enabled ? "true" : "false",
             control_mode,
             s_ctx.config->safety.max_speed_percent,
                                      s_ctx.config->safety.max_duration_ms);
    if (!json_snprintf_complete(json_written, json_size)) {
        free(json);
        return send_error(req, "500 Internal Server Error", "status json overflow");
    }

    const esp_err_t ret = send_json(req, json);
    free(json);
    return ret;
}

static esp_err_t status_lite_handler(httpd_req_t *req)
{
    atlas_wifi_status_t wifi;
    atlas_wifi_get_status(&wifi);

    atlas_llm_status_t llm;
    atlas_llm_client_get_status(s_ctx.config, &llm);

    atlas_audio_status_t audio;
    atlas_audio_get_status(&audio);

    atlas_audio_service_status_t audio_service;
    atlas_audio_service_get_status(&audio_service);

    atlas_opus_stream_status_t opus_stream;
    atlas_opus_stream_get_status(&opus_stream);

    atlas_ui_state_t ui_snapshot;
    atlas_ui_lock();
    ui_snapshot = *s_ctx.ui_state;
    atlas_ui_unlock();
    const atlas_ui_state_t *ui = &ui_snapshot;

    char pairing_code[8];
    char firmware[64];
    char ui_theme[ATLAS_UI_THEME_MAX * 2];
    char chat_mode[ATLAS_CHAT_MODE_MAX * 2];
    char chat_text[ATLAS_CHAT_TEXT_MAX * 2];
    char cal_title[ATLAS_CALENDAR_TITLE_MAX * 2];
    char cal_note[ATLAS_CALENDAR_NOTE_MAX * 2];
    char pom_task[ATLAS_POMODORO_TASK_MAX * 2];
    char wifi_sta_ip[ATLAS_WIFI_IP_MAX * 2];
    char wifi_ap_ip[ATLAS_WIFI_IP_MAX * 2];
    char wifi_ap_ssid[ATLAS_WIFI_AP_SSID_MAX * 2];
    char wifi_mode[32];
    char llm_mode[ATLAS_LLM_MODE_MAX * 2];
    char llm_provider[ATLAS_LLM_PROVIDER_MAX * 2];
    char llm_base[ATLAS_LLM_BASE_URL_MAX * 2];
    char llm_model[ATLAS_LLM_MODEL_MAX * 2];
    char device_name[ATLAS_DEVICE_NAME_MAX * 2];
    char service_failure[sizeof(audio_service.last_failure) * 2];
    char service_action[sizeof(audio_service.last_action) * 2];
    char voice_reason[48];
    char stream_stage[sizeof(opus_stream.stage) * 2];
    char runtime_reason[96];
    char brain_ws_json[620];
    atlas_brain_ws_status_t brain_ws_status;
    atlas_brain_ws_client_get_status(&brain_ws_status);
    char brain_offline_reason[72];
    char clock_time[16];
    char clock_date[16];
    char clock_weekday[24];
    char clock_weekday_json[48];
    char pet_asset[sizeof(ui->pet.asset_id) * 2];
    strlcpy(pairing_code, atlas_pairing_code(), sizeof(pairing_code));
    strlcpy(firmware, atlas_firmware_build_tag(), sizeof(firmware));
    json_escape(ui_theme, sizeof(ui_theme), s_ctx.config->ui.theme);
    json_escape(chat_mode,
                sizeof(chat_mode),
                atlas_config_chat_mode_is_valid(ui->chat_mode) ? ui->chat_mode : s_ctx.config->ui.chat_mode);
    json_escape(chat_text, sizeof(chat_text), ui->chat_text);
    json_escape(cal_title, sizeof(cal_title), ui->calendar_title);
    json_escape(cal_note, sizeof(cal_note), ui->calendar_note);
    json_escape(pom_task, sizeof(pom_task), ui->pomodoro_task_name);
    json_escape(wifi_sta_ip, sizeof(wifi_sta_ip), wifi.sta_ip);
    json_escape(wifi_ap_ip, sizeof(wifi_ap_ip), wifi.ap_ip);
    json_escape(wifi_ap_ssid, sizeof(wifi_ap_ssid), wifi.ap_ssid);
    json_escape(wifi_mode, sizeof(wifi_mode), atlas_wifi_mode_name(wifi.mode));
    json_escape(llm_mode, sizeof(llm_mode), llm.mode);
    json_escape(llm_provider, sizeof(llm_provider), llm.provider);
    json_escape(llm_base, sizeof(llm_base), llm.base_url);
    json_escape(llm_model, sizeof(llm_model), llm.model);
    json_escape(device_name, sizeof(device_name), s_ctx.config->device_name);
    json_escape(service_failure,
                sizeof(service_failure),
                audio_service.last_failure[0] == '\0' ? "" : audio_service.last_failure);
    json_escape(service_action, sizeof(service_action), audio_service.last_action);
    json_escape(voice_reason, sizeof(voice_reason), s_voice_wake_last_reason);
    json_escape(stream_stage, sizeof(stream_stage), opus_stream.stage);
    json_escape(runtime_reason, sizeof(runtime_reason), atlas_runtime_get_reason());
    json_escape(pet_asset, sizeof(pet_asset), ui->pet.asset_id);
    json_escape(brain_offline_reason,
                sizeof(brain_offline_reason),
                brain_ws_status.connected ? "" :
                (!brain_ws_status.enabled ? "disabled" :
                 (brain_ws_status.stage[0] == '\0' ? "not_connected" : brain_ws_status.stage)));
    const size_t brain_ws_len = atlas_brain_ws_client_write_json(brain_ws_json, sizeof(brain_ws_json));
    if (!json_fragment_complete(brain_ws_len, sizeof(brain_ws_json))) {
        return send_error(req, "500 Internal Server Error", "brain ws status json overflow");
    }
    const bool clock_synced = format_clock_snapshot(clock_time,
                                                    sizeof(clock_time),
                                                    clock_date,
                                                    sizeof(clock_date),
                                                    clock_weekday,
                                                    sizeof(clock_weekday));
    json_escape(clock_weekday_json, sizeof(clock_weekday_json), clock_weekday);

    const uint32_t ts = now_ms();
    const uint32_t idle_ms = ts - ui->pet.last_interaction_ms;
    const uint32_t interval_ms = (ui->pomodoro_interval_ms == 0u)
                                    ? (((ui->pomodoro_in_break && ui->pomodoro_break_minutes > 0u) ?
                                            ((uint32_t)ui->pomodoro_break_minutes * 60u * 1000u) :
                                            ((uint32_t)(ui->pomodoro_focus_minutes > 0u ? ui->pomodoro_focus_minutes : 25u) *
                                             60u *
                                             1000u)))
                                    : ui->pomodoro_interval_ms;
    const uint32_t elapsed = (ui->pomodoro_running && ts >= ui->pomodoro_interval_started_ms) ?
                                (ts - ui->pomodoro_interval_started_ms) : 0u;
    const uint32_t capped_elapsed = (interval_ms > 0u && elapsed >= interval_ms) ? interval_ms : elapsed;
    const uint8_t pomodoro_progress_percent = (interval_ms == 0u) ? 0u : (uint8_t)((capped_elapsed * 100u) / interval_ms);
    const uint32_t pomodoro_remaining_ms = (interval_ms == 0u) ? 0u : (interval_ms - capped_elapsed);

    atlas_scene_snapshot_t scene;
    atlas_scene_resolve(ui,
                        s_ctx.config,
                        &wifi,
                        &audio,
                        &audio_service,
                        atlas_runtime_get_state(),
                        atlas_runtime_get_reason(),
                        ts,
                        &scene);
    char scene_json[1100];
    const size_t scene_len = atlas_scene_write_json(&scene, scene_json, sizeof(scene_json));
    if (!json_fragment_complete(scene_len, sizeof(scene_json))) {
        return send_error(req, "500 Internal Server Error", "scene status json overflow");
    }

    char json[7000];
    const int written = snprintf(json,
                                 sizeof(json),
                                 "{"
                                 "\"ok\":true,"
                                 "\"lite\":true,"
                                 "\"pairing_code\":\"%s\","
                                 "\"firmware\":\"%s\","
                                 ATLAS_BUILD_FINGERPRINT_JSON ","
                                 "\"device\":{\"model\":\"waveshare-dualeye-s3-1.28\",\"name\":\"%s\",\"uptime_ms\":%" PRIu32 "},"
                                 "\"features\":{\"eyes\":true,\"clock_page\":true,\"status_page\":true,\"audio_hw\":%s,\"voice_ui\":%s,\"music_ui\":true,\"story_ui\":true,\"chat_ui\":true,\"calendar_ui\":true,\"pomodoro_ui\":true,\"pet\":true,\"pet_head\":true,\"desk_apps\":true,\"tools\":true,\"rover_motion\":%s},"
                                 "\"ui\":{\"page\":\"%s\",\"expression\":\"%s\",\"theme\":\"%s\",\"chat_mode\":\"%s\",\"brightness\":%u,\"volume\":%u,\"chat_text\":\"%s\"},"
                                 "\"scene\":%s,"
                                 "\"apps\":{\"clock\":{\"enabled\":true,\"synced\":%s,\"time\":\"%s\",\"date\":\"%s\",\"weekday\":\"%s\"},"
                                 "\"calendar\":{\"enabled\":%s,\"title\":\"%s\",\"note\":\"%s\"},"
                                 "\"pomodoro\":{\"enabled\":%s,\"running\":%s,\"in_break\":%s,\"task\":\"%s\",\"focus_minutes\":%" PRIu16 ",\"break_minutes\":%" PRIu16 ",\"progress_percent\":%" PRIu8 ",\"remaining_ms\":%" PRIu32 "}},"
                                 "\"pet\":{\"phase\":\"%s\",\"label\":\"%s\",\"mood\":%" PRIu8 ",\"energy\":%" PRIu8 ",\"curiosity\":%" PRIu8 ",\"asleep\":%s,\"asset_id\":\"%s\",\"idle_ms\":%" PRIu32 "},"
                                 "\"chat\":{\"text\":\"%s\"},"
                                 "\"calendar\":{\"enabled\":%s,\"title\":\"%s\",\"note\":\"%s\"},"
                                 "\"pomodoro\":{\"enabled\":%s,\"running\":%s,\"in_break\":%s,\"task\":\"%s\",\"focus_minutes\":%" PRIu16 ",\"break_minutes\":%" PRIu16 ",\"progress_percent\":%" PRIu8 ",\"remaining_ms\":%" PRIu32 "},"
                                 "\"wifi\":{\"mode\":\"%s\",\"sta_connected\":%s,\"sta_ip\":\"%s\",\"ap_started\":%s,\"ap_ip\":\"%s\",\"ap_ssid\":\"%s\"},"
                                 "\"llm\":{\"mode\":\"%s\",\"label\":\"%s\",\"provider\":\"%s\",\"base_url\":\"%s\",\"model\":\"%s\",\"configured\":%s,\"api_key_set\":%s},"
                                 "\"audio\":{\"input_ready\":%s,\"output_ready\":%s,\"volume\":%" PRIu8 ",\"mic_level\":%" PRIu8 ",\"mic_rms\":%" PRIu32 ",\"mic_peak\":%" PRIu32 "},"
                                 "\"audio_service\":{\"mode\":\"%s\",\"busy\":%s,\"job_running\":%s,\"continuous_enabled\":%s,\"muted\":%s,\"mute_remaining_ms\":%" PRIu32 ",\"turn_count\":%" PRIu32 ",\"job_error_count\":%" PRIu32 ",\"consecutive_failures\":%" PRIu32 ",\"last_action\":\"%s\",\"last_error\":\"%s\",\"last_failure\":\"%s\"},"
                                 "\"audio_stream\":{\"running\":%s,\"connected\":%s,\"stage\":\"%s\",\"frames_sent\":%" PRIu32 ",\"sequence\":%" PRIu32 ",\"send_failures\":%" PRIu32 ",\"capture_failures\":%" PRIu32 ",\"encode_failures\":%" PRIu32 "},"
                                 "\"brain_ws\":%s,"
                                 "\"voice\":{\"available\":%s,\"brain_online\":%s,\"turn_transport\":\"brain_ws_binary\",\"offline_reason\":\"%s\"},"
                                 "\"voice_wake\":{\"enabled\":%s,\"busy\":%s,\"threshold\":%u,\"hits_required\":%u,\"duration_ms\":%u,\"triggers\":%" PRIu32 ",\"mute_ms\":%" PRIu32 ",\"reason\":\"%s\"},"
                                 "\"safety\":{\"motion_enabled\":%s,\"control_mode\":\"%s\",\"max_speed_percent\":%u,\"max_duration_ms\":%u},"
                                 "\"runtime\":{\"state\":\"%s\",\"reason\":\"%s\",\"changed_ms\":%" PRIu32 "}"
                                 "}",
                                 pairing_code,
                                 firmware,
                                 firmware,
                                 device_name,
                                 ts,
                                 json_bool(audio.input_ready && audio.output_ready),
                                 json_bool(audio.input_ready && audio.output_ready),
                                 json_bool(atlas_config_motion_supported()),
                                 atlas_page_name(ui->page),
                                 atlas_expression_name(ui->expression),
                                 ui_theme,
                                 chat_mode,
                                 s_ctx.config->ui.brightness,
                                 s_ctx.config->ui.volume,
                                 chat_text,
                                 scene_json,
                                 json_bool(clock_synced),
                                 clock_time,
                                 clock_date,
                                 clock_weekday_json,
                                 json_bool(s_ctx.config->calendar.enabled),
                                 cal_title,
                                 cal_note,
                                 json_bool(s_ctx.config->pomodoro.enabled),
                                 json_bool(ui->pomodoro_running),
                                 json_bool(ui->pomodoro_in_break),
                                 pom_task,
                                 ui->pomodoro_focus_minutes,
                                 ui->pomodoro_break_minutes,
                                 pomodoro_progress_percent,
                                 pomodoro_remaining_ms,
                                 atlas_pet_phase_name(ui->pet.phase),
                                 atlas_pet_phase_label_zh(ui->pet.phase),
                                 ui->pet.mood,
                                 ui->pet.energy,
                                 ui->pet.curiosity,
                                 ui->pet.asleep ? "true" : "false",
                                 pet_asset,
                                 idle_ms,
                                 chat_text,
                                 json_bool(s_ctx.config->calendar.enabled),
                                 cal_title,
                                 cal_note,
                                 json_bool(s_ctx.config->pomodoro.enabled),
                                 json_bool(ui->pomodoro_running),
                                 json_bool(ui->pomodoro_in_break),
                                 pom_task,
                                 ui->pomodoro_focus_minutes,
                                 ui->pomodoro_break_minutes,
                                 pomodoro_progress_percent,
                                 pomodoro_remaining_ms,
                                 wifi_mode,
                                 wifi.sta_connected ? "true" : "false",
                                 wifi_sta_ip,
                                 wifi.ap_started ? "true" : "false",
                                 wifi_ap_ip,
                                 wifi_ap_ssid,
                                 llm_mode,
                                 atlas_llm_client_mode_label(llm.mode),
                                 llm_provider,
                                 llm_base,
                                 llm_model,
                                 json_bool(llm.configured),
                                 json_bool(llm.api_key_set),
                                 json_bool(audio.input_ready),
                                 json_bool(audio.output_ready),
                                 audio.volume,
                                 audio.last_mic_level,
                                 audio.last_mic_rms,
                                 audio.last_mic_peak,
                                 atlas_audio_service_mode_name(audio_service.mode),
                                 json_bool(audio_service.busy),
                                 json_bool(audio_service.job_running),
                                 json_bool(audio_service.continuous_enabled),
                                 json_bool(audio_service.muted),
                                 audio_service.mute_remaining_ms,
                                 audio_service.turn_count,
                                 audio_service.job_error_count,
                                 audio_service.consecutive_failures,
                                 service_action,
                                 esp_err_to_name(audio_service.last_error),
                                 service_failure,
                                 json_bool(opus_stream.running),
                                 json_bool(opus_stream.connected),
                                 stream_stage,
                                 opus_stream.frames_sent,
                                 opus_stream.sequence,
                                 opus_stream.send_failures,
                                 opus_stream.capture_failures,
                                 opus_stream.encode_failures,
                                 brain_ws_json,
                                 json_bool(audio.input_ready && audio.output_ready && brain_ws_status.connected),
                                 json_bool(brain_ws_status.connected),
                                 brain_offline_reason,
                                 json_bool(s_voice_wake_enabled),
                                 json_bool(s_voice_wake_busy),
                                 s_voice_wake_threshold,
                                 s_voice_wake_hits_required,
                                 s_voice_wake_duration_ms,
                                 s_voice_wake_triggers,
                                 remaining_ms(ts, s_voice_wake_mute_until_ms),
                                 voice_reason,
                                 json_bool(s_ctx.config->safety.motion_enabled),
                                 s_ctx.config->safety.control_mode,
                                 s_ctx.config->safety.max_speed_percent,
                                 s_ctx.config->safety.max_duration_ms,
                                 atlas_runtime_state_name(atlas_runtime_get_state()),
                                 runtime_reason,
                                 atlas_runtime_get_state_changed_ms());
    if (!json_snprintf_complete(written, sizeof(json))) {
        return send_error(req, "500 Internal Server Error", "status lite json overflow");
    }
    return send_json(req, json);
}

static esp_err_t capabilities_handler(httpd_req_t *req)
{
    atlas_audio_status_t audio;
    atlas_audio_get_status(&audio);

    atlas_llm_status_t llm;
    atlas_llm_client_get_status(s_ctx.config, &llm);

    char firmware[64];
    char theme[ATLAS_UI_THEME_MAX * 2];
    char chat_mode[ATLAS_CHAT_MODE_MAX * 2];
    char control_mode[ATLAS_CONTROL_MODE_MAX * 2];
    char audio_error[32];
    json_escape(firmware, sizeof(firmware), atlas_firmware_build_tag());
    json_escape(theme, sizeof(theme), s_ctx.config->ui.theme);
    json_escape(chat_mode, sizeof(chat_mode), s_ctx.config->ui.chat_mode);
    json_escape(control_mode, sizeof(control_mode), s_ctx.config->safety.control_mode);
    json_escape(audio_error, sizeof(audio_error), esp_err_to_name(audio.last_error));

    const size_t json_size = 5800;
    char *json = (char *)calloc(1, json_size);
    if (json == NULL) {
        return send_error(req, "500 Internal Server Error", "no memory");
    }

    snprintf(json,
             json_size,
             "{"
             "\"ok\":true,"
             "\"device_model\":\"waveshare-dualeye-s3-1.28\","
             "\"project\":\"atlas-rover-mk1\","
             "\"firmware\":\"%s\","
             ATLAS_BUILD_FINGERPRINT_JSON ","
             "\"display\":{\"screens\":2,\"width\":240,\"height\":240,"
             "\"themes\":[\"raptor\",\"mecha\",\"goggle\",\"pet\",\"blue_pupil\",\"no_smoking\",\"tomoe_spin\",\"classic\",\"amber\",\"mint\",\"alert\",\"night\"],"
             "\"current_theme\":\"%s\","
             "\"chat_modes\":[\"pet_head\",\"text\",\"eyes_only\"],\"current_chat_mode\":\"%s\","
             "\"expressions\":[\"idle\",\"blink\",\"happy\",\"listen\",\"thinking\",\"speaking\",\"moving\",\"curious\",\"sleepy\",\"surprised\",\"wink\",\"love\",\"money\",\"angry\",\"charging\",\"error\",\"cry\"],"
             "\"pages\":[\"eyes\",\"clock\",\"status\",\"voice\",\"music\",\"story\",\"chat\",\"calendar\",\"pomodoro\",\"photo\"]},"
             "\"audio\":{\"input_ready\":%s,\"output_ready\":%s,\"sample_rate\":%" PRIu16 ",\"volume\":%" PRIu8 ",\"last_error\":\"%s\"},"
             "\"voice\":{\"wake_endpoint\":\"/api/voice/wake\",\"turn_endpoint\":\"/api/voice/turn\","
             "\"continuous_supported\":true,\"turn_transport\":\"brain_ws_binary\"},"
             "\"brain_channel\":{\"protocol\":\"" ATLAS_BRAIN_SESSION_PROTOCOL "\",\"http_wav\":false,\"websocket_json\":%s,"
             "\"websocket_client\":true,\"websocket_client_endpoint\":\"/ws/brain\","
             "\"websocket_stage\":\"" ATLAS_BRAIN_SESSION_STAGE "\",\"ws_endpoint\":\"%s\","
             "\"audio_turn_binary\":true,\"tts_binary\":true,\"audio_streaming\":true,\"opus_streaming\":true,\"binary_audio\":true,"
             "\"opus_probe\":true,\"opus_probe_endpoint\":\"/api/audio/opus-probe\","
             "\"opus_stream_endpoint\":\"/api/audio/opus-stream/start\","
             "\"opus_stream_status_endpoint\":\"/api/audio/opus-stream/status\","
             "\"esp_sr_wake_word\":false,\"unsupported\":[\"wakenet\",\"aec\",\"device_mcp\"]},"
             "\"speech_recognition\":{\"p4_probe\":true,\"status_endpoint\":\"/api/sr/status\","
             "\"current_wake_engine\":\"energy_gate_vad\",\"esp_sr_wakenet\":false,\"aec\":false},"
             "\"control\":{\"modes\":[\"manual\",\"ai\"],\"current_mode\":\"%s\",\"motion_supported\":%s,\"motion_enabled\":%s,"
             "\"max_speed_percent\":%u,\"max_duration_ms\":%u,"
             "\"stop_endpoint\":\"/api/rover/stop\",\"move_endpoint\":\"%s\",\"disabled_reason\":\"%s\"},"
             "\"skills\":{\"brain_intent\":true,\"intent_alias_endpoint\":\"/api/brain/intent\",\"intent_endpoint\":\"/api/intent\",\"app_action\":true,\"pet_event\":true,\"pomodoro\":true,\"calendar\":true,"
             "\"tool_schema\":\"" ATLAS_TOOL_SCHEMA_VERSION "\",\"tools_list_endpoint\":\"/api/tools/list\",\"tools_call_endpoint\":\"/api/tools/call\"},"
             "\"network\":{\"softap\":true,\"sta\":true,\"wifi_scan\":true,\"status_endpoint\":\"/api/status\",\"lite_status_endpoint\":\"/api/status/lite\"},"
             "\"resources\":{\"spiffs_assets\":true,\"sdcard_required\":false,\"zh_font_level1_3500\":true,"
             "\"pet_head\":true,\"pet_head_version\":\"0.3.0\",\"pet_head_background\":\"transparent\","
             "\"pet_head_keyframes\":11,\"pet_head_views\":[\"yaw_l30\",\"yaw_l15\",\"yaw_c\",\"yaw_r15\",\"yaw_r30\"],"
             "\"pet_head_view_states\":[\"idle\",\"listen\",\"think\",\"speak\"],"
             "\"pet_head_transitions\":4,\"pet_head_animations\":[\"blink\",\"speak\",\"sing\",\"laugh\"]},"
             "\"ota\":{\"supported\":true,\"app_ota\":true,\"full_image_ota\":false,\"status_endpoint\":\"/api/ota/status\",\"manifest_endpoint\":\"/api/ota/manifest\",\"packages_endpoint\":\"/api/ota/packages\",\"apply_endpoint\":\"/api/ota/apply\",\"reason\":\"dual_ota_app_slots_ready__storage_and_partition_usb_only\"},"
             "\"security\":{\"pairing_required_for_config\":true,\"stop_without_pairing\":true}"
             "}",
             firmware,
             firmware,
             theme,
             chat_mode,
             json_bool(audio.input_ready),
             json_bool(audio.output_ready),
             audio.sample_rate,
             audio.volume,
             audio_error,
#if CONFIG_HTTPD_WS_SUPPORT
             "true",
             "/api/brain/ws",
#else
             "false",
             "",
#endif
             control_mode,
             json_bool(atlas_config_motion_supported()),
             json_bool(s_ctx.config->safety.motion_enabled),
             s_ctx.config->safety.max_speed_percent,
             s_ctx.config->safety.max_duration_ms,
             atlas_config_motion_supported() ? "/api/rover/move" : "",
             atlas_config_motion_supported() ? "" : "desk_companion_build");

    const esp_err_t ret = send_json(req, json);
    free(json);
    return ret;
}

static esp_err_t sr_status_handler(httpd_req_t *req)
{
    char sr_json[1400];
    atlas_sr_probe_write_json(sr_json, sizeof(sr_json));

    char json[1600];
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"speech_recognition\":%s}",
             sr_json);
    return send_json(req, json);
}

static esp_err_t diagnostics_turn_handler(httpd_req_t *req)
{
    atlas_wifi_status_t wifi;
    atlas_wifi_get_status(&wifi);
    atlas_audio_status_t audio;
    atlas_audio_get_status(&audio);
    atlas_audio_service_status_t audio_service_status;
    atlas_audio_service_get_status(&audio_service_status);

    char runtime_json[2200];
    atlas_runtime_write_json(runtime_json, sizeof(runtime_json));
    char audio_service_json[1400];
    atlas_audio_service_write_json(audio_service_json, sizeof(audio_service_json));
    atlas_scene_snapshot_t scene;
    atlas_scene_resolve(s_ctx.ui_state,
                        s_ctx.config,
                        &wifi,
                        &audio,
                        &audio_service_status,
                        atlas_runtime_get_state(),
                        atlas_runtime_get_reason(),
                        now_ms(),
                        &scene);
    char scene_json[1100];
    atlas_scene_write_json(&scene, scene_json, sizeof(scene_json));

    const uint32_t ts = now_ms();
    char json[5600];
    snprintf(json,
             sizeof(json),
             "{"
             "\"ok\":true,"
             "\"now_ms\":%" PRIu32 ","
             "\"voice_wake\":{\"enabled\":%s,\"busy\":%s,\"psram_stack\":%s,\"mute_ms\":%" PRIu32 ",\"triggers\":%" PRIu32 ",\"reason\":\"%s\"},"
             "\"scene\":%s,"
             "\"audio_service\":%s,"
             "\"runtime\":%s"
             "}",
             ts,
             json_bool(s_voice_wake_enabled),
             json_bool(s_voice_wake_busy),
             json_bool(s_voice_wake_psram_stack),
             remaining_ms(ts, s_voice_wake_mute_until_ms),
             s_voice_wake_triggers,
             s_voice_wake_last_reason,
             scene_json,
             audio_service_json,
             runtime_json);
    return send_json(req, json);
}

#if CONFIG_HTTPD_WS_SUPPORT
static esp_err_t brain_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        s_brain_ws_fd = httpd_req_to_sockfd(req);
        s_brain_ws_session_seq++;
        brain_ws_remember_event("ws.connected", "brain websocket connected");
        ESP_LOGI(TAG, "brain websocket handshake done fd=%d session=%" PRIu32, s_brain_ws_fd, s_brain_ws_session_seq);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = NULL,
        .len = 0,
    };
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        return err;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        brain_ws_remember_event("ws.disconnected", "brain websocket closed");
        s_brain_ws_fd = -1;
        return ESP_OK;
    }

    char payload[420] = "";
    if (frame.len > 0u) {
        const size_t read_len = frame.len >= sizeof(payload) ? sizeof(payload) - 1u : frame.len;
        frame.payload = (uint8_t *)payload;
        frame.len = read_len;
        err = httpd_ws_recv_frame(req, &frame, read_len);
        if (err != ESP_OK) {
            return err;
        }
        payload[read_len] = '\0';
    }

    char escaped[560];
    json_escape(escaped, sizeof(escaped), payload);
    const uint32_t seq = brain_ws_remember_event("ws.message", escaped);

    char msg_type[32] = "hello";
    cJSON *root = cJSON_Parse(payload);
    if (root != NULL) {
        cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
        if (cJSON_IsString(type) && type->valuestring != NULL && type->valuestring[0] != '\0') {
            strlcpy(msg_type, type->valuestring, sizeof(msg_type));
        }
    } else if (payload[0] != '\0') {
        strlcpy(msg_type, "message", sizeof(msg_type));
    }

    char state_json[6200];
    brain_ws_build_state_json(state_json, sizeof(state_json));
    char response[9800];
    if (strcmp(msg_type, "ping") == 0) {
        snprintf(response,
                 sizeof(response),
                 "{\"ok\":true,\"protocol\":\"" ATLAS_BRAIN_SESSION_PROTOCOL "\",\"type\":\"pong\","
                 "\"stage\":\"" ATLAS_BRAIN_SESSION_STAGE "\",\"session_id\":\"dualeye-ws-%" PRIu32 "\","
                 "\"seq\":%" PRIu32 ",\"now_ms\":%" PRIu32 "}",
                 s_brain_ws_session_seq,
                 seq,
                 now_ms());
    } else if (strcmp(msg_type, "recent_events") == 0) {
        char events_json[1200];
        brain_ws_write_recent_events(events_json, sizeof(events_json));
        snprintf(response,
                 sizeof(response),
                 "{\"ok\":true,\"protocol\":\"" ATLAS_BRAIN_SESSION_PROTOCOL "\",\"type\":\"recent_events\","
                 "\"stage\":\"" ATLAS_BRAIN_SESSION_STAGE "\",\"session_id\":\"dualeye-ws-%" PRIu32 "\","
                 "\"seq\":%" PRIu32 ",\"events\":%s}",
                 s_brain_ws_session_seq,
                 seq,
                 events_json);
    } else if (strcmp(msg_type, "status") == 0) {
        snprintf(response,
                 sizeof(response),
                 "{\"ok\":true,\"protocol\":\"" ATLAS_BRAIN_SESSION_PROTOCOL "\",\"type\":\"status\","
                 "\"stage\":\"" ATLAS_BRAIN_SESSION_STAGE "\",\"session_id\":\"dualeye-ws-%" PRIu32 "\","
                 "\"seq\":%" PRIu32 ",\"state\":%s}",
                 s_brain_ws_session_seq,
                 seq,
                 state_json);
    } else {
        snprintf(response,
                 sizeof(response),
                 "{\"ok\":true,\"protocol\":\"" ATLAS_BRAIN_SESSION_PROTOCOL "\",\"type\":\"hello\","
                 "\"stage\":\"" ATLAS_BRAIN_SESSION_STAGE "\",\"session_id\":\"dualeye-ws-%" PRIu32 "\","
                 "\"transport\":\"websocket\",\"seq\":%" PRIu32 ","
                 "\"features\":{\"scene\":true,\"runtime\":true,\"wav_turn\":true,"
                 "\"json_events\":true,\"event_push\":true,\"recent_events\":true,"
                 "\"audio_stream\":true,\"opus_stream\":true,\"binary_audio\":true,\"mcp\":false,\"wake_aec\":false},"
                 "\"audio_params\":{\"mode\":\"opus_ws\",\"format\":\"opus\",\"sample_rate\":16000,\"channels\":1,"
                 "\"frame_duration_ms\":60,\"binary_header\":\"AOP1\",\"opus_probe\":true,\"opus_streaming\":true},"
                 "\"unsupported\":[\"streaming_tts_playback\",\"wakenet\",\"aec\",\"device_mcp\"],"
                 "\"endpoints\":{\"turn\":\"/api/voice/turn\",\"opus_probe\":\"/api/audio/opus-probe\","
                 "\"opus_stream_start\":\"/api/audio/opus-stream/start\",\"opus_stream_stop\":\"/api/audio/opus-stream/stop\","
                 "\"opus_stream_status\":\"/api/audio/opus-stream/status\","
                 "\"sr_status\":\"/api/sr/status\",\"diagnostics\":\"/api/diagnostics/turn\"},"
                 "\"received_type\":\"%s\",\"received\":\"%s\",\"state\":%s}",
                 s_brain_ws_session_seq,
                 seq,
                 msg_type,
                 escaped,
                 state_json);
    }
    if (root != NULL) {
        cJSON_Delete(root);
    }

    httpd_ws_frame_t reply = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)response,
        .len = strlen(response),
    };
    return httpd_ws_send_frame(req, &reply);
}

static esp_err_t register_ws_uri(httpd_handle_t server, const char *uri, esp_err_t (*handler)(httpd_req_t *))
{
    const httpd_uri_t route = {
        .uri = uri,
        .method = HTTP_GET,
        .handler = handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };
    return httpd_register_uri_handler(server, &route);
}
#endif

static esp_err_t system_info_handler(httpd_req_t *req)
{
    atlas_wifi_status_t wifi;
    atlas_wifi_get_status(&wifi);

    atlas_audio_status_t audio;
    atlas_audio_get_status(&audio);

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    char firmware[64];
    char wifi_mode[32];
    char wifi_sta_ip[ATLAS_WIFI_IP_MAX * 2];
    char wifi_ap_ip[ATLAS_WIFI_IP_MAX * 2];
    char audio_error[32];
    json_escape(firmware, sizeof(firmware), atlas_firmware_build_tag());
    json_escape(wifi_mode, sizeof(wifi_mode), atlas_wifi_mode_name(wifi.mode));
    json_escape(wifi_sta_ip, sizeof(wifi_sta_ip), wifi.sta_ip);
    json_escape(wifi_ap_ip, sizeof(wifi_ap_ip), wifi.ap_ip);
    json_escape(audio_error, sizeof(audio_error), esp_err_to_name(audio.last_error));

    char runtime_json[1900];
    atlas_runtime_write_json(runtime_json, sizeof(runtime_json));
    char audio_service_json[1400];
    atlas_audio_service_write_json(audio_service_json, sizeof(audio_service_json));
    char sr_json[1400];
    atlas_sr_probe_write_json(sr_json, sizeof(sr_json));

    const size_t json_size = 7500;
    char *json = (char *)calloc(1, json_size);
    if (json == NULL) {
        return send_error(req, "500 Internal Server Error", "no memory");
    }

    snprintf(json,
             json_size,
             "{"
             "\"ok\":true,"
             "\"device_model\":\"waveshare-dualeye-s3-1.28\","
             "\"project\":\"atlas-rover-mk1\","
             "\"firmware\":\"%s\","
             ATLAS_BUILD_FINGERPRINT_JSON ","
             "\"build\":{\"date\":\"%s\",\"time\":\"%s\",\"motion_supported\":%s,\"ota_supported\":false},"
             "\"chip\":{\"model\":%d,\"cores\":%d,\"revision\":%d,\"features\":%u},"
             "\"memory\":{\"free_heap\":%u,\"min_free_heap\":%u,\"free_spiram\":%u},"
             "\"storage\":{\"app_partition\":\"factory\",\"assets_partition\":\"storage_spiffs\",\"assets_version\":\"" ATLAS_RESOURCE_VERSION "\",\"font\":\"" ATLAS_FONT_VERSION "\"},"
             "\"wifi\":{\"mode\":\"%s\",\"sta_connected\":%s,\"sta_ip\":\"%s\",\"ap_started\":%s,\"ap_ip\":\"%s\"},"
             "\"audio\":{\"input_ready\":%s,\"output_ready\":%s,\"sample_rate\":%" PRIu16 ",\"last_error\":\"%s\"},"
             "\"audio_service\":%s,"
             "\"speech_recognition\":%s,"
             "\"runtime\":%s"
             "}",
             firmware,
             firmware,
             __DATE__,
             __TIME__,
             json_bool(atlas_config_motion_supported()),
             (int)chip.model,
             chip.cores,
             chip.revision,
             (unsigned)chip.features,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             wifi_mode,
             wifi.sta_connected ? "true" : "false",
             wifi_sta_ip,
             wifi.ap_started ? "true" : "false",
             wifi_ap_ip,
             json_bool(audio.input_ready),
             json_bool(audio.output_ready),
             audio.sample_rate,
             audio_error,
             audio_service_json,
             sr_json,
             runtime_json);

    const esp_err_t ret = send_json(req, json);
    free(json);
    return ret;
}

static esp_err_t ota_status_handler(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    const esp_partition_t *ota_0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *ota_1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    const bool ota_ready = ota_0 != NULL && ota_1 != NULL && next != NULL;
    const char *running_label = running == NULL ? "unknown" : running->label;
    const char *next_label = next == NULL ? "" : next->label;
    char firmware[64];
    json_escape(firmware, sizeof(firmware), atlas_firmware_build_tag());

    char json[760];
    snprintf(json,
             sizeof(json),
             "{"
             "\"ok\":true,"
             "\"supported\":%s,"
             "\"device_model\":\"waveshare-dualeye-s3-1.28\","
             "\"project\":\"atlas-rover-mk1\","
             "\"firmware\":\"%s\","
             "\"current_slot\":\"%s\","
             "\"next_slot\":\"%s\","
             "\"partition_mode\":\"dual_ota_app\","
             "\"manifest_supported\":true,"
             "\"apply_endpoint\":\"%s\","
             "\"app_ota_supported\":%s,"
             "\"full_image_ota_supported\":false,"
             "\"reason\":\"%s\""
             "}",
             json_bool(ota_ready),
             firmware,
             running_label,
             next_label,
             ota_ready ? "/api/ota/apply" : "",
             json_bool(ota_ready),
             ota_ready ? "app OTA slot ready; partition/storage updates still require USB full flash" :
                         "OTA slots missing; full USB flash required");
    return send_json(req, json);
}

static esp_err_t ota_manifest_handler(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    const esp_partition_t *ota_0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *ota_1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    const bool ota_ready = ota_0 != NULL && ota_1 != NULL && next != NULL;
    char firmware[64];
    json_escape(firmware, sizeof(firmware), atlas_firmware_build_tag());

    char json[2200];
    snprintf(json,
             sizeof(json),
             "{"
             "\"ok\":true,"
             "\"protocol\":\"atlas.ota.manifest.v0\","
             "\"status\":\"%s\","
             "\"supported\":%s,"
             "\"reason\":\"%s\","
             "\"device_model\":\"waveshare-dualeye-s3-1.28\","
             "\"project\":\"atlas-rover-mk1\","
             "\"firmware\":\"%s\","
             ATLAS_BUILD_FINGERPRINT_JSON ","
             "\"slots\":{\"current\":\"%s\",\"next\":\"%s\",\"rollback\":false,\"ota_0\":%s,\"ota_1\":%s},"
             "\"packages\":["
             "{\"name\":\"bootloader\",\"offset\":\"0x0\",\"required\":true,\"source\":\"firmware/dualeye/build/bootloader/bootloader.bin\"},"
             "{\"name\":\"partition_table\",\"offset\":\"0x8000\",\"required\":true,\"source\":\"firmware/dualeye/build/partition_table/partition-table.bin\"},"
             "{\"name\":\"sr_model\",\"offset\":\"0x10000\",\"required\":true,\"source\":\"firmware/dualeye/build/srmodels/srmodels.bin\"},"
             "{\"name\":\"app_ota\",\"offset\":\"next_slot\",\"required\":true,\"source\":\"firmware/dualeye/build/atlas_rover_dualeye.bin\",\"apply_endpoint\":\"/api/ota/apply\"},"
             "{\"name\":\"spiffs_storage\",\"offset\":\"0xB00000\",\"required\":true,\"source\":\"firmware/dualeye/build/storage.bin\"}"
             "],"
             "\"flash_command\":\"idf.py -p /dev/cu.usbmodem101 flash\","
             "\"apply_endpoint\":\"%s\","
             "\"notes\":[\"/api/ota/apply 只更新 app OTA slot\",\"bootloader/partition/model/storage 变化仍需 USB 全量刷\",\"WakeNet 模型放入 model 分区后再启用 P3\"]"
             "}",
             ota_ready ? "app_ota_ready" : "usb_flash_required",
             json_bool(ota_ready),
             ota_ready ? "dual OTA slots present" : "OTA slots missing",
             firmware,
             firmware,
             running == NULL ? "unknown" : running->label,
             next == NULL ? "" : next->label,
             json_bool(ota_0 != NULL),
             json_bool(ota_1 != NULL),
             ota_ready ? "/api/ota/apply" : "");
    return send_json(req, json);
}

static esp_err_t ota_packages_handler(httpd_req_t *req)
{
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    const bool ota_ready = next != NULL;
    char json[920];
    snprintf(json,
             sizeof(json),
             "{"
             "\"ok\":true,"
             "\"protocol\":\"atlas.ota.packages.v0\","
             "\"status\":\"%s\","
             "\"package_count\":5,"
             "\"packages\":["
             "{\"name\":\"bootloader\",\"offset\":\"0x0\",\"path\":\"firmware/dualeye/build/bootloader/bootloader.bin\"},"
             "{\"name\":\"partition_table\",\"offset\":\"0x8000\",\"path\":\"firmware/dualeye/build/partition_table/partition-table.bin\"},"
             "{\"name\":\"sr_model\",\"offset\":\"0x10000\",\"path\":\"firmware/dualeye/build/srmodels/srmodels.bin\",\"required\":true},"
             "{\"name\":\"app_ota\",\"offset\":\"%s\",\"path\":\"firmware/dualeye/build/atlas_rover_dualeye.bin\",\"apply_supported\":%s},"
             "{\"name\":\"spiffs_storage\",\"offset\":\"0xB00000\",\"path\":\"firmware/dualeye/build/storage.bin\"}"
             "],"
             "\"apply_supported\":%s,"
             "\"apply_endpoint\":\"%s\""
             "}",
             ota_ready ? "app_ota_ready" : "usb_flash_required",
             next == NULL ? "next_slot" : next->label,
             json_bool(ota_ready),
             json_bool(ota_ready),
             ota_ready ? "/api/ota/apply" : "");
    return send_json(req, json);
}

static esp_err_t ota_apply_handler(httpd_req_t *req)
{
    char body[640];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char url[360] = "";
    char value[16] = "";
    bool reboot = false;
    (void)form_get_value(body, "url", url, sizeof(url));
    if (form_get_value(body, "reboot", value, sizeof(value))) {
        (void)parse_bool_flag(value, &reboot);
    }
    if (url[0] == '\0') {
        return send_error(req, "400 Bad Request", "url required");
    }
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        return send_error(req, "400 Bad Request", "http or https url required");
    }

    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (update == NULL) {
        return send_error(req, "409 Conflict", "no OTA update partition");
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return send_error(req, "500 Internal Server Error", "http client init failed");
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return send_error(req, "502 Bad Gateway", esp_err_to_name(err));
    }

    const int64_t content_length = esp_http_client_fetch_headers(client);
    if (content_length > 0 && (uint64_t)content_length > update->size) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return send_error(req, "413 Payload Too Large", "image larger than OTA partition");
    }

    esp_ota_handle_t ota = 0;
    err = esp_ota_begin(update, OTA_SIZE_UNKNOWN, &ota);
    if (err != ESP_OK) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }

    uint8_t *buffer = (uint8_t *)malloc(4096);
    if (buffer == NULL) {
        (void)esp_ota_abort(ota);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return send_error(req, "500 Internal Server Error", "ota buffer alloc failed");
    }

    size_t written = 0;
    while (true) {
        const int read_len = esp_http_client_read(client, (char *)buffer, 4096);
        if (read_len < 0) {
            err = ESP_FAIL;
            break;
        }
        if (read_len == 0) {
            break;
        }
        err = esp_ota_write(ota, buffer, (size_t)read_len);
        if (err != ESP_OK) {
            break;
        }
        written += (size_t)read_len;
        if (written > update->size) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
    }
    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        (void)esp_ota_abort(ota);
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    err = esp_ota_end(ota);
    if (err != ESP_OK) {
        return send_error(req, "400 Bad Request", esp_err_to_name(err));
    }
    err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }

    char safe_url[420];
    json_escape(safe_url, sizeof(safe_url), url);
    char response[760];
    snprintf(response,
             sizeof(response),
             "{\"ok\":true,\"protocol\":\"atlas.ota.apply.v0\","
             "\"written\":%u,\"content_length\":%lld,"
             "\"target_slot\":\"%s\",\"reboot_required\":true,\"rebooting\":%s,"
             "\"source\":\"%s\"}",
             (unsigned)written,
             (long long)content_length,
             update->label,
             json_bool(reboot),
             safe_url);
    const esp_err_t send_err = send_json(req, response);
    if (send_err == ESP_OK && reboot) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    return send_err;
}

static esp_err_t selftest_handler(httpd_req_t *req)
{
    atlas_wifi_status_t wifi;
    atlas_wifi_get_status(&wifi);

    atlas_llm_status_t llm;
    atlas_llm_client_get_status(s_ctx.config, &llm);

    atlas_brain_ws_status_t brain_ws;
    atlas_brain_ws_client_get_status(&brain_ws);

    atlas_audio_status_t audio;
    atlas_audio_get_status(&audio);

    atlas_audio_service_status_t audio_service;
    atlas_audio_service_get_status(&audio_service);

    size_t spiffs_total = 0;
    size_t spiffs_used = 0;
    const esp_err_t spiffs_err = esp_spiffs_info("storage", &spiffs_total, &spiffs_used);
    const bool storage_ok = spiffs_err == ESP_OK && spiffs_total > 0;
    atlas_common_assets_probe_t asset_probe;
    atlas_common_assets_probe(&asset_probe);
    const bool assets_pass = atlas_common_assets_probe_pass(&asset_probe, storage_ok);
    const bool assets_warn = atlas_common_assets_probe_warn(&asset_probe, storage_ok);
    const bool wifi_pass = wifi.sta_connected;
    const bool wifi_warn = !wifi.sta_connected && wifi.ap_started;
    const bool brain_pass = llm.configured && strcmp(llm.mode, "host") == 0 && llm.base_url[0] != '\0';
    const bool brain_ws_expected = brain_pass && wifi.sta_connected;
    const bool brain_ws_pass = !brain_ws_expected || brain_ws.connected;
    const bool audio_pass = audio.input_ready && audio.output_ready;
    const bool audio_warn = audio.initialized || audio.input_ready || audio.output_ready;
    const bool audio_service_pass = audio_service.initialized && audio_service.worker_started;
    const bool memory_pass = esp_get_free_heap_size() > 90000u;
    const bool memory_warn = esp_get_free_heap_size() > 50000u;
    const bool motion_pass = !atlas_config_motion_supported() && !s_ctx.config->safety.motion_enabled;
    const bool desk_apps_pass = s_ctx.config->calendar.enabled && s_ctx.config->pomodoro.enabled;
    const bool ota_slots_pass =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL) != NULL &&
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL) != NULL &&
        esp_ota_get_next_update_partition(NULL) != NULL;

    const atlas_common_check_status_t storage_check =
        atlas_common_check_status_from_flags(storage_ok, false);
    const atlas_common_check_status_t assets_check =
        atlas_common_check_status_from_flags(assets_pass, assets_warn);
    const atlas_common_check_status_t wifi_check =
        atlas_common_check_status_from_flags(wifi_pass, wifi_warn);
    const atlas_common_check_status_t brain_check =
        atlas_common_check_status_from_flags(brain_pass, true);
    const atlas_common_check_status_t audio_check =
        atlas_common_check_status_from_flags(audio_pass, audio_warn);
    const atlas_common_check_status_t audio_service_check =
        atlas_common_check_status_from_flags(audio_service_pass, false);
    const atlas_common_check_status_t brain_ws_check =
        atlas_common_check_status_from_flags(brain_ws_pass, true);
    const atlas_common_check_status_t sr_probe_check = ATLAS_COMMON_CHECK_WARN;
    const atlas_common_check_status_t ota_slots_check =
        atlas_common_check_status_from_flags(ota_slots_pass, false);
    const atlas_common_check_status_t desk_apps_check =
        atlas_common_check_status_from_flags(desk_apps_pass, true);
    const atlas_common_check_status_t memory_check =
        atlas_common_check_status_from_flags(memory_pass, memory_warn);
    const atlas_common_check_status_t motion_check =
        atlas_common_check_status_from_flags(motion_pass, true);

    atlas_common_selftest_summary_t summary;
    atlas_common_selftest_summary_reset(&summary);
    atlas_common_selftest_summary_count(&summary, ATLAS_COMMON_CHECK_PASS);  // firmware fingerprint
    atlas_common_selftest_summary_count(&summary, storage_check);
    atlas_common_selftest_summary_count(&summary, assets_check);
    atlas_common_selftest_summary_count(&summary, wifi_check);
    atlas_common_selftest_summary_count(&summary, brain_check);
    atlas_common_selftest_summary_count(&summary, audio_check);
    atlas_common_selftest_summary_count(&summary, audio_service_check);
    atlas_common_selftest_summary_count(&summary, brain_ws_check);
    atlas_common_selftest_summary_count(&summary, ATLAS_COMMON_CHECK_PASS);  // opus probe endpoint
    atlas_common_selftest_summary_count(&summary, ATLAS_COMMON_CHECK_PASS);  // opus websocket stream endpoint
    atlas_common_selftest_summary_count(&summary, sr_probe_check);           // WakeNet/AEC probe is pending
    atlas_common_selftest_summary_count(&summary, ATLAS_COMMON_CHECK_PASS);  // tool schema version
    atlas_common_selftest_summary_count(&summary, ota_slots_check);
    atlas_common_selftest_summary_count(&summary, desk_apps_check);
    atlas_common_selftest_summary_count(&summary, memory_check);
    atlas_common_selftest_summary_count(&summary, motion_check);

    const char *storage_status = atlas_common_check_status_name(storage_check);
    const char *assets_status = atlas_common_check_status_name(assets_check);
    const char *wifi_status = atlas_common_check_status_name(wifi_check);
    const char *brain_status = atlas_common_check_status_name(brain_check);
    const char *audio_status = atlas_common_check_status_name(audio_check);
    const char *audio_service_status = atlas_common_check_status_name(audio_service_check);
    const char *brain_ws_status = atlas_common_check_status_name(brain_ws_check);
    const char *ota_slots_status = atlas_common_check_status_name(ota_slots_check);
    const char *desk_apps_status = atlas_common_check_status_name(desk_apps_check);
    const char *memory_status = atlas_common_check_status_name(memory_check);
    const char *motion_status = atlas_common_check_status_name(motion_check);

    char firmware[64];
    char wifi_mode[32];
    char wifi_sta_ip[ATLAS_WIFI_IP_MAX * 2];
    char wifi_ap_ip[ATLAS_WIFI_IP_MAX * 2];
    char brain_mode[ATLAS_LLM_MODE_MAX * 2];
    char brain_url[ATLAS_LLM_BASE_URL_MAX * 2];
    char brain_ws_stage[sizeof(brain_ws.stage) * 2];
    char brain_ws_url[sizeof(brain_ws.url) * 2];
    char audio_error[32];
    char spiffs_error[32];
    char service_failure[160];
    json_escape(firmware, sizeof(firmware), atlas_firmware_build_tag());
    json_escape(wifi_mode, sizeof(wifi_mode), atlas_wifi_mode_name(wifi.mode));
    json_escape(wifi_sta_ip, sizeof(wifi_sta_ip), wifi.sta_ip);
    json_escape(wifi_ap_ip, sizeof(wifi_ap_ip), wifi.ap_ip);
    json_escape(brain_mode, sizeof(brain_mode), llm.mode);
    json_escape(brain_url, sizeof(brain_url), llm.base_url);
    json_escape(brain_ws_stage, sizeof(brain_ws_stage), brain_ws.stage);
    json_escape(brain_ws_url, sizeof(brain_ws_url), brain_ws.url);
    json_escape(audio_error, sizeof(audio_error), esp_err_to_name(audio.last_error));
    json_escape(spiffs_error, sizeof(spiffs_error), esp_err_to_name(spiffs_err));
    json_escape(service_failure,
                sizeof(service_failure),
                audio_service.last_failure[0] == '\0' ? "none" : audio_service.last_failure);

    const size_t json_size = 6200;
    char *json = (char *)calloc(1, json_size);
    if (json == NULL) {
        return send_error(req, "500 Internal Server Error", "no memory");
    }

    snprintf(json,
             json_size,
             "{"
             "\"ok\":true,"
             "\"ready_to_flash\":%s,"
             ATLAS_BUILD_FINGERPRINT_JSON ","
             "\"summary\":{\"pass\":%u,\"warn\":%u,\"fail\":%u},"
             "\"checks\":["
             "{\"id\":\"firmware_fingerprint\",\"status\":\"pass\",\"detail\":\"version " ATLAS_FIRMWARE_VERSION "\"},"
             "{\"id\":\"spiffs_storage\",\"status\":\"%s\",\"detail\":\"total=%u used=%u err=%s\"},"
             "{\"id\":\"eye_assets\",\"status\":\"%s\",\"detail\":\"manifest=%s pet=%s goggle=%s tomoe=%s no_smoking=%s pet_head_manifest=%s pet_head_idle=%s pet_head_speak=%s pet_head_views=%s pet_head_turn=%s version=" ATLAS_RESOURCE_VERSION "\"},"
             "{\"id\":\"wifi\",\"status\":\"%s\",\"detail\":\"mode=%s sta=%s sta_ip=%s ap=%s ap_ip=%s\"},"
             "{\"id\":\"mac_brain_config\",\"status\":\"%s\",\"detail\":\"mode=%s base_url=%s configured=%s\"},"
             "{\"id\":\"audio_hw\",\"status\":\"%s\",\"detail\":\"input=%s output=%s err=%s\"},"
             "{\"id\":\"audio_service\",\"status\":\"%s\",\"detail\":\"worker=%s job_running=%s last_failure=%s\"},"
             "{\"id\":\"brain_ws\",\"status\":\"%s\",\"detail\":\"protocol=" ATLAS_BRAIN_SESSION_PROTOCOL " client=%s connected=%s stage=%s url=%s sent=%" PRIu32 "\"},"
             "{\"id\":\"opus_probe\",\"status\":\"pass\",\"detail\":\"/api/audio/opus-probe " ATLAS_OPUS_PROBE_VERSION "\"},"
             "{\"id\":\"opus_stream\",\"status\":\"pass\",\"detail\":\"/api/audio/opus-stream/start AOP1 binary websocket frames\"},"
             "{\"id\":\"sr_probe\",\"status\":\"warn\",\"detail\":\"" ATLAS_SR_PROBE_STAGE "; WakeNet/AEC not enabled yet\"},"
             "{\"id\":\"tool_schema\",\"status\":\"pass\",\"detail\":\"" ATLAS_TOOL_SCHEMA_VERSION "\"},"
             "{\"id\":\"ota_slots\",\"status\":\"%s\",\"detail\":\"ota_0/ota_1=%s apply=/api/ota/apply full_image=false\"},"
             "{\"id\":\"desk_apps\",\"status\":\"%s\",\"detail\":\"clock=true calendar=%s pomodoro=%s protocol=atlas.desk_apps.v0\"},"
             "{\"id\":\"memory\",\"status\":\"%s\",\"detail\":\"free_heap=%u min_free=%u free_spiram=%u\"},"
             "{\"id\":\"motion_boundary\",\"status\":\"%s\",\"detail\":\"motion_supported=%s motion_enabled=%s\"}"
             "],"
             "\"manual_tests\":[\"/api/status/lite\",\"/api/tools/list\",\"/api/tools/call\",\"/api/ota/status\",\"/api/ota/manifest\",\"/api/ota/apply\",\"/api/audio/beep\",\"/api/audio/mic-level\",\"/api/audio/opus-probe\",\"/api/audio/opus-stream/start\",\"/api/audio/opus-stream/status\",\"/api/audio/opus-stream/stop\",\"/api/voice/turn?async=1\"],"
             "\"next_steps\":[\"确认 fail=0\",\"Mac 运行 tools/check_atlas_preflash.py\",\"烧录后再次打开 /api/selftest 与 Mac 验收页\"]"
             "}",
             json_bool(atlas_common_selftest_ready(&summary)),
             firmware,
             summary.pass,
             summary.warn,
             summary.fail,
             storage_status,
             (unsigned)spiffs_total,
             (unsigned)spiffs_used,
             spiffs_error,
             assets_status,
             json_bool(asset_probe.eye_manifest),
             json_bool(asset_probe.eye_pet_idle),
             json_bool(asset_probe.eye_goggle_idle),
             json_bool(asset_probe.eye_tomoe_idle),
             json_bool(asset_probe.eye_no_smoking_idle),
             json_bool(asset_probe.pet_head_manifest),
             json_bool(asset_probe.pet_head_idle),
             json_bool(asset_probe.pet_head_speak),
             json_bool(asset_probe.pet_head_view),
             json_bool(asset_probe.pet_head_turn),
             wifi_status,
             wifi_mode,
             json_bool(wifi.sta_connected),
             wifi_sta_ip,
             json_bool(wifi.ap_started),
             wifi_ap_ip,
             brain_status,
             brain_mode,
             brain_url,
             json_bool(llm.configured),
             audio_status,
             json_bool(audio.input_ready),
             json_bool(audio.output_ready),
             audio_error,
             audio_service_status,
             json_bool(audio_service.worker_started),
             json_bool(audio_service.job_running),
             service_failure,
             brain_ws_status,
             json_bool(brain_ws.enabled),
             json_bool(brain_ws.connected),
             brain_ws_stage,
             brain_ws_url,
             brain_ws.messages_sent,
             ota_slots_status,
             json_bool(ota_slots_pass),
             desk_apps_status,
             json_bool(s_ctx.config->calendar.enabled),
             json_bool(s_ctx.config->pomodoro.enabled),
             memory_status,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             motion_status,
             json_bool(atlas_config_motion_supported()),
             json_bool(s_ctx.config->safety.motion_enabled));

    const esp_err_t ret = send_json(req, json);
    free(json);
    return ret;
}

static const cJSON *tool_args_object(const cJSON *root)
{
    const cJSON *args = cJSON_GetObjectItemCaseSensitive(root, "arguments");
    if (!cJSON_IsObject(args)) {
        args = cJSON_GetObjectItemCaseSensitive(root, "args");
    }
    if (!cJSON_IsObject(args)) {
        args = cJSON_GetObjectItemCaseSensitive(root, "input");
    }
    return cJSON_IsObject(args) ? args : root;
}

static bool cjson_string_to_buffer(const cJSON *object, const char *key, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(value) || value->valuestring == NULL) {
        return false;
    }
    strlcpy(out, value->valuestring, out_size);
    return true;
}

static bool authorize_tool_body(const char *body, const cJSON *root)
{
    if (root != NULL) {
        char pin[16] = "";
        if (cjson_string_to_buffer(root, "pin", pin, sizeof(pin))) {
            trim_assign(pin);
            return atlas_pairing_authorize_pin(pin);
        }
    }
    return authorize_body(body);
}

static esp_err_t tools_list_handler(httpd_req_t *req)
{
    const char json[] =
        "{"
        "\"ok\":true,"
        "\"protocol\":\"" ATLAS_TOOL_SCHEMA_VERSION "\","
        "\"mcp_like\":true,"
        "\"tool_count\":15,"
        "\"call_endpoint\":\"/api/tools/call\","
        "\"tools\":["
        "{\"name\":\"atlas.show_page\",\"description\":\"切换 DualEye 页面\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"page\":{\"type\":\"string\",\"enum\":[\"eyes\",\"clock\",\"status\",\"voice\",\"music\",\"story\",\"chat\",\"calendar\",\"pomodoro\"]}},\"required\":[\"page\"]}},"
        "{\"name\":\"atlas.set_expression\",\"description\":\"切换表情\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"expression\":{\"type\":\"string\"}},\"required\":[\"expression\"]}},"
        "{\"name\":\"atlas.set_theme\",\"description\":\"切换并保存双眼主题\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"theme\":{\"type\":\"string\"}},\"required\":[\"theme\"]}},"
        "{\"name\":\"atlas.role.switch\",\"description\":\"联动角色、主题、表情和页面\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"role\":{\"type\":\"string\",\"enum\":[\"pet\",\"raptor\",\"mecha\",\"goggle\"]}},\"required\":[\"role\"]}},"
        "{\"name\":\"atlas.clock.show\",\"description\":\"打开桌面时钟\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.clock.sync\",\"description\":\"校准时钟\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"epoch_ms\":{\"type\":\"integer\"}}}},"
        "{\"name\":\"atlas.calendar.show\",\"description\":\"打开日历\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.calendar.today\",\"description\":\"显示今日日历\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"title\":{\"type\":\"string\"},\"note\":{\"type\":\"string\"}}}},"
        "{\"name\":\"atlas.calendar.set_note\",\"description\":\"设置日历便签\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"title\":{\"type\":\"string\"},\"note\":{\"type\":\"string\"}},\"required\":[\"note\"]}},"
        "{\"name\":\"atlas.pomodoro.show\",\"description\":\"打开番茄专注\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.pomodoro.start\",\"description\":\"开始番茄专注\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"task_name\":{\"type\":\"string\"},\"focus_minutes\":{\"type\":\"integer\"},\"break_minutes\":{\"type\":\"integer\"}}}},"
        "{\"name\":\"atlas.pomodoro.stop\",\"description\":\"停止番茄专注\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.pet.event\",\"description\":\"触发电子宠物事件\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"event\":{\"type\":\"string\"}},\"required\":[\"event\"]}},"
        "{\"name\":\"atlas.audio.opus_stream.status\",\"description\":\"读取 OPUS 真流状态\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.ota.check\",\"description\":\"读取固件包/OTA manifest\",\"inputSchema\":{\"type\":\"object\"}}"
        "],"
        "\"notes\":\"固件侧 Tool Schema V0 只执行桌面应用和诊断工具；运动工具已从当前桌面版本边界移除。\""
        "}";
    return send_json(req, json);
}

static esp_err_t apply_role_switch(const cJSON *args, char *result, size_t result_size)
{
    char role[24] = "";
    if (!cjson_string_to_buffer(args, "role", role, sizeof(role))) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *theme = "";
    atlas_page_t page = ATLAS_PAGE_EYES;
    atlas_expression_t expr = ATLAS_EXPR_IDLE;
    if (strcmp(role, "pet") == 0 || strcmp(role, "电子宠物") == 0) {
        theme = "pet";
        page = ATLAS_PAGE_CHAT;
        expr = ATLAS_EXPR_HAPPY;
    } else if (strcmp(role, "raptor") == 0 || strcmp(role, "猛禽") == 0) {
        theme = "raptor";
        page = ATLAS_PAGE_EYES;
        expr = ATLAS_EXPR_CURIOUS;
    } else if (strcmp(role, "mecha") == 0 || strcmp(role, "机械") == 0 || strcmp(role, "机械电子") == 0) {
        theme = "mecha";
        page = ATLAS_PAGE_STATUS;
        expr = ATLAS_EXPR_THINKING;
    } else if (strcmp(role, "goggle") == 0 || strcmp(role, "护目镜") == 0 || strcmp(role, "小黄人") == 0) {
        theme = "goggle";
        page = ATLAS_PAGE_EYES;
        expr = ATLAS_EXPR_HAPPY;
    } else {
        return ESP_ERR_NOT_FOUND;
    }

    atlas_ui_config_t ui_cfg = s_ctx.config->ui;
    strlcpy(ui_cfg.theme, theme, sizeof(ui_cfg.theme));
    esp_err_t err = atlas_config_save_ui(&ui_cfg);
    if (err == ESP_OK) {
        (void)atlas_config_load(s_ctx.config);
        atlas_display_set_theme(s_ctx.config->ui.theme);
        atlas_display_set_brightness(s_ctx.config->ui.brightness);
        (void)atlas_audio_set_volume(s_ctx.config->ui.volume);
        atlas_ui_apply_config(s_ctx.ui_state, s_ctx.config);
    }
    if (err != ESP_OK) {
        return err;
    }
    const uint32_t ts = now_ms();
    manual_ui_override(ts, "tool role switch");
    atlas_ui_lock();
    s_ctx.ui_state->page = page;
    s_ctx.ui_state->expression = expr;
    s_ctx.ui_state->audio_level = expr == ATLAS_EXPR_SPEAKING ? 58 : 0;
    s_ctx.ui_state->last_event_ms = ts;
    atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_INTERACTION, ts);
    atlas_ui_unlock();
    if (result != NULL && result_size > 0) {
        snprintf(result, result_size, "role=%s theme=%s", role, theme);
    }
    return ESP_OK;
}

static esp_err_t apply_theme_tool(const cJSON *args)
{
    char theme[ATLAS_UI_THEME_MAX] = "";
    if (!cjson_string_to_buffer(args, "theme", theme, sizeof(theme))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!atlas_expression_theme_is_valid(theme)) {
        return ESP_ERR_NOT_FOUND;
    }
    atlas_ui_config_t ui_cfg = s_ctx.config->ui;
    strlcpy(ui_cfg.theme, theme, sizeof(ui_cfg.theme));
    esp_err_t err = atlas_config_save_ui(&ui_cfg);
    if (err == ESP_OK) {
        (void)atlas_config_load(s_ctx.config);
        atlas_display_set_theme(s_ctx.config->ui.theme);
        atlas_display_set_brightness(s_ctx.config->ui.brightness);
        (void)atlas_audio_set_volume(s_ctx.config->ui.volume);
        atlas_ui_apply_config(s_ctx.ui_state, s_ctx.config);
        manual_ui_override(now_ms(), "tool set theme");
    }
    return err;
}

static esp_err_t tools_call_handler(httpd_req_t *req)
{
    char body[900];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    cJSON *root = cJSON_Parse(body);
    const bool json_body = cJSON_IsObject(root);
    if (!authorize_tool_body(body, json_body ? root : NULL)) {
        cJSON_Delete(root);
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char name[64] = "";
    if (json_body) {
        (void)cjson_string_to_buffer(root, "name", name, sizeof(name));
        if (name[0] == '\0') {
            (void)cjson_string_to_buffer(root, "tool", name, sizeof(name));
        }
    } else {
        (void)form_get_value(body, "name", name, sizeof(name));
        if (name[0] == '\0') {
            (void)form_get_value(body, "tool", name, sizeof(name));
        }
    }
    if (name[0] == '\0') {
        cJSON_Delete(root);
        return send_error(req, "400 Bad Request", "tool name required");
    }

    esp_err_t err = ESP_OK;
    char result[120] = "accepted";
    if (json_body && strcmp(name, "atlas.set_theme") == 0) {
        err = apply_theme_tool(tool_args_object(root));
        strlcpy(result, err == ESP_OK ? "theme saved" : esp_err_to_name(err), sizeof(result));
    } else if (json_body && strcmp(name, "atlas.role.switch") == 0) {
        err = apply_role_switch(tool_args_object(root), result, sizeof(result));
    } else if (json_body && strcmp(name, "atlas.clock.sync") == 0) {
        char epoch_ms[24] = "";
        const cJSON *args = tool_args_object(root);
        const cJSON *epoch = cJSON_GetObjectItemCaseSensitive(args, "epoch_ms");
        if (cJSON_IsNumber(epoch) && epoch->valuedouble > 0) {
            snprintf(epoch_ms, sizeof(epoch_ms), "%.0f", epoch->valuedouble);
        } else if (!cjson_string_to_buffer(args, "epoch_ms", epoch_ms, sizeof(epoch_ms))) {
            (void)cjson_string_to_buffer(args, "epoch", epoch_ms, sizeof(epoch_ms));
        }
        if (!set_clock_from_epoch_ms(epoch_ms)) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            manual_ui_override(now_ms(), "tool clock sync");
            ui_apply_page(ATLAS_PAGE_CLOCK, now_ms());
        }
    } else if (strcmp(name, "atlas.audio.opus_stream.status") == 0) {
        atlas_opus_stream_status_t status;
        atlas_opus_stream_get_status(&status);
        char stream_json[1200];
        atlas_opus_stream_write_status_json(&status, stream_json, sizeof(stream_json));
        char response[1500];
        snprintf(response,
                 sizeof(response),
                 "{\"ok\":true,\"tool\":\"%s\",\"result\":%s}",
                 name,
                 stream_json);
        cJSON_Delete(root);
        return send_json(req, response);
    } else if (strcmp(name, "atlas.ota.check") == 0) {
        cJSON_Delete(root);
        return ota_manifest_handler(req);
    } else if (json_body) {
        atlas_brain_intent_t intent;
        char error[96] = "";
        err = atlas_brain_intent_parse_json(body, &intent, error, sizeof(error));
        if (err == ESP_OK) {
            manual_ui_override(now_ms(), "tool call");
            err = atlas_brain_intent_apply_intent(s_ctx.config, s_ctx.ui_state, &intent, now_ms(), result, sizeof(result));
        } else if (error[0] != '\0') {
            strlcpy(result, error, sizeof(result));
        }
    } else {
        err = ESP_ERR_NOT_SUPPORTED;
        strlcpy(result, "json body required", sizeof(result));
    }

    cJSON_Delete(root);
    char safe_result[240];
    json_escape(safe_result, sizeof(safe_result), result);
    char safe_name[128];
    json_escape(safe_name, sizeof(safe_name), name);
    char response[760];
    snprintf(response,
             sizeof(response),
             "{\"ok\":%s,\"protocol\":\"" ATLAS_TOOL_SCHEMA_VERSION "\",\"tool\":\"%s\",\"result\":\"%s\",\"page\":\"%s\",\"expression\":\"%s\",\"error\":\"%s\"}",
             json_bool(err == ESP_OK),
             safe_name,
             safe_result,
             atlas_page_name(s_ctx.ui_state->page),
             atlas_expression_name(s_ctx.ui_state->expression),
             err == ESP_OK ? "" : esp_err_to_name(err));
    if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_set_status(req, "404 Not Found");
    } else if (err == ESP_ERR_NOT_SUPPORTED) {
        httpd_resp_set_status(req, "501 Not Implemented");
    } else if (err != ESP_OK) {
        httpd_resp_set_status(req, "409 Conflict");
    }
    return send_json(req, response);
}

static esp_err_t audio_status_handler(httpd_req_t *req)
{
    atlas_audio_status_t audio;
    atlas_audio_get_status(&audio);
    atlas_opus_stream_status_t stream;
    atlas_opus_stream_get_status(&stream);

    char error_name[32];
    json_escape(error_name, sizeof(error_name), esp_err_to_name(audio.last_error));
    char stream_json[1200];
    atlas_opus_stream_write_status_json(&stream, stream_json, sizeof(stream_json));

    char json[1800];
    snprintf(json,
             sizeof(json),
             "{"
             "\"ok\":true,"
             "\"initialized\":%s,\"i2c_ready\":%s,\"i2s_ready\":%s,\"input_ready\":%s,\"output_ready\":%s,"
             "\"sample_rate\":%" PRIu16 ",\"volume\":%" PRIu8 ","
             "\"mic_level\":%" PRIu8 ",\"mic_rms\":%" PRIu32 ",\"mic_peak\":%" PRIu32 ","
             "\"speaker_tests\":%" PRIu32 ",\"mic_tests\":%" PRIu32 ",\"last_error\":\"%s\","
             "\"opus_stream\":%s"
             "}",
             json_bool(audio.initialized),
             json_bool(audio.i2c_ready),
             json_bool(audio.i2s_ready),
             json_bool(audio.input_ready),
             json_bool(audio.output_ready),
             audio.sample_rate,
             audio.volume,
             audio.last_mic_level,
             audio.last_mic_rms,
             audio.last_mic_peak,
             audio.speaker_tests,
             audio.mic_tests,
             error_name,
             stream_json);
    return send_json(req, json);
}

static esp_err_t audio_beep_handler(httpd_req_t *req)
{
    char body[160];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char value[16] = "";
    uint16_t freq = 880;
    uint16_t duration = 260;
    uint8_t volume = s_ctx.config->ui.volume;
    if (form_get_value(body, "freq", value, sizeof(value))) {
        freq = (uint16_t)atoi(value);
    }
    if (form_get_value(body, "duration", value, sizeof(value))) {
        duration = (uint16_t)atoi(value);
    }
    if (form_get_value(body, "volume", value, sizeof(value))) {
        volume = (uint8_t)atoi(value);
    }

    const uint32_t ts = now_ms();
    ui_set_page_state(ATLAS_PAGE_EYES, ATLAS_EXPR_SPEAKING, 58, ts, ATLAS_PET_EVENT_SPEAK, true);

    voice_wake_mute_for((uint32_t)duration + ATLAS_VOICE_WAKE_PLAY_MUTE_MS);
    const esp_err_t err = atlas_audio_play_beep(freq, duration, volume);
    voice_wake_mute_for(ATLAS_VOICE_WAKE_PLAY_MUTE_MS);
    if (err != ESP_OK) {
        ui_set_expression_state(ATLAS_EXPR_ERROR, 0, now_ms());
        return send_error(req, "503 Service Unavailable", esp_err_to_name(err));
    }

    ui_finish_audio_state(ATLAS_EXPR_IDLE, now_ms());

    char json[160];
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"speaker\":\"beep\",\"freq\":%" PRIu16 ",\"duration_ms\":%" PRIu16 ",\"volume\":%" PRIu8 "}",
             freq,
             duration,
             volume);
    return send_json(req, json);
}

static esp_err_t audio_mic_level_handler(httpd_req_t *req)
{
    char body[128];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char value[16] = "";
    uint16_t duration = 500;
    if (form_get_value(body, "duration", value, sizeof(value))) {
        duration = (uint16_t)atoi(value);
    }

    const uint32_t ts = now_ms();
    ui_set_page_state(ATLAS_PAGE_VOICE, ATLAS_EXPR_LISTEN, 20, ts, ATLAS_PET_EVENT_VOICE_LISTEN, true);

    atlas_audio_mic_level_t level;
    const esp_err_t err = atlas_audio_measure_mic(duration, &level);
    if (err != ESP_OK) {
        ui_set_expression_state(ATLAS_EXPR_ERROR, 0, now_ms());
        return send_error(req, "503 Service Unavailable", esp_err_to_name(err));
    }

    ui_set_expression_state(ATLAS_EXPR_LISTEN, level.level, now_ms());
    char json[180];
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"mic\":\"level\",\"level\":%" PRIu8 ",\"rms\":%" PRIu32 ",\"peak\":%" PRIu32 ",\"samples\":%" PRIu32 "}",
             level.level,
             level.rms,
             level.peak,
             level.samples);
    return send_json(req, json);
}

static esp_err_t opus_probe_job(void *ctx)
{
    atlas_opus_probe_job_ctx_t *job = (atlas_opus_probe_job_ctx_t *)ctx;
    if (job == NULL || job->result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    atlas_audio_service_note_stage(ATLAS_AUDIO_SERVICE_MODE_RECORDING, "opus_probe_capture");
    brain_ws_emit_event("audio.opus_probe.started", "encode 60ms opus frames");
    const esp_err_t err = atlas_opus_probe_run(job->duration_ms, job->result);
    brain_ws_emit_event(err == ESP_OK ? "audio.opus_probe.done" : "audio.opus_probe.failed",
                        err == ESP_OK ? "opus probe encoded" : esp_err_to_name(err));
    return err;
}

static esp_err_t audio_opus_probe_handler(httpd_req_t *req)
{
    char body[160];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char value[16] = "";
    uint16_t duration = 600;
    if (form_get_value(body, "duration", value, sizeof(value))) {
        duration = (uint16_t)atoi(value);
    }
    if (duration < ATLAS_OPUS_PROBE_FRAME_MS) {
        duration = ATLAS_OPUS_PROBE_FRAME_MS;
    }
    if (duration > 3000u) {
        duration = 3000u;
    }

    const uint32_t ts = now_ms();
    ui_set_page_state(ATLAS_PAGE_VOICE, ATLAS_EXPR_LISTEN, 32, ts, ATLAS_PET_EVENT_VOICE_LISTEN, true);

    atlas_opus_probe_result_t probe;
    memset(&probe, 0, sizeof(probe));
    probe.last_error = ESP_OK;
    atlas_opus_probe_job_ctx_t job = {
        .duration_ms = duration,
        .result = &probe,
    };
    voice_wake_mute_for((uint32_t)duration + ATLAS_VOICE_WAKE_PLAY_MUTE_MS);
    const esp_err_t err = atlas_audio_service_run_turn(opus_probe_job, &job, 0);
    if (err != ESP_OK && probe.last_error == ESP_OK) {
        probe.last_error = err;
    }
    voice_wake_mute_for(ATLAS_VOICE_WAKE_PLAY_MUTE_MS);

    ui_finish_audio_state(err == ESP_OK ? ATLAS_EXPR_IDLE : ATLAS_EXPR_ERROR, now_ms());

    char probe_json[900];
    atlas_opus_probe_write_json(&probe, probe_json, sizeof(probe_json));
    char response[1080];
    snprintf(response,
             sizeof(response),
             "{\"ok\":%s,\"stage\":\"P2_opus_60ms_probe\",\"probe\":%s}",
             json_bool(err == ESP_OK),
             probe_json);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
    }
    return send_json(req, response);
}

static esp_err_t audio_opus_stream_status_handler(httpd_req_t *req)
{
    (void)req;
    atlas_opus_stream_status_t status;
    atlas_opus_stream_get_status(&status);

    char stream_json[1200];
    atlas_opus_stream_write_status_json(&status, stream_json, sizeof(stream_json));
    char response[1400];
    snprintf(response,
             sizeof(response),
             "{\"ok\":true,\"protocol\":\"" ATLAS_OPUS_STREAM_PROTOCOL "\","
             "\"stage\":\"P2_dualeye_ws_opus_stream\",\"binary_header\":\"AOP1\","
             "\"stream\":%s}",
             stream_json);
    return send_json(req, response);
}

static esp_err_t audio_opus_stream_start_handler(httpd_req_t *req)
{
    char body[420];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char url[192] = "";
    char value[32] = "";
    uint32_t duration_ms = 5000u;
    (void)form_get_value(body, "url", url, sizeof(url));
    if (form_get_value(body, "duration", value, sizeof(value)) ||
        form_get_value(body, "duration_ms", value, sizeof(value))) {
        duration_ms = (uint32_t)strtoul(value, NULL, 10);
    }
    if (url[0] == '\0') {
        if (strcmp(s_ctx.config->llm.mode, "host") != 0 || s_ctx.config->llm.base_url[0] == '\0') {
            return send_error(req, "409 Conflict", "host bridge not configured");
        }
        build_bridge_ws_url(url, sizeof(url), s_ctx.config->llm.base_url, "/ws/audio");
    }
    if (strncmp(url, "ws://", 5) != 0 && strncmp(url, "wss://", 6) != 0) {
        return send_error(req, "400 Bad Request", "ws url required");
    }

    const uint32_t ts = now_ms();
    ui_set_page_state(ATLAS_PAGE_VOICE, ATLAS_EXPR_LISTEN, 36, ts, ATLAS_PET_EVENT_VOICE_LISTEN, true);
    const uint32_t wake_pause_ms = duration_ms == 0u ? 10000u : duration_ms + ATLAS_VOICE_WAKE_PLAY_MUTE_MS;
    s_voice_wake_mute_until_ms = ts + wake_pause_ms;
    s_voice_wake_hit_count = 0;
    strlcpy(s_voice_wake_last_reason, "opus_stream", sizeof(s_voice_wake_last_reason));

    atlas_opus_stream_config_t config = {
        .url = url,
        .duration_ms = duration_ms,
    };
    const esp_err_t err = atlas_opus_stream_start(&config);
    brain_ws_emit_event(err == ESP_OK ? "audio.opus_stream.started" : "audio.opus_stream.start_failed",
                        err == ESP_OK ? url : esp_err_to_name(err));

    atlas_opus_stream_status_t status;
    atlas_opus_stream_get_status(&status);
    char stream_json[1200];
    atlas_opus_stream_write_status_json(&status, stream_json, sizeof(stream_json));
    char response[1400];
    snprintf(response,
             sizeof(response),
             "{\"ok\":%s,\"protocol\":\"" ATLAS_OPUS_STREAM_PROTOCOL "\","
             "\"stage\":\"P2_dualeye_ws_opus_stream\",\"stream\":%s}",
             json_bool(err == ESP_OK),
             stream_json);
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
    } else if (err != ESP_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
    }
    return send_json(req, response);
}

static esp_err_t audio_opus_stream_stop_handler(httpd_req_t *req)
{
    char body[128];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    const esp_err_t err = atlas_opus_stream_stop();
    brain_ws_emit_event(err == ESP_OK ? "audio.opus_stream.stopping" : "audio.opus_stream.stop_noop",
                        err == ESP_OK ? "stop requested" : esp_err_to_name(err));

    atlas_opus_stream_status_t status;
    atlas_opus_stream_get_status(&status);
    char stream_json[1200];
    atlas_opus_stream_write_status_json(&status, stream_json, sizeof(stream_json));
    char response[1400];
    snprintf(response,
             sizeof(response),
             "{\"ok\":%s,\"protocol\":\"" ATLAS_OPUS_STREAM_PROTOCOL "\","
             "\"stage\":\"P2_dualeye_ws_opus_stream\",\"stream\":%s}",
             json_bool(err == ESP_OK || err == ESP_ERR_INVALID_STATE),
             stream_json);
    return send_json(req, response);
}

static esp_err_t audio_play_url_handler(httpd_req_t *req)
{
    char body[512];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char url[220] = "";
    (void)form_get_value(body, "url", url, sizeof(url));
    if (url[0] == '\0') {
        if (strcmp(s_ctx.config->llm.mode, "host") != 0 || s_ctx.config->llm.base_url[0] == '\0') {
            return send_error(req, "409 Conflict", "host bridge not configured");
        }
        build_bridge_url(url, sizeof(url), s_ctx.config->llm.base_url, "/tts/latest.wav");
    }
    if (strncmp(url, "http://", 7) != 0) {
        return send_error(req, "400 Bad Request", "http wav url required");
    }

    const uint32_t ts = now_ms();
    ui_set_page_state(ATLAS_PAGE_CHAT, ATLAS_EXPR_SPEAKING, 58, ts, ATLAS_PET_EVENT_CHAT, true);

    size_t wav_len = 0;
    int http_status = 0;
    voice_wake_mute_for(ATLAS_VOICE_WAKE_PLAY_MUTE_MS);
    const esp_err_t err = play_wav_from_url(url, s_ctx.config->ui.volume, &wav_len, &http_status);
    voice_wake_mute_for(ATLAS_VOICE_WAKE_PLAY_MUTE_MS);
    ui_finish_audio_state(err == ESP_OK ? ATLAS_EXPR_IDLE : ATLAS_EXPR_ERROR, now_ms());
    if (err != ESP_OK) {
        return send_error(req, "502 Bad Gateway", esp_err_to_name(err));
    }

    char json[180];
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"played\":true,\"http_status\":%d,\"wav_bytes\":%u}",
             http_status,
             (unsigned)wav_len);
    return send_json(req, json);
}

static esp_err_t run_voice_turn(uint16_t duration, atlas_voice_turn_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));
    result->duration_ms = clamp_voice_duration(duration);
    result->play_err = ESP_ERR_NOT_FOUND;
    atlas_audio_service_note_turn();
    atlas_runtime_turn_begin("device_ws_turn", result->duration_ms, now_ms());
    brain_ws_emit_event("turn.started", "device_ws_turn");

    if (strcmp(s_ctx.config->llm.mode, "host") != 0 || s_ctx.config->llm.base_url[0] == '\0') {
        strlcpy(result->bridge_error, "brain_offline: host bridge not configured", sizeof(result->bridge_error));
        ui_show_brain_offline("未配置 Mac Atlas Brain 地址", now_ms());
        atlas_audio_service_note_failure(result->bridge_error, ESP_ERR_INVALID_STATE);
        brain_ws_emit_event("turn.failed", result->bridge_error);
        atlas_runtime_turn_finish(false, result->bridge_error, now_ms());
        return ESP_ERR_INVALID_STATE;
    }

    atlas_brain_ws_status_t ws_status;
    atlas_brain_ws_client_get_status(&ws_status);
    if (!ws_status.enabled || !ws_status.connected) {
        snprintf(result->bridge_error,
                 sizeof(result->bridge_error),
                 "brain_offline: ws %s",
                 ws_status.stage[0] == '\0' ? "not connected" : ws_status.stage);
        ui_show_brain_offline("Mac Brain WebSocket 未连接", now_ms());
        atlas_audio_service_note_failure(result->bridge_error, ESP_ERR_INVALID_STATE);
        brain_ws_emit_event("turn.failed", result->bridge_error);
        atlas_runtime_turn_finish(false, result->bridge_error, now_ms());
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t ts = now_ms();
    atlas_runtime_set_state(ATLAS_RUNTIME_STATE_RECORDING, "capture wav", ts);
    ui_set_page_state(ATLAS_PAGE_VOICE, ATLAS_EXPR_LISTEN, 26, ts, ATLAS_PET_EVENT_VOICE_LISTEN, true);

    uint8_t *wav = NULL;
    size_t wav_len = 0;
    atlas_audio_mic_level_t level;
    voice_wake_mute_for(result->duration_ms + ATLAS_VOICE_WAKE_PLAY_MUTE_MS);
    esp_err_t err = atlas_audio_service_capture_wav(result->duration_ms, &wav, &wav_len, &level);
    if (err != ESP_OK) {
        ui_set_expression_state(ATLAS_EXPR_ERROR, 0, now_ms());
        strlcpy(result->bridge_error, esp_err_to_name(err), sizeof(result->bridge_error));
        atlas_audio_service_note_failure(result->bridge_error, err);
        brain_ws_emit_event("turn.failed", result->bridge_error);
        atlas_runtime_turn_finish(false, result->bridge_error, now_ms());
        return err;
    }
    result->wav_len = wav_len;
    result->mic_level = level.level;
    result->mic_rms = level.rms;
    result->mic_peak = level.peak;
    atlas_runtime_turn_note_audio(wav_len, level.level, level.rms, level.peak, now_ms());
    brain_ws_emit_event("turn.audio_captured", "wav captured");

    ui_set_expression_state(ATLAS_EXPR_THINKING, level.level, now_ms());
    atlas_runtime_set_state(ATLAS_RUNTIME_STATE_TRANSCRIBING, "post wav to brain", now_ms());
    atlas_audio_service_note_stage(ATLAS_AUDIO_SERVICE_MODE_TRANSCRIBING, "post_wav_to_brain");
    brain_ws_emit_event("turn.transcribing", "post wav to brain");

    char *bridge_response = NULL;
    size_t bridge_response_len = 0;
    uint8_t *tts_wav = NULL;
    size_t tts_wav_inline_len = 0;
    int bridge_status = 0;
    err = atlas_brain_ws_client_turn_wav(wav,
                                         wav_len,
                                         &bridge_response,
                                         &bridge_response_len,
                                         &tts_wav,
                                         &tts_wav_inline_len,
                                         90000u);
    free(wav);
    result->bridge_status = err == ESP_OK ? 101 : bridge_status;
    if (err != ESP_OK) {
        snprintf(result->bridge_error,
                 sizeof(result->bridge_error),
                 "brain_offline: %s",
                 esp_err_to_name(err));
        ui_show_brain_offline("Mac Brain WebSocket 无响应", now_ms());
        atlas_audio_service_note_failure(result->bridge_error, err);
        brain_ws_emit_event("turn.failed", result->bridge_error);
        atlas_runtime_turn_note_bridge(bridge_status, false, "", "", result->bridge_error, now_ms());
        atlas_runtime_turn_finish(false, result->bridge_error, now_ms());
        free(tts_wav);
        return err;
    }

    atlas_runtime_set_state(ATLAS_RUNTIME_STATE_THINKING, "brain ws response", now_ms());
    atlas_audio_service_note_stage(ATLAS_AUDIO_SERVICE_MODE_THINKING, "brain_ws_response");
    brain_ws_emit_event("turn.thinking", "brain ws response");
    cJSON *root = cJSON_ParseWithLength(bridge_response, bridge_response_len);
    if (root != NULL) {
        result->bridge_ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok"));
        result->tts_ready = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "tts_ready"));
        cjson_string(root, "asr_text", result->asr_text, sizeof(result->asr_text));
        cjson_string(root, "reply", result->reply, sizeof(result->reply));
        cjson_string(root, "error", result->bridge_error, sizeof(result->bridge_error));
    }
    atlas_runtime_turn_note_bridge(result->bridge_status,
                                   result->bridge_ok,
                                   result->asr_text,
                                   result->reply,
                                   result->bridge_error,
                                   now_ms());

    if (!result->bridge_ok) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        free(bridge_response);
        if (result->bridge_error[0] == '\0') {
            strlcpy(result->bridge_error, "brain_offline: turn failed", sizeof(result->bridge_error));
        }
        ui_show_brain_offline(result->bridge_error, now_ms());
        atlas_audio_service_note_failure(result->bridge_error, ESP_FAIL);
        brain_ws_emit_event("turn.failed", result->bridge_error);
        atlas_runtime_turn_finish(false, result->bridge_error, now_ms());
        free(tts_wav);
        return ESP_FAIL;
    }

    const bool has_reply = result->reply[0] != '\0';
    if (has_reply) {
        atlas_ui_set_chat_text(s_ctx.ui_state, result->reply);
    } else if (result->asr_text[0] != '\0') {
        atlas_ui_set_chat_text(s_ctx.ui_state, result->asr_text);
    }

    if (!has_reply) {
        atlas_ui_lock();
        s_ctx.ui_state->audio_level = 0;
        s_ctx.ui_state->last_event_ms = now_ms();
        if (!s_ctx.ui_state->moving && s_ctx.ui_state->page == ATLAS_PAGE_VOICE) {
            s_ctx.ui_state->page = ATLAS_PAGE_EYES;
            s_ctx.ui_state->expression = ATLAS_EXPR_IDLE;
        }
        atlas_ui_unlock();
        if (root != NULL) {
            cJSON_Delete(root);
        }
        free(bridge_response);
        free(tts_wav);
        brain_ws_emit_event("turn.done", "no reply");
        atlas_runtime_turn_finish(true, "", now_ms());
        return ESP_OK;
    }

    ui_set_page_state(ATLAS_PAGE_CHAT, ATLAS_EXPR_SPEAKING, 58, now_ms(), ATLAS_PET_EVENT_CHAT, true);
    atlas_runtime_set_state(ATLAS_RUNTIME_STATE_SPEAKING, "play tts", now_ms());
    atlas_audio_service_note_stage(ATLAS_AUDIO_SERVICE_MODE_PLAYING, "play_tts");
    brain_ws_emit_event("turn.speaking", "play tts");

    size_t tts_wav_len = 0;
    esp_err_t play_err = ESP_OK;
    if (result->tts_ready && tts_wav != NULL && tts_wav_inline_len > 0) {
        voice_wake_mute_for(ATLAS_VOICE_WAKE_PLAY_MUTE_MS);
        play_err = atlas_audio_service_play_wav_pcm(tts_wav, tts_wav_inline_len, s_ctx.config->ui.volume);
        voice_wake_mute_for(ATLAS_VOICE_WAKE_PLAY_MUTE_MS);
        tts_wav_len = tts_wav_inline_len;
    } else {
        play_err = ESP_ERR_NOT_FOUND;
    }
    result->play_err = play_err;
    result->tts_wav_len = tts_wav_len;
    atlas_runtime_turn_note_tts(result->tts_ready, tts_wav_len, play_err, now_ms());
    if (play_err != ESP_OK) {
        atlas_audio_service_note_failure(esp_err_to_name(play_err), play_err);
        brain_ws_emit_event("turn.tts_failed", esp_err_to_name(play_err));
    }

    ui_finish_audio_state(play_err == ESP_OK ? ATLAS_EXPR_IDLE : ATLAS_EXPR_THINKING, now_ms());

    if (root != NULL) {
        cJSON_Delete(root);
    }
    free(bridge_response);
    free(tts_wav);
    if (play_err == ESP_OK) {
        atlas_runtime_set_state(ATLAS_RUNTIME_STATE_COOLDOWN, "playback mute", now_ms());
        atlas_audio_service_note_stage(ATLAS_AUDIO_SERVICE_MODE_COOLDOWN, "playback_mute");
        brain_ws_emit_event("turn.played", "tts played");
        atlas_runtime_turn_finish(true, "", now_ms());
    } else {
        atlas_runtime_turn_finish(false, esp_err_to_name(play_err), now_ms());
    }
    return play_err == ESP_OK ? ESP_OK : play_err;
}

static esp_err_t run_voice_turn_job(void *ctx)
{
    atlas_voice_turn_job_ctx_t *job = (atlas_voice_turn_job_ctx_t *)ctx;
    if (job == NULL || job->result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return run_voice_turn(job->duration_ms, job->result);
}

static esp_err_t run_voice_turn_async_job(void *ctx)
{
    atlas_voice_turn_async_ctx_t *job = (atlas_voice_turn_async_ctx_t *)ctx;
    if (job == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t err = run_voice_turn(job->duration_ms, &job->result);
    free(job);
    return err;
}

static esp_err_t run_voice_turn_via_service(uint16_t duration, atlas_voice_turn_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));
    result->duration_ms = clamp_voice_duration(duration);
    result->play_err = ESP_ERR_INVALID_STATE;

    atlas_voice_turn_job_ctx_t job = {
        .duration_ms = duration,
        .result = result,
    };
    const esp_err_t err = atlas_audio_service_run_turn(run_voice_turn_job, &job, 0);
    if (err != ESP_OK && result->bridge_error[0] == '\0') {
        strlcpy(result->bridge_error, esp_err_to_name(err), sizeof(result->bridge_error));
    }
    return err;
}

static esp_err_t voice_turn_handler(httpd_req_t *req)
{
    char body[160];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char value[16] = "";
    uint16_t duration = 2800;
    if (form_get_value(body, "duration", value, sizeof(value))) {
        duration = (uint16_t)atoi(value);
    }
    bool async = false;
    if (form_get_value(body, "async", value, sizeof(value))) {
        (void)parse_bool_flag(value, &async);
    }
    if (!async && form_get_value(body, "mode", value, sizeof(value)) && strcmp(value, "async") == 0) {
        async = true;
    }

    if (async) {
        atlas_voice_turn_async_ctx_t *job = (atlas_voice_turn_async_ctx_t *)calloc(1, sizeof(*job));
        if (job == NULL) {
            return send_error(req, "500 Internal Server Error", "async voice job alloc failed");
        }
        job->duration_ms = duration;
        const esp_err_t err = atlas_audio_service_submit_turn(run_voice_turn_async_job, job);
        if (err != ESP_OK) {
            free(job);
            if (err == ESP_ERR_INVALID_STATE) {
                return send_error(req, "409 Conflict", esp_err_to_name(err));
            }
            return send_error(req, "503 Service Unavailable", esp_err_to_name(err));
        }

        atlas_audio_service_status_t service;
        atlas_audio_service_get_status(&service);
        char response[520];
        snprintf(response,
                 sizeof(response),
                 "{\"ok\":true,\"accepted\":true,\"async\":true,"
                 "\"duration_ms\":%" PRIu16 ",\"status_endpoint\":\"/api/status/lite\","
                 "\"events_endpoint\":\"/api/brain/events\",\"audio_service\":{\"mode\":\"%s\",\"busy\":%s,\"job_count\":%" PRIu32 "}}",
                 clamp_voice_duration(duration),
                 atlas_audio_service_mode_name(service.mode),
                 json_bool(service.busy),
                 service.job_count);
        httpd_resp_set_status(req, "202 Accepted");
        return send_json(req, response);
    }

    atlas_voice_turn_result_t result;
    const esp_err_t err = run_voice_turn_via_service(duration, &result);
    if (err != ESP_OK && !result.bridge_ok) {
        const char *message = result.bridge_error[0] == '\0' ? esp_err_to_name(err) : result.bridge_error;
        if (strstr(message, "brain_offline") != NULL) {
            char safe[180];
            char brain_ws_json[620];
            json_escape(safe, sizeof(safe), message);
            const size_t brain_ws_len = atlas_brain_ws_client_write_json(brain_ws_json, sizeof(brain_ws_json));
            if (!json_fragment_complete(brain_ws_len, sizeof(brain_ws_json))) {
                strlcpy(brain_ws_json, "{\"error\":\"brain_ws_status_truncated\"}", sizeof(brain_ws_json));
            }
            char response[920];
            snprintf(response,
                     sizeof(response),
                     "{\"ok\":false,\"error\":\"brain_offline\",\"message\":\"Brain 离线\","
                     "\"detail\":\"%s\",\"duration_ms\":%" PRIu16 ",\"brain_ws\":%s}",
                     safe,
                     result.duration_ms,
                     brain_ws_json);
            httpd_resp_set_status(req, "503 Service Unavailable");
            return send_json(req, response);
        }
        if (err == ESP_ERR_INVALID_STATE) {
            return send_error(req, "409 Conflict", message);
        }
        return send_error(req,
                          result.bridge_status >= 400 ? "502 Bad Gateway" : "500 Internal Server Error",
                          message);
    }

    char asr_json[320];
    char reply_json[360];
    char play_error[48];
    json_escape(asr_json, sizeof(asr_json), result.asr_text);
    json_escape(reply_json, sizeof(reply_json), result.reply);
    json_escape(play_error, sizeof(play_error), esp_err_to_name(result.play_err));

    char response[1200];
    snprintf(response,
             sizeof(response),
             "{"
             "\"ok\":true,"
             "\"duration_ms\":%" PRIu16 ",\"wav_bytes\":%u,"
             "\"mic_level\":%" PRIu8 ",\"mic_rms\":%" PRIu32 ",\"mic_peak\":%" PRIu32 ","
             "\"asr_text\":\"%s\",\"reply\":\"%s\","
             "\"tts_ready\":%s,\"tts_bytes\":%u,\"played\":%s,\"play_error\":\"%s\""
             "}",
             result.duration_ms,
             (unsigned)result.wav_len,
             result.mic_level,
             result.mic_rms,
             result.mic_peak,
             asr_json,
             reply_json,
             json_bool(result.tts_ready),
             (unsigned)result.tts_wav_len,
             json_bool(result.play_err == ESP_OK),
             play_error);

    return send_json(req, response);
}

static void voice_wake_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG,
             "voice wake task started threshold=%u hits=%u duration=%u measure=%u idle=%u",
             s_voice_wake_threshold,
             s_voice_wake_hits_required,
             s_voice_wake_duration_ms,
             ATLAS_VOICE_WAKE_MEASURE_MS,
             ATLAS_VOICE_WAKE_IDLE_MS);
    vTaskDelay(pdMS_TO_TICKS(ATLAS_VOICE_WAKE_START_DELAY_MS));
    uint8_t hit_count = 0;
    while (s_voice_wake_enabled) {
        const uint32_t ts = now_ms();
        uint32_t service_mute_ms = 0;
        (void)atlas_audio_service_is_muted(&service_mute_ms);
        const uint32_t mute_ms = remaining_ms(ts, s_voice_wake_mute_until_ms) > service_mute_ms ?
                                    remaining_ms(ts, s_voice_wake_mute_until_ms) : service_mute_ms;
        if (mute_ms > 0u) {
            hit_count = 0;
            s_voice_wake_hit_count = 0;
            strlcpy(s_voice_wake_last_reason, "muted", sizeof(s_voice_wake_last_reason));
            const uint32_t wait_ms = mute_ms > ATLAS_VOICE_WAKE_IDLE_MS ? ATLAS_VOICE_WAKE_IDLE_MS : mute_ms;
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
            continue;
        }

        atlas_audio_mic_level_t level = {0};
        const esp_err_t err = atlas_audio_service_measure_mic(ATLAS_VOICE_WAKE_MEASURE_MS, &level);
        if (!s_voice_wake_enabled) {
            break;
        }
        const bool hit = (err == ESP_OK) && voice_wake_sample_hit(&level);
        if (err == ESP_OK) {
            voice_wake_note_sample(&level, hit);
        } else {
            strlcpy(s_voice_wake_last_reason, esp_err_to_name(err), sizeof(s_voice_wake_last_reason));
        }

        if (hit && !s_voice_wake_busy) {
            if (hit_count < 255) {
                hit_count++;
            }
        } else {
            hit_count = 0;
        }
        s_voice_wake_hit_count = hit_count;
        if (hit_count >= s_voice_wake_hits_required && !s_voice_wake_busy) {
            hit_count = 0;
            s_voice_wake_hit_count = 0;
            s_voice_wake_busy = true;
            s_voice_wake_triggers++;
            s_voice_wake_last_ms = now_ms();
            ESP_LOGI(TAG,
                     "voice wake trigger reason=%s level=%u rms=%" PRIu32 " peak=%" PRIu32
                     " noise_rms=%" PRIu32 " noise_peak=%" PRIu32 " hits=%u",
                     s_voice_wake_last_reason,
                     level.level,
                     level.rms,
                     level.peak,
                     s_voice_wake_noise_rms,
                     s_voice_wake_noise_peak,
                     s_voice_wake_hits_required);
            brain_ws_emit_event("wake.triggered", s_voice_wake_last_reason);

            atlas_voice_turn_result_t result;
            const esp_err_t turn_err = run_voice_turn_via_service(s_voice_wake_duration_ms, &result);
            ESP_LOGI(TAG,
                     "voice wake turn done err=%s asr='%s' reply='%s' played=%s",
                     esp_err_to_name(turn_err),
                     result.asr_text,
                     result.reply,
                     result.play_err == ESP_OK ? "true" : "false");
            s_voice_wake_busy = false;
            if (s_voice_wake_enabled) {
                const uint32_t rearm_ts = now_ms();
                atlas_ui_lock();
                if (s_ctx.ui_state->page == ATLAS_PAGE_VOICE) {
                    s_ctx.ui_state->expression = ATLAS_EXPR_LISTEN;
                    s_ctx.ui_state->audio_level = 18;
                }
                s_ctx.ui_state->last_event_ms = rearm_ts;
                atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_VOICE_LISTEN, rearm_ts);
                atlas_ui_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(ATLAS_VOICE_WAKE_REARM_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(ATLAS_VOICE_WAKE_IDLE_MS));
        }
    }
    s_voice_wake_busy = false;
    s_voice_wake_task = NULL;
    ESP_LOGI(TAG, "voice wake task stopped");
    atlas_audio_service_set_continuous_enabled(false);
    const bool psram_stack = s_voice_wake_psram_stack;
    s_voice_wake_psram_stack = false;
    if (psram_stack) {
        vTaskDeleteWithCaps(NULL);
    } else {
        vTaskDelete(NULL);
    }
}

static esp_err_t start_voice_wake_task(void)
{
    s_voice_wake_enabled = true;
    atlas_audio_service_set_continuous_enabled(true);
    if (s_voice_wake_task != NULL) {
        return ESP_OK;
    }
    BaseType_t ok = xTaskCreateWithCaps(voice_wake_task,
                                        "atlas_voice_wake",
                                        ATLAS_VOICE_WAKE_TASK_STACK_BYTES,
                                        NULL,
                                        ATLAS_VOICE_WAKE_TASK_PRIORITY,
                                        &s_voice_wake_task,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_voice_wake_psram_stack = ok == pdPASS;
    if (ok != pdPASS) {
        ok = xTaskCreate(voice_wake_task,
                         "atlas_voice_wake",
                         ATLAS_VOICE_WAKE_TASK_STACK_BYTES,
                         NULL,
                         ATLAS_VOICE_WAKE_TASK_PRIORITY,
                         &s_voice_wake_task);
        s_voice_wake_psram_stack = false;
    }
    if (ok != pdPASS) {
        s_voice_wake_enabled = false;
        s_voice_wake_task = NULL;
        atlas_audio_service_set_continuous_enabled(false);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t voice_wake_handler(httpd_req_t *req)
{
    char body[200];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char value[16] = "";
    const bool was_enabled = s_voice_wake_enabled;
    bool enabled = s_voice_wake_enabled;
    if (form_get_value(body, "enabled", value, sizeof(value))) {
        (void)parse_bool_flag(value, &enabled);
    }
    if (form_get_value(body, "threshold", value, sizeof(value))) {
        uint8_t threshold = (uint8_t)atoi(value);
        if (threshold < 5) {
            threshold = 5;
        }
        if (threshold > 90) {
            threshold = 90;
        }
        s_voice_wake_threshold = threshold;
    }
    if (form_get_value(body, "hits", value, sizeof(value))) {
        uint8_t hits = (uint8_t)atoi(value);
        if (hits < 1) {
            hits = 1;
        }
        if (hits > 8) {
            hits = 8;
        }
        s_voice_wake_hits_required = hits;
    }
    if (form_get_value(body, "duration", value, sizeof(value))) {
        s_voice_wake_duration_ms = clamp_voice_duration((uint16_t)atoi(value));
    }

    esp_err_t err = ESP_OK;
    if (enabled) {
        if (strcmp(s_ctx.config->llm.mode, "host") != 0 || s_ctx.config->llm.base_url[0] == '\0') {
            return send_error(req, "409 Conflict", "host bridge not configured");
        }
        if (!was_enabled) {
            s_voice_wake_hit_count = 0;
            s_voice_wake_noise_rms = 0;
            s_voice_wake_noise_peak = 0;
            s_voice_wake_mute_until_ms = 0;
            strlcpy(s_voice_wake_last_reason, "arming", sizeof(s_voice_wake_last_reason));
        }
        err = start_voice_wake_task();
    } else {
        s_voice_wake_enabled = false;
        s_voice_wake_hit_count = 0;
        atlas_audio_service_set_continuous_enabled(false);
        strlcpy(s_voice_wake_last_reason, "off", sizeof(s_voice_wake_last_reason));
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }

    char reason[48];
    json_escape(reason, sizeof(reason), s_voice_wake_last_reason);
    const uint32_t ts = now_ms();
    char json[520];
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"voice_wake\":{\"enabled\":%s,\"busy\":%s,\"psram_stack\":%s,\"continuous\":true,\"threshold\":%u,\"hits_required\":%u,\"duration_ms\":%u,"
             "\"triggers\":%" PRIu32 ",\"last_ms\":%" PRIu32 ",\"last_level\":%" PRIu32 ",\"last_rms\":%" PRIu32 ",\"last_peak\":%" PRIu32 ","
             "\"noise_rms\":%" PRIu32 ",\"noise_peak\":%" PRIu32 ",\"hit_count\":%u,\"mute_ms\":%" PRIu32 ",\"reason\":\"%s\"}}",
             json_bool(s_voice_wake_enabled),
             json_bool(s_voice_wake_busy),
             json_bool(s_voice_wake_psram_stack),
             s_voice_wake_threshold,
             s_voice_wake_hits_required,
             s_voice_wake_duration_ms,
             s_voice_wake_triggers,
             s_voice_wake_last_ms,
             s_voice_wake_last_level,
             s_voice_wake_last_rms,
             s_voice_wake_last_peak,
             s_voice_wake_noise_rms,
             s_voice_wake_noise_peak,
             s_voice_wake_hit_count,
             remaining_ms(ts, s_voice_wake_mute_until_ms),
             reason);
    return send_json(req, json);
}

static esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    atlas_wifi_scan_result_t scan;
    const esp_err_t err = atlas_wifi_scan(&scan);
    if (err != ESP_OK) {
        return send_error(req, "503 Service Unavailable", esp_err_to_name(err));
    }

    const size_t json_size = 4096;
    char *json = (char *)calloc(1, json_size);
    if (json == NULL) {
        return send_error(req, "500 Internal Server Error", "no memory");
    }

    size_t off = 0;
    off += snprintf(json + off, json_size - off, "{\"ok\":true,\"count\":%u,\"networks\":[", (unsigned)scan.count);
    for (size_t i = 0; i < scan.count && off < json_size; ++i) {
        char ssid[ATLAS_WIFI_SCAN_SSID_MAX * 2];
        json_escape(ssid, sizeof(ssid), scan.records[i].ssid);
        off += snprintf(json + off,
                        json_size - off,
                        "%s{\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%u,\"secure\":%s,\"auth\":\"%s\"}",
                        i == 0 ? "" : ",",
                        ssid,
                        scan.records[i].rssi,
                        scan.records[i].channel,
                        json_bool(scan.records[i].secure),
                        wifi_auth_name(scan.records[i].authmode));
    }
    if (off < json_size) {
        snprintf(json + off, json_size - off, "]}");
    } else {
        strlcpy(json + json_size - 4, "]}", 4);
    }

    const esp_err_t ret = send_json(req, json);
    free(json);
    return ret;
}

static esp_err_t app_expression_handler(httpd_req_t *req)
{
    char body[160];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");

    char expression_name[24] = "";
    (void)form_get_value(body, "expression", expression_name, sizeof(expression_name));
    atlas_expression_t expression = ATLAS_EXPR_IDLE;
    if (!atlas_expression_from_name(expression_name, &expression)) {
        return send_error(req, "400 Bad Request", "bad expression");
    }

    const uint32_t ts = now_ms();
    manual_ui_override(ts, "manual expression");
    ESP_LOGI(TAG, "app expression requested: %s", expression_name);
    ui_set_page_state(ATLAS_PAGE_EYES,
                      expression,
                      expression == ATLAS_EXPR_SPEAKING ? 58 : 0,
                      ts,
                      ATLAS_PET_EVENT_INTERACTION,
                      false);
    return send_json(req, "{\"ok\":true,\"app\":\"expression\"}");
}

static esp_err_t app_page_handler(httpd_req_t *req)
{
    char body[160];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");

    char page_name[24] = "";
    (void)form_get_value(body, "page", page_name, sizeof(page_name));
    atlas_page_t page = ATLAS_PAGE_EYES;
    if (!atlas_page_from_name(page_name, &page)) {
        return send_error(req, "400 Bad Request", "bad page");
    }

    if (!is_supported_page(page)) {
        return send_error(req, "501 Not Implemented", "feature not available yet");
    }
    const uint32_t ts = now_ms();
    manual_ui_override(ts, "manual page");
    ESP_LOGI(TAG, "app page requested: %s", page_name);
    ui_apply_page(page, ts);
    return send_json(req, "{\"ok\":true,\"app\":\"page\"}");
}

static esp_err_t app_action_handler(httpd_req_t *req)
{
    char body[512];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");

    char action[32] = "";
    char run_flag[8] = "";
    char task_name[ATLAS_POMODORO_TASK_MAX] = "";
    char focus_minutes_s[12] = "";
    char break_minutes_s[12] = "";
    char chat_text[ATLAS_BRAIN_INTENT_SPEECH_MAX] = "";
    char cal_title[ATLAS_CALENDAR_TITLE_MAX] = "";
    char cal_note[ATLAS_CALENDAR_NOTE_MAX] = "";
    char epoch_ms_s[24] = "";
    (void)form_get_value(body, "action", action, sizeof(action));
    (void)form_get_value(body, "running", run_flag, sizeof(run_flag));
    (void)form_get_value(body, "task_name", task_name, sizeof(task_name));
    (void)form_get_value(body, "focus_minutes", focus_minutes_s, sizeof(focus_minutes_s));
    (void)form_get_value(body, "break_minutes", break_minutes_s, sizeof(break_minutes_s));
    (void)form_get_value(body, "chat_text", chat_text, sizeof(chat_text));
    (void)form_get_value(body, "title", cal_title, sizeof(cal_title));
    (void)form_get_value(body, "note", cal_note, sizeof(cal_note));
    (void)form_get_value(body, "epoch_ms", epoch_ms_s, sizeof(epoch_ms_s));
    if (epoch_ms_s[0] == '\0') {
        (void)form_get_value(body, "epoch", epoch_ms_s, sizeof(epoch_ms_s));
    }
    const uint32_t ts = now_ms();

    atlas_ui_lock();
    const bool was_moving = s_ctx.ui_state->moving;
    atlas_ui_unlock();
    if (was_moving) {
        (void)atlas_ui_stop(s_ctx.ui_state, ts);
    }

    if (!is_supported_action(action)) {
        return send_error(req, "501 Not Implemented", "feature not available yet");
    }

    manual_ui_override(ts, "manual action");
    ESP_LOGI(TAG, "app action requested: %s", action);
    if (strcmp(action, "clock") == 0 || strcmp(action, "clock.show") == 0 || strcmp(action, "clock.status") == 0) {
        atlas_ui_lock();
        s_ctx.ui_state->page = ATLAS_PAGE_CLOCK;
        s_ctx.ui_state->expression = ATLAS_EXPR_CURIOUS;
        s_ctx.ui_state->audio_level = 0;
        atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_THINK, ts);
        atlas_ui_unlock();
    } else if (strcmp(action, "clock.sync") == 0) {
        if (!set_clock_from_epoch_ms(epoch_ms_s)) {
            return send_error(req, "400 Bad Request", "invalid epoch");
        }
        atlas_ui_lock();
        s_ctx.ui_state->page = ATLAS_PAGE_CLOCK;
        s_ctx.ui_state->expression = ATLAS_EXPR_CURIOUS;
        s_ctx.ui_state->audio_level = 0;
        atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_THINK, ts);
        atlas_ui_unlock();
    } else if (strcmp(action, "music") == 0) {
        atlas_ui_lock();
        s_ctx.ui_state->page = ATLAS_PAGE_MUSIC;
        s_ctx.ui_state->expression = ATLAS_EXPR_SPEAKING;
        s_ctx.ui_state->audio_level = 64;
        atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_MUSIC, ts);
        atlas_ui_unlock();
    } else if (strcmp(action, "story") == 0) {
        atlas_ui_lock();
        s_ctx.ui_state->page = ATLAS_PAGE_STORY;
        s_ctx.ui_state->expression = ATLAS_EXPR_SPEAKING;
        s_ctx.ui_state->audio_level = 58;
        atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_STORY, ts);
        atlas_ui_unlock();
    } else if (strcmp(action, "chat") == 0) {
        atlas_ui_lock();
        s_ctx.ui_state->page = ATLAS_PAGE_CHAT;
        s_ctx.ui_state->expression = ATLAS_EXPR_LISTEN;
        s_ctx.ui_state->audio_level = 28;
        if (chat_text[0] != '\0') {
            atlas_ui_set_chat_text(s_ctx.ui_state, chat_text);
        }
        atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_CHAT, ts);
        atlas_ui_unlock();
    } else if (strcmp(action, "calendar") == 0 || strcmp(action, "calendar.show") == 0 ||
               strcmp(action, "calendar.today") == 0 || strcmp(action, "calendar.set_note") == 0) {
        atlas_ui_lock();
        s_ctx.ui_state->page = ATLAS_PAGE_CALENDAR;
        s_ctx.ui_state->expression = ATLAS_EXPR_CURIOUS;
        s_ctx.ui_state->audio_level = 0;
        if (strcmp(action, "calendar.today") == 0 && cal_title[0] == '\0' && cal_note[0] == '\0') {
            char clock_time[16] = "";
            char clock_date[16] = "";
            char clock_weekday[24] = "";
            if (format_clock_snapshot(clock_time,
                                      sizeof(clock_time),
                                      clock_date,
                                      sizeof(clock_date),
                                      clock_weekday,
                                      sizeof(clock_weekday))) {
                strlcpy(cal_title, "今日日历", sizeof(cal_title));
                snprintf(cal_note, sizeof(cal_note), "%s %s", clock_date, clock_weekday);
            } else {
                strlcpy(cal_title, "今日日历", sizeof(cal_title));
                strlcpy(cal_note, "时钟待校准，先同步时间", sizeof(cal_note));
            }
        }
        if (cal_title[0] != '\0' || cal_note[0] != '\0') {
            atlas_ui_set_calendar_text(s_ctx.ui_state,
                                      cal_title[0] == '\0' ? s_ctx.ui_state->calendar_title : cal_title,
                                      cal_note[0] == '\0' ? NULL : cal_note);
        }
        atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_THINK, ts);
        atlas_ui_unlock();
    } else if (strcmp(action, "pomodoro.show") == 0 || strcmp(action, "pomodoro.status") == 0) {
        atlas_ui_lock();
        s_ctx.ui_state->page = ATLAS_PAGE_POMODORO;
        s_ctx.ui_state->expression = s_ctx.ui_state->pomodoro_running ? ATLAS_EXPR_THINKING : ATLAS_EXPR_CURIOUS;
        s_ctx.ui_state->audio_level = s_ctx.ui_state->pomodoro_running ? 20 : 0;
        atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_THINK, ts);
        atlas_ui_unlock();
    } else if (strcmp(action, "pomodoro") == 0) {
        atlas_ui_lock();
        if (focus_minutes_s[0] != '\0') {
            const uint16_t v = (uint16_t)atoi(focus_minutes_s);
            s_ctx.ui_state->pomodoro_focus_minutes = (v == 0) ? 25u : (v > 120u ? 120u : v);
        }
        if (break_minutes_s[0] != '\0') {
            const uint16_t v = (uint16_t)atoi(break_minutes_s);
            s_ctx.ui_state->pomodoro_break_minutes = (v == 0) ? 5u : (v > 30u ? 30u : v);
        }
        bool run = s_ctx.ui_state->pomodoro_running;
        bool parsed_run = false;
        if (parse_bool_flag(run_flag, &parsed_run)) {
            run = parsed_run;
        }
        atlas_ui_set_pomodoro_running(s_ctx.ui_state,
                                      run,
                                      false,
                                      ts,
                                      task_name[0] == '\0' ? NULL : task_name,
                                      false);
        s_ctx.ui_state->page = ATLAS_PAGE_POMODORO;
        s_ctx.ui_state->expression = run ? ATLAS_EXPR_SPEAKING : ATLAS_EXPR_THINKING;
        s_ctx.ui_state->audio_level = run ? 24 : 0;
        atlas_pet_handle_event(&s_ctx.ui_state->pet, run ? ATLAS_PET_EVENT_THINK : ATLAS_PET_EVENT_STOP, ts);
        atlas_ui_unlock();
    } else if (strcmp(action, "pomodoro.stop") == 0 || strcmp(action, "pomodoro.reset") == 0) {
        atlas_ui_lock();
        atlas_ui_set_pomodoro_running(s_ctx.ui_state, false, false, ts, NULL, true);
        s_ctx.ui_state->page = ATLAS_PAGE_POMODORO;
        s_ctx.ui_state->expression = ATLAS_EXPR_CURIOUS;
        s_ctx.ui_state->audio_level = 0;
        atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_STOP, ts);
        atlas_ui_unlock();
    } else if (strcmp(action, "pomodoro.start") == 0) {
        atlas_ui_lock();
        if (focus_minutes_s[0] != '\0') {
            const uint16_t v = (uint16_t)atoi(focus_minutes_s);
            s_ctx.ui_state->pomodoro_focus_minutes = (v == 0) ? 25u : (v > 120u ? 120u : v);
        }
        if (break_minutes_s[0] != '\0') {
            const uint16_t v = (uint16_t)atoi(break_minutes_s);
            s_ctx.ui_state->pomodoro_break_minutes = (v == 0) ? 5u : (v > 30u ? 30u : v);
        }
        const bool run = true;
        atlas_ui_set_pomodoro_running(s_ctx.ui_state,
                                      run,
                                      false,
                                      ts,
                                      task_name[0] == '\0' ? NULL : task_name,
                                      false);
        s_ctx.ui_state->page = ATLAS_PAGE_POMODORO;
        s_ctx.ui_state->expression = ATLAS_EXPR_SPEAKING;
        s_ctx.ui_state->audio_level = 24;
        atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_THINK, ts);
        atlas_ui_unlock();
    } else if (strcmp(action, "alarm") == 0) {
        atlas_ui_lock();
        s_ctx.ui_state->page = ATLAS_PAGE_ALARM;
        s_ctx.ui_state->expression = ATLAS_EXPR_CURIOUS;
        s_ctx.ui_state->audio_level = 0;
        atlas_pet_handle_event(&s_ctx.ui_state->pet, ATLAS_PET_EVENT_THINK, ts);
        atlas_ui_unlock();
    } else {
        return send_error(req, "400 Bad Request", "bad action");
    }

    atlas_ui_lock();
    s_ctx.ui_state->last_event_ms = ts;
    atlas_ui_unlock();
    return send_json(req, "{\"ok\":true,\"app\":\"action\",\"note\":\"backend accepted\"}");
}

static esp_err_t pet_event_handler(httpd_req_t *req)
{
    char body[160];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");

    char event_name[24] = "";
    (void)form_get_value(body, "event", event_name, sizeof(event_name));
    atlas_pet_event_t event = ATLAS_PET_EVENT_INTERACTION;
    if (!atlas_pet_event_from_name(event_name, &event)) {
        return send_error(req, "400 Bad Request", "bad pet event");
    }

    const uint32_t ts = now_ms();
    const esp_err_t err = atlas_ui_handle_pet_event(s_ctx.ui_state, event, ts);
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }

    char json[220];
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"pet_event\":\"%s\",\"phase\":\"%s\",\"expression\":\"%s\"}",
             atlas_pet_event_name(event),
             atlas_pet_phase_name(s_ctx.ui_state->pet.phase),
             atlas_expression_name(s_ctx.ui_state->expression));
    return send_json(req, json);
}

static esp_err_t pet_wake_handler(httpd_req_t *req)
{
    (void)req;
    const uint32_t ts = now_ms();
    const esp_err_t err = atlas_ui_handle_pet_event(s_ctx.ui_state, ATLAS_PET_EVENT_TOUCH, ts);
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }

    char json[140];
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"pet_event\":\"wake\",\"phase\":\"%s\",\"expression\":\"%s\"}",
             atlas_pet_phase_name(s_ctx.ui_state->pet.phase),
             atlas_expression_name(s_ctx.ui_state->expression));
    return send_json(req, json);
}

static esp_err_t stop_handler(httpd_req_t *req)
{
    (void)req;
    esp_err_t err = atlas_ui_stop(s_ctx.ui_state, now_ms());
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"action\":\"stop\"}");
}

static esp_err_t move_handler(httpd_req_t *req)
{
    char body[256];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }
    if (!atlas_config_motion_allowed(s_ctx.config)) {
        return send_error(req, "423 Locked", "motion disabled");
    }
    if (!atlas_config_manual_control_allowed(s_ctx.config)) {
        return send_error(req, "409 Conflict", "manual mode required");
    }

    char dir[8] = "";
    char speed_s[8] = "";
    char duration_s[8] = "";
    (void)form_get_value(body, "dir", dir, sizeof(dir));
    (void)form_get_value(body, "speed", speed_s, sizeof(speed_s));
    (void)form_get_value(body, "duration", duration_s, sizeof(duration_s));

    atlas_voice_event_t event = ATLAS_VOICE_EVENT_NONE;
    if (strcmp(dir, "F") == 0) {
        event = ATLAS_VOICE_EVENT_MOVE_FORWARD;
    } else if (strcmp(dir, "B") == 0) {
        event = ATLAS_VOICE_EVENT_MOVE_BACKWARD;
    } else if (strcmp(dir, "L") == 0) {
        event = ATLAS_VOICE_EVENT_TURN_LEFT;
    } else if (strcmp(dir, "R") == 0) {
        event = ATLAS_VOICE_EVENT_TURN_RIGHT;
    } else {
        return send_error(req, "400 Bad Request", "bad direction");
    }

    atlas_voice_intent_t intent = atlas_voice_intent_from_event(event);
    const int requested_speed = atoi(speed_s);
    const int requested_duration = atoi(duration_s);
    if (requested_speed > 0) {
        intent.speed = (uint8_t)requested_speed;
    }
    if (requested_duration > 0) {
        intent.duration_ms = (uint16_t)requested_duration;
    }
    if (intent.speed > s_ctx.config->safety.max_speed_percent) {
        intent.speed = s_ctx.config->safety.max_speed_percent;
    }
    if (intent.duration_ms > s_ctx.config->safety.max_duration_ms) {
        intent.duration_ms = s_ctx.config->safety.max_duration_ms;
    }

    const esp_err_t err = atlas_ui_handle_voice_intent(s_ctx.ui_state, intent, now_ms());
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"action\":\"move\"}");
}

static esp_err_t save_wifi_handler(httpd_req_t *req)
{
    char body[320];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char ssid[ATLAS_WIFI_SSID_MAX] = "";
    char password[ATLAS_WIFI_PASSWORD_MAX] = "";
    (void)form_get_value(body, "ssid", ssid, sizeof(ssid));
    (void)form_get_value(body, "password", password, sizeof(password));
    atlas_common_config_trim(ssid);
    if (!atlas_common_config_has_value(ssid)) {
        return send_error(req, "400 Bad Request", "ssid required");
    }

    esp_err_t err = atlas_config_save_wifi(ssid, password);
    if (err == ESP_OK) {
        strlcpy(s_ctx.config->wifi_ssid, ssid, sizeof(s_ctx.config->wifi_ssid));
        strlcpy(s_ctx.config->wifi_password, password, sizeof(s_ctx.config->wifi_password));
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"saved\":\"wifi\",\"note\":\"reboot to connect STA\"}");
}

static esp_err_t save_llm_handler(httpd_req_t *req)
{
    char body[640];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    atlas_llm_config_t llm = s_ctx.config->llm;
    (void)form_get_value(body, "mode", llm.mode, sizeof(llm.mode));
    (void)form_get_value(body, "provider", llm.provider, sizeof(llm.provider));
    (void)form_get_value(body, "base_url", llm.base_url, sizeof(llm.base_url));
    (void)form_get_value(body, "model", llm.model, sizeof(llm.model));
    atlas_common_config_trim(llm.mode);
    atlas_common_config_trim(llm.provider);
    atlas_common_config_trim(llm.base_url);
    atlas_common_config_trim(llm.model);

    char api_key[ATLAS_LLM_API_KEY_MAX] = "";
    if (form_get_value(body, "api_key", api_key, sizeof(api_key))) {
        atlas_common_config_trim(api_key);
    }
    if (api_key[0] != '\0') {
        strlcpy(llm.api_key, api_key, sizeof(llm.api_key));
    }

    esp_err_t err = atlas_config_save_llm(&llm);
    if (err == ESP_OK) {
        s_ctx.config->llm = llm;
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"saved\":\"llm\"}");
}

static esp_err_t save_safety_handler(httpd_req_t *req)
{
    char body[320];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char value[16] = "";
    atlas_safety_config_t safety = s_ctx.config->safety;
    if (form_get_value(body, "motion_enabled", value, sizeof(value))) {
        safety.motion_enabled = strcmp(value, "1") == 0 || strcmp(value, "true") == 0;
    }
    if (form_get_value(body, "control_mode", value, sizeof(value))) {
        strlcpy(safety.control_mode, value, sizeof(safety.control_mode));
    }
    if (form_get_value(body, "max_speed", value, sizeof(value))) {
        safety.max_speed_percent = (uint8_t)atoi(value);
    }
    if (form_get_value(body, "max_duration", value, sizeof(value))) {
        safety.max_duration_ms = (uint16_t)atoi(value);
    }

    esp_err_t err = atlas_config_save_safety(&safety);
    if (err == ESP_OK) {
        (void)atlas_config_load(s_ctx.config);
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"saved\":\"safety\"}");
}

static esp_err_t save_ui_handler(httpd_req_t *req)
{
    char body[384];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char value[32] = "";
    atlas_ui_config_t ui = s_ctx.config->ui;
    if (form_get_value(body, "theme", value, sizeof(value))) {
        strlcpy(ui.theme, value, sizeof(ui.theme));
    }
    if (form_get_value(body, "chat_mode", value, sizeof(value)) ||
        form_get_value(body, "mode", value, sizeof(value))) {
        if (!atlas_config_chat_mode_is_valid(value)) {
            return send_error(req, "400 Bad Request", "bad chat mode");
        }
        strlcpy(ui.chat_mode, value, sizeof(ui.chat_mode));
    }
    if (form_get_value(body, "brightness", value, sizeof(value))) {
        ui.brightness = (uint8_t)atoi(value);
    }
    if (form_get_value(body, "volume", value, sizeof(value))) {
        ui.volume = (uint8_t)atoi(value);
    }

    esp_err_t err = atlas_config_save_ui(&ui);
    if (err == ESP_OK) {
        (void)atlas_config_load(s_ctx.config);
        atlas_display_set_theme(s_ctx.config->ui.theme);
        atlas_display_set_brightness(s_ctx.config->ui.brightness);
        (void)atlas_audio_set_volume(s_ctx.config->ui.volume);
        atlas_ui_apply_config(s_ctx.ui_state, s_ctx.config);
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"saved\":\"ui\"}");
}

static esp_err_t save_pomodoro_handler(httpd_req_t *req)
{
    char body[240];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    atlas_pomodoro_config_t cfg = s_ctx.config->pomodoro;
    char value[24] = "";

    if (form_get_value(body, "task_name", value, sizeof(value)) && value[0] != '\0') {
        strlcpy(cfg.task_name, value, sizeof(cfg.task_name));
    }

    if (form_get_value(body, "focus_minutes", value, sizeof(value))) {
        cfg.focus_minutes = (uint16_t)atoi(value);
    }
    if (form_get_value(body, "break_minutes", value, sizeof(value))) {
        cfg.break_minutes = (uint16_t)atoi(value);
    }
    if (form_get_value(body, "enabled", value, sizeof(value))) {
        cfg.enabled = strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0;
    }
    if (form_get_value(body, "running", value, sizeof(value))) {
        cfg.enabled = strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0;
    }

    const esp_err_t err = atlas_config_save_pomodoro(&cfg);
    if (err == ESP_OK) {
        (void)atlas_config_load(s_ctx.config);
        atlas_ui_apply_config(s_ctx.ui_state, s_ctx.config);
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"saved\":\"pomodoro\"}");
}

static esp_err_t save_calendar_handler(httpd_req_t *req)
{
    char body[320];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    atlas_calendar_config_t cfg = s_ctx.config->calendar;
    char value[ATLAS_CALENDAR_NOTE_MAX] = "";

    if (form_get_value(body, "title", value, sizeof(value))) {
        strlcpy(cfg.title, value, sizeof(cfg.title));
    }
    if (form_get_value(body, "note", value, sizeof(value))) {
        strlcpy(cfg.note, value, sizeof(cfg.note));
    }
    if (form_get_value(body, "enabled", value, sizeof(value))) {
        cfg.enabled = strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0;
    }

    const esp_err_t err = atlas_config_save_calendar(&cfg);
    if (err == ESP_OK) {
        (void)atlas_config_load(s_ctx.config);
        atlas_ui_apply_config(s_ctx.ui_state, s_ctx.config);
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"saved\":\"calendar\"}");
}

static esp_err_t voice_text_handler(httpd_req_t *req)
{
    char body[320];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char text[160] = "";
    (void)form_get_value(body, "text", text, sizeof(text));
    atlas_brain_result_t result = atlas_brain_resolve_text(s_ctx.config, text);
    if (is_motion_event(result.intent.event)) {
        if (!atlas_config_motion_allowed(s_ctx.config)) {
            return send_error(req, "423 Locked", "motion disabled");
        }
        if (!atlas_config_ai_control_allowed(s_ctx.config)) {
            return send_error(req, "409 Conflict", "ai mode required");
        }
    }

    const esp_err_t err = atlas_ui_handle_voice_intent(s_ctx.ui_state, result.intent, now_ms());
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }

    char json[220];
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"source\":\"%s\",\"used_llm\":%s,\"event\":\"%s\"}",
             atlas_brain_source_name(result.source),
             result.used_llm ? "true" : "false",
             atlas_voice_event_name(result.intent.event));
    return send_json(req, json);
}

static esp_err_t brain_intent_handler(httpd_req_t *req)
{
    char body[3200];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char intent_json[2600] = "";
    (void)form_get_value(body, "intent", intent_json, sizeof(intent_json));
    if (intent_json[0] == '\0') {
        return send_error(req, "400 Bad Request", "intent required");
    }

    atlas_brain_intent_t intent;
    char error[96] = "";
    esp_err_t err = atlas_brain_intent_parse_json(intent_json, &intent, error, sizeof(error));
    if (err != ESP_OK) {
        return send_error(req, "400 Bad Request", error[0] == '\0' ? esp_err_to_name(err) : error);
    }

    char result[80] = "";
    err = atlas_brain_intent_apply_intent(s_ctx.config, s_ctx.ui_state, &intent, now_ms(), result, sizeof(result));
    if (err == ESP_ERR_NOT_FINISHED) {
        char json[220];
        snprintf(json,
                 sizeof(json),
                 "{\"ok\":true,\"accepted\":false,\"requires_confirmation\":true,\"result\":\"%s\"}",
                 result);
        return send_json(req, json);
    }
    if (err != ESP_OK) {
        return send_error(req, "409 Conflict", result[0] == '\0' ? esp_err_to_name(err) : result);
    }

    char json[260];
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"accepted\":true,\"page\":\"%s\",\"expression\":\"%s\",\"motion\":\"%s\"}",
             atlas_page_name(s_ctx.ui_state->page),
             atlas_expression_name(s_ctx.ui_state->expression),
             atlas_motion_name(s_ctx.ui_state->motion));
    return send_json(req, json);
}

static esp_err_t reset_handler(httpd_req_t *req)
{
    char body[128];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }
    esp_err_t err = atlas_config_reset_network_and_llm();
    if (err == ESP_OK) {
        (void)atlas_config_load(s_ctx.config);
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"cleared\":\"network_llm\",\"note\":\"reboot recommended\"}");
}

static esp_err_t reboot_handler(httpd_req_t *req)
{
    char body[128];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }
    (void)send_json(req, "{\"ok\":true,\"rebooting\":true}");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

static esp_err_t register_uri(httpd_handle_t server, const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    const httpd_uri_t route = {
        .uri = uri,
        .method = method,
        .handler = handler,
        .user_ctx = NULL,
    };
    return httpd_register_uri_handler(server, &route);
}

esp_err_t atlas_admin_http_start(atlas_config_t *config,
                                 atlas_ui_state_t *ui_state,
                                 atlas_admin_now_ms_fn_t now_ms_fn)
{
    if (config == NULL || ui_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_ctx.config = config;
    s_ctx.ui_state = ui_state;
    s_ctx.now_ms = now_ms_fn;

    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.server_port = 80;
    http_config.stack_size = 24576;
    http_config.max_uri_handlers = 64;
    http_config.recv_wait_timeout = 10;
    http_config.send_wait_timeout = 20;
    http_config.lru_purge_enable = true;

    ESP_RETURN_ON_ERROR(httpd_start(&s_ctx.server, &http_config), TAG, "httpd_start failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/", HTTP_GET, app_handler), TAG, "route / failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/app", HTTP_GET, app_handler), TAG, "route app failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/admin", HTTP_GET, admin_handler), TAG, "route admin failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/status", HTTP_GET, status_handler), TAG, "route status failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/status/lite", HTTP_GET, status_lite_handler), TAG, "route status lite failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/capabilities", HTTP_GET, capabilities_handler), TAG, "route capabilities failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/system/info", HTTP_GET, system_info_handler), TAG, "route system info failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/selftest", HTTP_GET, selftest_handler), TAG, "route selftest failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/diagnostics/turn", HTTP_GET, diagnostics_turn_handler), TAG, "route turn diagnostics failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/sr/status", HTTP_GET, sr_status_handler), TAG, "route sr status failed");
#if CONFIG_HTTPD_WS_SUPPORT
    ESP_RETURN_ON_ERROR(register_ws_uri(s_ctx.server, "/api/brain/ws", brain_ws_handler), TAG, "route brain websocket failed");
#endif
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/ota/status", HTTP_GET, ota_status_handler), TAG, "route ota status failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/ota/manifest", HTTP_GET, ota_manifest_handler), TAG, "route ota manifest failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/ota/packages", HTTP_GET, ota_packages_handler), TAG, "route ota packages failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/ota/apply", HTTP_POST, ota_apply_handler), TAG, "route ota apply failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/tools/list", HTTP_GET, tools_list_handler), TAG, "route tools list failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/tools/call", HTTP_POST, tools_call_handler), TAG, "route tools call failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/wifi/scan", HTTP_GET, wifi_scan_handler), TAG, "route wifi scan failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/audio/status", HTTP_GET, audio_status_handler), TAG, "route audio status failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/audio/beep", HTTP_POST, audio_beep_handler), TAG, "route audio beep failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/audio/mic-level", HTTP_POST, audio_mic_level_handler), TAG, "route audio mic failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/audio/opus-probe", HTTP_POST, audio_opus_probe_handler), TAG, "route opus probe failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/audio/opus-stream/status", HTTP_GET, audio_opus_stream_status_handler), TAG, "route opus stream status failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/audio/opus-stream/start", HTTP_POST, audio_opus_stream_start_handler), TAG, "route opus stream start failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/audio/opus-stream/stop", HTTP_POST, audio_opus_stream_stop_handler), TAG, "route opus stream stop failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/audio/play-url", HTTP_POST, audio_play_url_handler), TAG, "route audio play url failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/voice/turn", HTTP_POST, voice_turn_handler), TAG, "route voice turn failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/voice/wake", HTTP_POST, voice_wake_handler), TAG, "route voice wake failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/rover/stop", HTTP_POST, stop_handler), TAG, "route stop failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/rover/move", HTTP_POST, move_handler), TAG, "route move failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/app/expression", HTTP_POST, app_expression_handler), TAG, "route app expression failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/app/page", HTTP_POST, app_page_handler), TAG, "route app page failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/app/action", HTTP_POST, app_action_handler), TAG, "route app action failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/pet/event", HTTP_POST, pet_event_handler), TAG, "route pet event failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/pet/wake", HTTP_POST, pet_wake_handler), TAG, "route pet wake failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/config/wifi", HTTP_POST, save_wifi_handler), TAG, "route wifi failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/config/llm", HTTP_POST, save_llm_handler), TAG, "route llm failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/config/safety", HTTP_POST, save_safety_handler), TAG, "route safety failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/config/ui", HTTP_POST, save_ui_handler), TAG, "route ui failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/config/pomodoro", HTTP_POST, save_pomodoro_handler), TAG, "route pomodoro failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/config/calendar", HTTP_POST, save_calendar_handler), TAG, "route calendar failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/voice/text", HTTP_POST, voice_text_handler), TAG, "route voice failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/intent", HTTP_POST, brain_intent_handler), TAG, "route intent failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/brain/intent", HTTP_POST, brain_intent_handler), TAG, "route brain compat failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/config/reset", HTTP_POST, reset_handler), TAG, "route reset failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/system/reboot", HTTP_POST, reboot_handler), TAG, "route reboot failed");

    if (strcmp(s_ctx.config->llm.mode, "host") == 0 && s_ctx.config->llm.base_url[0] != '\0') {
        s_voice_wake_hit_count = 0;
        s_voice_wake_noise_rms = 0;
        s_voice_wake_noise_peak = 0;
        s_voice_wake_mute_until_ms = 0;
        strlcpy(s_voice_wake_last_reason, "auto", sizeof(s_voice_wake_last_reason));
        const esp_err_t wake_err = start_voice_wake_task();
        if (wake_err != ESP_OK) {
            ESP_LOGW(TAG, "auto voice wake start failed: %s", esp_err_to_name(wake_err));
        }
    }

    ESP_LOGI(TAG, "admin HTTP server started on port 80");
    return ESP_OK;
}
