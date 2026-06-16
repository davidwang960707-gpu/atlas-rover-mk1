#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "atlas_display.h"
#include "atlas_rover_uart.h"
#include "atlas_voice.h"

typedef struct {
    atlas_page_t page;
    atlas_expression_t expression;
    atlas_motion_t motion;
    uint8_t audio_level;
    uint8_t last_speed;
    uint32_t last_event_ms;
    uint32_t last_motion_ms;
    uint32_t safety_stop_due_ms;
    atlas_rover_ack_t last_ack;
    bool moving;
    bool charging;
} atlas_ui_state_t;

void atlas_ui_init(atlas_ui_state_t *state);
esp_err_t atlas_ui_handle_voice_intent(atlas_ui_state_t *state,
                                       atlas_voice_intent_t intent,
                                       uint32_t now_ms);
esp_err_t atlas_ui_stop(atlas_ui_state_t *state, uint32_t now_ms);
void atlas_ui_handle_chassis_ack(atlas_ui_state_t *state, atlas_rover_ack_t ack, uint32_t now_ms);
void atlas_ui_tick(atlas_ui_state_t *state, uint32_t now_ms);
