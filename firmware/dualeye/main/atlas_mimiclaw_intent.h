#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "atlas_config.h"
#include "atlas_display.h"
#include "atlas_expression.h"
#include "atlas_ui.h"

#define ATLAS_MIMICLAW_INTENT_ACTION_MAX 48
#define ATLAS_MIMICLAW_INTENT_SPEECH_MAX 160
#define ATLAS_MIMICLAW_INTENT_REASON_MAX 96

typedef struct {
    float confidence;
    bool has_expression;
    atlas_expression_t expression;
    bool has_page;
    atlas_page_t page;
    bool has_action;
    char action[ATLAS_MIMICLAW_INTENT_ACTION_MAX];
    bool has_motion;
    atlas_voice_intent_t motion;
    bool has_speech;
    char speech[ATLAS_MIMICLAW_INTENT_SPEECH_MAX];
    bool requires_confirmation;
    char safety_reason[ATLAS_MIMICLAW_INTENT_REASON_MAX];
} atlas_mimiclaw_intent_t;

void atlas_mimiclaw_intent_init(atlas_mimiclaw_intent_t *intent);
esp_err_t atlas_mimiclaw_intent_parse_json(const char *json,
                                           atlas_mimiclaw_intent_t *intent,
                                           char *error,
                                           size_t error_size);
esp_err_t atlas_mimiclaw_intent_apply_intent(const atlas_config_t *config,
                                             atlas_ui_state_t *state,
                                             const atlas_mimiclaw_intent_t *intent,
                                             uint32_t now_ms,
                                             char *result,
                                             size_t result_size);
