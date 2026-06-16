#pragma once

#include <stdbool.h>

#include "atlas_config.h"
#include "atlas_voice.h"

typedef enum {
    ATLAS_MIMICLAW_SOURCE_LOCAL = 0,
    ATLAS_MIMICLAW_SOURCE_HOST,
    ATLAS_MIMICLAW_SOURCE_CLOUD,
    ATLAS_MIMICLAW_SOURCE_EMBEDDED,
} atlas_mimiclaw_source_t;

typedef struct {
    atlas_mimiclaw_source_t source;
    atlas_voice_intent_t intent;
    bool used_llm;
    bool accepted;
} atlas_mimiclaw_result_t;

atlas_mimiclaw_result_t atlas_mimiclaw_resolve_text(const atlas_config_t *config, const char *text);
const char *atlas_mimiclaw_source_name(atlas_mimiclaw_source_t source);
