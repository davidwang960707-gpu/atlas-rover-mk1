#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "atlas_expression.h"

typedef enum {
    ATLAS_PAGE_EYES = 0,
    ATLAS_PAGE_CLOCK,
    ATLAS_PAGE_STATUS,
    ATLAS_PAGE_VOICE,
    ATLAS_PAGE_SETTINGS,
} atlas_page_t;

esp_err_t atlas_display_init(void);

void atlas_display_render(atlas_page_t page,
                          atlas_expression_t expression,
                          atlas_motion_t motion,
                          uint8_t audio_level,
                          uint32_t now_ms);

const char *atlas_page_name(atlas_page_t page);
