#include "atlas_config.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "atlas_expression.h"

static const char *TAG = "atlas_config";
static const char *NS = "atlas";

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strlcpy(dst, src, dst_size);
}

static esp_err_t nvs_get_string_default(nvs_handle_t handle,
                                        const char *key,
                                        char *value,
                                        size_t value_size,
                                        const char *default_value)
{
    size_t required = value_size;
    esp_err_t err = nvs_get_str(handle, key, value, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        copy_string(value, value_size, default_value);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        copy_string(value, value_size, default_value);
        return err;
    }
    return ESP_OK;
}

static esp_err_t nvs_set_string_checked(nvs_handle_t handle, const char *key, const char *value)
{
    return nvs_set_str(handle, key, value == NULL ? "" : value);
}

void atlas_config_defaults(atlas_config_t *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    copy_string(config->device_name, sizeof(config->device_name), "Atlas Rover Mk.1");
    copy_string(config->llm.mode, sizeof(config->llm.mode), "off");
    copy_string(config->llm.provider, sizeof(config->llm.provider), "openai_compatible");
    copy_string(config->ui.theme, sizeof(config->ui.theme), "classic");
    config->ui.brightness = 70;
    config->ui.volume = 60;
    config->safety.motion_enabled = true;
    copy_string(config->safety.control_mode, sizeof(config->safety.control_mode), "manual");
    config->safety.max_speed_percent = 40;
    config->safety.max_duration_ms = 700;
    config->safety.require_confirm_for_patrol = true;
}

esp_err_t atlas_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase: %s", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs_flash_erase failed");
        err = nvs_flash_init();
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS ready");
    }
    return err;
}

esp_err_t atlas_config_load(atlas_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    atlas_config_defaults(config);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved config, using defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    (void)nvs_get_string_default(handle, "device", config->device_name, sizeof(config->device_name), "Atlas Rover Mk.1");
    (void)nvs_get_string_default(handle, "wifi_ssid", config->wifi_ssid, sizeof(config->wifi_ssid), "");
    (void)nvs_get_string_default(handle, "wifi_pass", config->wifi_password, sizeof(config->wifi_password), "");
    (void)nvs_get_string_default(handle, "llm_mode", config->llm.mode, sizeof(config->llm.mode), "off");
    (void)nvs_get_string_default(handle, "llm_provider", config->llm.provider, sizeof(config->llm.provider), "openai_compatible");
    (void)nvs_get_string_default(handle, "llm_base", config->llm.base_url, sizeof(config->llm.base_url), "");
    (void)nvs_get_string_default(handle, "llm_model", config->llm.model, sizeof(config->llm.model), "");
    (void)nvs_get_string_default(handle, "llm_key", config->llm.api_key, sizeof(config->llm.api_key), "");
    (void)nvs_get_string_default(handle, "ui_theme", config->ui.theme, sizeof(config->ui.theme), "classic");
    if (!atlas_expression_theme_is_valid(config->ui.theme)) {
        copy_string(config->ui.theme, sizeof(config->ui.theme), "classic");
    } else if (strcmp(config->ui.theme, "atlas_blue") == 0) {
        copy_string(config->ui.theme, sizeof(config->ui.theme), "classic");
    }
    (void)nvs_get_string_default(handle, "ctrl_mode", config->safety.control_mode, sizeof(config->safety.control_mode), "manual");
    if (strcmp(config->safety.control_mode, "manual") != 0 && strcmp(config->safety.control_mode, "ai") != 0) {
        copy_string(config->safety.control_mode, sizeof(config->safety.control_mode), "manual");
    }

    uint8_t value_u8 = 0;
    uint16_t value_u16 = 0;
    if (nvs_get_u8(handle, "motion_en", &value_u8) == ESP_OK) {
        config->safety.motion_enabled = value_u8 != 0;
    }
    if (nvs_get_u8(handle, "max_speed", &value_u8) == ESP_OK) {
        config->safety.max_speed_percent = value_u8;
    }
    if (nvs_get_u16(handle, "max_dur", &value_u16) == ESP_OK) {
        config->safety.max_duration_ms = value_u16;
    }
    if (nvs_get_u8(handle, "confirm_patrol", &value_u8) == ESP_OK) {
        config->safety.require_confirm_for_patrol = value_u8 != 0;
    }
    if (nvs_get_u8(handle, "brightness", &value_u8) == ESP_OK) {
        config->ui.brightness = value_u8;
    }
    if (nvs_get_u8(handle, "volume", &value_u8) == ESP_OK) {
        config->ui.volume = value_u8;
    }

    nvs_close(handle);
    ESP_LOGI(TAG,
             "config loaded: wifi=%s llm_mode=%s api_key=%s motion=%s control=%s max_speed=%u max_duration=%u",
             atlas_config_has_wifi(config) ? "set" : "unset",
             config->llm.mode,
             atlas_config_has_llm_api_key(config) ? "set" : "unset",
             config->safety.motion_enabled ? "enabled" : "disabled",
             config->safety.control_mode,
             config->safety.max_speed_percent,
             config->safety.max_duration_ms);
    return ESP_OK;
}

esp_err_t atlas_config_save_wifi(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &handle), TAG, "nvs_open failed");
    esp_err_t err = nvs_set_string_checked(handle, "wifi_ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_string_checked(handle, "wifi_pass", password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "Wi-Fi config saved: ssid=%s password=%s", ssid == NULL ? "" : ssid, password != NULL && password[0] != '\0' ? "set" : "empty");
    return err;
}

esp_err_t atlas_config_save_llm(const atlas_llm_config_t *llm)
{
    if (llm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &handle), TAG, "nvs_open failed");
    esp_err_t err = nvs_set_string_checked(handle, "llm_mode", llm->mode);
    if (err == ESP_OK) {
        err = nvs_set_string_checked(handle, "llm_provider", llm->provider);
    }
    if (err == ESP_OK) {
        err = nvs_set_string_checked(handle, "llm_base", llm->base_url);
    }
    if (err == ESP_OK) {
        err = nvs_set_string_checked(handle, "llm_model", llm->model);
    }
    if (err == ESP_OK && llm->api_key[0] != '\0') {
        err = nvs_set_string_checked(handle, "llm_key", llm->api_key);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "LLM config saved: mode=%s provider=%s base=%s model=%s api_key=%s",
             llm->mode,
             llm->provider,
             llm->base_url,
             llm->model,
             llm->api_key[0] != '\0' ? "updated" : "unchanged");
    return err;
}

esp_err_t atlas_config_save_safety(const atlas_safety_config_t *safety)
{
    if (safety == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    atlas_safety_config_t clipped = *safety;
    if (clipped.max_speed_percent > 80) {
        clipped.max_speed_percent = 80;
    }
    if (clipped.max_duration_ms > 2000) {
        clipped.max_duration_ms = 2000;
    }
    if (clipped.max_speed_percent == 0) {
        clipped.max_speed_percent = 1;
    }
    if (clipped.max_duration_ms < 100) {
        clipped.max_duration_ms = 100;
    }
    if (strcmp(clipped.control_mode, "manual") != 0 && strcmp(clipped.control_mode, "ai") != 0) {
        copy_string(clipped.control_mode, sizeof(clipped.control_mode), "manual");
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &handle), TAG, "nvs_open failed");
    esp_err_t err = nvs_set_u8(handle, "motion_en", clipped.motion_enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_set_string_checked(handle, "ctrl_mode", clipped.control_mode);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "max_speed", clipped.max_speed_percent);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(handle, "max_dur", clipped.max_duration_ms);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "confirm_patrol", clipped.require_confirm_for_patrol ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_LOGI(TAG,
             "safety config saved: motion=%s control=%s max_speed=%u max_duration=%u",
             clipped.motion_enabled ? "enabled" : "disabled",
             clipped.control_mode,
             clipped.max_speed_percent,
             clipped.max_duration_ms);
    return err;
}

esp_err_t atlas_config_save_ui(const atlas_ui_config_t *ui)
{
    if (ui == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    atlas_ui_config_t clipped = *ui;
    if (!atlas_expression_theme_is_valid(clipped.theme)) {
        copy_string(clipped.theme, sizeof(clipped.theme), "classic");
    } else if (strcmp(clipped.theme, "atlas_blue") == 0) {
        copy_string(clipped.theme, sizeof(clipped.theme), "classic");
    }
    if (clipped.brightness > 100) {
        clipped.brightness = 100;
    }
    if (clipped.volume > 100) {
        clipped.volume = 100;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &handle), TAG, "nvs_open failed");
    esp_err_t err = nvs_set_string_checked(handle, "ui_theme", clipped.theme);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "brightness", clipped.brightness);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "volume", clipped.volume);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_LOGI(TAG,
             "ui config saved: theme=%s brightness=%u volume=%u",
             clipped.theme,
             clipped.brightness,
             clipped.volume);
    return err;
}

esp_err_t atlas_config_reset_network_and_llm(void)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &handle), TAG, "nvs_open failed");
    (void)nvs_erase_key(handle, "wifi_ssid");
    (void)nvs_erase_key(handle, "wifi_pass");
    (void)nvs_erase_key(handle, "llm_mode");
    (void)nvs_erase_key(handle, "llm_provider");
    (void)nvs_erase_key(handle, "llm_base");
    (void)nvs_erase_key(handle, "llm_model");
    (void)nvs_erase_key(handle, "llm_key");
    esp_err_t err = nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGW(TAG, "network and LLM config cleared");
    return err;
}

bool atlas_config_has_wifi(const atlas_config_t *config)
{
    return config != NULL && config->wifi_ssid[0] != '\0';
}

bool atlas_config_has_llm_api_key(const atlas_config_t *config)
{
    return config != NULL && config->llm.api_key[0] != '\0';
}

bool atlas_config_motion_allowed(const atlas_config_t *config)
{
    return config != NULL && config->safety.motion_enabled;
}

bool atlas_config_manual_control_allowed(const atlas_config_t *config)
{
    return atlas_config_motion_allowed(config) &&
           strcmp(config->safety.control_mode, "manual") == 0;
}

bool atlas_config_ai_control_allowed(const atlas_config_t *config)
{
    return atlas_config_motion_allowed(config) &&
           strcmp(config->safety.control_mode, "ai") == 0;
}
