#include "atlas_voice.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static int contains_ascii_token(const char *text, const char *token)
{
    if (text == NULL || token == NULL || token[0] == '\0') {
        return 0;
    }

    const size_t token_len = strlen(token);
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        size_t matched = 0;
        while (matched < token_len && cursor[matched] != '\0') {
            const int a = tolower((unsigned char)cursor[matched]);
            const int b = tolower((unsigned char)token[matched]);
            if (a != b) {
                break;
            }
            matched++;
        }
        if (matched == token_len) {
            return 1;
        }
    }
    return 0;
}

const char *atlas_voice_event_name(atlas_voice_event_t event)
{
    switch (event) {
    case ATLAS_VOICE_EVENT_WAKE:
        return "wake";
    case ATLAS_VOICE_EVENT_LISTENING:
        return "listening";
    case ATLAS_VOICE_EVENT_THINKING:
        return "thinking";
    case ATLAS_VOICE_EVENT_SPEAKING:
        return "speaking";
    case ATLAS_VOICE_EVENT_SUCCESS:
        return "success";
    case ATLAS_VOICE_EVENT_STOP:
        return "stop";
    case ATLAS_VOICE_EVENT_MOVE_FORWARD:
        return "move_forward";
    case ATLAS_VOICE_EVENT_MOVE_BACKWARD:
        return "move_backward";
    case ATLAS_VOICE_EVENT_TURN_LEFT:
        return "turn_left";
    case ATLAS_VOICE_EVENT_TURN_RIGHT:
        return "turn_right";
    case ATLAS_VOICE_EVENT_CHARGING:
        return "charging";
    case ATLAS_VOICE_EVENT_SLEEP:
        return "sleep";
    case ATLAS_VOICE_EVENT_ERROR:
        return "error";
    case ATLAS_VOICE_EVENT_NONE:
    default:
        return "none";
    }
}

atlas_voice_intent_t atlas_voice_intent_from_event(atlas_voice_event_t event)
{
    atlas_voice_intent_t intent = {
        .event = event,
        .motion = ATLAS_MOTION_NONE,
        .speed = 40,
        .duration_ms = 500,
    };

    switch (event) {
    case ATLAS_VOICE_EVENT_MOVE_FORWARD:
        intent.motion = ATLAS_MOTION_FORWARD;
        break;
    case ATLAS_VOICE_EVENT_MOVE_BACKWARD:
        intent.motion = ATLAS_MOTION_BACKWARD;
        intent.speed = 35;
        intent.duration_ms = 400;
        break;
    case ATLAS_VOICE_EVENT_TURN_LEFT:
        intent.motion = ATLAS_MOTION_LEFT;
        intent.speed = 30;
        intent.duration_ms = 350;
        break;
    case ATLAS_VOICE_EVENT_TURN_RIGHT:
        intent.motion = ATLAS_MOTION_RIGHT;
        intent.speed = 30;
        intent.duration_ms = 350;
        break;
    default:
        break;
    }

    return intent;
}

atlas_voice_intent_t atlas_voice_intent_from_text(const char *text)
{
    if (contains_ascii_token(text, "stop") || contains_ascii_token(text, "tingzhi")) {
        return atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_STOP);
    }
    if (contains_ascii_token(text, "forward") || contains_ascii_token(text, "qianjin")) {
        return atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_MOVE_FORWARD);
    }
    if (contains_ascii_token(text, "back") || contains_ascii_token(text, "houtui")) {
        return atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_MOVE_BACKWARD);
    }
    if (contains_ascii_token(text, "left") || contains_ascii_token(text, "zuozhuan")) {
        return atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_TURN_LEFT);
    }
    if (contains_ascii_token(text, "right") || contains_ascii_token(text, "youzhuan")) {
        return atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_TURN_RIGHT);
    }
    if (contains_ascii_token(text, "listen")) {
        return atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_LISTENING);
    }
    if (contains_ascii_token(text, "think")) {
        return atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_THINKING);
    }
    if (contains_ascii_token(text, "speak")) {
        return atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_SPEAKING);
    }
    if (contains_ascii_token(text, "charge")) {
        return atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_CHARGING);
    }
    if (contains_ascii_token(text, "sleep")) {
        return atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_SLEEP);
    }
    if (contains_ascii_token(text, "error")) {
        return atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_ERROR);
    }
    return atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_NONE);
}
