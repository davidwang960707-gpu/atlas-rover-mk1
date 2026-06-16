#include "atlas_wifi.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "lwip/inet.h"

static const char *TAG = "atlas_wifi";

#define ATLAS_WIFI_MAX_STA_RETRY 5
#define ATLAS_WIFI_AP_CHANNEL 1
#define ATLAS_WIFI_AP_MAX_CONN 4

static atlas_wifi_status_t s_status;
static bool s_wifi_ready;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;

const char *atlas_wifi_mode_name(atlas_wifi_mode_t mode)
{
    switch (mode) {
    case ATLAS_WIFI_MODE_AP:
        return "ap";
    case ATLAS_WIFI_MODE_STA:
        return "sta";
    case ATLAS_WIFI_MODE_APSTA:
        return "apsta";
    default:
        return "unknown";
    }
}

static void copy_ip(char *dst, size_t dst_size, const esp_netif_ip_info_t *ip_info)
{
    if (dst == NULL || dst_size == 0 || ip_info == NULL) {
        return;
    }
    snprintf(dst, dst_size, IPSTR, IP2STR(&ip_info->ip));
}

static void build_ap_ssid(char *ssid, size_t ssid_size)
{
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
        esp_fill_random(mac, sizeof(mac));
    }
    snprintf(ssid, ssid_size, "AtlasRover-%02X%02X", mac[4], mac[5]);
}

static esp_err_t configure_softap(void)
{
    build_ap_ssid(s_status.ap_ssid, sizeof(s_status.ap_ssid));

    wifi_config_t ap_config = {0};
    strlcpy((char *)ap_config.ap.ssid, s_status.ap_ssid, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(s_status.ap_ssid);
    ap_config.ap.channel = ATLAS_WIFI_AP_CHANNEL;
    ap_config.ap.max_connection = ATLAS_WIFI_AP_MAX_CONN;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "set AP config failed");
    s_status.ap_started = true;
    strlcpy(s_status.ap_ip, "192.168.4.1", sizeof(s_status.ap_ip));
    ESP_LOGI(TAG, "SoftAP ready: ssid=%s url=http://192.168.4.1", s_status.ap_ssid);
    return ESP_OK;
}

static esp_err_t switch_to_apsta(void)
{
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "set APSTA mode failed");
    s_status.mode = ATLAS_WIFI_MODE_APSTA;
    return configure_softap();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA start, connecting");
        (void)esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_status.sta_connected = false;
        s_status.sta_ip[0] = '\0';
        if (s_status.sta_retry_count < ATLAS_WIFI_MAX_STA_RETRY) {
            s_status.sta_retry_count++;
            ESP_LOGW(TAG, "STA disconnected, retry %u/%u", s_status.sta_retry_count, ATLAS_WIFI_MAX_STA_RETRY);
            (void)esp_wifi_connect();
        } else if (!s_status.ap_started) {
            ESP_LOGW(TAG, "STA failed, enabling SoftAP fallback");
            (void)switch_to_apsta();
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "AP client joined: " MACSTR, MAC2STR(event->mac));
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "AP client left: " MACSTR, MAC2STR(event->mac));
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_status.sta_connected = true;
        s_status.sta_retry_count = 0;
        copy_ip(s_status.sta_ip, sizeof(s_status.sta_ip), &event->ip_info);
        ESP_LOGI(TAG, "STA connected: ip=%s admin=http://%s", s_status.sta_ip, s_status.sta_ip);
    }
}

esp_err_t atlas_wifi_start(const atlas_config_t *config)
{
    if (s_wifi_ready) {
        return ESP_OK;
    }

    memset(&s_status, 0, sizeof(s_status));
    strlcpy(s_status.ap_ip, "192.168.4.1", sizeof(s_status.ap_ip));

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_sta_netif == NULL || s_ap_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi set storage failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL), TAG, "wifi handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL), TAG, "ip handler failed");

    if (atlas_config_has_wifi(config)) {
        wifi_config_t sta_config = {0};
        strlcpy((char *)sta_config.sta.ssid, config->wifi_ssid, sizeof(sta_config.sta.ssid));
        strlcpy((char *)sta_config.sta.password, config->wifi_password, sizeof(sta_config.sta.password));
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta_config.sta.pmf_cfg.capable = true;
        sta_config.sta.pmf_cfg.required = false;

        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set STA mode failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG, "set STA config failed");
        s_status.mode = ATLAS_WIFI_MODE_STA;
        ESP_LOGI(TAG, "Wi-Fi STA configured: ssid=%s", config->wifi_ssid);
    } else {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set AP mode failed");
        s_status.mode = ATLAS_WIFI_MODE_AP;
        ESP_RETURN_ON_ERROR(configure_softap(), TAG, "configure SoftAP failed");
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    s_wifi_ready = true;
    ESP_LOGI(TAG, "Wi-Fi started in %s mode", atlas_wifi_mode_name(s_status.mode));
    return ESP_OK;
}

void atlas_wifi_get_status(atlas_wifi_status_t *status)
{
    if (status == NULL) {
        return;
    }
    *status = s_status;
}
