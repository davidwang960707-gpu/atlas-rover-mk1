#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "atlas_config.h"

#define ATLAS_WIFI_AP_SSID_MAX 32
#define ATLAS_WIFI_IP_MAX 16
#define ATLAS_WIFI_SCAN_MAX_RESULTS 16
#define ATLAS_WIFI_SCAN_SSID_MAX 33

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

typedef struct {
    char ssid[ATLAS_WIFI_SCAN_SSID_MAX];
    int8_t rssi;
    uint8_t channel;
    int authmode;
    bool secure;
} atlas_wifi_scan_record_t;

typedef struct {
    size_t count;
    atlas_wifi_scan_record_t records[ATLAS_WIFI_SCAN_MAX_RESULTS];
} atlas_wifi_scan_result_t;

esp_err_t atlas_wifi_start(const atlas_config_t *config);
void atlas_wifi_get_status(atlas_wifi_status_t *status);
const char *atlas_wifi_mode_name(atlas_wifi_mode_t mode);
esp_err_t atlas_wifi_scan(atlas_wifi_scan_result_t *result);
