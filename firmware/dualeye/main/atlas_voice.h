#pragma once

#include <stdint.h>

#include "atlas_expression.h"

typedef enum {
    ATLAS_VOICE_EVENT_NONE = 0,
    ATLAS_VOICE_EVENT_WAKE,
    ATLAS_VOICE_EVENT_LISTENING,
    ATLAS_VOICE_EVENT_THINKING,
    ATLAS_VOICE_EVENT_SPEAKING,
    ATLAS_VOICE_EVENT_SUCCESS,
    ATLAS_VOICE_EVENT_STOP,
    ATLAS_VOICE_EVENT_MOVE_FORWARD,
    ATLAS_VOICE_EVENT_MOVE_BACKWARD,
    ATLAS_VOICE_EVENT_TURN_LEFT,
    ATLAS_VOICE_EVENT_TURN_RIGHT,
    ATLAS_VOICE_EVENT_CHARGING,
    ATLAS_VOICE_EVENT_SLEEP,
    ATLAS_VOICE_EVENT_ERROR,
} atlas_voice_event_t;

typedef struct {
    atlas_voice_event_t event;
    atlas_motion_t motion;
    uint8_t speed;
    uint16_t duration_ms;
} atlas_voice_intent_t;

const char *atlas_voice_event_name(atlas_voice_event_t event);
atlas_voice_intent_t atlas_voice_intent_from_event(atlas_voice_event_t event);
atlas_voice_intent_t atlas_voice_intent_from_text(const char *text);
