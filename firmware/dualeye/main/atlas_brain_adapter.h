#pragma once

#include <stdbool.h>

#include "atlas_config.h"
#include "atlas_voice.h"

typedef enum {
    ATLAS_BRAIN_SOURCE_LOCAL = 0,
    ATLAS_BRAIN_SOURCE_HOST,
    ATLAS_BRAIN_SOURCE_CLOUD,
    ATLAS_BRAIN_SOURCE_EMBEDDED,
} atlas_brain_source_t;

typedef struct {
    atlas_brain_source_t source;
    atlas_voice_intent_t intent;
    bool used_llm;
    bool accepted;
} atlas_brain_result_t;

atlas_brain_result_t atlas_brain_resolve_text(const atlas_config_t *config, const char *text);
const char *atlas_brain_source_name(atlas_brain_source_t source);
