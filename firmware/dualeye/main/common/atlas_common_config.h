#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATLAS_COMMON_CONFIG_PROTOCOL "atlas.config.v0"
#define ATLAS_COMMON_WIFI_PROVISIONING_PROTOCOL "atlas.wifi.provisioning.v0"

#define ATLAS_COMMON_CONFIG_DEFAULT_DEVICE_NAME "Atlas Rover Mk.1"
#define ATLAS_COMMON_CONFIG_DEFAULT_LLM_MODE "off"
#define ATLAS_COMMON_CONFIG_DEFAULT_LLM_PROVIDER "openai_compatible"

#define ATLAS_COMMON_WIFI_AP_SSID_PREFIX "AtlasRover"
#define ATLAS_COMMON_WIFI_AP_DEFAULT_IP "192.168.4.1"
#define ATLAS_COMMON_WIFI_MAX_STA_RETRY 5u
#define ATLAS_COMMON_WIFI_AP_CHANNEL 1u
#define ATLAS_COMMON_WIFI_AP_MAX_CONN 4u

typedef struct {
    bool has_wifi_credentials;
    bool sta_connected;
    bool ap_started;
    const char *mode;
    const char *sta_ip;
    const char *ap_ip;
    const char *ap_ssid;
    uint8_t sta_retry_count;
} atlas_common_wifi_config_summary_t;

typedef struct {
    bool configured;
    bool api_key_set;
    const char *mode;
    const char *provider;
    const char *base_url;
    const char *model;
} atlas_common_brain_config_summary_t;

const char *atlas_common_config_protocol(void);
const char *atlas_common_wifi_provisioning_protocol(void);
const char *atlas_common_config_default_device_name(void);
const char *atlas_common_config_default_llm_mode(void);
const char *atlas_common_config_default_llm_provider(void);

void atlas_common_config_copy_string(char *dst, size_t dst_size, const char *src);
void atlas_common_config_trim(char *text);
bool atlas_common_config_has_value(const char *text);
bool atlas_common_config_llm_mode_is_known(const char *mode);
bool atlas_common_config_llm_provider_is_known(const char *provider);
