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
    ATLAS_PAGE_ALARM,
    ATLAS_PAGE_PHOTO,
    ATLAS_PAGE_MUSIC,
    ATLAS_PAGE_STORY,
    ATLAS_PAGE_CHAT,
    ATLAS_PAGE_CALENDAR,
    ATLAS_PAGE_POMODORO,
} atlas_page_t;

void atlas_display_set_theme(const char *theme_id);
void atlas_display_set_brightness(uint8_t brightness);
esp_err_t atlas_display_init(void);

void atlas_display_render(atlas_page_t page,
                          atlas_expression_t expression,
                          atlas_motion_t motion,
                          uint8_t audio_level,
                          uint32_t now_ms);

const char *atlas_page_name(atlas_page_t page);
