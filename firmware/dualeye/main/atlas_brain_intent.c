#include "atlas_brain_intent.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "atlas_brain";

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0) {
        strlcpy(error, message, error_size);
    }
}

static bool json_string_to_buffer(const cJSON *object, const char *key, char *out, size_t out_size)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(value) || value->valuestring == NULL || out == NULL || out_size == 0) {
        return false;
    }
    strlcpy(out, value->valuestring, out_size);
    return true;
}

static bool json_bool_to_bool(const cJSON *value, bool *out)
{
    if (out == NULL || value == NULL) {
        return false;
    }
    if (cJSON_IsBool(value)) {
        *out = cJSON_IsTrue(value);
        return true;
    }
    if (cJSON_IsNumber(value)) {
        *out = value->valuedouble != 0.0;
        return true;
    }
    if (cJSON_IsString(value) && value->valuestring != NULL) {
        if (strcasecmp(value->valuestring, "true") == 0 || strcmp(value->valuestring, "1") == 0) {
            *out = true;
            return true;
        }
        if (strcasecmp(value->valuestring, "false") == 0 || strcmp(value->valuestring, "0") == 0) {
            *out = false;
            return true;
        }
    }
    return false;
}

static bool json_u16_to_u16(const cJSON *object, const char *key, uint16_t *out)
{
    if (object == NULL || key == NULL || out == NULL) {
        return false;
    }

    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(value)) {
        if (value->valueint < 0 || value->valueint > 65535) {
            return false;
        }
        *out = (uint16_t)value->valueint;
        return true;
    }
    if (cJSON_IsString(value) && value->valuestring != NULL) {
        const int parsed = atoi(value->valuestring);
        if (parsed < 0 || parsed > 65535) {
            return false;
        }
        *out = (uint16_t)parsed;
        return true;
    }
    return false;
}

static bool motion_from_direction(const char *direction, atlas_voice_intent_t *motion)
{
    if (direction == NULL || motion == NULL) {
        return false;
    }

    if (strcmp(direction, "forward") == 0 || strcmp(direction, "F") == 0) {
        *motion = atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_MOVE_FORWARD);
    } else if (strcmp(direction, "backward") == 0 || strcmp(direction, "back") == 0 ||
               strcmp(direction, "B") == 0) {
        *motion = atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_MOVE_BACKWARD);
    } else if (strcmp(direction, "left") == 0 || strcmp(direction, "L") == 0) {
        *motion = atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_TURN_LEFT);
    } else if (strcmp(direction, "right") == 0 || strcmp(direction, "R") == 0) {
        *motion = atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_TURN_RIGHT);
    } else {
        return false;
    }
    return true;
}

static bool parse_motion_object(const cJSON *motion, atlas_voice_intent_t *out, char *error, size_t error_size)
{
    char direction[16] = "";
    if (!cJSON_IsObject(motion) ||
        !json_string_to_buffer(motion, "direction", direction, sizeof(direction)) ||
        !motion_from_direction(direction, out)) {
        set_error(error, error_size, "bad motion direction");
        return false;
    }

    const cJSON *speed = cJSON_GetObjectItemCaseSensitive(motion, "speed");
    if (cJSON_IsNumber(speed) && speed->valueint > 0) {
        out->speed = (uint8_t)speed->valueint;
    }

    const cJSON *duration_ms = cJSON_GetObjectItemCaseSensitive(motion, "duration_ms");
    if (cJSON_IsNumber(duration_ms) && duration_ms->valueint > 0) {
        out->duration_ms = (uint16_t)duration_ms->valueint;
    }
    return true;
}

static void parse_calendar_payload(const cJSON *payload, atlas_brain_intent_t *intent)
{
    if (payload == NULL || intent == NULL) {
        return;
    }

    char title[ATLAS_CALENDAR_TITLE_MAX] = "";
    char note[ATLAS_CALENDAR_NOTE_MAX] = "";
    if (json_string_to_buffer(payload, "title", title, sizeof(title))) {
        strlcpy(intent->calendar_title, title, sizeof(intent->calendar_title));
        intent->has_calendar_title = true;
    }
    if (json_string_to_buffer(payload, "note", note, sizeof(note))) {
        strlcpy(intent->calendar_note, note, sizeof(intent->calendar_note));
        intent->has_calendar_note = true;
    }
    if (intent->has_calendar_title || intent->has_calendar_note) {
        return;
    }
    if (json_string_to_buffer(payload, "content", note, sizeof(note))) {
        strlcpy(intent->calendar_note, note, sizeof(intent->calendar_note));
        intent->has_calendar_note = true;
    }
}

static void parse_pomodoro_payload(const cJSON *payload, atlas_brain_intent_t *intent)
{
    if (payload == NULL || intent == NULL) {
        return;
    }

    char task[ATLAS_POMODORO_TASK_MAX] = "";
    uint16_t value = 0;
    bool bval = false;

    if (json_string_to_buffer(payload, "task_name", task, sizeof(task))) {
        strlcpy(intent->pomodoro_task_name, task, sizeof(intent->pomodoro_task_name));
        intent->has_pomodoro_task_name = true;
    }
    if (json_string_to_buffer(payload, "task", task, sizeof(task))) {
        strlcpy(intent->pomodoro_task_name, task, sizeof(intent->pomodoro_task_name));
        intent->has_pomodoro_task_name = true;
    }

    if (json_u16_to_u16(payload, "focus_minutes", &value) || json_u16_to_u16(payload, "focus", &value)) {
        if (value == 0) {
            value = 25u;
        }
        if (value > ATLAS_BRAIN_POMODORO_MINUTES_MAX) {
            value = ATLAS_BRAIN_POMODORO_MINUTES_MAX;
        }
        intent->pomodoro_focus_minutes = value;
        intent->has_pomodoro_focus_minutes = true;
    }

    if (json_u16_to_u16(payload, "break_minutes", &value) || json_u16_to_u16(payload, "break", &value)) {
        if (value == 0) {
            value = 5u;
        }
        if (value > ATLAS_BRAIN_POMODORO_MINUTES_MAX) {
            value = ATLAS_BRAIN_POMODORO_MINUTES_MAX;
        }
        intent->pomodoro_break_minutes = value;
        intent->has_pomodoro_break_minutes = true;
    }

    if (json_bool_to_bool(cJSON_GetObjectItemCaseSensitive(payload, "running"), &bval)) {
        intent->pomodoro_running = bval;
        intent->has_pomodoro_running = true;
    }
    if (json_bool_to_bool(cJSON_GetObjectItemCaseSensitive(payload, "in_break"), &bval)) {
        intent->pomodoro_in_break = bval;
        intent->has_pomodoro_in_break = true;
    }

    const cJSON *action = cJSON_GetObjectItemCaseSensitive(payload, "action");
    if (cJSON_IsString(action) && action->valuestring != NULL) {
        if (strcmp(action->valuestring, "start") == 0) {
            intent->pomodoro_running = true;
            intent->has_pomodoro_running = true;
        } else if (strcmp(action->valuestring, "stop") == 0) {
            intent->pomodoro_running = false;
            intent->has_pomodoro_running = true;
        }
    }
}

static bool parse_chat_mode_payload(const cJSON *payload, atlas_brain_intent_t *intent)
{
    if (payload == NULL || intent == NULL) {
        return false;
    }

    char mode[ATLAS_CHAT_MODE_MAX] = "";
    if (!json_string_to_buffer(payload, "chat_mode", mode, sizeof(mode)) &&
        !json_string_to_buffer(payload, "mode", mode, sizeof(mode))) {
        return false;
    }
    if (!atlas_config_chat_mode_is_valid(mode)) {
        return false;
    }

    strlcpy(intent->chat_mode, mode, sizeof(intent->chat_mode));
    intent->has_chat_mode = true;
    return true;
}

static bool map_pet_state_to_expression(const char *state, atlas_expression_t *expression)
{
    if (state == NULL || expression == NULL) {
        return false;
    }
    if (strcmp(state, "idle") == 0 || strcmp(state, "blink") == 0) {
        *expression = ATLAS_EXPR_IDLE;
    } else if (strcmp(state, "listen") == 0) {
        *expression = ATLAS_EXPR_LISTEN;
    } else if (strcmp(state, "speak") == 0 || strcmp(state, "speaking") == 0) {
        *expression = ATLAS_EXPR_SPEAKING;
    } else if (strcmp(state, "sing") == 0 || strcmp(state, "music") == 0) {
        *expression = ATLAS_EXPR_SPEAKING;
    } else if (strcmp(state, "happy") == 0 || strcmp(state, "laugh") == 0) {
        *expression = ATLAS_EXPR_HAPPY;
    } else if (strcmp(state, "cry") == 0 || strcmp(state, "sad") == 0) {
        *expression = ATLAS_EXPR_CRY;
    } else if (strcmp(state, "sleepy") == 0 || strcmp(state, "sleep") == 0) {
        *expression = ATLAS_EXPR_SLEEPY;
    } else if (strcmp(state, "think") == 0 || strcmp(state, "thinking") == 0 || strcmp(state, "offline") == 0) {
        *expression = ATLAS_EXPR_THINKING;
    } else if (strcmp(state, "surprised") == 0 || strcmp(state, "surprise") == 0) {
        *expression = ATLAS_EXPR_SURPRISED;
    } else {
        return false;
    }
    return true;
}

static bool parse_pet_visual_payload(atlas_brain_intent_t *intent,
                                     const cJSON *payload,
                                     const char *key,
                                     char *error,
                                     size_t error_size)
{
    if (intent == NULL || payload == NULL || key == NULL) {
        return false;
    }

    char state_name[24] = "";
    if (!json_string_to_buffer(payload, key, state_name, sizeof(state_name))) {
        set_error(error, error_size, "bad pet state");
        return false;
    }

    atlas_expression_t expression = ATLAS_EXPR_IDLE;
    if (!map_pet_state_to_expression(state_name, &expression)) {
        set_error(error, error_size, "bad pet state");
        return false;
    }

    strlcpy(intent->chat_mode, "pet_head", sizeof(intent->chat_mode));
    intent->has_chat_mode = true;
    intent->page = (strcmp(state_name, "sing") == 0 || strcmp(state_name, "music") == 0) ? ATLAS_PAGE_MUSIC : ATLAS_PAGE_CHAT;
    intent->has_page = true;
    intent->expression = expression;
    intent->has_expression = true;
    if (strcmp(state_name, "sing") == 0 || strcmp(state_name, "music") == 0) {
        strlcpy(intent->action, "music.play", sizeof(intent->action));
        intent->has_action = true;
    } else if (strcmp(state_name, "speak") == 0 || strcmp(state_name, "speaking") == 0) {
        strlcpy(intent->action, "chat.reply", sizeof(intent->action));
        intent->has_action = true;
    }

    char value[ATLAS_BRAIN_INTENT_SPEECH_MAX] = "";
    if (json_string_to_buffer(payload, "right_text", value, sizeof(value)) ||
        json_string_to_buffer(payload, "text", value, sizeof(value)) ||
        json_string_to_buffer(payload, "chat_text", value, sizeof(value))) {
        strlcpy(intent->chat_text, value, sizeof(intent->chat_text));
        intent->has_chat_text = true;
    }
    return true;
}

static bool parse_tool_payload(atlas_brain_intent_t *intent,
                              const cJSON *payload,
                              const char *tool,
                              char *error,
                              size_t error_size)
{
    bool parsed_any = false;
    char value[ATLAS_BRAIN_INTENT_SPEECH_MAX] = "";
    if (payload == NULL || intent == NULL) {
        return false;
    }

    if (json_string_to_buffer(payload, "text", value, sizeof(value))) {
        strlcpy(intent->chat_text, value, sizeof(intent->chat_text));
        intent->has_chat_text = true;
        parsed_any = true;
    }
    if (json_string_to_buffer(payload, "chat_text", value, sizeof(value))) {
        strlcpy(intent->chat_text, value, sizeof(intent->chat_text));
        intent->has_chat_text = true;
        parsed_any = true;
    }
    if (json_string_to_buffer(payload, "speech", value, sizeof(value))) {
        strlcpy(intent->speech, value, sizeof(intent->speech));
        intent->has_speech = true;
        if (!intent->has_chat_text && value[0] != '\0') {
            strlcpy(intent->chat_text, value, sizeof(intent->chat_text));
            intent->has_chat_text = true;
        }
        parsed_any = true;
    }

    if (cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(payload, "chat"))) {
        const cJSON *chat = cJSON_GetObjectItemCaseSensitive(payload, "chat");
        if (json_string_to_buffer(chat, "text", value, sizeof(value))) {
            strlcpy(intent->chat_text, value, sizeof(intent->chat_text));
            intent->has_chat_text = true;
            parsed_any = true;
        }
        if (json_string_to_buffer(chat, "speech", value, sizeof(value))) {
            strlcpy(intent->speech, value, sizeof(intent->speech));
            intent->has_speech = true;
            if (!intent->has_chat_text) {
                strlcpy(intent->chat_text, value, sizeof(intent->chat_text));
                intent->has_chat_text = true;
            }
            parsed_any = true;
        }
    }

    parse_calendar_payload(payload, intent);
    parse_pomodoro_payload(payload, intent);
    if (parse_chat_mode_payload(payload, intent)) {
        parsed_any = true;
    }

    if (json_string_to_buffer(payload, "action", value, sizeof(value))) {
        strlcpy(intent->action, value, sizeof(intent->action));
        if (value[0] != '\0') {
            intent->has_action = true;
            parsed_any = true;
        }
    }

    if (strcmp(tool, "atlas_calendar") == 0 || strcmp(tool, "calendar.update") == 0 ||
        strcmp(tool, "atlas_calendar_update") == 0 || strcmp(tool, "atlas.calendar.set_note") == 0 ||
        strcmp(tool, "atlas.calendar.today") == 0) {
        if (intent->has_calendar_title || intent->has_calendar_note) {
            parsed_any = true;
        }
    }

    if (strcmp(tool, "atlas_pomodoro") == 0 || strcmp(tool, "pomodoro.control") == 0 ||
        strcmp(tool, "atlas_pomodoro_control") == 0 || strcmp(tool, "atlas.pomodoro.start") == 0 ||
        strcmp(tool, "atlas.pomodoro.stop") == 0 || strcmp(tool, "atlas.pomodoro.reset") == 0) {
        if (intent->has_pomodoro_task_name || intent->has_pomodoro_running ||
            intent->has_pomodoro_focus_minutes || intent->has_pomodoro_break_minutes ||
            intent->has_pomodoro_in_break) {
            parsed_any = true;
        }
    }

    if (strcmp(tool, "atlas.ui.set_chat_mode") == 0 || strcmp(tool, "atlas_chat_mode") == 0) {
        if (intent->has_chat_mode) {
            parsed_any = true;
        }
    }

    if (!parsed_any) {
        set_error(error, error_size, "unknown brain tool payload");
        return false;
    }
    return true;
}

static bool parse_tool_call(const cJSON *root, atlas_brain_intent_t *intent, char *error, size_t error_size)
{
    char tool[ATLAS_BRAIN_INTENT_ACTION_MAX] = "";
    if (!json_string_to_buffer(root, "tool", tool, sizeof(tool)) &&
        !json_string_to_buffer(root, "name", tool, sizeof(tool))) {
        return false;
    }

    const cJSON *input = cJSON_GetObjectItemCaseSensitive(root, "input");
    if (!cJSON_IsObject(input)) {
        input = cJSON_GetObjectItemCaseSensitive(root, "args");
    }
    if (!cJSON_IsObject(input)) {
        input = cJSON_GetObjectItemCaseSensitive(root, "arguments");
    }
    if (!cJSON_IsObject(input)) {
        input = root;
    }

    if (strcmp(tool, "atlas_rover_stop") == 0 || strcmp(tool, "rover.stop") == 0) {
        strlcpy(intent->action, "rover.stop", sizeof(intent->action));
        intent->has_action = true;
        return true;
    }

    if (strcmp(tool, "atlas_rover_move") == 0 || strcmp(tool, "rover.move") == 0) {
        if (!parse_motion_object(input, &intent->motion, error, error_size)) {
            return false;
        }
        intent->has_motion = true;
        return true;
    }

    if (strcmp(tool, "atlas_set_expression") == 0 || strcmp(tool, "eyes.set_expression") == 0) {
        char expression_name[24] = "";
        if (!json_string_to_buffer(input, "expression", expression_name, sizeof(expression_name)) ||
            !atlas_expression_from_name(expression_name, &intent->expression)) {
            set_error(error, error_size, "bad expression");
            return false;
        }
        intent->has_expression = true;
        return true;
    }

    if (strcmp(tool, "atlas_show_page") == 0 || strcmp(tool, "display.show_page") == 0) {
        char page_name[24] = "";
        if (!json_string_to_buffer(input, "page", page_name, sizeof(page_name)) ||
            !atlas_page_from_name(page_name, &intent->page)) {
            set_error(error, error_size, "bad page");
            return false;
        }
        intent->has_page = true;
        return true;
    }

    if (strcmp(tool, "atlas_app_action") == 0) {
        char action[ATLAS_BRAIN_INTENT_ACTION_MAX] = "";
        if (!json_string_to_buffer(input, "action", action, sizeof(action))) {
            set_error(error, error_size, "bad action");
            return false;
        }
        strlcpy(intent->action, action, sizeof(intent->action));
        intent->has_action = true;
        return true;
    }

    if (strcmp(tool, "atlas.ui.set_chat_mode") == 0 || strcmp(tool, "atlas_chat_mode") == 0) {
        if (!parse_chat_mode_payload(input, intent)) {
            set_error(error, error_size, "bad chat mode");
            return false;
        }
        return true;
    }

    if (strcmp(tool, "atlas.pet.set_state") == 0 || strcmp(tool, "atlas_pet_set_state") == 0) {
        return parse_pet_visual_payload(intent, input, "state", error, error_size);
    }

    if (strcmp(tool, "atlas.pet.play_animation") == 0 || strcmp(tool, "atlas_pet_play_animation") == 0) {
        return parse_pet_visual_payload(intent, input, "animation", error, error_size);
    }

    if (strcmp(tool, "atlas.clock.show") == 0 || strcmp(tool, "atlas.clock.status") == 0) {
        strlcpy(intent->action,
                strcmp(tool, "atlas.clock.status") == 0 ? "clock.status" : "clock.show",
                sizeof(intent->action));
        intent->has_action = true;
        return true;
    }

    if (strcmp(tool, "atlas.calendar.show") == 0 || strcmp(tool, "atlas.calendar.today") == 0 ||
        strcmp(tool, "atlas.calendar.set_note") == 0) {
        parse_calendar_payload(input, intent);
        strlcpy(intent->action,
                strcmp(tool, "atlas.calendar.today") == 0 ? "calendar.today" :
                strcmp(tool, "atlas.calendar.set_note") == 0 ? "calendar.set_note" : "calendar.show",
                sizeof(intent->action));
        intent->has_action = true;
        return true;
    }

    if (strcmp(tool, "atlas.pomodoro.show") == 0 || strcmp(tool, "atlas.pomodoro.status") == 0 ||
        strcmp(tool, "atlas.pomodoro.start") == 0 || strcmp(tool, "atlas.pomodoro.stop") == 0 ||
        strcmp(tool, "atlas.pomodoro.reset") == 0) {
        parse_pomodoro_payload(input, intent);
        if (strcmp(tool, "atlas.pomodoro.start") == 0) {
            intent->pomodoro_running = true;
            intent->has_pomodoro_running = true;
        } else if (strcmp(tool, "atlas.pomodoro.stop") == 0 || strcmp(tool, "atlas.pomodoro.reset") == 0) {
            intent->pomodoro_running = false;
            intent->has_pomodoro_running = true;
        }
        strlcpy(intent->action,
                strcmp(tool, "atlas.pomodoro.status") == 0 ? "pomodoro.status" :
                strcmp(tool, "atlas.pomodoro.show") == 0 ? "pomodoro.show" :
                strcmp(tool, "atlas.pomodoro.reset") == 0 ? "pomodoro.reset" :
                strcmp(tool, "atlas.pomodoro.stop") == 0 ? "pomodoro.stop" : "pomodoro.start",
                sizeof(intent->action));
        intent->has_action = true;
        return true;
    }

    if (strcmp(tool, "atlas_pet_event") == 0 || strcmp(tool, "pet.event") == 0) {
        char event_name[24] = "";
        if (!json_string_to_buffer(input, "event", event_name, sizeof(event_name)) ||
            !atlas_pet_event_from_name(event_name, &intent->pet_event)) {
            set_error(error, error_size, "bad pet event");
            return false;
        }
        intent->has_pet_event = true;
        return true;
    }

    if (strcmp(tool, "atlas_chat") == 0 || strcmp(tool, "chat") == 0) {
        return parse_tool_payload(intent, input, tool, error, error_size);
    }

    if (strcmp(tool, "atlas_calendar") == 0 || strcmp(tool, "calendar.update") == 0 ||
        strcmp(tool, "atlas_calendar_update") == 0 || strcmp(tool, "atlas.calendar.set_note") == 0 ||
        strcmp(tool, "atlas.calendar.today") == 0) {
        return parse_tool_payload(intent, input, tool, error, error_size);
    }

    if (strcmp(tool, "atlas_pomodoro") == 0 || strcmp(tool, "pomodoro.control") == 0 ||
        strcmp(tool, "atlas_pomodoro_control") == 0 || strcmp(tool, "atlas.pomodoro.start") == 0 ||
        strcmp(tool, "atlas.pomodoro.stop") == 0 || strcmp(tool, "atlas.pomodoro.reset") == 0) {
        return parse_tool_payload(intent, input, tool, error, error_size);
    }

    if (parse_tool_payload(intent, input, tool, error, error_size)) {
        return true;
    }

    set_error(error, error_size, "unknown brain tool");
    return false;
}

static void set_page_defaults(atlas_ui_state_t *state, atlas_page_t page)
{
    state->page = page;
    if (page == ATLAS_PAGE_VOICE) {
        state->expression = ATLAS_EXPR_LISTEN;
        state->audio_level = 24;
    } else if (page == ATLAS_PAGE_MUSIC || page == ATLAS_PAGE_STORY) {
        state->expression = ATLAS_EXPR_SPEAKING;
        state->audio_level = 58;
    } else if (page == ATLAS_PAGE_CHAT) {
        state->expression = ATLAS_EXPR_LISTEN;
        state->audio_level = 28;
    } else if (page == ATLAS_PAGE_CLOCK ||
               page == ATLAS_PAGE_CALENDAR || page == ATLAS_PAGE_POMODORO ||
               page == ATLAS_PAGE_ALARM || page == ATLAS_PAGE_PHOTO) {
        state->expression = ATLAS_EXPR_CURIOUS;
        state->audio_level = 0;
    } else if (!state->moving) {
        state->expression = ATLAS_EXPR_IDLE;
        state->audio_level = 0;
    }
}

static uint16_t clamp_nonzero_u16(uint16_t value, uint16_t fallback, uint16_t max)
{
    if (value == 0) {
        return fallback;
    }
    return value > max ? max : value;
}

static void apply_pomodoro_payload_fields(atlas_ui_state_t *state, const atlas_brain_intent_t *intent, uint32_t now_ms)
{
    if (state == NULL || intent == NULL) {
        return;
    }

    if (intent->has_pomodoro_focus_minutes) {
        state->pomodoro_focus_minutes = clamp_nonzero_u16(intent->pomodoro_focus_minutes, 25u, 120u);
    }
    if (intent->has_pomodoro_break_minutes) {
        state->pomodoro_break_minutes = clamp_nonzero_u16(intent->pomodoro_break_minutes, 5u, 30u);
    }

    if (intent->has_pomodoro_running) {
        const char *task = intent->has_pomodoro_task_name ? intent->pomodoro_task_name : NULL;
        const bool in_break = intent->has_pomodoro_in_break ? intent->pomodoro_in_break : false;
        atlas_ui_set_pomodoro_running(state,
                                      intent->pomodoro_running,
                                      in_break,
                                      now_ms,
                                      (task != NULL && task[0] != '\0') ? task : NULL,
                                      false);
        return;
    }

    if (intent->has_pomodoro_in_break) {
        state->pomodoro_in_break = intent->pomodoro_in_break;
    }
    if (intent->has_pomodoro_task_name) {
        strlcpy(state->pomodoro_task_name, intent->pomodoro_task_name, sizeof(state->pomodoro_task_name));
    }
}

static bool apply_action(atlas_ui_state_t *state, const char *action, uint32_t now_ms)
{
    if (state == NULL || action == NULL || action[0] == '\0') {
        return false;
    }

    if (state->moving) {
        (void)atlas_ui_stop(state, now_ms);
    }

    if (strcmp(action, "music.play") == 0 || strcmp(action, "music") == 0) {
        set_page_defaults(state, ATLAS_PAGE_MUSIC);
        state->expression = ATLAS_EXPR_SPEAKING;
        state->audio_level = 64;
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_MUSIC, now_ms);
    } else if (strcmp(action, "story.tell") == 0 || strcmp(action, "story") == 0) {
        set_page_defaults(state, ATLAS_PAGE_STORY);
        state->expression = ATLAS_EXPR_SPEAKING;
        state->audio_level = 58;
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_STORY, now_ms);
    } else if (strcmp(action, "chat.reply") == 0 || strcmp(action, "chat") == 0 ||
               strcmp(action, "audio.speak") == 0) {
        set_page_defaults(state, ATLAS_PAGE_CHAT);
        state->expression = ATLAS_EXPR_SPEAKING;
        state->audio_level = 58;
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_CHAT, now_ms);
    } else if (strcmp(action, "clock.show") == 0 || strcmp(action, "clock.status") == 0 ||
               strcmp(action, "clock") == 0) {
        set_page_defaults(state, ATLAS_PAGE_CLOCK);
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_THINK, now_ms);
    } else if (strcmp(action, "calendar.show") == 0 || strcmp(action, "calendar.add_reminder") == 0 ||
               strcmp(action, "calendar.today") == 0 || strcmp(action, "calendar.set_note") == 0 ||
               strcmp(action, "calendar") == 0) {
        set_page_defaults(state, ATLAS_PAGE_CALENDAR);
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_THINK, now_ms);
    } else if (strcmp(action, "pomodoro.start") == 0 || strcmp(action, "pomodoro.stop") == 0 ||
               strcmp(action, "pomodoro.show") == 0 || strcmp(action, "pomodoro.status") == 0 ||
               strcmp(action, "pomodoro.reset") == 0 || strcmp(action, "pomodoro") == 0) {
        set_page_defaults(state, ATLAS_PAGE_POMODORO);
        state->expression = ATLAS_EXPR_THINKING;
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_THINK, now_ms);
    } else if (strcmp(action, "status.report") == 0 || strcmp(action, "status") == 0) {
        set_page_defaults(state, ATLAS_PAGE_STATUS);
    } else if (strcmp(action, "display.show_page") == 0 || strcmp(action, "eyes.set_expression") == 0 ||
               strcmp(action, "display.set_theme") == 0 || strcmp(action, "display.set_brightness") == 0) {
        return true;
    } else {
        return false;
    }

    state->last_event_ms = now_ms;
    return true;
}

void atlas_brain_intent_init(atlas_brain_intent_t *intent)
{
    if (intent == NULL) {
        return;
    }
    *intent = (atlas_brain_intent_t) {
        .confidence = 1.0f,
        .expression = ATLAS_EXPR_IDLE,
        .page = ATLAS_PAGE_EYES,
        .motion = atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_NONE),
        .pet_event = ATLAS_PET_EVENT_INTERACTION,
    };
}

esp_err_t atlas_brain_intent_parse_json(const char *json,
                                           atlas_brain_intent_t *intent,
                                           char *error,
                                           size_t error_size)
{
    if (json == NULL || intent == NULL) {
        set_error(error, error_size, "invalid argument");
        return ESP_ERR_INVALID_ARG;
    }

    atlas_brain_intent_init(intent);
    cJSON *root = cJSON_Parse(json);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        set_error(error, error_size, "bad json");
        return ESP_ERR_INVALID_ARG;
    }

    if (cJSON_HasObjectItem(root, "tool") || cJSON_HasObjectItem(root, "name")) {
        const bool ok = parse_tool_call(root, intent, error, error_size);
        cJSON_Delete(root);
        return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
    }

    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (cJSON_IsString(version) && version->valuestring != NULL &&
        strcmp(version->valuestring, "atlas.brain.v1") != 0) {
        cJSON_Delete(root);
        set_error(error, error_size, "unsupported intent version");
        return ESP_ERR_NOT_SUPPORTED;
    }

    const cJSON *confidence = cJSON_GetObjectItemCaseSensitive(root, "confidence");
    if (cJSON_IsNumber(confidence)) {
        intent->confidence = (float)confidence->valuedouble;
    }

    char value[48] = "";
    if (json_string_to_buffer(root, "expression", value, sizeof(value))) {
        if (!atlas_expression_from_name(value, &intent->expression)) {
            cJSON_Delete(root);
            set_error(error, error_size, "bad expression");
            return ESP_ERR_INVALID_ARG;
        }
        intent->has_expression = true;
    }

    if (json_string_to_buffer(root, "page", value, sizeof(value))) {
        if (!atlas_page_from_name(value, &intent->page)) {
            cJSON_Delete(root);
            set_error(error, error_size, "bad page");
            return ESP_ERR_INVALID_ARG;
        }
        intent->has_page = true;
    }

    if (parse_chat_mode_payload(root, intent)) {
        // accepted as a transient runtime preference; persistent settings still go through /api/config/ui.
    }

    const cJSON *speech = cJSON_GetObjectItemCaseSensitive(root, "speech");
    if (cJSON_IsString(speech) && speech->valuestring != NULL && speech->valuestring[0] != '\0') {
        strlcpy(intent->speech, speech->valuestring, sizeof(intent->speech));
        intent->has_speech = true;
        if (!intent->has_chat_text) {
            strlcpy(intent->chat_text, speech->valuestring, sizeof(intent->chat_text));
            intent->has_chat_text = true;
        }
    }

    const cJSON *chat = cJSON_GetObjectItemCaseSensitive(root, "chat");
    if (cJSON_IsObject(chat)) {
        if (json_string_to_buffer(chat, "text", value, sizeof(value))) {
            strlcpy(intent->chat_text, value, sizeof(intent->chat_text));
            intent->has_chat_text = true;
        }
        if (json_string_to_buffer(chat, "speech", value, sizeof(value))) {
            strlcpy(intent->speech, value, sizeof(intent->speech));
            intent->has_speech = true;
            if (!intent->has_chat_text) {
                strlcpy(intent->chat_text, value, sizeof(intent->chat_text));
                intent->has_chat_text = true;
            }
        }
    }

    const cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (cJSON_IsString(action) && action->valuestring != NULL) {
        strlcpy(intent->action, action->valuestring, sizeof(intent->action));
        intent->has_action = true;
    } else if (cJSON_IsObject(action)) {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(action, "name");
        if (cJSON_IsString(name) && name->valuestring != NULL) {
            strlcpy(intent->action, name->valuestring, sizeof(intent->action));
            intent->has_action = true;
        }
    }

    const cJSON *motion = cJSON_GetObjectItemCaseSensitive(root, "motion");
    if (cJSON_IsObject(motion)) {
        if (!parse_motion_object(motion, &intent->motion, error, error_size)) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }
        intent->has_motion = true;
    }

    if (json_string_to_buffer(root, "pet_event", value, sizeof(value))) {
        if (!atlas_pet_event_from_name(value, &intent->pet_event)) {
            cJSON_Delete(root);
            set_error(error, error_size, "bad pet event");
            return ESP_ERR_INVALID_ARG;
        }
        intent->has_pet_event = true;
    }

    const cJSON *safety = cJSON_GetObjectItemCaseSensitive(root, "safety");
    if (cJSON_IsObject(safety)) {
        const cJSON *requires_confirmation = cJSON_GetObjectItemCaseSensitive(safety, "requires_confirmation");
        if (cJSON_IsBool(requires_confirmation)) {
            intent->requires_confirmation = cJSON_IsTrue(requires_confirmation);
        }
        const cJSON *reason = cJSON_GetObjectItemCaseSensitive(safety, "reason");
        if (cJSON_IsString(reason) && reason->valuestring != NULL) {
            strlcpy(intent->safety_reason, reason->valuestring, sizeof(intent->safety_reason));
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t atlas_brain_intent_apply_intent(const atlas_config_t *config,
                                             atlas_ui_state_t *state,
                                             const atlas_brain_intent_t *intent,
                                             uint32_t now_ms,
                                             char *result,
                                             size_t result_size)
{
    if (config == NULL || state == NULL || intent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (intent->confidence > 0.0f && intent->confidence < 0.45f) {
        state->page = ATLAS_PAGE_VOICE;
        state->expression = ATLAS_EXPR_THINKING;
        state->audio_level = 0;
        state->last_event_ms = now_ms;
        if (result != NULL && result_size > 0) {
            snprintf(result, result_size, "low confidence");
        }
        return ESP_ERR_NOT_FINISHED;
    }

    if (intent->requires_confirmation) {
        state->page = ATLAS_PAGE_VOICE;
        state->expression = ATLAS_EXPR_THINKING;
        state->audio_level = 0;
        state->motion = ATLAS_MOTION_NONE;
        state->moving = false;
        state->last_event_ms = now_ms;
        if (result != NULL && result_size > 0) {
            snprintf(result, result_size, "confirmation required");
        }
        return ESP_ERR_NOT_FINISHED;
    }

    if (intent->has_action && strcmp(intent->action, "rover.stop") == 0) {
        return atlas_ui_stop(state, now_ms);
    }

    if (intent->has_chat_mode) {
        strlcpy(state->chat_mode,
                atlas_config_chat_mode_is_valid(intent->chat_mode) ? intent->chat_mode : "pet_head",
                sizeof(state->chat_mode));
        state->last_event_ms = now_ms;
    }

    if (intent->has_page) {
        set_page_defaults(state, intent->page);
        state->last_event_ms = now_ms;
    }

    if (intent->has_action && !apply_action(state, intent->action, now_ms)) {
        ESP_LOGW(TAG, "unknown brain action: %s", intent->action);
    }

    if (intent->has_pet_event) {
        const esp_err_t err = atlas_ui_handle_pet_event(state, intent->pet_event, now_ms);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (intent->has_speech && !intent->has_expression && !intent->has_motion) {
        state->expression = ATLAS_EXPR_SPEAKING;
        state->audio_level = 58;
        state->last_event_ms = now_ms;
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_SPEAK, now_ms);
    }

    if (intent->has_chat_text) {
        atlas_ui_set_chat_text(state, intent->chat_text);
    }
    if (intent->has_calendar_title || intent->has_calendar_note) {
        atlas_ui_set_calendar_text(state,
                                  intent->has_calendar_title ? intent->calendar_title : state->calendar_title,
                                  intent->has_calendar_note ? intent->calendar_note : state->calendar_note);
    }

    if (intent->has_pomodoro_task_name || intent->has_pomodoro_focus_minutes || intent->has_pomodoro_break_minutes ||
        intent->has_pomodoro_running || intent->has_pomodoro_in_break) {
        apply_pomodoro_payload_fields(state, intent, now_ms);
    }

    if (intent->has_expression) {
        state->expression = intent->expression;
        state->audio_level = intent->expression == ATLAS_EXPR_SPEAKING ? 58 : state->audio_level;
        state->last_event_ms = now_ms;
    }

    if (intent->has_motion) {
        if (!atlas_config_motion_allowed(config)) {
            if (result != NULL && result_size > 0) {
                snprintf(result, result_size, "motion disabled");
            }
            return ESP_ERR_INVALID_STATE;
        }
        if (!atlas_config_ai_control_allowed(config)) {
            if (result != NULL && result_size > 0) {
                snprintf(result, result_size, "ai mode required");
            }
            return ESP_ERR_INVALID_STATE;
        }

        atlas_voice_intent_t motion = intent->motion;
        if (motion.speed > config->safety.max_speed_percent) {
            motion.speed = config->safety.max_speed_percent;
        }
        if (motion.duration_ms > config->safety.max_duration_ms) {
            motion.duration_ms = config->safety.max_duration_ms;
        }
        return atlas_ui_handle_voice_intent(state, motion, now_ms);
    }

    if (result != NULL && result_size > 0) {
        snprintf(result, result_size, "accepted");
    }
    return ESP_OK;
}
