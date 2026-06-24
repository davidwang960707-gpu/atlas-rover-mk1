#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "atlas_config.h"
#include "atlas_wifi.h"

#include "atlas_page.h"

#include "atlas_expression.h"

void atlas_display_set_theme(const char *theme_id);
void atlas_display_set_brightness(uint8_t brightness);
esp_err_t atlas_display_init(void);

typedef struct {
    char chat_mode[ATLAS_CHAT_MODE_MAX];
    char chat_text[ATLAS_CHAT_TEXT_MAX];
    char calendar_title[ATLAS_CALENDAR_TITLE_MAX];
    char calendar_note[ATLAS_CALENDAR_NOTE_MAX];
    char scene_state[32];
    char scene_title[48];
    char scene_subtitle[72];
    char scene_hint[96];
    char scene_left_role[40];
    char scene_right_role[40];
    char scene_severity[16];
    bool scene_needs_attention;
    bool pomodoro_running;
    bool pomodoro_in_break;
    uint8_t pomodoro_progress_percent;
    uint32_t pomodoro_remaining_ms;
    uint16_t pomodoro_focus_minutes;
    uint16_t pomodoro_break_minutes;
    char pomodoro_task_name[ATLAS_POMODORO_TASK_MAX];
    char pet_ip[ATLAS_WIFI_IP_MAX];
    bool pet_ip_valid;
} atlas_display_payload_t;

void atlas_display_render(atlas_page_t page,
                          atlas_expression_t expression,
                          atlas_motion_t motion,
                          uint8_t audio_level,
                          uint32_t now_ms,
                          const atlas_display_payload_t *payload);

const char *atlas_page_name(atlas_page_t page);
bool atlas_page_from_name(const char *name, atlas_page_t *page);
