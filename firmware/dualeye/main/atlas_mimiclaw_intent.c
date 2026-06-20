#include "atlas_mimiclaw_intent.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "atlas_mimiclaw";

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

static bool parse_tool_call(const cJSON *root, atlas_mimiclaw_intent_t *intent, char *error, size_t error_size)
{
    char tool[ATLAS_MIMICLAW_INTENT_ACTION_MAX] = "";
    if (!json_string_to_buffer(root, "tool", tool, sizeof(tool)) &&
        !json_string_to_buffer(root, "name", tool, sizeof(tool))) {
        return false;
    }

    const cJSON *input = cJSON_GetObjectItemCaseSensitive(root, "input");
    if (!cJSON_IsObject(input)) {
        input = cJSON_GetObjectItemCaseSensitive(root, "args");
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
        char action[ATLAS_MIMICLAW_INTENT_ACTION_MAX] = "";
        if (!json_string_to_buffer(input, "action", action, sizeof(action))) {
            set_error(error, error_size, "bad action");
            return false;
        }
        strlcpy(intent->action, action, sizeof(intent->action));
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

    set_error(error, error_size, "unknown mimiclaw tool");
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
    } else if (page == ATLAS_PAGE_CALENDAR || page == ATLAS_PAGE_POMODORO ||
               page == ATLAS_PAGE_ALARM || page == ATLAS_PAGE_PHOTO) {
        state->expression = ATLAS_EXPR_CURIOUS;
        state->audio_level = 0;
    } else if (!state->moving) {
        state->expression = ATLAS_EXPR_IDLE;
        state->audio_level = 0;
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
    } else if (strcmp(action, "calendar.show") == 0 || strcmp(action, "calendar.add_reminder") == 0 ||
               strcmp(action, "calendar") == 0) {
        set_page_defaults(state, ATLAS_PAGE_CALENDAR);
        atlas_pet_handle_event(&state->pet, ATLAS_PET_EVENT_THINK, now_ms);
    } else if (strcmp(action, "pomodoro.start") == 0 || strcmp(action, "pomodoro.stop") == 0 ||
               strcmp(action, "pomodoro") == 0) {
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

void atlas_mimiclaw_intent_init(atlas_mimiclaw_intent_t *intent)
{
    if (intent == NULL) {
        return;
    }
    *intent = (atlas_mimiclaw_intent_t) {
        .confidence = 1.0f,
        .expression = ATLAS_EXPR_IDLE,
        .page = ATLAS_PAGE_EYES,
        .motion = atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_NONE),
        .pet_event = ATLAS_PET_EVENT_INTERACTION,
    };
}

esp_err_t atlas_mimiclaw_intent_parse_json(const char *json,
                                           atlas_mimiclaw_intent_t *intent,
                                           char *error,
                                           size_t error_size)
{
    if (json == NULL || intent == NULL) {
        set_error(error, error_size, "invalid argument");
        return ESP_ERR_INVALID_ARG;
    }

    atlas_mimiclaw_intent_init(intent);
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
        strcmp(version->valuestring, "atlas.mimiclaw.v1") != 0) {
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

    const cJSON *speech = cJSON_GetObjectItemCaseSensitive(root, "speech");
    if (cJSON_IsString(speech) && speech->valuestring != NULL && speech->valuestring[0] != '\0') {
        strlcpy(intent->speech, speech->valuestring, sizeof(intent->speech));
        intent->has_speech = true;
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

esp_err_t atlas_mimiclaw_intent_apply_intent(const atlas_config_t *config,
                                             atlas_ui_state_t *state,
                                             const atlas_mimiclaw_intent_t *intent,
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

    if (intent->has_page) {
        set_page_defaults(state, intent->page);
        state->last_event_ms = now_ms;
    }

    if (intent->has_action && !apply_action(state, intent->action, now_ms)) {
        ESP_LOGW(TAG, "unknown mimiclaw action: %s", intent->action);
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
