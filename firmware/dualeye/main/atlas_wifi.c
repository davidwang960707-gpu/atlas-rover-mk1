#include "atlas_wifi.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "apps/esp_sntp.h"
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
static bool s_sntp_started;
static bool s_sta_configured;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;

static const char *wifi_disconnect_reason_name(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_BEACON_TIMEOUT:
        return "BEACON_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND:
        return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL:
        return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL:
        return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_CONNECTION_FAIL:
        return "CONNECTION_FAIL";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        return "NO_AP_FOUND_COMPATIBLE_SECURITY";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "NO_AP_FOUND_AUTHMODE_THRESHOLD";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "NO_AP_FOUND_RSSI_THRESHOLD";
    default:
        return "UNKNOWN";
    }
}

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

static void time_sync_cb(struct timeval *tv)
{
    if (tv == NULL) {
        return;
    }

    time_t now = tv->tv_sec;
    struct tm timeinfo = {0};
    localtime_r(&now, &timeinfo);
    char text[32];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "SNTP time synced: %s", text);
}

static void start_sntp_if_needed(void)
{
    if (s_sntp_started) {
        return;
    }

    setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();
    s_sntp_started = true;
    ESP_LOGI(TAG, "SNTP started: server=ntp.aliyun.com timezone=CST-8");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_sta_configured) {
            ESP_LOGI(TAG, "STA start, connecting");
            (void)esp_wifi_connect();
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)event_data;
        const uint8_t reason = event == NULL ? 0 : event->reason;
        s_status.sta_connected = false;
        s_status.sta_ip[0] = '\0';
        if (s_sta_configured && s_status.sta_retry_count < ATLAS_WIFI_MAX_STA_RETRY) {
            s_status.sta_retry_count++;
            ESP_LOGW(TAG,
                     "STA disconnected, reason=%u(%s), retry %u/%u",
                     reason,
                     wifi_disconnect_reason_name(reason),
                     s_status.sta_retry_count,
                     ATLAS_WIFI_MAX_STA_RETRY);
            (void)esp_wifi_connect();
        } else if (!s_status.ap_started) {
            ESP_LOGW(TAG,
                     "STA failed, reason=%u(%s), enabling SoftAP fallback",
                     reason,
                     wifi_disconnect_reason_name(reason));
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
        start_sntp_if_needed();
    }
}

static int compare_scan_record_rssi(const void *a, const void *b)
{
    const wifi_ap_record_t *left = (const wifi_ap_record_t *)a;
    const wifi_ap_record_t *right = (const wifi_ap_record_t *)b;
    return (int)right->rssi - (int)left->rssi;
}

static bool scan_result_has_ssid(const atlas_wifi_scan_result_t *result, const char *ssid)
{
    if (result == NULL || ssid == NULL || ssid[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < result->count; ++i) {
        if (strcmp(result->records[i].ssid, ssid) == 0) {
            return true;
        }
    }
    return false;
}

static bool auth_is_secure(wifi_auth_mode_t authmode)
{
    return authmode != WIFI_AUTH_OPEN && authmode != WIFI_AUTH_OWE;
}

esp_err_t atlas_wifi_scan(atlas_wifi_scan_result_t *result)
{
    if (result == NULL || !s_wifi_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(result, 0, sizeof(*result));

    if (s_status.mode == ATLAS_WIFI_MODE_AP) {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "enable APSTA for scan failed");
        s_status.mode = ATLAS_WIFI_MODE_APSTA;
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 80,
        .scan_time.active.max = 160,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t ap_count = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&ap_count), TAG, "scan ap num failed");
    if (ap_count == 0) {
        return ESP_OK;
    }

    wifi_ap_record_t records[32];
    uint16_t read_count = ap_count > 32 ? 32 : ap_count;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_records(&read_count, records), TAG, "scan records failed");
    qsort(records, read_count, sizeof(records[0]), compare_scan_record_rssi);

    for (uint16_t i = 0; i < read_count && result->count < ATLAS_WIFI_SCAN_MAX_RESULTS; ++i) {
        const char *ssid = (const char *)records[i].ssid;
        if (ssid[0] == '\0' || scan_result_has_ssid(result, ssid)) {
            continue;
        }
        atlas_wifi_scan_record_t *dst = &result->records[result->count++];
        strlcpy(dst->ssid, ssid, sizeof(dst->ssid));
        dst->rssi = records[i].rssi;
        dst->channel = records[i].primary;
        dst->authmode = records[i].authmode;
        dst->secure = auth_is_secure(records[i].authmode);
    }
    ESP_LOGI(TAG, "Wi-Fi scan done: found=%u listed=%u", ap_count, (unsigned)result->count);
    return ESP_OK;
}

esp_err_t atlas_wifi_start(const atlas_config_t *config)
{
    if (s_wifi_ready) {
        return ESP_OK;
    }

    memset(&s_status, 0, sizeof(s_status));
    strlcpy(s_status.ap_ip, "192.168.4.1", sizeof(s_status.ap_ip));
    s_sta_configured = atlas_config_has_wifi(config);

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

    if (s_sta_configured) {
        wifi_config_t sta_config = {0};
        strlcpy((char *)sta_config.sta.ssid, config->wifi_ssid, sizeof(sta_config.sta.ssid));
        strlcpy((char *)sta_config.sta.password, config->wifi_password, sizeof(sta_config.sta.password));
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta_config.sta.pmf_cfg.capable = true;
        sta_config.sta.pmf_cfg.required = false;

        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "set APSTA mode failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG, "set STA config failed");
        s_status.mode = ATLAS_WIFI_MODE_APSTA;
        ESP_RETURN_ON_ERROR(configure_softap(), TAG, "configure SoftAP failed");
        ESP_LOGI(TAG, "Wi-Fi STA configured with AP fallback open: ssid=%s ap=%s", config->wifi_ssid, s_status.ap_ssid);
    } else {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "set APSTA mode failed");
        s_status.mode = ATLAS_WIFI_MODE_APSTA;
        ESP_RETURN_ON_ERROR(configure_softap(), TAG, "configure SoftAP failed");
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "disable Wi-Fi power save failed");
    s_wifi_ready = true;
    ESP_LOGI(TAG, "Wi-Fi started in %s mode, power_save=off", atlas_wifi_mode_name(s_status.mode));
    return ESP_OK;
}

void atlas_wifi_get_status(atlas_wifi_status_t *status)
{
    if (status == NULL) {
        return;
    }
    *status = s_status;
}
