#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "atlas_config.h"

typedef struct {
    bool configured;
    bool api_key_set;
    char mode[ATLAS_LLM_MODE_MAX];
    char provider[ATLAS_LLM_PROVIDER_MAX];
    char base_url[ATLAS_LLM_BASE_URL_MAX];
    char model[ATLAS_LLM_MODEL_MAX];
} atlas_llm_status_t;

void atlas_llm_client_get_status(const atlas_config_t *config, atlas_llm_status_t *status);
bool atlas_llm_client_ready(const atlas_config_t *config);
const char *atlas_llm_client_mode_label(const char *mode);
