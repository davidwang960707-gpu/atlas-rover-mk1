#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "atlas_config.h"

#define ATLAS_WIFI_AP_SSID_MAX 32
#define ATLAS_WIFI_IP_MAX 16

typedef enum {
    ATLAS_WIFI_MODE_AP = 0,
    ATLAS_WIFI_MODE_STA,
    ATLAS_WIFI_MODE_APSTA,
} atlas_wifi_mode_t;

typedef struct {
    atlas_wifi_mode_t mode;
    bool sta_connected;
    bool ap_started;
    char sta_ip[ATLAS_WIFI_IP_MAX];
    char ap_ip[ATLAS_WIFI_IP_MAX];
    char ap_ssid[ATLAS_WIFI_AP_SSID_MAX];
    uint8_t sta_retry_count;
} atlas_wifi_status_t;

esp_err_t atlas_wifi_start(const atlas_config_t *config);
void atlas_wifi_get_status(atlas_wifi_status_t *status);
const char *atlas_wifi_mode_name(atlas_wifi_mode_t mode);
