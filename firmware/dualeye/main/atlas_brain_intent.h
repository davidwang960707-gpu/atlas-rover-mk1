#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "atlas_config.h"
#include "atlas_page.h"
#include "atlas_display.h"
#include "atlas_expression.h"
#include "atlas_pet.h"
#include "atlas_voice.h"
#include "atlas_ui.h"

#define ATLAS_BRAIN_INTENT_ACTION_MAX 48
#define ATLAS_BRAIN_INTENT_SPEECH_MAX 160
#define ATLAS_BRAIN_INTENT_REASON_MAX 96

#define ATLAS_BRAIN_POMODORO_MINUTES_MAX 120

typedef struct {
    float confidence;
    bool has_expression;
    atlas_expression_t expression;
    bool has_page;
    atlas_page_t page;
    bool has_chat_mode;
    char chat_mode[ATLAS_CHAT_MODE_MAX];
    bool has_action;
    char action[ATLAS_BRAIN_INTENT_ACTION_MAX];
    bool has_motion;
    atlas_voice_intent_t motion;
    bool has_pet_event;
    atlas_pet_event_t pet_event;
    bool has_speech;
    char speech[ATLAS_BRAIN_INTENT_SPEECH_MAX];
    bool has_chat_text;
    char chat_text[ATLAS_BRAIN_INTENT_SPEECH_MAX];
    bool has_calendar_title;
    char calendar_title[ATLAS_CALENDAR_TITLE_MAX];
    bool has_calendar_note;
    char calendar_note[ATLAS_CALENDAR_NOTE_MAX];
    bool has_pomodoro_task_name;
    char pomodoro_task_name[ATLAS_POMODORO_TASK_MAX];
    bool has_pomodoro_focus_minutes;
    uint16_t pomodoro_focus_minutes;
    bool has_pomodoro_break_minutes;
    uint16_t pomodoro_break_minutes;
    bool has_pomodoro_running;
    bool pomodoro_running;
    bool has_pomodoro_in_break;
    bool pomodoro_in_break;
    bool requires_confirmation;
    char safety_reason[ATLAS_BRAIN_INTENT_REASON_MAX];
} atlas_brain_intent_t;

void atlas_brain_intent_init(atlas_brain_intent_t *intent);
esp_err_t atlas_brain_intent_parse_json(const char *json,
                                           atlas_brain_intent_t *intent,
                                           char *error,
                                           size_t error_size);
esp_err_t atlas_brain_intent_apply_intent(const atlas_config_t *config,
                                             atlas_ui_state_t *state,
                                             const atlas_brain_intent_t *intent,
                                             uint32_t now_ms,
                                             char *result,
                                             size_t result_size);
