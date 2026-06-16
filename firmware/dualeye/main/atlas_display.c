#include "atlas_display.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"

#if CONFIG_ATLAS_DISPLAY_WAVESHARE_LVGL
#include <assert.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "esp_lcd_gc9a01.h"
#include "lvgl.h"
#endif

static const char *TAG = "atlas_display";

static uint32_t s_last_render_log_ms;
static const atlas_theme_palette_t *s_theme;
static uint8_t s_brightness = 70;

#if CONFIG_ATLAS_DISPLAY_WAVESHARE_LVGL
/*
 * Waveshare official ESP-IDF example pin map for ESP32-S3-DualEye-Touch-LCD-1.28:
 * dual GC9A01 240x240 LCDs on SPI2, LVGL 8.3, CST816S touch on two I2C buses.
 */
#define ATLAS_LCD_HOST SPI2_HOST
#define ATLAS_LCD_WIDTH 240
#define ATLAS_LCD_HEIGHT 240
#define ATLAS_LCD_BUF_LINES 10
#define ATLAS_LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)
#define ATLAS_LCD_CMD_BITS 8
#define ATLAS_LCD_PARAM_BITS 8
#define ATLAS_SPI_MISO_GPIO 40
#define ATLAS_SPI_MOSI_GPIO 42
#define ATLAS_SPI_SCLK_GPIO 41
#define ATLAS_LCD_DC_GPIO 45
#define ATLAS_LEFT_LCD_CS_GPIO 47
#define ATLAS_LEFT_LCD_RST_GPIO 48
#define ATLAS_LEFT_LCD_BL_GPIO 46
#define ATLAS_RIGHT_LCD_CS_GPIO 38
#define ATLAS_RIGHT_LCD_RST_GPIO 8
#define ATLAS_RIGHT_LCD_BL_GPIO 39
#define ATLAS_LVGL_TICK_MS 2

typedef struct {
    int cs_gpio;
    int rst_gpio;
    int bl_gpio;
    ledc_channel_t ledc_channel;
    bool mirror_x;
    bool mirror_y;
} atlas_lcd_panel_cfg_t;

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *iris;
    lv_obj_t *pupil;
    lv_obj_t *slit;
    lv_obj_t *top_lid;
    lv_obj_t *bottom_lid;
    lv_obj_t *caption;
} atlas_lvgl_eye_t;

static const atlas_lcd_panel_cfg_t s_panel_cfg[2] = {
    {
        .cs_gpio = ATLAS_LEFT_LCD_CS_GPIO,
        .rst_gpio = ATLAS_LEFT_LCD_RST_GPIO,
        .bl_gpio = ATLAS_LEFT_LCD_BL_GPIO,
        .ledc_channel = LEDC_CHANNEL_0,
        .mirror_x = true,
        .mirror_y = false,
    },
    {
        .cs_gpio = ATLAS_RIGHT_LCD_CS_GPIO,
        .rst_gpio = ATLAS_RIGHT_LCD_RST_GPIO,
        .bl_gpio = ATLAS_RIGHT_LCD_BL_GPIO,
        .ledc_channel = LEDC_CHANNEL_1,
        .mirror_x = false,
        .mirror_y = true,
    },
};

static esp_lcd_panel_handle_t s_panel[2];
static lv_disp_draw_buf_t s_draw_buf[2];
static lv_disp_drv_t s_disp_drv[2];
static lv_disp_t *s_disp[2];
static atlas_lvgl_eye_t s_eye[2];
static bool s_lvgl_ready;

static lv_color_t rgb(uint32_t color)
{
    return lv_color_hex(color & 0xFFFFFFu);
}

static uint16_t clamp_u16(int value, uint16_t min, uint16_t max)
{
    if (value < (int)min) {
        return min;
    }
    if (value > (int)max) {
        return max;
    }
    return (uint16_t)value;
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(ATLAS_LVGL_TICK_MS);
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,
                                              area->x1,
                                              area->y1,
                                              area->x2 + 1,
                                              area->y2 + 1,
                                              color_map));
    lv_disp_flush_ready(drv);
}

static esp_err_t init_backlight(size_t index)
{
    const atlas_lcd_panel_cfg_t *cfg = &s_panel_cfg[index];
    if (index == 0) {
        const ledc_timer_config_t timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_13_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc_timer_config failed");
    }

    const gpio_config_t bk_gpio_config = {
        .pin_bit_mask = 1ULL << cfg->bl_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bk_gpio_config), TAG, "gpio_config backlight failed");

    const ledc_channel_config_t channel = {
        .gpio_num = cfg->bl_gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = cfg->ledc_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "ledc_channel_config failed");
    return ESP_OK;
}

static void apply_backlight(size_t index, uint8_t brightness)
{
    const uint32_t max_duty = (1u << LEDC_TIMER_13_BIT) - 1u;
    uint32_t duty = ((uint32_t)(brightness > 100 ? 100 : brightness) * max_duty) / 100u;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, s_panel_cfg[index].ledc_channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, s_panel_cfg[index].ledc_channel);
}

static esp_err_t init_lcd_bus(void)
{
    const spi_bus_config_t buscfg = {
        .miso_io_num = ATLAS_SPI_MISO_GPIO,
        .mosi_io_num = ATLAS_SPI_MOSI_GPIO,
        .sclk_io_num = ATLAS_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ATLAS_LCD_WIDTH * ATLAS_LCD_BUF_LINES * sizeof(lv_color_t),
    };
    return spi_bus_initialize(ATLAS_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
}

static esp_err_t init_panel(size_t index)
{
    const atlas_lcd_panel_cfg_t *cfg = &s_panel_cfg[index];
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = ATLAS_LCD_DC_GPIO,
        .cs_gpio_num = cfg->cs_gpio,
        .pclk_hz = ATLAS_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = ATLAS_LCD_CMD_BITS,
        .lcd_param_bits = ATLAS_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)ATLAS_LCD_HOST,
                                                 &io_config,
                                                 &io_handle),
                        TAG,
                        "esp_lcd_new_panel_io_spi failed");

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = cfg->rst_gpio,
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &s_panel[index]),
                        TAG,
                        "esp_lcd_new_panel_gc9a01 failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel[index]), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel[index]), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel[index], true), TAG, "panel invert failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel[index], cfg->mirror_x, cfg->mirror_y),
                        TAG,
                        "panel mirror failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel[index], false), TAG, "panel swap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel[index], true), TAG, "panel on failed");
    return ESP_OK;
}

static void *alloc_lvgl_buffer(void)
{
    const size_t bytes = ATLAS_LCD_WIDTH * ATLAS_LCD_BUF_LINES * sizeof(lv_color_t);
    void *buffer = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buffer == NULL) {
        buffer = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buffer;
}

static esp_err_t init_lvgl_display(size_t index)
{
    lv_color_t *buf_a = alloc_lvgl_buffer();
    lv_color_t *buf_b = alloc_lvgl_buffer();
    ESP_RETURN_ON_FALSE(buf_a != NULL && buf_b != NULL, ESP_ERR_NO_MEM, TAG, "LVGL buffer alloc failed");

    lv_disp_draw_buf_init(&s_draw_buf[index],
                          buf_a,
                          buf_b,
                          ATLAS_LCD_WIDTH * ATLAS_LCD_BUF_LINES);

    lv_disp_drv_init(&s_disp_drv[index]);
    s_disp_drv[index].hor_res = ATLAS_LCD_WIDTH;
    s_disp_drv[index].ver_res = ATLAS_LCD_HEIGHT;
    s_disp_drv[index].flush_cb = lvgl_flush_cb;
    s_disp_drv[index].draw_buf = &s_draw_buf[index];
    s_disp_drv[index].user_data = s_panel[index];
    s_disp[index] = lv_disp_drv_register(&s_disp_drv[index]);
    ESP_RETURN_ON_FALSE(s_disp[index] != NULL, ESP_FAIL, TAG, "lv_disp_drv_register failed");
    return ESP_OK;
}

static void create_eye_screen(size_t index)
{
    atlas_lvgl_eye_t *eye = &s_eye[index];
    lv_disp_set_default(s_disp[index]);

    eye->screen = lv_obj_create(NULL);
    lv_obj_clear_flag(eye->screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(eye->screen, rgb(s_theme->eye_bg_rgb), LV_PART_MAIN);
    lv_obj_set_style_border_width(eye->screen, 0, LV_PART_MAIN);

    eye->iris = lv_obj_create(eye->screen);
    lv_obj_clear_flag(eye->iris, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(eye->iris, 132, 132);
    lv_obj_center(eye->iris);
    lv_obj_set_style_radius(eye->iris, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(eye->iris, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_border_width(eye->iris, 8, LV_PART_MAIN);

    eye->pupil = lv_obj_create(eye->iris);
    lv_obj_clear_flag(eye->pupil, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(eye->pupil, 32, 32);
    lv_obj_center(eye->pupil);
    lv_obj_set_style_radius(eye->pupil, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(eye->pupil, 0, LV_PART_MAIN);

    eye->slit = lv_obj_create(eye->screen);
    lv_obj_clear_flag(eye->slit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(eye->slit, 92, 9);
    lv_obj_center(eye->slit);
    lv_obj_set_style_radius(eye->slit, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(eye->slit, 0, LV_PART_MAIN);
    lv_obj_add_flag(eye->slit, LV_OBJ_FLAG_HIDDEN);

    eye->top_lid = lv_obj_create(eye->screen);
    eye->bottom_lid = lv_obj_create(eye->screen);
    lv_obj_clear_flag(eye->top_lid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(eye->bottom_lid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(eye->top_lid, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(eye->bottom_lid, 0, LV_PART_MAIN);
    lv_obj_set_width(eye->top_lid, ATLAS_LCD_WIDTH);
    lv_obj_set_width(eye->bottom_lid, ATLAS_LCD_WIDTH);
    lv_obj_align(eye->top_lid, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_align(eye->bottom_lid, LV_ALIGN_BOTTOM_MID, 0, 0);

    eye->caption = lv_label_create(eye->screen);
    lv_obj_set_style_text_color(eye->caption, rgb(s_theme->muted_rgb), LV_PART_MAIN);
    lv_obj_set_style_text_font(eye->caption, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(eye->caption, LV_ALIGN_BOTTOM_MID, 0, -12);

    lv_disp_load_scr(eye->screen);
}

static void apply_theme_to_eye(size_t index)
{
    atlas_lvgl_eye_t *eye = &s_eye[index];
    if (eye->screen == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(eye->screen, rgb(s_theme->eye_bg_rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_color(eye->top_lid, rgb(s_theme->eye_bg_rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_color(eye->bottom_lid, rgb(s_theme->eye_bg_rgb), LV_PART_MAIN);
    lv_obj_set_style_text_color(eye->caption, rgb(s_theme->muted_rgb), LV_PART_MAIN);
}

static void render_lvgl_eye(size_t index,
                            const atlas_eye_pose_t *pose,
                            atlas_page_t page,
                            atlas_expression_t expression)
{
    atlas_lvgl_eye_t *eye = &s_eye[index];
    if (eye->screen == NULL || pose == NULL) {
        return;
    }

    apply_theme_to_eye(index);

    const uint16_t iris_size = clamp_u16((int)(128 * pose->iris_scale) / 100, 24, 190);
    const uint16_t pupil_size = clamp_u16((int)(34 * pose->pupil_scale) / 100, 8, 80);
    const int x = (pose->look_x * 42) / 100;
    const int y = (pose->look_y * 42) / 100;
    const uint16_t top_h = clamp_u16((int)(ATLAS_LCD_HEIGHT * pose->top_lid) / 100, 0, ATLAS_LCD_HEIGHT);
    const uint16_t bottom_h = clamp_u16((int)(ATLAS_LCD_HEIGHT * pose->bottom_lid) / 100, 0, ATLAS_LCD_HEIGHT);
    const lv_color_t accent = rgb(pose->accent_rgb);

    lv_obj_set_size(eye->iris, iris_size, iris_size);
    lv_obj_align(eye->iris, LV_ALIGN_CENTER, x, y);
    lv_obj_set_size(eye->pupil, pupil_size, pupil_size);
    lv_obj_center(eye->pupil);
    lv_obj_set_style_border_color(eye->iris, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_color(eye->iris, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_color(eye->pupil, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_color(eye->slit, accent, LV_PART_MAIN);

    if (pose->visible) {
        lv_obj_clear_flag(eye->iris, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(eye->pupil, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(eye->slit, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(eye->iris, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(eye->pupil, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(eye->slit, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_set_height(eye->top_lid, top_h);
    lv_obj_set_height(eye->bottom_lid, bottom_h);
    lv_obj_move_foreground(eye->top_lid);
    lv_obj_move_foreground(eye->bottom_lid);
    lv_obj_move_foreground(eye->caption);

    if (page == ATLAS_PAGE_EYES) {
        lv_label_set_text(eye->caption, atlas_expression_name(expression));
    } else {
        lv_label_set_text(eye->caption, atlas_page_name(page));
    }
}

static esp_err_t init_waveshare_lvgl_backend(void)
{
    ESP_LOGI(TAG, "display backend: Waveshare DualEye GC9A01/LVGL");
    ESP_RETURN_ON_ERROR(init_lcd_bus(), TAG, "init_lcd_bus failed");
    for (size_t i = 0; i < 2; ++i) {
        ESP_RETURN_ON_ERROR(init_backlight(i), TAG, "init_backlight failed");
        ESP_RETURN_ON_ERROR(init_panel(i), TAG, "init_panel failed");
    }

    lv_init();
    for (size_t i = 0; i < 2; ++i) {
        ESP_RETURN_ON_ERROR(init_lvgl_display(i), TAG, "init_lvgl_display failed");
        create_eye_screen(i);
        apply_backlight(i, s_brightness);
    }

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "atlas_lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &tick_timer), TAG, "lvgl tick timer create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, ATLAS_LVGL_TICK_MS * 1000),
                        TAG,
                        "lvgl tick timer start failed");

    s_lvgl_ready = true;
    return ESP_OK;
}
#endif

static const char *effect_name(atlas_eye_effect_t effect)
{
    switch (effect) {
    case ATLAS_EYE_EFFECT_PULSE:
        return "pulse";
    case ATLAS_EYE_EFFECT_SCAN:
        return "scan";
    case ATLAS_EYE_EFFECT_TALK:
        return "talk";
    case ATLAS_EYE_EFFECT_CHARGE:
        return "charge";
    case ATLAS_EYE_EFFECT_ERROR:
        return "error";
    case ATLAS_EYE_EFFECT_NONE:
    default:
        return "none";
    }
}

const char *atlas_page_name(atlas_page_t page)
{
    switch (page) {
    case ATLAS_PAGE_EYES:
        return "eyes";
    case ATLAS_PAGE_CLOCK:
        return "clock";
    case ATLAS_PAGE_STATUS:
        return "status";
    case ATLAS_PAGE_VOICE:
        return "voice";
    case ATLAS_PAGE_SETTINGS:
        return "settings";
    case ATLAS_PAGE_ALARM:
        return "alarm";
    case ATLAS_PAGE_PHOTO:
        return "photo";
    case ATLAS_PAGE_MUSIC:
        return "music";
    case ATLAS_PAGE_STORY:
        return "story";
    case ATLAS_PAGE_CHAT:
        return "chat";
    case ATLAS_PAGE_CALENDAR:
        return "calendar";
    case ATLAS_PAGE_POMODORO:
        return "pomodoro";
    default:
        return "unknown";
    }
}

void atlas_display_set_theme(const char *theme_id)
{
    s_theme = atlas_expression_theme_palette_by_id(theme_id);
#if CONFIG_ATLAS_DISPLAY_WAVESHARE_LVGL
    if (s_lvgl_ready) {
        apply_theme_to_eye(0);
        apply_theme_to_eye(1);
    }
#endif
    ESP_LOGI(TAG, "display theme=%s", s_theme->id);
}

void atlas_display_set_brightness(uint8_t brightness)
{
    s_brightness = brightness > 100 ? 100 : brightness;
#if CONFIG_ATLAS_DISPLAY_WAVESHARE_LVGL
    if (s_lvgl_ready) {
        apply_backlight(0, s_brightness);
        apply_backlight(1, s_brightness);
    }
#endif
}

esp_err_t atlas_display_init(void)
{
    if (s_theme == NULL) {
        s_theme = atlas_expression_default_theme();
    }

#if CONFIG_ATLAS_DISPLAY_WAVESHARE_LVGL
    const esp_err_t err = init_waveshare_lvgl_backend();
    if (err == ESP_OK) {
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Waveshare LVGL backend failed: %s; falling back to log renderer", esp_err_to_name(err));
#endif

    ESP_LOGI(TAG, "display backend: log renderer fallback, dual 240x240 eye frames");
    return ESP_OK;
}

void atlas_display_render(atlas_page_t page,
                          atlas_expression_t expression,
                          atlas_motion_t motion,
                          uint8_t audio_level,
                          uint32_t now_ms)
{
    if (s_theme == NULL) {
        s_theme = atlas_expression_default_theme();
    }

    atlas_eye_frame_t frame;
    atlas_expression_make_frame_with_theme(expression, motion, now_ms, audio_level, s_theme, &frame);

#if CONFIG_ATLAS_DISPLAY_WAVESHARE_LVGL
    if (s_lvgl_ready) {
        render_lvgl_eye(0, &frame.left, page, expression);
        render_lvgl_eye(1, &frame.right, page, expression);
        lv_timer_handler();
    }
#endif

    if (now_ms - s_last_render_log_ms < 1000) {
        return;
    }
    s_last_render_log_ms = now_ms;

    ESP_LOGI(TAG,
             "theme=%s page=%s expr=%s motion=%s L{x=%d y=%d iris=%u pupil=%u lid=%u/%u color=%06x effect=%s visible=%d} "
             "R{x=%d y=%d iris=%u pupil=%u lid=%u/%u color=%06x effect=%s visible=%d}",
             s_theme->id,
             atlas_page_name(page),
             atlas_expression_name(expression),
             atlas_motion_name(motion),
             frame.left.look_x,
             frame.left.look_y,
             frame.left.iris_scale,
             frame.left.pupil_scale,
             frame.left.top_lid,
             frame.left.bottom_lid,
             (unsigned int)frame.left.accent_rgb,
             effect_name(frame.left.effect),
             frame.left.visible,
             frame.right.look_x,
             frame.right.look_y,
             frame.right.iris_scale,
             frame.right.pupil_scale,
             frame.right.top_lid,
             frame.right.bottom_lid,
             (unsigned int)frame.right.accent_rgb,
             effect_name(frame.right.effect),
             frame.right.visible);
}
