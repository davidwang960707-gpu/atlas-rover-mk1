#include "atlas_mimiclaw_adapter.h"

#include <string.h>

#include "atlas_llm_client.h"

const char *atlas_mimiclaw_source_name(atlas_mimiclaw_source_t source)
{
    switch (source) {
    case ATLAS_MIMICLAW_SOURCE_LOCAL:
        return "local";
    case ATLAS_MIMICLAW_SOURCE_HOST:
        return "host";
    case ATLAS_MIMICLAW_SOURCE_CLOUD:
        return "cloud";
    case ATLAS_MIMICLAW_SOURCE_EMBEDDED:
        return "embedded";
    default:
        return "unknown";
    }
}

atlas_mimiclaw_result_t atlas_mimiclaw_resolve_text(const atlas_config_t *config, const char *text)
{
    atlas_mimiclaw_result_t result = {
        .source = ATLAS_MIMICLAW_SOURCE_LOCAL,
        .intent = atlas_voice_intent_from_text(text),
        .used_llm = false,
        .accepted = false,
    };

    if (result.intent.event != ATLAS_VOICE_EVENT_NONE) {
        result.accepted = true;
        return result;
    }

    if (config == NULL || !atlas_llm_client_ready(config)) {
        result.intent = atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_NONE);
        return result;
    }

    result.used_llm = true;
    if (strcmp(config->llm.mode, "host") == 0) {
        result.source = ATLAS_MIMICLAW_SOURCE_HOST;
    } else if (strcmp(config->llm.mode, "embedded") == 0) {
        result.source = ATLAS_MIMICLAW_SOURCE_EMBEDDED;
    } else {
        result.source = ATLAS_MIMICLAW_SOURCE_CLOUD;
    }

    /*
     * V0.2 deliberately does not call an LLM from firmware yet.
     * The later implementation must return a structured atlas_voice_intent_t
     * and still pass through atlas_ui safety handling.
     */
    result.intent = atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_THINKING);
    result.accepted = true;
    return result;
}
