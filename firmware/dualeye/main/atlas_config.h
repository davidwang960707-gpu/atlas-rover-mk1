#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define ATLAS_DEVICE_NAME_MAX 32
#define ATLAS_WIFI_SSID_MAX 33
#define ATLAS_WIFI_PASSWORD_MAX 65
#define ATLAS_LLM_MODE_MAX 12
#define ATLAS_LLM_PROVIDER_MAX 32
#define ATLAS_LLM_BASE_URL_MAX 128
#define ATLAS_LLM_MODEL_MAX 64
#define ATLAS_LLM_API_KEY_MAX 128
#define ATLAS_UI_THEME_MAX 32
#define ATLAS_CHAT_MODE_MAX 16
#define ATLAS_CONTROL_MODE_MAX 8
#define ATLAS_POMODORO_TASK_MAX 64
#define ATLAS_CALENDAR_TITLE_MAX 48
#define ATLAS_CALENDAR_NOTE_MAX 160
#define ATLAS_CHAT_TEXT_MAX 160

#ifndef ATLAS_ROVER_MOTION_BUILD_ENABLED
#define ATLAS_ROVER_MOTION_BUILD_ENABLED 0
#endif

typedef struct {
    char task_name[ATLAS_POMODORO_TASK_MAX];
    uint16_t focus_minutes;
    uint16_t break_minutes;
    bool enabled;
} atlas_pomodoro_config_t;

typedef struct {
    bool enabled;
    char title[ATLAS_CALENDAR_TITLE_MAX];
    char note[ATLAS_CALENDAR_NOTE_MAX];
} atlas_calendar_config_t;

typedef struct {
    bool motion_enabled;
    char control_mode[ATLAS_CONTROL_MODE_MAX]; // manual / ai
    uint8_t max_speed_percent;
    uint16_t max_duration_ms;
    bool require_confirm_for_patrol;
} atlas_safety_config_t;

typedef struct {
    char mode[ATLAS_LLM_MODE_MAX];           // off / cloud / host / embedded
    char provider[ATLAS_LLM_PROVIDER_MAX];   // openai_compatible / deepseek / host
    char base_url[ATLAS_LLM_BASE_URL_MAX];
    char model[ATLAS_LLM_MODEL_MAX];
    char api_key[ATLAS_LLM_API_KEY_MAX];
} atlas_llm_config_t;

typedef struct {
    char theme[ATLAS_UI_THEME_MAX];
    char chat_mode[ATLAS_CHAT_MODE_MAX]; // text / pet_head / eyes_only
    uint8_t brightness;
    uint8_t volume;
} atlas_ui_config_t;

typedef struct {
    char device_name[ATLAS_DEVICE_NAME_MAX];
    char wifi_ssid[ATLAS_WIFI_SSID_MAX];
    char wifi_password[ATLAS_WIFI_PASSWORD_MAX];
    atlas_llm_config_t llm;
    atlas_safety_config_t safety;
    atlas_ui_config_t ui;
    atlas_pomodoro_config_t pomodoro;
    atlas_calendar_config_t calendar;
} atlas_config_t;

esp_err_t atlas_config_init(void);
void atlas_config_defaults(atlas_config_t *config);
esp_err_t atlas_config_load(atlas_config_t *config);
esp_err_t atlas_config_save_wifi(const char *ssid, const char *password);
esp_err_t atlas_config_save_llm(const atlas_llm_config_t *llm);
esp_err_t atlas_config_save_safety(const atlas_safety_config_t *safety);
esp_err_t atlas_config_save_ui(const atlas_ui_config_t *ui);
esp_err_t atlas_config_save_pomodoro(const atlas_pomodoro_config_t *pomodoro);
esp_err_t atlas_config_save_calendar(const atlas_calendar_config_t *calendar);
esp_err_t atlas_config_reset_network_and_llm(void);

bool atlas_config_has_wifi(const atlas_config_t *config);
bool atlas_config_has_llm_api_key(const atlas_config_t *config);
bool atlas_config_motion_supported(void);
bool atlas_config_motion_allowed(const atlas_config_t *config);
bool atlas_config_manual_control_allowed(const atlas_config_t *config);
bool atlas_config_ai_control_allowed(const atlas_config_t *config);
bool atlas_config_chat_mode_is_valid(const char *mode);
