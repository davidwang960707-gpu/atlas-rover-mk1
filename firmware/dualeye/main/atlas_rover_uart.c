#include "atlas_rover_uart.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "atlas_uart";

static bool s_uart_ready;

static uint8_t clamp_speed(uint8_t speed)
{
    return speed > 100 ? 100 : speed;
}

static const char *motion_token(atlas_motion_t motion)
{
    switch (motion) {
    case ATLAS_MOTION_FORWARD:
        return "F";
    case ATLAS_MOTION_BACKWARD:
        return "B";
    case ATLAS_MOTION_LEFT:
        return "L";
    case ATLAS_MOTION_RIGHT:
        return "R";
    case ATLAS_MOTION_NONE:
    default:
        return "?";
    }
}

esp_err_t atlas_rover_uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = ATLAS_ROVER_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(ATLAS_ROVER_UART_PORT, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_driver_install(ATLAS_ROVER_UART_PORT, 2048, 0, 0, NULL, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "UART driver already installed; reusing it");
        err = ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    s_uart_ready = true;
    ESP_LOGI(TAG,
             "UART ready on LCD1 Pin10 TXD / Pin9 RXD, %d baud, protocol prefix %s",
             ATLAS_ROVER_UART_BAUD,
             ATLAS_ROVER_UART_PREFIX);
    return ESP_OK;
}

esp_err_t atlas_rover_uart_send_line(const char *line)
{
    if (!s_uart_ready || line == NULL || line[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    if (strncmp(line, ATLAS_ROVER_UART_PREFIX, strlen(ATLAS_ROVER_UART_PREFIX)) != 0) {
        ESP_LOGE(TAG, "blocked non-AR1 line: %s", line);
        return ESP_ERR_INVALID_ARG;
    }

    uart_write_bytes(ATLAS_ROVER_UART_PORT, line, strlen(line));
    uart_write_bytes(ATLAS_ROVER_UART_PORT, "\n", 1);
    ESP_LOGI(TAG, "TX %s", line);
    return ESP_OK;
}

esp_err_t atlas_rover_uart_send_stop(void)
{
    return atlas_rover_uart_send_line("AR1,STOP");
}

esp_err_t atlas_rover_uart_send_move(atlas_motion_t motion, uint8_t speed, uint16_t duration_ms)
{
    if (motion != ATLAS_MOTION_FORWARD && motion != ATLAS_MOTION_BACKWARD) {
        return ESP_ERR_INVALID_ARG;
    }

    if (duration_ms > 5000) {
        duration_ms = 5000;
    }

    char line[40];
    snprintf(line,
             sizeof(line),
             "AR1,MOVE,%s,%u,%u",
             motion_token(motion),
             clamp_speed(speed),
             duration_ms);
    return atlas_rover_uart_send_line(line);
}

esp_err_t atlas_rover_uart_send_turn(atlas_motion_t motion, uint8_t speed, uint16_t duration_ms)
{
    if (motion != ATLAS_MOTION_LEFT && motion != ATLAS_MOTION_RIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (duration_ms > 5000) {
        duration_ms = 5000;
    }

    char line[40];
    snprintf(line,
             sizeof(line),
             "AR1,TURN,%s,%u,%u",
             motion_token(motion),
             clamp_speed(speed),
             duration_ms);
    return atlas_rover_uart_send_line(line);
}

int atlas_rover_uart_read_line(char *buffer, size_t buffer_size, TickType_t timeout_ticks)
{
    if (!s_uart_ready || buffer == NULL || buffer_size < 2) {
        return -1;
    }

    size_t length = 0;
    while (length < buffer_size - 1) {
        uint8_t byte = 0;
        const int read = uart_read_bytes(ATLAS_ROVER_UART_PORT, &byte, 1, timeout_ticks);
        if (read <= 0) {
            break;
        }
        if (byte == '\n' || byte == '\r') {
            if (length == 0) {
                continue;
            }
            break;
        }
        buffer[length++] = (char)byte;
    }

    buffer[length] = '\0';
    return (int)length;
}

atlas_rover_ack_t atlas_rover_uart_parse_ack(const char *line)
{
    if (line == NULL || strncmp(line, ATLAS_ROVER_UART_PREFIX, strlen(ATLAS_ROVER_UART_PREFIX)) != 0) {
        return ATLAS_ROVER_ACK_NONE;
    }
    if (strstr(line, ",ACK,OK") != NULL || strstr(line, ",OK") != NULL) {
        return ATLAS_ROVER_ACK_OK;
    }
    if (strstr(line, ",ACK,BUSY") != NULL || strstr(line, ",BUSY") != NULL) {
        return ATLAS_ROVER_ACK_BUSY;
    }
    if (strstr(line, ",ACK,ERR") != NULL || strstr(line, ",ERR") != NULL) {
        return ATLAS_ROVER_ACK_ERROR;
    }
    return ATLAS_ROVER_ACK_NONE;
}
