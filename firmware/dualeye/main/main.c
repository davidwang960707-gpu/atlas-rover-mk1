#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "atlas_admin_http.h"
#include "atlas_config.h"
#include "atlas_display.h"
#include "atlas_pairing.h"
#include "atlas_rover_uart.h"
#include "atlas_ui.h"
#include "atlas_voice.h"
#include "atlas_wifi.h"

#ifndef ATLAS_ENABLE_DEV_EVENT_DEMO
#define ATLAS_ENABLE_DEV_EVENT_DEMO 1
#endif

static const char *TAG = "atlas_dualeye";

static atlas_config_t s_config;
static atlas_ui_state_t s_ui_state;

static uint32_t atlas_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void ui_task(void *arg)
{
    (void)arg;
    while (true) {
        atlas_ui_tick(&s_ui_state, atlas_now_ms());
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void chassis_rx_task(void *arg)
{
    (void)arg;
    char line[96];

    while (true) {
        const int len = atlas_rover_uart_read_line(line, sizeof(line), pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }

        const atlas_rover_ack_t ack = atlas_rover_uart_parse_ack(line);
        if (ack == ATLAS_ROVER_ACK_NONE) {
            ESP_LOGD(TAG, "RX ignored: %s", line);
            continue;
        }
        ESP_LOGI(TAG, "RX %s", line);
        atlas_ui_handle_chassis_ack(&s_ui_state, ack, atlas_now_ms());
    }
}

#if ATLAS_ENABLE_DEV_EVENT_DEMO
static void dev_event_demo_task(void *arg)
{
    (void)arg;

    /*
     * This task demonstrates the event pipeline without moving the chassis.
     * Remove or disable it when MimiClaw starts feeding real events.
     */
    vTaskDelay(pdMS_TO_TICKS(1800));
    (void)atlas_ui_handle_voice_intent(&s_ui_state,
                                       atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_LISTENING),
                                       atlas_now_ms());

    vTaskDelay(pdMS_TO_TICKS(1200));
    (void)atlas_ui_handle_voice_intent(&s_ui_state,
                                       atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_THINKING),
                                       atlas_now_ms());

    vTaskDelay(pdMS_TO_TICKS(1200));
    (void)atlas_ui_handle_voice_intent(&s_ui_state,
                                       atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_SPEAKING),
                                       atlas_now_ms());

    vTaskDelay(pdMS_TO_TICKS(1200));
    (void)atlas_ui_handle_voice_intent(&s_ui_state,
                                       atlas_voice_intent_from_event(ATLAS_VOICE_EVENT_SUCCESS),
                                       atlas_now_ms());

    vTaskDelete(NULL);
}
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "Atlas Rover Mk.1 DualEye firmware V0.5");
    ESP_LOGI(TAG, "DualEye role: HMI, expressions, voice events, UART motion intent");
    ESP_LOGI(TAG, "Chassis role: open-loop timed N20 differential drive, DRV8833/PWM, limit, timeout stop");
    ESP_LOGI(TAG, "UART wiring: LCD1 Pin10 TXD -> XIAO D7/GPIO20 RX, LCD1 Pin9 RXD <- XIAO D6/GPIO21 TX, GND common");
    ESP_LOGI(TAG, "Power rule: never power motors from DualEye");

    ESP_ERROR_CHECK(atlas_config_init());
    ESP_ERROR_CHECK(atlas_config_load(&s_config));
    atlas_display_set_theme(s_config.ui.theme);
    atlas_display_set_brightness(s_config.ui.brightness);
    atlas_pairing_init();
    atlas_ui_init(&s_ui_state);

    ESP_ERROR_CHECK(atlas_display_init());
    ESP_ERROR_CHECK(atlas_rover_uart_init());
    ESP_ERROR_CHECK(atlas_wifi_start(&s_config));
    ESP_ERROR_CHECK(atlas_admin_http_start(&s_config, &s_ui_state, atlas_now_ms));

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_ERROR_CHECK(atlas_ui_stop(&s_ui_state, atlas_now_ms()));

    xTaskCreate(ui_task, "atlas_ui", 4096, NULL, 5, NULL);
    xTaskCreate(chassis_rx_task, "chassis_rx", 4096, NULL, 6, NULL);
#if ATLAS_ENABLE_DEV_EVENT_DEMO
    xTaskCreate(dev_event_demo_task, "dev_events", 4096, NULL, 4, NULL);
#endif
}
