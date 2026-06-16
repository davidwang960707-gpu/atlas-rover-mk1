#include "atlas_ui.h"

#include <stddef.h>

#include "esp_log.h"

static const char *TAG = "atlas_ui";

#define ATLAS_DEFAULT_SAFETY_STOP_MS 700u

static bool is_motion_intent(atlas_voice_event_t event)
{
    return event == ATLAS_VOICE_EVENT_MOVE_FORWARD ||
           event == ATLAS_VOICE_EVENT_MOVE_BACKWARD ||
           event == ATLAS_VOICE_EVENT_TURN_LEFT ||
           event == ATLAS_VOICE_EVENT_TURN_RIGHT;
}

void atlas_ui_init(atlas_ui_state_t *state)
{
    if (state == NULL) {
        return;
    }

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
    };
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
        err = atlas_rover_uart_send_turn(intent.motion, intent.speed);
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
    state->safety_stop_due_ms = now_ms + ATLAS_DEFAULT_SAFETY_STOP_MS;
    state->moving = true;

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

    const esp_err_t err = atlas_rover_uart_send_stop();
    state->page = ATLAS_PAGE_EYES;
    state->expression = ATLAS_EXPR_IDLE;
    state->motion = ATLAS_MOTION_NONE;
    state->moving = false;
    state->last_speed = 0;
    state->last_event_ms = now_ms;
    state->safety_stop_due_ms = 0;

    if (err != ESP_OK) {
        state->page = ATLAS_PAGE_STATUS;
        state->expression = ATLAS_EXPR_ERROR;
        ESP_LOGE(TAG, "stop command failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "stop command sent");
    return ESP_OK;
}

esp_err_t atlas_ui_handle_voice_intent(atlas_ui_state_t *state,
                                       atlas_voice_intent_t intent,
                                       uint32_t now_ms)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG,
             "voice intent: event=%s motion=%s speed=%u duration=%u",
             atlas_voice_event_name(intent.event),
             atlas_motion_name(intent.motion),
             intent.speed,
             intent.duration_ms);

    if (is_motion_intent(intent.event)) {
        return send_motion(state, intent, now_ms);
    }

    state->last_event_ms = now_ms;

    switch (intent.event) {
    case ATLAS_VOICE_EVENT_WAKE:
    case ATLAS_VOICE_EVENT_LISTENING:
        state->page = ATLAS_PAGE_VOICE;
        state->expression = ATLAS_EXPR_LISTEN;
        state->audio_level = 24;
        break;

    case ATLAS_VOICE_EVENT_THINKING:
        state->page = ATLAS_PAGE_EYES;
        state->expression = ATLAS_EXPR_THINKING;
        break;

    case ATLAS_VOICE_EVENT_SPEAKING:
        state->page = ATLAS_PAGE_EYES;
        state->expression = ATLAS_EXPR_SPEAKING;
        state->audio_level = 58;
        break;

    case ATLAS_VOICE_EVENT_SUCCESS:
        state->page = ATLAS_PAGE_EYES;
        state->expression = ATLAS_EXPR_HAPPY;
        break;

    case ATLAS_VOICE_EVENT_STOP:
        return atlas_ui_stop(state, now_ms);

    case ATLAS_VOICE_EVENT_CHARGING:
        state->page = ATLAS_PAGE_STATUS;
        state->expression = ATLAS_EXPR_CHARGING;
        state->charging = true;
        break;

    case ATLAS_VOICE_EVENT_SLEEP:
        state->page = ATLAS_PAGE_EYES;
        state->expression = ATLAS_EXPR_SLEEPY;
        break;

    case ATLAS_VOICE_EVENT_ERROR:
        state->page = ATLAS_PAGE_STATUS;
        state->expression = ATLAS_EXPR_ERROR;
        break;

    case ATLAS_VOICE_EVENT_NONE:
    default:
        state->page = ATLAS_PAGE_EYES;
        state->expression = ATLAS_EXPR_CURIOUS;
        break;
    }

    state->motion = ATLAS_MOTION_NONE;
    state->moving = false;
    return ESP_OK;
}

void atlas_ui_handle_chassis_ack(atlas_ui_state_t *state, atlas_rover_ack_t ack, uint32_t now_ms)
{
    if (state == NULL || ack == ATLAS_ROVER_ACK_NONE) {
        return;
    }

    state->last_ack = ack;
    state->last_event_ms = now_ms;

    if (ack == ATLAS_ROVER_ACK_OK) {
        ESP_LOGI(TAG, "chassis ACK OK");
        return;
    }

    if (ack == ATLAS_ROVER_ACK_BUSY) {
        ESP_LOGW(TAG, "chassis ACK BUSY");
        state->expression = ATLAS_EXPR_CURIOUS;
        return;
    }

    ESP_LOGE(TAG, "chassis ACK ERROR, forcing stop state");
    state->page = ATLAS_PAGE_STATUS;
    state->expression = ATLAS_EXPR_ERROR;
    state->moving = false;
    state->motion = ATLAS_MOTION_NONE;
    state->safety_stop_due_ms = 0;
}

void atlas_ui_tick(atlas_ui_state_t *state, uint32_t now_ms)
{
    if (state == NULL) {
        return;
    }

    if (state->moving && state->safety_stop_due_ms != 0 && now_ms >= state->safety_stop_due_ms) {
        ESP_LOGW(TAG, "DualEye safety timeout, sending STOP");
        (void)atlas_ui_stop(state, now_ms);
    }

    if (!state->moving && state->expression != ATLAS_EXPR_IDLE &&
        state->expression != ATLAS_EXPR_CHARGING && state->expression != ATLAS_EXPR_ERROR &&
        now_ms - state->last_event_ms > 3500) {
        state->page = ATLAS_PAGE_EYES;
        state->expression = ATLAS_EXPR_IDLE;
        state->motion = ATLAS_MOTION_NONE;
        state->audio_level = 0;
    }

    atlas_display_render(state->page,
                         state->expression,
                         state->motion,
                         state->audio_level,
                         now_ms);
}
