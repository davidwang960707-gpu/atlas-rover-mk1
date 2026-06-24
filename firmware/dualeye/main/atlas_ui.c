#include "atlas_ui.h"
#include "atlas_display.h"

#include <string.h>
#include <stddef.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "atlas_audio.h"
#include "atlas_audio_service.h"
#include "atlas_runtime.h"
#include "atlas_scene.h"
#include "atlas_wifi.h"

static const char *TAG = "atlas_ui";

#define ATLAS_DEFAULT_SAFETY_STOP_MS 700u

static SemaphoreHandle_t s_ui_mutex;

static bool atlas_ui_ensure_lock(void)
{
    if (s_ui_mutex == NULL) {
        s_ui_mutex = xSemaphoreCreateRecursiveMutex();
    }
    return s_ui_mutex != NULL;
}

void atlas_ui_lock(void)
{
    if (atlas_ui_ensure_lock()) {
        (void)xSemaphoreTakeRecursive(s_ui_mutex, portMAX_DELAY);
    }
}

void atlas_ui_unlock(void)
{
    if (s_ui_mutex != NULL) {
        xSemaphoreGiveRecursive(s_ui_mutex);
    }
}

static bool is_motion_intent(atlas_voice_event_t event)
{
    return event == ATLAS_VOICE_EVENT_MOVE_FORWARD ||
           event == ATLAS_VOICE_EVENT_MOVE_BACKWARD ||
           event == ATLAS_VOICE_EVENT_TURN_LEFT ||
           event == ATLAS_VOICE_EVENT_TURN_RIGHT;
}

static bool is_persistent_page(atlas_page_t page)
{
    return page == ATLAS_PAGE_CLOCK ||
           page == ATLAS_PAGE_STATUS ||
           page == ATLAS_PAGE_ALARM ||
           page == ATLAS_PAGE_PHOTO ||
           page == ATLAS_PAGE_MUSIC ||
           page == ATLAS_PAGE_STORY ||
           page == ATLAS_PAGE_CHAT ||
           page == ATLAS_PAGE_CALENDAR ||
           page == ATLAS_PAGE_POMODORO;
}

static uint32_t safety_stop_delay_ms(uint16_t duration_ms)
{
    uint32_t delay_ms = (uint32_t)duration_ms + 200u;
    if (delay_ms < ATLAS_DEFAULT_SAFETY_STOP_MS) {
        delay_ms = ATLAS_DEFAULT_SAFETY_STOP_MS;
    }
    if (delay_ms > 2200u) {
        delay_ms = 2200u;
    }
    return delay_ms;
}

static void copy_state_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src == NULL ? "" : src);
}

static void update_pomodoro_state(atlas_ui_state_t *state, bool reset_clock, bool in_break, uint32_t now_ms)
{
    if (state == NULL || !state->pomodoro_running) {
        return;
    }

    if (state->pomodoro_focus_minutes == 0) {
        state->pomodoro_focus_minutes = 25u;
    }
    if (state->pomodoro_break_minutes == 0) {
        state->pomodoro_break_minutes = 5u;
    }

    if (reset_clock) {
        state->pomodoro_in_break = in_break;
        state->pomodoro_interval_ms = (uint32_t)(state->pomodoro_in_break ? state->pomodoro_break_minutes : state->pomodoro_focus_minutes) * 60u * 1000u;
        state->pomodoro_interval_started_ms = now_ms;
        state->pomodoro_progress_ms = 0u;
        return;
    }

    if (state->pomodoro_interval_ms == 0) {
        state->pomodoro_interval_ms = (uint32_t)state->pomodoro_focus_minutes * 60u * 1000u;
        state->pomodoro_interval_started_ms = now_ms;
    }

    const uint32_t elapsed = now_ms >= state->pomodoro_interval_started_ms ?
                                (now_ms - state->pomodoro_interval_started_ms) : 0u;
    state->pomodoro_progress_ms = elapsed > state->pomodoro_interval_ms ? state->pomodoro_interval_ms : elapsed;
    if (state->pomodoro_progress_ms < state->pomodoro_interval_ms) {
        return;
    }

    if (!state->pomodoro_in_break && state->pomodoro_break_minutes > 0) {
        state->pomodoro_in_break = true;
        state->pomodoro_interval_ms = (uint32_t)state->pomodoro_break_minutes * 60u * 1000u;
        state->pomodoro_interval_started_ms = now_ms;
        state->pomodoro_progress_ms = 0u;
        return;
    }

    state->pomodoro_running = false;
    state->pomodoro_interval_ms = 0u;
    state->pomodoro_progress_ms = 0u;
}

void atlas_ui_init(atlas_ui_state_t *state)
{
    if (state == NULL) {
        return;
    }

    atlas_ui_lock();
    *state = (atlas_ui_state_t) {
        .page = ATLAS_PAGE_EYES,
        .expression = ATLAS_EXPR_IDLE,
        .motion = ATLAS_MOTION_NONE,
        .audio_level = 0,
        .last_speed = 0,
        .last_event_ms = 0,
        .last_motion_ms = 0,
        .safety_stop_due_ms = 0,
        .last_ack = ATLAS_ROVER_ACK_NONE,
        .moving = false,
        .charging = false,
        .chat_mode = "pet_head",
        .chat_text = "",
        .calendar_title = "电子宠物日历",
        .calendar_note = "今日状态：待命，晚间记得充电",
        .pomodoro_task_name = "巡检任务",
        .pet_ip = "",
        .pomodoro_focus_minutes = 25,
        .pomodoro_break_minutes = 5,
        .pomodoro_running = false,
        .pomodoro_in_break = false,
        .pomodoro_interval_started_ms = 0,
        .pomodoro_interval_ms = 0,
        .pomodoro_progress_ms = 0,
    };
    atlas_pet_init(&state->pet);
    atlas_ui_unlock();
}

void atlas_ui_apply_config(atlas_ui_state_t *state, const atlas_config_t *config)
{
    if (state == NULL || config == NULL) {
        return;
    }

    atlas_ui_lock();
    copy_state_text(state->chat_mode, sizeof(state->chat_mode),
                    atlas_config_chat_mode_is_valid(config->ui.chat_mode) ? config->ui.chat_mode : "pet_head");
    copy_state_text(state->calendar_title, sizeof(state->calendar_title), config->calendar.title);
    copy_state_text(state->calendar_note, sizeof(state->calendar_note), config->calendar.note);
    copy_state_text(state->pomodoro_task_name, sizeof(state->pomodoro_task_name), config->pomodoro.task_name);
    state->pomodoro_focus_minutes = config->pomodoro.focus_minutes == 0 ? 25u : config->pomodoro.focus_minutes;
    state->pomodoro_break_minutes = config->pomodoro.break_minutes == 0 ? 5u : config->pomodoro.break_minutes;
    atlas_ui_unlock();
}

void atlas_ui_set_chat_text(atlas_ui_state_t *state, const char *text)
{
    if (state == NULL) {
        return;
    }
    atlas_ui_lock();
    copy_state_text(state->chat_text, sizeof(state->chat_text), text);
    atlas_ui_unlock();
}

void atlas_ui_set_calendar_text(atlas_ui_state_t *state, const char *title, const char *note)
{
    if (state == NULL) {
        return;
    }
    atlas_ui_lock();
    if (title != NULL) {
        copy_state_text(state->calendar_title, sizeof(state->calendar_title), title);
    }
    if (note != NULL) {
        copy_state_text(state->calendar_note, sizeof(state->calendar_note), note);
    }
    atlas_ui_unlock();
}

void atlas_ui_set_pomodoro_running(atlas_ui_state_t *state,
                                  bool running,
                                  bool in_break,
                                  uint32_t now_ms,
                                  const char *task_name,
                                  bool reset_counter)
{
    if (state == NULL) {
        return;
    }
    atlas_ui_lock();
    if (!running) {
        state->pomodoro_running = false;
        state->pomodoro_in_break = false;
        state->pomodoro_interval_started_ms = 0u;
        state->pomodoro_interval_ms = 0u;
        state->pomodoro_progress_ms = 0u;
        atlas_ui_unlock();
        return;
    }

    if (task_name != NULL && task_name[0] != '\0') {
        copy_state_text(state->pomodoro_task_name, sizeof(state->pomodoro_task_name), task_name);
    }
    state->pomodoro_running = true;
    if (state->pomodoro_focus_minutes == 0) {
        state->pomodoro_focus_minutes = 25u;
    }
    if (state->pomodoro_break_minutes == 0) {
        state->pomodoro_break_minutes = 5u;
    }
    update_pomodoro_state(state, true, in_break, now_ms);
    if (!reset_counter && state->pomodoro_interval_started_ms == 0) {
        state->pomodoro_interval_started_ms = now_ms;
        state->pomodoro_progress_ms = 0u;
    }
    atlas_ui_unlock();
}

static esp_err_t send_motion(atlas_ui_state_t *state, atlas_voice_intent_t intent, uint32_t now_ms)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err;
    if (intent.motion == ATLAS_MOTION_FORWARD || intent.motion == ATLAS_MOTION_BACKWARD) {
        err = atlas_rover_uart_send_move(intent.motion, intent.speed, intent.duration_ms);
    } else if (intent.motion == ATLAS_MOTION_LEFT || intent.motion == ATLAS_MOTION_RIGHT) {
        err = atlas_rover_uart_send_turn(intent.motion, intent.speed, intent.duration_ms);
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    if (err != ESP_OK) {
        state->page = ATLAS_PAGE_STATUS;
        state->expression = ATLAS_EXPR_ERROR;
        state->moving = false;
        state->motion = ATLAS_MOTION_NONE;
        ESP_LOGE(TAG, "motion command failed: %s", esp_err_to_name(err));
        return err;
    }

    state->page = ATLAS_PAGE_EYES;
    state->expression = ATLAS_EXPR_MOVING;
    state->motion = intent.motion;
    state->last_speed = intent.speed;
    state->last_motion_ms = now_ms;
    state->last_event_ms = now_ms;
    state->safety_stop_due_ms = now_ms + safety_stop_delay_ms(intent.duration_ms);
    state->moving = true;
    atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_PATROL, now_ms);

    ESP_LOGI(TAG,
             "motion intent accepted: motion=%s speed=%u duration=%u",
             atlas_motion_name(intent.motion),
             intent.speed,
             intent.duration_ms);
    return ESP_OK;
}

esp_err_t atlas_ui_stop(atlas_ui_state_t *state, uint32_t now_ms)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    atlas_ui_lock();
    const esp_err_t err = atlas_rover_uart_send_stop();
    state->page = ATLAS_PAGE_EYES;
    state->expression = ATLAS_EXPR_IDLE;
    state->motion = ATLAS_MOTION_NONE;
    state->moving = false;
    state->last_speed = 0;
    state->last_event_ms = now_ms;
    state->safety_stop_due_ms = 0;
    atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_STOP, now_ms);

    if (err != ESP_OK) {
        state->page = ATLAS_PAGE_STATUS;
        state->expression = ATLAS_EXPR_ERROR;
        ESP_LOGE(TAG, "stop command failed: %s", esp_err_to_name(err));
        atlas_ui_unlock();
        return err;
    }

    ESP_LOGI(TAG, "stop command sent");
    atlas_ui_unlock();
    return ESP_OK;
}

esp_err_t atlas_ui_handle_voice_intent(atlas_ui_state_t *state,
                                       atlas_voice_intent_t intent,
                                       uint32_t now_ms)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    atlas_ui_lock();
    ESP_LOGI(TAG,
             "voice intent: event=%s motion=%s speed=%u duration=%u",
             atlas_voice_event_name(intent.event),
             atlas_motion_name(intent.motion),
             intent.speed,
             intent.duration_ms);

    if (is_motion_intent(intent.event)) {
        const esp_err_t err = send_motion(state, intent, now_ms);
        atlas_ui_unlock();
        return err;
    }

    state->last_event_ms = now_ms;

    switch (intent.event) {
    case ATLAS_VOICE_EVENT_WAKE:
    case ATLAS_VOICE_EVENT_LISTENING:
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_VOICE_LISTEN, now_ms);
        state->page = ATLAS_PAGE_VOICE;
        state->expression = ATLAS_EXPR_LISTEN;
        state->audio_level = 24;
        break;

    case ATLAS_VOICE_EVENT_THINKING:
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_THINK, now_ms);
        state->page = ATLAS_PAGE_EYES;
        state->expression = ATLAS_EXPR_THINKING;
        break;

    case ATLAS_VOICE_EVENT_SPEAKING:
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_SPEAK, now_ms);
        state->page = ATLAS_PAGE_EYES;
        state->expression = ATLAS_EXPR_SPEAKING;
        state->audio_level = 58;
        break;

    case ATLAS_VOICE_EVENT_SUCCESS:
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_TOUCH, now_ms);
        state->page = ATLAS_PAGE_EYES;
        state->expression = ATLAS_EXPR_HAPPY;
        break;

    case ATLAS_VOICE_EVENT_STOP:
    {
        const esp_err_t err = atlas_ui_stop(state, now_ms);
        atlas_ui_unlock();
        return err;
    }

    case ATLAS_VOICE_EVENT_CHARGING:
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_CHARGE, now_ms);
        state->page = ATLAS_PAGE_STATUS;
        state->expression = ATLAS_EXPR_CHARGING;
        state->charging = true;
        break;

    case ATLAS_VOICE_EVENT_SLEEP:
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_REST, now_ms);
        state->page = ATLAS_PAGE_EYES;
        state->expression = ATLAS_EXPR_SLEEPY;
        break;

    case ATLAS_VOICE_EVENT_ERROR:
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_ERROR, now_ms);
        state->page = ATLAS_PAGE_STATUS;
        state->expression = ATLAS_EXPR_ERROR;
        break;

    case ATLAS_VOICE_EVENT_NONE:
    default:
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_INTERACTION, now_ms);
        state->page = ATLAS_PAGE_EYES;
        state->expression = ATLAS_EXPR_CURIOUS;
        break;
    }

    state->motion = ATLAS_MOTION_NONE;
    state->moving = false;
    atlas_ui_unlock();
    return ESP_OK;
}

esp_err_t atlas_ui_handle_pet_event(atlas_ui_state_t *state, atlas_pet_event_t event, uint32_t now_ms)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    atlas_ui_lock();
    if (state->moving && event != ATLAS_PET_EVENT_PATROL) {
        (void)atlas_ui_stop(state, now_ms);
    }

    atlas_pet_handle_event(&state->pet, event, now_ms);
    state->last_event_ms = now_ms;

    switch (event) {
    case ATLAS_PET_EVENT_MUSIC:
        state->page = ATLAS_PAGE_MUSIC;
        state->expression = ATLAS_EXPR_SPEAKING;
        state->audio_level = 64;
        break;
    case ATLAS_PET_EVENT_STORY:
        state->page = ATLAS_PAGE_STORY;
        state->expression = ATLAS_EXPR_SPEAKING;
        state->audio_level = 58;
        break;
    case ATLAS_PET_EVENT_CHAT:
    case ATLAS_PET_EVENT_SPEAK:
        state->page = ATLAS_PAGE_CHAT;
        state->expression = event == ATLAS_PET_EVENT_CHAT ? ATLAS_EXPR_LISTEN : ATLAS_EXPR_SPEAKING;
        state->audio_level = event == ATLAS_PET_EVENT_CHAT ? 28 : 58;
        break;
    case ATLAS_PET_EVENT_REST:
        state->page = ATLAS_PAGE_EYES;
        state->expression = ATLAS_EXPR_SLEEPY;
        state->audio_level = 0;
        break;
    case ATLAS_PET_EVENT_PATROL:
        state->page = ATLAS_PAGE_EYES;
        state->expression = ATLAS_EXPR_MOVING;
        state->audio_level = 0;
        break;
    case ATLAS_PET_EVENT_TOUCH:
    case ATLAS_PET_EVENT_PLAY:
    case ATLAS_PET_EVENT_FEED:
    case ATLAS_PET_EVENT_INTERACTION:
    case ATLAS_PET_EVENT_VOICE_LISTEN:
    case ATLAS_PET_EVENT_THINK:
    case ATLAS_PET_EVENT_STOP:
    case ATLAS_PET_EVENT_CHARGE:
    case ATLAS_PET_EVENT_ERROR:
    default:
        state->page = ATLAS_PAGE_EYES;
        state->expression = atlas_pet_expression(&state->pet);
        state->audio_level = 0;
        break;
    }

    state->motion = ATLAS_MOTION_NONE;
    state->moving = false;
    atlas_ui_unlock();
    return ESP_OK;
}

void atlas_ui_handle_chassis_ack(atlas_ui_state_t *state, atlas_rover_ack_t ack, uint32_t now_ms)
{
    if (state == NULL || ack == ATLAS_ROVER_ACK_NONE) {
        return;
    }

    atlas_ui_lock();
    state->last_ack = ack;
    state->last_event_ms = now_ms;

    if (ack == ATLAS_ROVER_ACK_OK) {
        ESP_LOGI(TAG, "chassis ACK OK");
        atlas_ui_unlock();
        return;
    }

    if (ack == ATLAS_ROVER_ACK_BUSY) {
        ESP_LOGW(TAG, "chassis ACK BUSY");
        state->expression = ATLAS_EXPR_CURIOUS;
        atlas_ui_unlock();
        return;
    }

    ESP_LOGE(TAG, "chassis ACK ERROR, forcing stop state");
    atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_ERROR, now_ms);
    state->page = ATLAS_PAGE_STATUS;
    state->expression = ATLAS_EXPR_ERROR;
    state->moving = false;
    state->motion = ATLAS_MOTION_NONE;
    state->safety_stop_due_ms = 0;
    atlas_ui_unlock();
}

void atlas_ui_tick(atlas_ui_state_t *state, const atlas_config_t *config, uint32_t now_ms)
{
    if (state == NULL) {
        return;
    }

    atlas_ui_lock();
    atlas_pet_tick(&state->pet, now_ms);

    if (state->moving && state->safety_stop_due_ms != 0 && now_ms >= state->safety_stop_due_ms) {
        ESP_LOGW(TAG, "DualEye safety timeout, sending STOP");
        (void)atlas_ui_stop(state, now_ms);
    }

    if (!state->moving && is_persistent_page(state->page) && state->audio_level == 0 &&
        (state->expression == ATLAS_EXPR_SPEAKING ||
         state->expression == ATLAS_EXPR_THINKING ||
         state->expression == ATLAS_EXPR_LISTEN) &&
        now_ms - state->last_event_ms > 3500u) {
        atlas_expression_t next_expression = atlas_pet_phase_is_character_active(&state->pet) ?
                                                atlas_pet_expression(&state->pet) : ATLAS_EXPR_IDLE;
        if (next_expression == ATLAS_EXPR_SPEAKING ||
            next_expression == ATLAS_EXPR_THINKING ||
            next_expression == ATLAS_EXPR_LISTEN) {
            next_expression = ATLAS_EXPR_IDLE;
        }
        state->expression = next_expression;
    }

    const bool explicit_recent = now_ms - state->last_event_ms <= 3500u;
    if (!state->moving && !explicit_recent && !is_persistent_page(state->page) &&
        state->expression != ATLAS_EXPR_CHARGING && state->expression != ATLAS_EXPR_ERROR &&
        atlas_pet_phase_is_character_active(&state->pet)) {
        state->page = ATLAS_PAGE_EYES;
        state->expression = atlas_pet_expression(&state->pet);
        state->motion = ATLAS_MOTION_NONE;
        state->audio_level = 0;
    }

    if (!state->moving && !is_persistent_page(state->page) && state->expression != ATLAS_EXPR_IDLE &&
        state->expression != ATLAS_EXPR_CHARGING && state->expression != ATLAS_EXPR_ERROR &&
        !atlas_pet_phase_is_character_active(&state->pet) && now_ms - state->last_event_ms > 3500) {
        state->page = ATLAS_PAGE_EYES;
        state->expression = ATLAS_EXPR_IDLE;
        state->motion = ATLAS_MOTION_NONE;
        state->audio_level = 0;
    }

    update_pomodoro_state(state, false, state->pomodoro_in_break, now_ms);

    atlas_wifi_status_t wifi;
    atlas_wifi_get_status(&wifi);
    if (wifi.sta_connected && wifi.sta_ip[0] != '\0') {
        copy_state_text(state->pet_ip, sizeof(state->pet_ip), wifi.sta_ip);
    } else if (wifi.ap_started && wifi.ap_ip[0] != '\0') {
        copy_state_text(state->pet_ip, sizeof(state->pet_ip), wifi.ap_ip);
    } else {
        state->pet_ip[0] = '\0';
    }

    atlas_audio_status_t audio;
    atlas_audio_get_status(&audio);

    atlas_audio_service_status_t audio_service;
    atlas_audio_service_get_status(&audio_service);

    atlas_scene_snapshot_t scene;
    atlas_scene_resolve(state,
                        config,
                        &wifi,
                        &audio,
                        &audio_service,
                        atlas_runtime_get_state(),
                        atlas_runtime_get_reason(),
                        now_ms,
                        &scene);

    atlas_display_payload_t payload = {
        .chat_text = "",
        .calendar_title = "",
        .calendar_note = "",
        .scene_state = "",
        .scene_title = "",
        .scene_subtitle = "",
        .scene_hint = "",
        .scene_left_role = "",
        .scene_right_role = "",
        .scene_severity = "info",
        .scene_needs_attention = false,
        .chat_mode = "",
        .pomodoro_running = state->pomodoro_running,
        .pomodoro_in_break = state->pomodoro_in_break,
        .pomodoro_progress_percent = 0,
        .pomodoro_remaining_ms = 0,
        .pomodoro_focus_minutes = state->pomodoro_focus_minutes,
        .pomodoro_break_minutes = state->pomodoro_break_minutes,
        .pomodoro_task_name = "",
        .pet_ip = "",
        .pet_ip_valid = false,
    };

    copy_state_text(payload.chat_mode, sizeof(payload.chat_mode),
                    atlas_config_chat_mode_is_valid(state->chat_mode) ? state->chat_mode : "pet_head");
    copy_state_text(payload.chat_text, sizeof(payload.chat_text), state->chat_text);
    copy_state_text(payload.calendar_title, sizeof(payload.calendar_title), state->calendar_title);
    copy_state_text(payload.calendar_note, sizeof(payload.calendar_note), state->calendar_note);
    copy_state_text(payload.scene_state, sizeof(payload.scene_state), scene.state);
    copy_state_text(payload.scene_title, sizeof(payload.scene_title), scene.title);
    copy_state_text(payload.scene_subtitle, sizeof(payload.scene_subtitle), scene.subtitle);
    copy_state_text(payload.scene_hint, sizeof(payload.scene_hint), scene.hint);
    copy_state_text(payload.scene_left_role, sizeof(payload.scene_left_role), scene.left_role);
    copy_state_text(payload.scene_right_role, sizeof(payload.scene_right_role), scene.right_role);
    copy_state_text(payload.scene_severity, sizeof(payload.scene_severity), scene.severity);
    payload.scene_needs_attention = scene.needs_attention;
    copy_state_text(payload.pomodoro_task_name, sizeof(payload.pomodoro_task_name), state->pomodoro_task_name);
    copy_state_text(payload.pet_ip, sizeof(payload.pet_ip), state->pet_ip);
    payload.pomodoro_running = state->pomodoro_running;
    payload.pomodoro_in_break = state->pomodoro_in_break;
    payload.pomodoro_focus_minutes = state->pomodoro_focus_minutes;
    payload.pomodoro_break_minutes = state->pomodoro_break_minutes;
    payload.pet_ip_valid = state->pet_ip[0] != '\0';

    if (state->pomodoro_running && state->pomodoro_interval_ms > 0) {
        const uint32_t elapsed = state->pomodoro_progress_ms;
        const uint32_t interval = state->pomodoro_interval_ms;
        const uint32_t remaining = elapsed >= interval ? 0 : (interval - elapsed);
        const uint32_t percent = (elapsed >= interval) ? 100u : (elapsed * 100u) / interval;
        payload.pomodoro_remaining_ms = remaining;
        payload.pomodoro_progress_percent = (uint8_t)(percent > 100u ? 100u : percent);
    }

    atlas_display_render(scene.page,
                         scene.expression,
                         state->motion,
                         scene.audio_level,
                         now_ms,
                         &payload);
    atlas_ui_unlock();
}
