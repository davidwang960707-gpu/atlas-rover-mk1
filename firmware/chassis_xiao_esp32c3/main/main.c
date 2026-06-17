#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CHASSIS_UART_PORT UART_NUM_1
#define CHASSIS_UART_TX_GPIO GPIO_NUM_21  // XIAO D6 -> DualEye Pin9 RXD
#define CHASSIS_UART_RX_GPIO GPIO_NUM_20  // XIAO D7 <- DualEye Pin10 TXD
#define CHASSIS_UART_BAUD 115200

#define MOTOR_LEFT_IN1_GPIO GPIO_NUM_4   // XIAO D2 -> DRV8833 AIN1
#define MOTOR_LEFT_IN2_GPIO GPIO_NUM_5   // XIAO D3 -> DRV8833 AIN2
#define MOTOR_RIGHT_IN1_GPIO GPIO_NUM_6  // XIAO D4 -> DRV8833 BIN1
#define MOTOR_RIGHT_IN2_GPIO GPIO_NUM_7  // XIAO D5 -> DRV8833 BIN2

#define PWM_FREQ_HZ 20000
#define PWM_TIMER LEDC_TIMER_0
#define PWM_MODE LEDC_LOW_SPEED_MODE
#define PWM_RESOLUTION LEDC_TIMER_10_BIT
#define PWM_MAX_DUTY ((1U << 10) - 1U)

#define SPEED_MAX_PERCENT 60
#define DURATION_MIN_MS 50
#define DURATION_MAX_MS 1500
#define CONTROL_TICK_MS 20
#define UART_LINE_MAX 96

static const char *TAG = "atlas_chassis";

typedef enum {
    MOTION_STOP = 0,
    MOTION_FORWARD,
    MOTION_BACKWARD,
    MOTION_TURN_LEFT,
    MOTION_TURN_RIGHT,
} motion_t;

typedef struct {
    bool moving;
    motion_t motion;
    uint8_t speed_percent;
    int64_t deadline_us;
} chassis_state_t;

static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static chassis_state_t s_state = {
    .moving = false,
    .motion = MOTION_STOP,
    .speed_percent = 0,
    .deadline_us = 0,
};

static uint8_t clamp_speed(unsigned value)
{
    if (value > SPEED_MAX_PERCENT) {
        return SPEED_MAX_PERCENT;
    }
    return (uint8_t)value;
}

static uint16_t clamp_duration(unsigned value)
{
    if (value < DURATION_MIN_MS) {
        return DURATION_MIN_MS;
    }
    if (value > DURATION_MAX_MS) {
        return DURATION_MAX_MS;
    }
    return (uint16_t)value;
}

static uint32_t speed_to_duty(uint8_t speed_percent)
{
    return (PWM_MAX_DUTY * speed_percent) / 100U;
}

static void set_pwm(ledc_channel_t channel, uint32_t duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(PWM_MODE, channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(PWM_MODE, channel));
}

static void drive_motor_pair(int left_dir, int right_dir, uint8_t speed_percent)
{
    const uint32_t duty = speed_to_duty(speed_percent);

    set_pwm(LEDC_CHANNEL_0, left_dir > 0 ? duty : 0);
    set_pwm(LEDC_CHANNEL_1, left_dir < 0 ? duty : 0);
    set_pwm(LEDC_CHANNEL_2, right_dir > 0 ? duty : 0);
    set_pwm(LEDC_CHANNEL_3, right_dir < 0 ? duty : 0);
}

static void motors_stop(void)
{
    set_pwm(LEDC_CHANNEL_0, 0);
    set_pwm(LEDC_CHANNEL_1, 0);
    set_pwm(LEDC_CHANNEL_2, 0);
    set_pwm(LEDC_CHANNEL_3, 0);
}

static void apply_motion(motion_t motion, uint8_t speed_percent)
{
    switch (motion) {
    case MOTION_FORWARD:
        drive_motor_pair(1, 1, speed_percent);
        break;
    case MOTION_BACKWARD:
        drive_motor_pair(-1, -1, speed_percent);
        break;
    case MOTION_TURN_LEFT:
        drive_motor_pair(-1, 1, speed_percent);
        break;
    case MOTION_TURN_RIGHT:
        drive_motor_pair(1, -1, speed_percent);
        break;
    case MOTION_STOP:
    default:
        motors_stop();
        break;
    }
}

static void set_state_stopped(void)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state.moving = false;
    s_state.motion = MOTION_STOP;
    s_state.speed_percent = 0;
    s_state.deadline_us = 0;
    portEXIT_CRITICAL(&s_state_lock);
}

static void stop_now(void)
{
    motors_stop();
    set_state_stopped();
}

static void start_motion(motion_t motion, uint8_t speed_percent, uint16_t duration_ms)
{
    const int64_t now_us = esp_timer_get_time();

    apply_motion(motion, speed_percent);

    portENTER_CRITICAL(&s_state_lock);
    s_state.moving = motion != MOTION_STOP && speed_percent > 0;
    s_state.motion = motion;
    s_state.speed_percent = speed_percent;
    s_state.deadline_us = now_us + ((int64_t)duration_ms * 1000);
    portEXIT_CRITICAL(&s_state_lock);
}

static void send_ack(const char *status)
{
    char line[24];
    const int written = snprintf(line, sizeof(line), "AR1,ACK,%s\n", status);
    if (written > 0) {
        uart_write_bytes(CHASSIS_UART_PORT, line, written);
    }
}

static bool parse_unsigned(const char *text, unsigned *out)
{
    if (text == NULL || *text == '\0') {
        return false;
    }

    char *end = NULL;
    const unsigned long value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > 65535UL) {
        return false;
    }

    *out = (unsigned)value;
    return true;
}

static bool parse_motion_command(char *line, motion_t *motion, uint8_t *speed, uint16_t *duration)
{
    char *save = NULL;
    const char *prefix = strtok_r(line, ",", &save);
    const char *cmd = strtok_r(NULL, ",", &save);
    const char *dir = strtok_r(NULL, ",", &save);
    const char *speed_text = strtok_r(NULL, ",", &save);
    const char *duration_text = strtok_r(NULL, ",", &save);
    const char *extra = strtok_r(NULL, ",", &save);

    if (prefix == NULL || cmd == NULL || strcmp(prefix, "AR1") != 0 || extra != NULL) {
        return false;
    }

    if (strcmp(cmd, "MOVE") == 0) {
        if (dir == NULL || speed_text == NULL || duration_text == NULL) {
            return false;
        }
        if (strcmp(dir, "F") == 0) {
            *motion = MOTION_FORWARD;
        } else if (strcmp(dir, "B") == 0) {
            *motion = MOTION_BACKWARD;
        } else {
            return false;
        }
    } else if (strcmp(cmd, "TURN") == 0) {
        if (dir == NULL || speed_text == NULL || duration_text == NULL) {
            return false;
        }
        if (strcmp(dir, "L") == 0) {
            *motion = MOTION_TURN_LEFT;
        } else if (strcmp(dir, "R") == 0) {
            *motion = MOTION_TURN_RIGHT;
        } else {
            return false;
        }
    } else {
        return false;
    }

    unsigned speed_value = 0;
    unsigned duration_value = 0;
    if (!parse_unsigned(speed_text, &speed_value) || !parse_unsigned(duration_text, &duration_value)) {
        return false;
    }

    *speed = clamp_speed(speed_value);
    *duration = clamp_duration(duration_value);
    return true;
}

static void handle_ar1_line(char *line)
{
    if (line == NULL || strncmp(line, "AR1,", 4) != 0) {
        return;
    }

    if (strcmp(line, "AR1,STOP") == 0) {
        stop_now();
        send_ack("OK");
        ESP_LOGI(TAG, "STOP");
        return;
    }

    char line_copy[UART_LINE_MAX];
    strlcpy(line_copy, line, sizeof(line_copy));

    motion_t motion = MOTION_STOP;
    uint8_t speed = 0;
    uint16_t duration = 0;
    if (!parse_motion_command(line_copy, &motion, &speed, &duration)) {
        stop_now();
        send_ack("ERR");
        ESP_LOGW(TAG, "Rejected command: %s", line);
        return;
    }

    start_motion(motion, speed, duration);
    send_ack("OK");
    ESP_LOGI(TAG, "Motion=%d speed=%u duration=%u", motion, speed, duration);
}

static esp_err_t init_uart(void)
{
    const uart_config_t uart_config = {
        .baud_rate = CHASSIS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_param_config(CHASSIS_UART_PORT, &uart_config), TAG, "uart_param_config");
    ESP_RETURN_ON_ERROR(uart_set_pin(CHASSIS_UART_PORT,
                                     CHASSIS_UART_TX_GPIO,
                                     CHASSIS_UART_RX_GPIO,
                                     UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG,
                        "uart_set_pin");
    ESP_RETURN_ON_ERROR(uart_driver_install(CHASSIS_UART_PORT, 2048, 0, 0, NULL, 0), TAG, "uart_driver_install");
    return ESP_OK;
}

static esp_err_t init_pwm(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = PWM_MODE,
        .duty_resolution = PWM_RESOLUTION,
        .timer_num = PWM_TIMER,
        .freq_hz = PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc_timer_config");

    const ledc_channel_config_t channels[] = {
        {
            .gpio_num = MOTOR_LEFT_IN1_GPIO,
            .speed_mode = PWM_MODE,
            .channel = LEDC_CHANNEL_0,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = PWM_TIMER,
            .duty = 0,
            .hpoint = 0,
        },
        {
            .gpio_num = MOTOR_LEFT_IN2_GPIO,
            .speed_mode = PWM_MODE,
            .channel = LEDC_CHANNEL_1,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = PWM_TIMER,
            .duty = 0,
            .hpoint = 0,
        },
        {
            .gpio_num = MOTOR_RIGHT_IN1_GPIO,
            .speed_mode = PWM_MODE,
            .channel = LEDC_CHANNEL_2,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = PWM_TIMER,
            .duty = 0,
            .hpoint = 0,
        },
        {
            .gpio_num = MOTOR_RIGHT_IN2_GPIO,
            .speed_mode = PWM_MODE,
            .channel = LEDC_CHANNEL_3,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = PWM_TIMER,
            .duty = 0,
            .hpoint = 0,
        },
    };

    for (size_t i = 0; i < sizeof(channels) / sizeof(channels[0]); ++i) {
        ESP_RETURN_ON_ERROR(ledc_channel_config(&channels[i]), TAG, "ledc_channel_config");
    }

    motors_stop();
    return ESP_OK;
}

static void uart_task(void *arg)
{
    (void)arg;

    char line[UART_LINE_MAX] = {0};
    size_t len = 0;
    uint8_t byte = 0;

    while (true) {
        const int read = uart_read_bytes(CHASSIS_UART_PORT, &byte, 1, pdMS_TO_TICKS(50));
        if (read <= 0) {
            continue;
        }

        if (byte == '\r' || byte == '\n') {
            if (len > 0) {
                line[len] = '\0';
                handle_ar1_line(line);
                len = 0;
            }
            continue;
        }

        if (len + 1 < sizeof(line)) {
            line[len++] = (char)byte;
        } else {
            len = 0;
            stop_now();
            send_ack("ERR");
            ESP_LOGW(TAG, "UART line overflow, stopped");
        }
    }
}

static void watchdog_task(void *arg)
{
    (void)arg;

    while (true) {
        bool expired = false;

        portENTER_CRITICAL(&s_state_lock);
        if (s_state.moving && esp_timer_get_time() >= s_state.deadline_us) {
            expired = true;
        }
        portEXIT_CRITICAL(&s_state_lock);

        if (expired) {
            stop_now();
            ESP_LOGI(TAG, "Motion deadline reached, stopped");
        }

        vTaskDelay(pdMS_TO_TICKS(CONTROL_TICK_MS));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Atlas Rover Mk.1 chassis firmware for Seeed XIAO ESP32C3");
    ESP_LOGI(TAG, "UART1 TX=D6/GPIO21 RX=D7/GPIO20, DRV8833 pins D2-D5");

    ESP_ERROR_CHECK(init_pwm());
    ESP_ERROR_CHECK(init_uart());

    xTaskCreate(uart_task, "atlas_uart", 4096, NULL, 10, NULL);
    xTaskCreate(watchdog_task, "atlas_watchdog", 2048, NULL, 9, NULL);
}
