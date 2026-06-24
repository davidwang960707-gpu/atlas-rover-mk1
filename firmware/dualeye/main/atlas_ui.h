#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "atlas_page.h"
#include "atlas_config.h"
#include "atlas_pet.h"
#include "atlas_rover_uart.h"
#include "atlas_wifi.h"
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
    atlas_pet_state_t pet;
    bool moving;
    bool charging;
    char chat_mode[ATLAS_CHAT_MODE_MAX];
    char chat_text[ATLAS_CHAT_TEXT_MAX];
    char calendar_title[ATLAS_CALENDAR_TITLE_MAX];
    char calendar_note[ATLAS_CALENDAR_NOTE_MAX];
    char pomodoro_task_name[ATLAS_POMODORO_TASK_MAX];
    char pet_ip[ATLAS_WIFI_IP_MAX];
    bool pomodoro_running;
    bool pomodoro_in_break;
    uint16_t pomodoro_focus_minutes;
    uint16_t pomodoro_break_minutes;
    uint32_t pomodoro_interval_started_ms;
    uint32_t pomodoro_interval_ms;
    uint32_t pomodoro_progress_ms;
} atlas_ui_state_t;

void atlas_ui_init(atlas_ui_state_t *state);
void atlas_ui_lock(void);
void atlas_ui_unlock(void);
void atlas_ui_apply_config(atlas_ui_state_t *state, const atlas_config_t *config);
void atlas_ui_set_pomodoro_running(atlas_ui_state_t *state,
                                  bool running,
                                  bool in_break,
                                  uint32_t now_ms,
                                  const char *task_name,
                                  bool reset_counter);
void atlas_ui_set_chat_text(atlas_ui_state_t *state, const char *text);
void atlas_ui_set_calendar_text(atlas_ui_state_t *state,
                               const char *title,
                               const char *note);
esp_err_t atlas_ui_handle_voice_intent(atlas_ui_state_t *state,
                                       atlas_voice_intent_t intent,
                                       uint32_t now_ms);
esp_err_t atlas_ui_stop(atlas_ui_state_t *state, uint32_t now_ms);
esp_err_t atlas_ui_handle_pet_event(atlas_ui_state_t *state, atlas_pet_event_t event, uint32_t now_ms);
void atlas_ui_handle_chassis_ack(atlas_ui_state_t *state, atlas_rover_ack_t ack, uint32_t now_ms);
void atlas_ui_tick(atlas_ui_state_t *state, const atlas_config_t *config, uint32_t now_ms);
