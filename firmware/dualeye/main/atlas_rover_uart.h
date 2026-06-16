#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "atlas_expression.h"

#define ATLAS_ROVER_UART_PORT UART_NUM_0
#define ATLAS_ROVER_UART_BAUD 115200
#define ATLAS_ROVER_UART_PREFIX "AR1,"

typedef enum {
    ATLAS_ROVER_ACK_NONE = 0,
    ATLAS_ROVER_ACK_OK,
    ATLAS_ROVER_ACK_ERROR,
    ATLAS_ROVER_ACK_BUSY,
} atlas_rover_ack_t;

esp_err_t atlas_rover_uart_init(void);
esp_err_t atlas_rover_uart_send_line(const char *line);
esp_err_t atlas_rover_uart_send_stop(void);
esp_err_t atlas_rover_uart_send_move(atlas_motion_t motion, uint8_t speed, uint16_t duration_ms);
esp_err_t atlas_rover_uart_send_turn(atlas_motion_t motion, uint8_t speed, uint16_t duration_ms);
int atlas_rover_uart_read_line(char *buffer, size_t buffer_size, TickType_t timeout_ticks);
atlas_rover_ack_t atlas_rover_uart_parse_ack(const char *line);
