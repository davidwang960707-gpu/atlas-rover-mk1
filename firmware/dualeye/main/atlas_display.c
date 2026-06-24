#include "atlas_display.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
#include "esp_spiffs.h"
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
 *
 * The panel orientation follows the Waveshare ESP-IDF 90-degree example mapping
 * verified on the user's DualEye board.
 */
#define ATLAS_LCD_HOST SPI2_HOST
#define ATLAS_LCD_WIDTH 240
#define ATLAS_LCD_HEIGHT 240
#define ATLAS_LCD_BUF_LINES 10
#define ATLAS_LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)
#define ATLAS_LCD_CMD_BITS 8
#define ATLAS_LCD_PARAM_BITS 8
#define ATLAS_CLOCK_TICK_COUNT 12
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
    bool swap_xy;
} atlas_lcd_panel_cfg_t;

static void set_eye_page_content(size_t index,
                                atlas_page_t page,
                                const atlas_display_payload_t *payload,
                                uint32_t now_ms);

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *iris;
    lv_obj_t *pupil;
    lv_obj_t *slit;
    lv_obj_t *asset_img;
    lv_obj_t *progress_arc;
    lv_obj_t *clock_face;
    lv_obj_t *clock_ticks[ATLAS_CLOCK_TICK_COUNT];
    lv_obj_t *clock_hour_hand;
    lv_obj_t *clock_minute_hand;
    lv_obj_t *clock_second_hand;
    lv_obj_t *clock_center;
    lv_obj_t *top_lid;
    lv_obj_t *bottom_lid;
    lv_obj_t *caption;
    lv_obj_t *content;
    lv_obj_t *status_bar;
    lv_obj_t *status_text;
    char asset_src[128];
} atlas_lvgl_eye_t;

static const atlas_lcd_panel_cfg_t s_panel_cfg[2] = {
    {
        .cs_gpio = ATLAS_LEFT_LCD_CS_GPIO,
        .rst_gpio = ATLAS_LEFT_LCD_RST_GPIO,
        .bl_gpio = ATLAS_LEFT_LCD_BL_GPIO,
        .ledc_channel = LEDC_CHANNEL_0,
        .mirror_x = false,
        .mirror_y = false,
        .swap_xy = true,
    },
    {
        .cs_gpio = ATLAS_RIGHT_LCD_CS_GPIO,
        .rst_gpio = ATLAS_RIGHT_LCD_RST_GPIO,
        .bl_gpio = ATLAS_RIGHT_LCD_BL_GPIO,
        .ledc_channel = LEDC_CHANNEL_1,
        .mirror_x = true,
        .mirror_y = true,
        .swap_xy = true,
    },
};

static esp_lcd_panel_handle_t s_panel[2];
static lv_disp_draw_buf_t s_draw_buf[2];
static lv_disp_drv_t s_disp_drv[2];
static lv_disp_t *s_disp[2];
static atlas_lvgl_eye_t s_eye[2];
static bool s_lvgl_ready;
static bool s_assets_ready;
static bool s_lvgl_asset_fs_registered;

#define ATLAS_ASSET_MOUNT_PATH "/spiffs"
#define ATLAS_ASSET_LVGL_LETTER 'A'
#define ATLAS_VALIDATED_EYE_ASSET_MAX 192

static char s_validated_eye_assets[ATLAS_VALIDATED_EYE_ASSET_MAX][128];
static size_t s_validated_eye_asset_count;

static lv_color_t rgb(uint32_t color)
{
    return lv_color_hex(color & 0xFFFFFFu);
}

LV_FONT_DECLARE(atlas_font_zh_16);

static const lv_font_t *font_cjk(void)
{
    return &atlas_font_zh_16;
}

static const lv_font_t *font_countdown(void)
{
#if CONFIG_LV_FONT_MONTSERRAT_36
    return &lv_font_montserrat_36;
#elif CONFIG_LV_FONT_MONTSERRAT_28
    return &lv_font_montserrat_28;
#elif CONFIG_LV_FONT_MONTSERRAT_24
    return &lv_font_montserrat_24;
#else
    return &lv_font_montserrat_14;
#endif
}

static const lv_font_t *font_ui_small(void)
{
    return &atlas_font_zh_16;
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

static void asset_path_to_local(char *dst, size_t dst_size, const char *path)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (path == NULL) {
        dst[0] = '\0';
        return;
    }

    const char *rel = strchr(path, ':');
    rel = rel == NULL ? path : rel + 1;
    while (*rel == '/') {
        ++rel;
    }
    snprintf(dst, dst_size, "%s/%s", ATLAS_ASSET_MOUNT_PATH, rel);
}

static void *asset_fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    (void)drv;
    if ((mode & LV_FS_MODE_WR) != 0) {
        return NULL;
    }

    char local_path[176];
    asset_path_to_local(local_path, sizeof(local_path), path);
    return fopen(local_path, "rb");
}

static lv_fs_res_t asset_fs_close(lv_fs_drv_t *drv, void *file_p)
{
    (void)drv;
    if (file_p == NULL) {
        return LV_FS_RES_INV_PARAM;
    }
    return fclose((FILE *)file_p) == 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t asset_fs_read(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br)
{
    (void)drv;
    if (file_p == NULL || buf == NULL || br == NULL) {
        return LV_FS_RES_INV_PARAM;
    }
    *br = (uint32_t)fread(buf, 1, btr, (FILE *)file_p);
    return ferror((FILE *)file_p) ? LV_FS_RES_UNKNOWN : LV_FS_RES_OK;
}

static lv_fs_res_t asset_fs_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence)
{
    (void)drv;
    if (file_p == NULL) {
        return LV_FS_RES_INV_PARAM;
    }

    int origin = SEEK_SET;
    if (whence == LV_FS_SEEK_CUR) {
        origin = SEEK_CUR;
    } else if (whence == LV_FS_SEEK_END) {
        origin = SEEK_END;
    }
    return fseek((FILE *)file_p, (long)pos, origin) == 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t asset_fs_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
    (void)drv;
    if (file_p == NULL || pos_p == NULL) {
        return LV_FS_RES_INV_PARAM;
    }

    const long pos = ftell((FILE *)file_p);
    if (pos < 0) {
        return LV_FS_RES_UNKNOWN;
    }
    *pos_p = (uint32_t)pos;
    return LV_FS_RES_OK;
}

static void register_lvgl_asset_fs(void)
{
    if (s_lvgl_asset_fs_registered) {
        return;
    }

    static lv_fs_drv_t fs_drv;
    lv_fs_drv_init(&fs_drv);
    fs_drv.letter = ATLAS_ASSET_LVGL_LETTER;
    fs_drv.cache_size = 4096;
    fs_drv.open_cb = asset_fs_open;
    fs_drv.close_cb = asset_fs_close;
    fs_drv.read_cb = asset_fs_read;
    fs_drv.seek_cb = asset_fs_seek;
    fs_drv.tell_cb = asset_fs_tell;
    lv_fs_drv_register(&fs_drv);
    s_lvgl_asset_fs_registered = true;
}

static esp_err_t mount_asset_spiffs(void)
{
    if (s_assets_ready) {
        return ESP_OK;
    }

    const esp_vfs_spiffs_conf_t conf = {
        .base_path = ATLAS_ASSET_MOUNT_PATH,
        .partition_label = "storage",
        .max_files = 8,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "eye asset SPIFFS mount failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0;
    size_t used = 0;
    if (esp_spiffs_info("storage", &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "eye asset SPIFFS mounted: total=%u used=%u", (unsigned)total, (unsigned)used);
    }
    s_assets_ready = true;
    return ESP_OK;
}

static bool theme_has_eye_assets(const char *theme_id)
{
    return theme_id != NULL &&
           (strcmp(theme_id, "raptor") == 0 ||
            strcmp(theme_id, "mecha") == 0 ||
            strcmp(theme_id, "goggle") == 0 ||
            strcmp(theme_id, "pet") == 0 ||
            strcmp(theme_id, "blue_pupil") == 0 ||
            strcmp(theme_id, "no_smoking") == 0 ||
            strcmp(theme_id, "tomoe_spin") == 0);
}

static bool theme_uses_clockwise_rotation(const char *theme_id)
{
    return theme_id != NULL &&
           strcmp(theme_id, "tomoe_spin") == 0;
}

static bool s_tomoe_spin_active;
static uint32_t s_tomoe_spin_started_ms;

static uint16_t tomoe_spin_angle(uint32_t now_ms)
{
    if (!s_tomoe_spin_active) {
        s_tomoe_spin_active = true;
        s_tomoe_spin_started_ms = now_ms;
    }

    const uint32_t elapsed_ms = now_ms - s_tomoe_spin_started_ms;
    const uint32_t ramp_ms = 4000u;
    const uint32_t initial_period_ms = 1800u;
    const uint32_t target_period_ms = 900u;
    const uint64_t initial_speed = (3600ull * 1000ull) / initial_period_ms;
    const uint64_t target_speed = (3600ull * 1000ull) / target_period_ms;

    uint64_t angle_x1000 = 0;
    if (elapsed_ms < ramp_ms) {
        const uint64_t t = elapsed_ms;
        const uint64_t delta_speed = target_speed - initial_speed;
        angle_x1000 = initial_speed * t + (delta_speed * t * t) / (2ull * ramp_ms);
    } else {
        const uint64_t ramp_angle = initial_speed * ramp_ms + ((target_speed - initial_speed) * ramp_ms) / 2ull;
        const uint32_t loop_ms = (elapsed_ms - ramp_ms) % target_period_ms;
        angle_x1000 = ramp_angle + target_speed * loop_ms;
    }

    return (uint16_t)((angle_x1000 / 1000ull) % 3600ull);
}

static int16_t triangle_wave(uint32_t now_ms, uint32_t period_ms, int16_t amplitude)
{
    if (period_ms == 0 || amplitude == 0) {
        return 0;
    }

    const uint32_t phase = now_ms % period_ms;
    const uint32_t half = period_ms / 2u;
    const uint32_t ramp = phase < half ? phase : period_ms - phase;
    return (int16_t)(((int32_t)ramp * 4 * amplitude) / (int32_t)period_ms - amplitude);
}

static const char *asset_state_for_expression(atlas_expression_t expression, uint32_t now_ms, size_t index)
{
    switch (expression) {
    case ATLAS_EXPR_LISTEN:
    case ATLAS_EXPR_THINKING:
    case ATLAS_EXPR_SPEAKING:
    case ATLAS_EXPR_CURIOUS:
        return "listen";
    case ATLAS_EXPR_WINK:
    case ATLAS_EXPR_SLEEPY:
        return "blink";
    case ATLAS_EXPR_IDLE:
    case ATLAS_EXPR_HAPPY:
    case ATLAS_EXPR_LOVE:
    case ATLAS_EXPR_MONEY:
    case ATLAS_EXPR_MOVING:
    case ATLAS_EXPR_SURPRISED:
    case ATLAS_EXPR_ANGRY:
    case ATLAS_EXPR_CHARGING:
    case ATLAS_EXPR_ERROR:
    case ATLAS_EXPR_CRY:
    default:
        if (((now_ms + (uint32_t)index * 170u) % 4300u) < 145u) {
            return "blink";
        }
        return "idle";
    }
}

static void apply_eye_asset_motion(size_t index, atlas_expression_t expression, uint32_t now_ms)
{
    atlas_lvgl_eye_t *eye = &s_eye[index];
    if (eye->asset_img == NULL) {
        return;
    }

    int16_t x = index == 0 ? 2 : -2;
    int16_t y = triangle_wave(now_ms + (uint32_t)index * 211u, 2600u, 2);
    int16_t zoom = 258 + triangle_wave(now_ms + (uint32_t)index * 137u, 3200u, 2);
    uint16_t angle = 0;

    switch (expression) {
    case ATLAS_EXPR_LISTEN:
    case ATLAS_EXPR_THINKING:
    case ATLAS_EXPR_SPEAKING:
    case ATLAS_EXPR_CURIOUS:
        x += triangle_wave(now_ms + (uint32_t)index * 89u, 1200u, 3);
        y += triangle_wave(now_ms + (uint32_t)index * 131u, 900u, 2);
        zoom = 263 + triangle_wave(now_ms + (uint32_t)index * 173u, 780u, 4);
        break;
    case ATLAS_EXPR_MOVING:
        x += triangle_wave(now_ms + (uint32_t)index * 67u, 760u, 5);
        y += triangle_wave(now_ms + (uint32_t)index * 97u, 1180u, 3);
        zoom = 262;
        break;
    case ATLAS_EXPR_WINK:
    case ATLAS_EXPR_SLEEPY:
        x = 0;
        y = 0;
        zoom = 256;
        break;
    default:
        break;
    }

    const char *theme_id = s_theme == NULL ? NULL : s_theme->id;
    if (theme_id != NULL && strcmp(theme_id, "goggle") == 0) {
        zoom += 8;
    } else if (theme_id != NULL && strcmp(theme_id, "no_smoking") == 0) {
        x = 0;
        y = 0;
        zoom = 274;
    }

    if (!theme_uses_clockwise_rotation(theme_id)) {
        s_tomoe_spin_active = false;
    }

    if (theme_uses_clockwise_rotation(theme_id)) {
        angle = tomoe_spin_angle(now_ms);
        x = 0;
        y = 0;
        zoom = expression == ATLAS_EXPR_LISTEN ||
                       expression == ATLAS_EXPR_THINKING ||
                       expression == ATLAS_EXPR_SPEAKING ||
                       expression == ATLAS_EXPR_CURIOUS
                   ? 276
                   : 270;
    }

    lv_obj_align(eye->asset_img, LV_ALIGN_CENTER, x, y);
    lv_img_set_zoom(eye->asset_img, (uint16_t)clamp_u16(zoom, 252, 286));
    lv_img_set_angle(eye->asset_img, angle);
}

static bool asset_exists(const char *lvgl_src)
{
    char local_path[176];
    asset_path_to_local(local_path, sizeof(local_path), lvgl_src);
    FILE *file = fopen(local_path, "rb");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

static bool eye_asset_was_validated(const char *src)
{
    if (src == NULL) {
        return false;
    }

    for (size_t i = 0; i < s_validated_eye_asset_count; ++i) {
        if (strcmp(s_validated_eye_assets[i], src) == 0) {
            return true;
        }
    }
    return false;
}

static void remember_valid_eye_asset(const char *src)
{
    if (src == NULL || eye_asset_was_validated(src) ||
        s_validated_eye_asset_count >= ATLAS_VALIDATED_EYE_ASSET_MAX) {
        return;
    }

    strlcpy(s_validated_eye_assets[s_validated_eye_asset_count],
            src,
            sizeof(s_validated_eye_assets[s_validated_eye_asset_count]));
    ++s_validated_eye_asset_count;
}

static bool validate_eye_asset(const char *src)
{
    if (eye_asset_was_validated(src)) {
        return true;
    }

    lv_img_header_t header;
    if (lv_img_decoder_get_info(src, &header) != LV_RES_OK) {
        ESP_LOGW(TAG, "eye PNG header decode failed: %s", src);
        return false;
    }

    if (header.w != ATLAS_LCD_WIDTH || header.h != ATLAS_LCD_HEIGHT) {
        ESP_LOGW(TAG,
                 "eye PNG size mismatch: %s is %ux%u, expected %ux%u",
                 src,
                 (unsigned)header.w,
                 (unsigned)header.h,
                 (unsigned)ATLAS_LCD_WIDTH,
                 (unsigned)ATLAS_LCD_HEIGHT);
        return false;
    }

    lv_img_decoder_dsc_t dsc;
    if (lv_img_decoder_open(&dsc, src, rgb(s_theme->primary_rgb), 0) != LV_RES_OK) {
        ESP_LOGW(TAG, "eye PNG full decode failed, fallback to vector eye: %s", src);
        return false;
    }
    lv_img_decoder_close(&dsc);
    ESP_LOGI(TAG, "eye PNG ready: %s (%ux%u cf=%u)",
             src,
             (unsigned)header.w,
             (unsigned)header.h,
             (unsigned)header.cf);
    remember_valid_eye_asset(src);
    return true;
}

static void set_vector_objects_hidden(atlas_lvgl_eye_t *eye, bool hidden)
{
    if (eye == NULL) {
        return;
    }
    lv_obj_t *objects[] = {
        eye->iris,
        eye->pupil,
        eye->slit,
        eye->top_lid,
        eye->bottom_lid,
        eye->caption,
    };
    for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); ++i) {
        if (objects[i] == NULL) {
            continue;
        }
        if (hidden) {
            lv_obj_add_flag(objects[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(objects[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void set_clock_dial_hidden(atlas_lvgl_eye_t *eye, bool hidden)
{
    if (eye == NULL) {
        return;
    }
    lv_obj_t *objects[] = {
        eye->clock_face,
        eye->clock_hour_hand,
        eye->clock_minute_hand,
        eye->clock_second_hand,
        eye->clock_center,
    };
    for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); ++i) {
        if (objects[i] == NULL) {
            continue;
        }
        if (hidden) {
            lv_obj_add_flag(objects[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(objects[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    for (size_t i = 0; i < ATLAS_CLOCK_TICK_COUNT; ++i) {
        if (eye->clock_ticks[i] == NULL) {
            continue;
        }
        if (hidden) {
            lv_obj_add_flag(eye->clock_ticks[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(eye->clock_ticks[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void hide_page_content(atlas_lvgl_eye_t *eye)
{
    if (eye == NULL) {
        return;
    }
    set_clock_dial_hidden(eye, true);
    lv_obj_add_flag(eye->content, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(eye->status_bar, LV_OBJ_FLAG_HIDDEN);
    if (eye->progress_arc != NULL) {
        lv_obj_add_flag(eye->progress_arc, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(eye->status_text, "");
}

static const char *display_chat_mode(const atlas_display_payload_t *payload)
{
    if (payload != NULL && atlas_config_chat_mode_is_valid(payload->chat_mode)) {
        return payload->chat_mode;
    }
    return "pet_head";
}

static bool page_uses_chat_mode(atlas_page_t page)
{
    return page == ATLAS_PAGE_CHAT ||
           page == ATLAS_PAGE_VOICE ||
           page == ATLAS_PAGE_MUSIC ||
           page == ATLAS_PAGE_STORY;
}

static const char *pet_head_state_for_expression(atlas_page_t page, atlas_expression_t expression)
{
    if (page == ATLAS_PAGE_MUSIC) {
        return "sing";
    }
    if (page == ATLAS_PAGE_STORY) {
        return expression == ATLAS_EXPR_SPEAKING ? "speak" : "think";
    }

    switch (expression) {
    case ATLAS_EXPR_LISTEN:
        return "listen";
    case ATLAS_EXPR_SPEAKING:
        return "speak";
    case ATLAS_EXPR_THINKING:
    case ATLAS_EXPR_CURIOUS:
        return "think";
    case ATLAS_EXPR_HAPPY:
    case ATLAS_EXPR_LOVE:
        return "happy";
    case ATLAS_EXPR_CRY:
        return "cry";
    case ATLAS_EXPR_SLEEPY:
    case ATLAS_EXPR_WINK:
        return "sleepy";
    case ATLAS_EXPR_SURPRISED:
        return "surprised";
    case ATLAS_EXPR_ERROR:
    case ATLAS_EXPR_ANGRY:
        return "think";
    case ATLAS_EXPR_CHARGING:
    case ATLAS_EXPR_MONEY:
    case ATLAS_EXPR_MOVING:
    case ATLAS_EXPR_IDLE:
    case ATLAS_EXPR_COUNT:
    default:
        return "idle";
    }
}

static bool show_pet_head_keyframe(size_t index, const char *state, uint16_t zoom, int16_t x, int16_t y)
{
    atlas_lvgl_eye_t *eye = &s_eye[index];
    if (!s_assets_ready || eye->asset_img == NULL) {
        return false;
    }

    const char *safe_state = (state == NULL || state[0] == '\0') ? "idle" : state;
    char src[128];
    snprintf(src,
             sizeof(src),
             "%c:/atlas_pet_head/keyframes/%s.png",
             ATLAS_ASSET_LVGL_LETTER,
             safe_state);

    if (strcmp(eye->asset_src, src) != 0) {
        if (!asset_exists(src) || !validate_eye_asset(src)) {
            return false;
        }
        lv_img_set_src(eye->asset_img, src);
        strlcpy(eye->asset_src, src, sizeof(eye->asset_src));
    }

    lv_obj_clear_flag(eye->asset_img, LV_OBJ_FLAG_HIDDEN);
    lv_img_set_zoom(eye->asset_img, zoom);
    lv_img_set_angle(eye->asset_img, 0);
    lv_obj_align(eye->asset_img, LV_ALIGN_CENTER, x, y);
    lv_obj_move_background(eye->asset_img);
    return true;
}

static bool show_pet_head_view(size_t index,
                               const char *state,
                               const char *view,
                               uint16_t zoom,
                               int16_t x,
                               int16_t y)
{
    atlas_lvgl_eye_t *eye = &s_eye[index];
    if (!s_assets_ready || eye->asset_img == NULL) {
        return false;
    }

    const char *safe_state = (state == NULL || state[0] == '\0') ? "idle" : state;
    const char *safe_view = (view == NULL || view[0] == '\0') ? "yaw_c" : view;
    char src[128];
    snprintf(src,
             sizeof(src),
             "%c:/atlas_pet_head/views/%s/%s.png",
             ATLAS_ASSET_LVGL_LETTER,
             safe_state,
             safe_view);

    if (strcmp(eye->asset_src, src) != 0) {
        if (!asset_exists(src) || !validate_eye_asset(src)) {
            return show_pet_head_keyframe(index, safe_state, zoom, x, y);
        }
        lv_img_set_src(eye->asset_img, src);
        strlcpy(eye->asset_src, src, sizeof(eye->asset_src));
    }

    lv_obj_clear_flag(eye->asset_img, LV_OBJ_FLAG_HIDDEN);
    lv_img_set_zoom(eye->asset_img, zoom);
    lv_img_set_angle(eye->asset_img, 0);
    lv_obj_align(eye->asset_img, LV_ALIGN_CENTER, x, y);
    lv_obj_move_background(eye->asset_img);
    return true;
}

static bool show_pet_head_transition(size_t index,
                                     const char *transition,
                                     uint8_t frame_count,
                                     uint8_t fps,
                                     uint32_t elapsed_ms,
                                     uint16_t zoom,
                                     int16_t x,
                                     int16_t y,
                                     const char *fallback_state,
                                     const char *fallback_view)
{
    if (transition == NULL || transition[0] == '\0' || frame_count == 0 || fps == 0) {
        return show_pet_head_view(index, fallback_state, fallback_view, zoom, x, y);
    }

    atlas_lvgl_eye_t *eye = &s_eye[index];
    if (!s_assets_ready || eye->asset_img == NULL) {
        return false;
    }

    const uint32_t frame_ms = 1000u / fps;
    uint8_t frame = (uint8_t)((elapsed_ms / (frame_ms == 0 ? 1u : frame_ms)));
    if (frame >= frame_count) {
        frame = frame_count - 1;
    }
    char src[128];
    snprintf(src,
             sizeof(src),
             "%c:/atlas_pet_head/transitions/%s/frame_%02u.png",
             ATLAS_ASSET_LVGL_LETTER,
             transition,
             frame);

    if (strcmp(eye->asset_src, src) != 0) {
        if (!asset_exists(src) || !validate_eye_asset(src)) {
            return show_pet_head_view(index, fallback_state, fallback_view, zoom, x, y);
        }
        lv_img_set_src(eye->asset_img, src);
        strlcpy(eye->asset_src, src, sizeof(eye->asset_src));
    }

    lv_obj_clear_flag(eye->asset_img, LV_OBJ_FLAG_HIDDEN);
    lv_img_set_zoom(eye->asset_img, zoom);
    lv_img_set_angle(eye->asset_img, 0);
    lv_obj_align(eye->asset_img, LV_ALIGN_CENTER, x, y);
    lv_obj_move_background(eye->asset_img);
    return true;
}

static bool show_pet_head_animation(size_t index,
                                    const char *animation,
                                    uint8_t frame_count,
                                    uint8_t fps,
                                    uint32_t now_ms,
                                    uint16_t zoom,
                                    int16_t x,
                                    int16_t y,
                                    const char *fallback_state)
{
    if (animation == NULL || animation[0] == '\0' || frame_count == 0 || fps == 0) {
        return show_pet_head_keyframe(index, fallback_state, zoom, x, y);
    }

    atlas_lvgl_eye_t *eye = &s_eye[index];
    if (!s_assets_ready || eye->asset_img == NULL) {
        return false;
    }

    const uint32_t frame_ms = 1000u / fps;
    const uint8_t frame = (uint8_t)((now_ms / (frame_ms == 0 ? 1u : frame_ms)) % frame_count);
    char src[128];
    snprintf(src,
             sizeof(src),
             "%c:/atlas_pet_head/animations/%s/frame_%02u.png",
             ATLAS_ASSET_LVGL_LETTER,
             animation,
             frame);

    if (strcmp(eye->asset_src, src) != 0) {
        if (!asset_exists(src) || !validate_eye_asset(src)) {
            return show_pet_head_keyframe(index, fallback_state, zoom, x, y);
        }
        lv_img_set_src(eye->asset_img, src);
        strlcpy(eye->asset_src, src, sizeof(eye->asset_src));
    }

    lv_obj_clear_flag(eye->asset_img, LV_OBJ_FLAG_HIDDEN);
    lv_img_set_zoom(eye->asset_img, zoom);
    lv_img_set_angle(eye->asset_img, 0);
    lv_obj_align(eye->asset_img, LV_ALIGN_CENTER, x, y);
    lv_obj_move_background(eye->asset_img);
    return true;
}

static const char *pet_head_view_for_state(const char *state, uint32_t now_ms)
{
    if (state == NULL) {
        return "yaw_c";
    }
    if (strcmp(state, "listen") == 0) {
        return ((now_ms / 1600u) % 2u) == 0u ? "yaw_l15" : "yaw_r15";
    }
    if (strcmp(state, "think") == 0) {
        const uint32_t phase = (now_ms / 900u) % 4u;
        return phase < 2u ? "yaw_l15" : "yaw_r15";
    }
    if (strcmp(state, "speak") == 0) {
        return ((now_ms / 1400u) % 3u) == 0u ? "yaw_l15" : "yaw_c";
    }
    return "yaw_c";
}

static bool show_pet_head_idle_motion(size_t index, uint32_t now_ms, uint16_t zoom, int16_t x, int16_t y)
{
    const uint32_t cycle = now_ms % 11200u;
    if (cycle < 500u) {
        return show_pet_head_transition(index,
                                        "turn_yaw_c_to_yaw_l30",
                                        6,
                                        12,
                                        cycle,
                                        zoom,
                                        x,
                                        y,
                                        "idle",
                                        "yaw_l30");
    }
    if (cycle < 2100u) {
        return show_pet_head_view(index, "idle", "yaw_l30", zoom, x, y);
    }
    if (cycle < 2600u) {
        return show_pet_head_transition(index,
                                        "turn_yaw_l30_to_yaw_c",
                                        6,
                                        12,
                                        cycle - 2100u,
                                        zoom,
                                        x,
                                        y,
                                        "idle",
                                        "yaw_c");
    }
    if (cycle < 6600u) {
        return show_pet_head_view(index, "idle", "yaw_c", zoom, x, y);
    }
    if (cycle < 7100u) {
        return show_pet_head_transition(index,
                                        "turn_yaw_c_to_yaw_r30",
                                        6,
                                        12,
                                        cycle - 6600u,
                                        zoom,
                                        x,
                                        y,
                                        "idle",
                                        "yaw_r30");
    }
    if (cycle < 8700u) {
        return show_pet_head_view(index, "idle", "yaw_r30", zoom, x, y);
    }
    if (cycle < 9200u) {
        return show_pet_head_transition(index,
                                        "turn_yaw_r30_to_yaw_c",
                                        6,
                                        12,
                                        cycle - 8700u,
                                        zoom,
                                        x,
                                        y,
                                        "idle",
                                        "yaw_c");
    }
    return show_pet_head_view(index, "idle", "yaw_c", zoom, x, y);
}

static bool show_pet_head_for_page(size_t index,
                                   atlas_page_t page,
                                   atlas_expression_t expression,
                                   uint32_t now_ms,
                                   uint16_t zoom,
                                   int16_t x,
                                   int16_t y)
{
    const char *state = pet_head_state_for_expression(page, expression);
    if (strcmp(state, "speak") == 0) {
        return show_pet_head_animation(index, "speak", 8, 10, now_ms, zoom, x, y, "speak");
    }
    if (strcmp(state, "sing") == 0) {
        return show_pet_head_animation(index, "sing", 10, 10, now_ms, zoom, x, y, "sing");
    }
    if (strcmp(state, "happy") == 0 && page == ATLAS_PAGE_CHAT) {
        return show_pet_head_animation(index, "laugh", 8, 12, now_ms, zoom, x, y, "happy");
    }
    if (strcmp(state, "idle") == 0 && ((now_ms + 313u) % 6400u) < 520u) {
        return show_pet_head_animation(index, "blink", 6, 12, now_ms, zoom, x, y, "idle");
    }
    if (strcmp(state, "idle") == 0) {
        return show_pet_head_idle_motion(index, now_ms, zoom, x, y);
    }
    return show_pet_head_view(index, state, pet_head_view_for_state(state, now_ms), zoom, x, y);
}

static bool render_eye_asset(size_t index, atlas_expression_t expression, uint32_t now_ms)
{
    atlas_lvgl_eye_t *eye = &s_eye[index];
    if (!s_assets_ready || eye->asset_img == NULL || !theme_has_eye_assets(s_theme->id)) {
        return false;
    }

    const char *eye_name = index == 0 ? "left" : "right";
    const char *state = asset_state_for_expression(expression, now_ms, index);
    char src[128];
    snprintf(src,
             sizeof(src),
             "%c:/atlas_eyes/%s/%s/%s.png",
             ATLAS_ASSET_LVGL_LETTER,
             s_theme->id,
             state,
             eye_name);

    if (strcmp(eye->asset_src, src) != 0) {
        if (!asset_exists(src)) {
            ESP_LOGW(TAG, "missing eye PNG asset: %s", src);
            return false;
        }
        if (!validate_eye_asset(src)) {
            return false;
        }
        lv_img_set_src(eye->asset_img, src);
        strlcpy(eye->asset_src, src, sizeof(eye->asset_src));
    }

    lv_obj_clear_flag(eye->asset_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(eye->asset_img);
    apply_eye_asset_motion(index, expression, now_ms);
    set_vector_objects_hidden(eye, true);
    hide_page_content(eye);
    return true;
}

static void payload_text_fallback(const char *src, char *dst, size_t dst_len)
{
    if (dst_len == 0) {
        return;
    }
    if (src == NULL || src[0] == '\0') {
        dst[0] = '\0';
    } else {
        snprintf(dst, dst_len, "%s", src);
    }
}

static void payload_to_local(const atlas_display_payload_t *payload, atlas_display_payload_t *local_payload)
{
    if (local_payload == NULL) {
        return;
    }
    if (payload == NULL) {
        memset(local_payload, 0, sizeof(*local_payload));
        return;
    }
    *local_payload = *payload;
    payload_text_fallback(payload->chat_mode, local_payload->chat_mode, sizeof(local_payload->chat_mode));
    payload_text_fallback(payload->chat_text, local_payload->chat_text, sizeof(local_payload->chat_text));
    payload_text_fallback(payload->calendar_title, local_payload->calendar_title, sizeof(local_payload->calendar_title));
    payload_text_fallback(payload->calendar_note, local_payload->calendar_note, sizeof(local_payload->calendar_note));
    payload_text_fallback(payload->scene_state, local_payload->scene_state, sizeof(local_payload->scene_state));
    payload_text_fallback(payload->scene_title, local_payload->scene_title, sizeof(local_payload->scene_title));
    payload_text_fallback(payload->scene_subtitle, local_payload->scene_subtitle, sizeof(local_payload->scene_subtitle));
    payload_text_fallback(payload->scene_hint, local_payload->scene_hint, sizeof(local_payload->scene_hint));
    payload_text_fallback(payload->scene_left_role, local_payload->scene_left_role, sizeof(local_payload->scene_left_role));
    payload_text_fallback(payload->scene_right_role, local_payload->scene_right_role, sizeof(local_payload->scene_right_role));
    payload_text_fallback(payload->scene_severity, local_payload->scene_severity, sizeof(local_payload->scene_severity));
    payload_text_fallback(payload->pomodoro_task_name,
                          local_payload->pomodoro_task_name,
                          sizeof(local_payload->pomodoro_task_name));
    payload_text_fallback(payload->pet_ip, local_payload->pet_ip, sizeof(local_payload->pet_ip));

    if (!payload->pet_ip_valid) {
        local_payload->pet_ip[0] = '\0';
    }
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
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel[index], cfg->swap_xy), TAG, "panel swap failed");
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

    eye->asset_img = lv_img_create(eye->screen);
    lv_obj_set_size(eye->asset_img, ATLAS_LCD_WIDTH, ATLAS_LCD_HEIGHT);
    lv_img_set_pivot(eye->asset_img, ATLAS_LCD_WIDTH / 2, ATLAS_LCD_HEIGHT / 2);
    lv_obj_center(eye->asset_img);
    lv_obj_add_flag(eye->asset_img, LV_OBJ_FLAG_HIDDEN);

    eye->progress_arc = lv_arc_create(eye->screen);
    lv_obj_set_size(eye->progress_arc, 212, 212);
    lv_obj_center(eye->progress_arc);
    lv_arc_set_rotation(eye->progress_arc, 270);
    lv_arc_set_bg_angles(eye->progress_arc, 0, 360);
    lv_arc_set_range(eye->progress_arc, 0, 100);
    lv_arc_set_value(eye->progress_arc, 0);
    lv_obj_remove_style(eye->progress_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(eye->progress_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(eye->progress_arc, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_width(eye->progress_arc, 9, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(eye->progress_arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(eye->progress_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(eye->progress_arc, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
    lv_obj_set_style_arc_color(eye->progress_arc, rgb(s_theme->primary_rgb), LV_PART_INDICATOR);
    lv_obj_add_flag(eye->progress_arc, LV_OBJ_FLAG_HIDDEN);

    eye->clock_face = lv_obj_create(eye->screen);
    lv_obj_clear_flag(eye->clock_face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(eye->clock_face, 170, 170);
    lv_obj_center(eye->clock_face);
    lv_obj_set_style_radius(eye->clock_face, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(eye->clock_face, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(eye->clock_face, LV_OPA_60, LV_PART_MAIN);
    lv_obj_add_flag(eye->clock_face, LV_OBJ_FLAG_HIDDEN);

    for (size_t i = 0; i < ATLAS_CLOCK_TICK_COUNT; ++i) {
        const bool major = (i % 3u) == 0u;
        const int tick_w = major ? 4 : 2;
        const int tick_h = major ? 16 : 10;
        const int tick_y = major ? 31 : 36;
        eye->clock_ticks[i] = lv_obj_create(eye->screen);
        lv_obj_clear_flag(eye->clock_ticks[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(eye->clock_ticks[i], tick_w, tick_h);
        lv_obj_set_pos(eye->clock_ticks[i], (ATLAS_LCD_WIDTH - tick_w) / 2, tick_y);
        lv_obj_set_style_radius(eye->clock_ticks[i], tick_w, LV_PART_MAIN);
        lv_obj_set_style_border_width(eye->clock_ticks[i], 0, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_x(eye->clock_ticks[i], tick_w / 2, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_y(eye->clock_ticks[i], ATLAS_LCD_HEIGHT / 2 - tick_y, LV_PART_MAIN);
        lv_obj_set_style_transform_angle(eye->clock_ticks[i], (int32_t)i * 300, LV_PART_MAIN);
        lv_obj_add_flag(eye->clock_ticks[i], LV_OBJ_FLAG_HIDDEN);
    }

    eye->clock_hour_hand = lv_obj_create(eye->screen);
    eye->clock_minute_hand = lv_obj_create(eye->screen);
    eye->clock_second_hand = lv_obj_create(eye->screen);
    eye->clock_center = lv_obj_create(eye->screen);
    lv_obj_t *clock_parts[] = {
        eye->clock_hour_hand,
        eye->clock_minute_hand,
        eye->clock_second_hand,
        eye->clock_center,
    };
    for (size_t i = 0; i < sizeof(clock_parts) / sizeof(clock_parts[0]); ++i) {
        lv_obj_clear_flag(clock_parts[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(clock_parts[i], 0, LV_PART_MAIN);
        lv_obj_add_flag(clock_parts[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_size(eye->clock_center, 12, 12);
    lv_obj_center(eye->clock_center);
    lv_obj_set_style_radius(eye->clock_center, LV_RADIUS_CIRCLE, LV_PART_MAIN);

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

    eye->content = lv_label_create(eye->screen);
    lv_obj_set_width(eye->content, 178);
    lv_obj_set_style_text_color(eye->content, rgb(s_theme->text_rgb), LV_PART_MAIN);
    lv_obj_set_style_text_font(eye->content, font_cjk(), LV_PART_MAIN);
    lv_obj_set_style_text_align(eye->content, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(eye->content, 4, LV_PART_MAIN);
    lv_label_set_long_mode(eye->content, LV_LABEL_LONG_WRAP);
    lv_obj_align(eye->content, LV_ALIGN_CENTER, 0, 0);

    eye->status_bar = lv_bar_create(eye->screen);
    lv_obj_set_width(eye->status_bar, 150);
    lv_obj_set_height(eye->status_bar, 10);
    lv_obj_align(eye->status_bar, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_bar_set_range(eye->status_bar, 0, 100);
    lv_bar_set_value(eye->status_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->amber_rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->positive_rgb), LV_PART_INDICATOR);

    eye->status_text = lv_label_create(eye->screen);
    lv_obj_set_style_text_color(eye->status_text, rgb(s_theme->muted_rgb), LV_PART_MAIN);
    lv_obj_set_style_text_font(eye->status_text, font_ui_small(), LV_PART_MAIN);
    lv_obj_set_width(eye->status_text, 180);
    lv_obj_set_style_text_align(eye->status_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(eye->status_text, LV_ALIGN_BOTTOM_MID, 0, -8);

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
    lv_obj_set_style_text_color(eye->content, rgb(s_theme->text_rgb), LV_PART_MAIN);
    if (eye->progress_arc != NULL) {
        lv_obj_set_style_arc_color(eye->progress_arc, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
        lv_obj_set_style_arc_color(eye->progress_arc, rgb(s_theme->primary_rgb), LV_PART_INDICATOR);
    }
    if (eye->clock_face != NULL) {
        lv_obj_set_style_bg_color(eye->clock_face, rgb(s_theme->panel_rgb), LV_PART_MAIN);
        lv_obj_set_style_border_color(eye->clock_face, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
    }
    for (size_t i = 0; i < ATLAS_CLOCK_TICK_COUNT; ++i) {
        if (eye->clock_ticks[i] != NULL) {
            const uint32_t color = (i % 3u) == 0u ? s_theme->text_rgb : s_theme->muted_rgb;
            lv_obj_set_style_bg_color(eye->clock_ticks[i], rgb(color), LV_PART_MAIN);
        }
    }
    if (eye->clock_hour_hand != NULL) {
        lv_obj_set_style_bg_color(eye->clock_hour_hand, rgb(s_theme->text_rgb), LV_PART_MAIN);
    }
    if (eye->clock_minute_hand != NULL) {
        lv_obj_set_style_bg_color(eye->clock_minute_hand, rgb(s_theme->text_rgb), LV_PART_MAIN);
    }
    if (eye->clock_second_hand != NULL) {
        lv_obj_set_style_bg_color(eye->clock_second_hand, rgb(s_theme->primary_rgb), LV_PART_MAIN);
    }
    if (eye->clock_center != NULL) {
        lv_obj_set_style_bg_color(eye->clock_center, rgb(s_theme->primary_rgb), LV_PART_MAIN);
    }
    lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->amber_rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->positive_rgb), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(eye->status_text, rgb(s_theme->muted_rgb), LV_PART_MAIN);
}

static void render_lvgl_eye(size_t index,
                            const atlas_eye_pose_t *pose,
                            atlas_page_t page,
                            atlas_expression_t expression,
                            uint32_t now_ms,
                            const atlas_display_payload_t *payload)
{
    atlas_lvgl_eye_t *eye = &s_eye[index];
    if (eye->screen == NULL || pose == NULL) {
        return;
    }

    apply_theme_to_eye(index);

    if (page == ATLAS_PAGE_EYES && render_eye_asset(index, expression, now_ms)) {
        return;
    }

    if (page_uses_chat_mode(page) && strcmp(display_chat_mode(payload), "eyes_only") == 0) {
        if (render_eye_asset(index, expression, now_ms)) {
            return;
        }
        page = ATLAS_PAGE_EYES;
    }

    lv_obj_add_flag(eye->asset_img, LV_OBJ_FLAG_HIDDEN);
    lv_img_set_zoom(eye->asset_img, 256);
    lv_img_set_angle(eye->asset_img, 0);
    lv_obj_center(eye->asset_img);
    set_vector_objects_hidden(eye, page == ATLAS_PAGE_CLOCK ||
                                   page == ATLAS_PAGE_STATUS ||
                                   page == ATLAS_PAGE_VOICE ||
                                   page == ATLAS_PAGE_MUSIC ||
                                   page == ATLAS_PAGE_STORY ||
                                   page == ATLAS_PAGE_CALENDAR ||
                                   page == ATLAS_PAGE_POMODORO ||
                                   page == ATLAS_PAGE_PHOTO ||
                                   page == ATLAS_PAGE_CHAT);

    if (page == ATLAS_PAGE_CLOCK || page == ATLAS_PAGE_STATUS ||
        page == ATLAS_PAGE_VOICE || page == ATLAS_PAGE_MUSIC ||
        page == ATLAS_PAGE_STORY || page == ATLAS_PAGE_CALENDAR ||
        page == ATLAS_PAGE_POMODORO || page == ATLAS_PAGE_PHOTO ||
        page == ATLAS_PAGE_CHAT) {
        set_eye_page_content(index, page, payload, now_ms);
        return;
    }

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
        lv_label_set_text(eye->caption, "");
        lv_obj_add_flag(eye->caption, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(eye->caption, atlas_page_name(page));
        lv_obj_clear_flag(eye->caption, LV_OBJ_FLAG_HIDDEN);
    }
    set_eye_page_content(index, page, payload, now_ms);
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
    lv_extra_init();
    if (mount_asset_spiffs() == ESP_OK) {
        register_lvgl_asset_fs();
    }

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

static void format_countdown_mmss(char *dst, size_t dst_size, uint32_t remaining_ms)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    const uint32_t total_seconds = remaining_ms / 1000u;
    const uint32_t mm = total_seconds / 60u;
    const uint32_t ss = total_seconds % 60u;
    snprintf(dst, dst_size, "%02u:%02u", (unsigned)mm, (unsigned)ss);
}

static void utf8_copy_chars(char *dst, size_t dst_size, const char *src, size_t max_chars)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL || max_chars == 0) {
        return;
    }

    size_t out = 0;
    size_t chars = 0;
    const unsigned char *p = (const unsigned char *)src;
    while (*p != '\0' && chars < max_chars && out + 1 < dst_size) {
        size_t len = 1;
        if ((*p & 0x80u) == 0) {
            len = 1;
        } else if ((*p & 0xE0u) == 0xC0u) {
            len = 2;
        } else if ((*p & 0xF0u) == 0xE0u) {
            len = 3;
        } else if ((*p & 0xF8u) == 0xF0u) {
            len = 4;
        }
        if (out + len >= dst_size) {
            break;
        }
        for (size_t i = 0; i < len && p[i] != '\0'; ++i) {
            dst[out++] = (char)p[i];
        }
        p += len;
        ++chars;
    }
    dst[out] = '\0';
}

static bool format_clock_face(uint32_t now_ms,
                              char *clock,
                              size_t clock_size,
                              char *date,
                              size_t date_size,
                              char *status,
                              size_t status_size,
                              uint8_t *progress_percent)
{
    if (clock == NULL || date == NULL || status == NULL || progress_percent == NULL) {
        return false;
    }

    time_t unix_now = time(NULL);
    struct tm tm_now;
    if (unix_now > 1700000000 && localtime_r(&unix_now, &tm_now) != NULL) {
        snprintf(clock, clock_size, "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
        snprintf(date,
                 date_size,
                 "%04d-%02d-%02d",
                 tm_now.tm_year + 1900,
                 tm_now.tm_mon + 1,
                 tm_now.tm_mday);
        snprintf(status, status_size, "秒 %02d", tm_now.tm_sec);
        const uint32_t day_seconds = (uint32_t)tm_now.tm_hour * 3600u +
                                     (uint32_t)tm_now.tm_min * 60u +
                                     (uint32_t)tm_now.tm_sec;
        *progress_percent = (uint8_t)((day_seconds * 100u) / (24u * 3600u));
        return true;
    }

    const uint32_t total_seconds = now_ms / 1000u;
    const uint32_t hh = (total_seconds / 3600u) % 24u;
    const uint32_t mm = (total_seconds / 60u) % 60u;
    const uint32_t ss = total_seconds % 60u;
    snprintf(clock, clock_size, "%02u:%02u", (unsigned)hh, (unsigned)mm);
    snprintf(date,
             date_size,
             "运行 %uh %02um",
             (unsigned)(total_seconds / 3600u),
             (unsigned)((total_seconds / 60u) % 60u));
    snprintf(status, status_size, "待校时  %02u", (unsigned)ss);
    *progress_percent = (uint8_t)(((total_seconds % 3600u) * 100u) / 3600u);
    return false;
}

static const char *weekday_zh(int weekday)
{
    static const char *const names[] = {
        "星期日",
        "星期一",
        "星期二",
        "星期三",
        "星期四",
        "星期五",
        "星期六",
    };
    if (weekday < 0 || weekday > 6) {
        return "星期?";
    }
    return names[weekday];
}

static bool format_calendar_today(uint32_t now_ms,
                                  char *month_day,
                                  size_t month_day_size,
                                  char *weekday,
                                  size_t weekday_size,
                                  char *hint,
                                  size_t hint_size)
{
    if (month_day == NULL || weekday == NULL || hint == NULL) {
        return false;
    }

    time_t unix_now = time(NULL);
    struct tm tm_now;
    if (unix_now > 1700000000 && localtime_r(&unix_now, &tm_now) != NULL) {
        snprintf(month_day, month_day_size, "%02d/%02d", tm_now.tm_mon + 1, tm_now.tm_mday);
        strlcpy(weekday, weekday_zh(tm_now.tm_wday), weekday_size);
        snprintf(hint, hint_size, "%04d", tm_now.tm_year + 1900);
        return true;
    }

    const uint32_t total_seconds = now_ms / 1000u;
    snprintf(month_day, month_day_size, "--/--");
    strlcpy(weekday, "待校时", weekday_size);
    snprintf(hint, hint_size, "运行%uh", (unsigned)(total_seconds / 3600u));
    return false;
}

static uint8_t pulse_percent(uint32_t now_ms, uint16_t period_ms, uint8_t low, uint8_t high)
{
    if (period_ms == 0 || high <= low) {
        return low;
    }

    const uint32_t phase = now_ms % period_ms;
    const uint32_t half = period_ms / 2u;
    const uint32_t pos = phase <= half ? phase : period_ms - phase;
    return (uint8_t)(low + ((uint32_t)(high - low) * pos) / (half == 0 ? 1u : half));
}

static uint8_t lively_progress(uint8_t progress, uint32_t now_ms, bool active)
{
    if (progress > 100) {
        return 100;
    }
    if (progress > 0) {
        return progress;
    }
    return active ? pulse_percent(now_ms, 1800, 5, 13) : 3;
}

static uint8_t day_progress_percent(uint32_t now_ms)
{
    time_t unix_now = time(NULL);
    struct tm tm_now;
    if (unix_now > 1700000000 && localtime_r(&unix_now, &tm_now) != NULL) {
        const uint32_t day_seconds = (uint32_t)tm_now.tm_hour * 3600u +
                                     (uint32_t)tm_now.tm_min * 60u +
                                     (uint32_t)tm_now.tm_sec;
        return (uint8_t)((day_seconds * 100u) / (24u * 3600u));
    }

    const uint32_t total_seconds = now_ms / 1000u;
    return (uint8_t)(((total_seconds % (24u * 3600u)) * 100u) / (24u * 3600u));
}

static const char *pomodoro_phase_label(bool running, bool in_break)
{
    if (!running) {
        return "待开始";
    }
    return in_break ? "休息中" : "专注中";
}

static const char *pomodoro_phase_short(bool running, bool in_break)
{
    if (!running) {
        return "READY";
    }
    return in_break ? "BREAK" : "FOCUS";
}

static uint32_t pomodoro_accent_rgb(const atlas_display_payload_t *payload)
{
    if (payload != NULL && payload->pomodoro_in_break) {
        return s_theme->positive_rgb;
    }
    return s_theme->primary_rgb;
}

static void clock_components(uint32_t now_ms, uint8_t *hour, uint8_t *minute, uint8_t *second)
{
    if (hour == NULL || minute == NULL || second == NULL) {
        return;
    }

    time_t unix_now = time(NULL);
    struct tm tm_now;
    if (unix_now > 1700000000 && localtime_r(&unix_now, &tm_now) != NULL) {
        *hour = (uint8_t)tm_now.tm_hour;
        *minute = (uint8_t)tm_now.tm_min;
        *second = (uint8_t)tm_now.tm_sec;
        return;
    }

    const uint32_t total_seconds = now_ms / 1000u;
    *hour = (uint8_t)((total_seconds / 3600u) % 24u);
    *minute = (uint8_t)((total_seconds / 60u) % 60u);
    *second = (uint8_t)(total_seconds % 60u);
}

static void place_clock_hand(lv_obj_t *hand, int width, int length, int angle_tenths)
{
    if (hand == NULL) {
        return;
    }
    lv_obj_set_size(hand, width, length);
    lv_obj_set_pos(hand, (ATLAS_LCD_WIDTH - width) / 2, ATLAS_LCD_HEIGHT / 2 - length);
    lv_obj_set_style_radius(hand, width, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(hand, width / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(hand, length, LV_PART_MAIN);
    lv_obj_set_style_transform_angle(hand, angle_tenths, LV_PART_MAIN);
    lv_obj_clear_flag(hand, LV_OBJ_FLAG_HIDDEN);
}

static void render_quartz_clock(size_t index, uint32_t now_ms, uint8_t progress)
{
    atlas_lvgl_eye_t *eye = &s_eye[index];
    set_clock_dial_hidden(eye, false);

    if (eye->progress_arc != NULL) {
        lv_obj_set_size(eye->progress_arc, 214, 214);
        lv_arc_set_value(eye->progress_arc, progress);
        lv_obj_set_style_arc_width(eye->progress_arc, 7, LV_PART_MAIN);
        lv_obj_set_style_arc_width(eye->progress_arc, 10, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(eye->progress_arc, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
        lv_obj_set_style_arc_color(eye->progress_arc, rgb(s_theme->primary_rgb), LV_PART_INDICATOR);
        lv_obj_clear_flag(eye->progress_arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(eye->progress_arc);
    }
    if (eye->clock_face != NULL) {
        lv_obj_move_foreground(eye->clock_face);
    }

    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    clock_components(now_ms, &hour, &minute, &second);

    const int hour_angle = (int)(hour % 12u) * 300 + (int)minute * 5;
    const int minute_angle = (int)minute * 60 + (int)second;
    const int second_angle = (int)second * 60;

    for (size_t i = 0; i < ATLAS_CLOCK_TICK_COUNT; ++i) {
        if (eye->clock_ticks[i] != NULL) {
            lv_obj_move_foreground(eye->clock_ticks[i]);
        }
    }
    place_clock_hand(eye->clock_hour_hand, 7, 48, hour_angle);
    place_clock_hand(eye->clock_minute_hand, 5, 68, minute_angle);
    place_clock_hand(eye->clock_second_hand, 2, 78, second_angle);
    if (eye->clock_center != NULL) {
        lv_obj_clear_flag(eye->clock_center, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(eye->clock_center);
    }
    lv_obj_move_foreground(eye->clock_hour_hand);
    lv_obj_move_foreground(eye->clock_minute_hand);
    lv_obj_move_foreground(eye->clock_second_hand);
}

static const char *status_mood_caption(bool is_error, bool is_warn)
{
    if (is_error) {
        return "我卡住了";
    }
    if (is_warn) {
        return "需要确认";
    }
    return "状态正常";
}

static void render_status_vector_face(size_t index,
                                      uint32_t accent,
                                      bool is_error,
                                      bool is_warn,
                                      uint32_t now_ms)
{
    atlas_lvgl_eye_t *eye = &s_eye[index];
    if (eye == NULL || eye->iris == NULL || eye->pupil == NULL) {
        return;
    }

    const int16_t drift = triangle_wave(now_ms + (uint32_t)index * 173u,
                                        is_error ? 900u : 1800u,
                                        is_error ? 5 : 2);
    const uint16_t iris_size = is_error ? 118 : (is_warn ? 124 : 132);
    const uint16_t pupil_size = is_error ? 22 : (is_warn ? 28 : 34);

    set_vector_objects_hidden(eye, false);
    lv_obj_add_flag(eye->caption, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(eye->slit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(eye->iris, iris_size, iris_size);
    lv_obj_align(eye->iris, LV_ALIGN_CENTER, drift, -18);
    lv_obj_set_style_border_color(eye->iris, rgb(accent), LV_PART_MAIN);
    lv_obj_set_style_bg_color(eye->iris, rgb(accent), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(eye->iris, is_error ? LV_OPA_20 : LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_size(eye->pupil, pupil_size, pupil_size);
    lv_obj_align(eye->pupil, LV_ALIGN_CENTER, 0, is_error ? 8 : 0);
    lv_obj_set_style_bg_color(eye->pupil, rgb(accent), LV_PART_MAIN);
    lv_obj_set_height(eye->top_lid, is_error ? 54 : (is_warn ? 24 : 0));
    lv_obj_set_height(eye->bottom_lid, is_error ? 24 : 0);
    lv_obj_move_foreground(eye->top_lid);
    lv_obj_move_foreground(eye->bottom_lid);
}

static void split_month_day(const char *month_day, char *month, size_t month_size, char *day, size_t day_size)
{
    if (month == NULL || day == NULL || month_size == 0 || day_size == 0) {
        return;
    }
    month[0] = '\0';
    day[0] = '\0';
    if (month_day == NULL || month_day[0] == '\0') {
        strlcpy(month, "--", month_size);
        strlcpy(day, "--", day_size);
        return;
    }
    const char *slash = strchr(month_day, '/');
    if (slash == NULL) {
        strlcpy(month, "--", month_size);
        strlcpy(day, month_day, day_size);
        return;
    }
    const size_t month_len = (size_t)(slash - month_day);
    const size_t copy_len = month_len < month_size - 1 ? month_len : month_size - 1;
    memcpy(month, month_day, copy_len);
    month[copy_len] = '\0';
    strlcpy(day, slash + 1, day_size);
}

static void set_eye_page_content(size_t index,
                                atlas_page_t page,
                                const atlas_display_payload_t *payload,
                                uint32_t now_ms)
{
    atlas_lvgl_eye_t *eye = &s_eye[index];
    if (eye->screen == NULL || payload == NULL) {
        return;
    }

    const bool is_left = (index == 0);
    char line1[256] = "";
    char bar_text[128] = "";

    lv_label_set_text(eye->status_text, "");
    lv_obj_add_flag(eye->status_bar, LV_OBJ_FLAG_HIDDEN);
    if (eye->progress_arc != NULL) {
        lv_obj_add_flag(eye->progress_arc, LV_OBJ_FLAG_HIDDEN);
    }
    set_clock_dial_hidden(eye, true);
    lv_obj_clear_flag(eye->content, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(eye->content, 178);
    lv_obj_set_width(eye->status_text, 178);
    lv_obj_set_style_text_font(eye->content, font_cjk(), LV_PART_MAIN);
    lv_obj_set_style_text_font(eye->status_text, font_ui_small(), LV_PART_MAIN);
    lv_obj_set_style_text_color(eye->content, rgb(s_theme->text_rgb), LV_PART_MAIN);
    lv_obj_set_style_text_color(eye->status_text, rgb(s_theme->muted_rgb), LV_PART_MAIN);
    lv_obj_set_style_text_align(eye->content, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(eye->status_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(eye->content, 4, LV_PART_MAIN);
    lv_label_set_long_mode(eye->content, LV_LABEL_LONG_WRAP);
    lv_obj_align(eye->content, LV_ALIGN_CENTER, 0, 0);
    lv_obj_align(eye->status_text, LV_ALIGN_BOTTOM_MID, 0, -10);

    if (page_uses_chat_mode(page) && strcmp(display_chat_mode(payload), "pet_head") == 0) {
        char short_text[104];
        char title[32];
        const bool story = page == ATLAS_PAGE_STORY;
        const bool music = page == ATLAS_PAGE_MUSIC;
        const bool voice = page == ATLAS_PAGE_VOICE;
        const char *raw_text = payload->chat_text[0] == '\0' ? "" : payload->chat_text;
        if (voice) {
            strlcpy(title, "我在听", sizeof(title));
            raw_text = payload->scene_hint[0] == '\0' ? "说完自动识别" : payload->scene_hint;
        } else if (music) {
            strlcpy(title, "音乐", sizeof(title));
            raw_text = "准备播放";
        } else if (story) {
            strlcpy(title, "故事", sizeof(title));
            raw_text = payload->chat_text[0] == '\0' ? "准备讲故事" : payload->chat_text;
        } else {
            strlcpy(title, "对话", sizeof(title));
            raw_text = payload->chat_text[0] == '\0' ? "等你说话" : payload->chat_text;
        }
        utf8_copy_chars(short_text, sizeof(short_text), raw_text, is_left ? 8 : 28);
        if (is_left) {
            const atlas_expression_t pet_expression =
                voice ? ATLAS_EXPR_LISTEN :
                (payload->chat_text[0] != '\0' ? ATLAS_EXPR_SPEAKING : ATLAS_EXPR_IDLE);
            const bool used_head = show_pet_head_for_page(index, page, pet_expression, now_ms, 250, 0, 0);
            if (used_head) {
                lv_obj_add_flag(eye->content, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_width(eye->status_text, 154);
                lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
                lv_obj_set_style_text_font(eye->status_text, font_ui_small(), LV_PART_MAIN);
                lv_obj_set_style_text_color(eye->status_text, rgb(s_theme->primary_rgb), LV_PART_MAIN);
                lv_label_set_text(eye->status_text, title);
                lv_obj_align(eye->status_text, LV_ALIGN_BOTTOM_MID, 0, -18);
                lv_obj_move_foreground(eye->status_text);
                return;
            }
        }

        lv_obj_set_width(eye->content, is_left ? 150 : 166);
        lv_obj_set_style_text_font(eye->content, font_cjk(), LV_PART_MAIN);
        lv_obj_set_style_text_color(eye->content, rgb(is_left ? s_theme->primary_rgb : s_theme->text_rgb), LV_PART_MAIN);
        lv_obj_set_style_text_line_space(eye->content, 5, LV_PART_MAIN);
        lv_label_set_long_mode(eye->content, LV_LABEL_LONG_WRAP);
        snprintf(line1, sizeof(line1), "%s\n%s", is_left ? "宠物头" : title, short_text);
        lv_label_set_text(eye->content, line1);
        lv_obj_align(eye->content, LV_ALIGN_CENTER, 0, -20);
        lv_obj_set_width(eye->status_text, 160);
        lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(eye->status_text, rgb(s_theme->muted_rgb), LV_PART_MAIN);
        lv_label_set_text(eye->status_text, is_left ? "pet_head" : display_chat_mode(payload));
        lv_obj_align(eye->status_text, LV_ALIGN_BOTTOM_MID, 0, -24);
        return;
    }

    if (page == ATLAS_PAGE_STATUS) {
        char title_short[40];
        char subtitle_short[64];
        char hint_short[84];
        const bool is_error = strcmp(payload->scene_severity, "error") == 0;
        const bool is_warn = strcmp(payload->scene_severity, "warn") == 0 || payload->scene_needs_attention;
        const uint32_t accent = is_error ? s_theme->danger_rgb : (is_warn ? s_theme->amber_rgb : s_theme->primary_rgb);
        utf8_copy_chars(title_short,
                        sizeof(title_short),
                        payload->scene_title[0] == '\0' ? "状态" : payload->scene_title,
                        is_left ? 5 : 8);
        utf8_copy_chars(subtitle_short,
                        sizeof(subtitle_short),
                        payload->scene_subtitle[0] == '\0' ? "系统待机" : payload->scene_subtitle,
                        12);
        utf8_copy_chars(hint_short,
                        sizeof(hint_short),
                        payload->scene_hint[0] == '\0' ? "查看管理端" : payload->scene_hint,
                        14);
        lv_obj_set_style_text_color(eye->content, rgb(accent), LV_PART_MAIN);
        lv_obj_set_style_text_line_space(eye->content, 5, LV_PART_MAIN);
        lv_label_set_long_mode(eye->content, LV_LABEL_LONG_WRAP);
        if (is_left) {
            const char *pet_state = is_error ? "think" : (is_warn ? "listen" : "idle");
            const bool used_pet = show_pet_head_keyframe(index, pet_state, is_error ? 238 : 244, 0, 0);
            if (!used_pet) {
                render_status_vector_face(index, accent, is_error, is_warn, now_ms);
            }
            lv_obj_set_width(eye->content, 150);
            lv_obj_set_style_text_font(eye->content, font_cjk(), LV_PART_MAIN);
            lv_obj_set_style_text_color(eye->content, rgb(accent), LV_PART_MAIN);
            lv_label_set_long_mode(eye->content, LV_LABEL_LONG_CLIP);
            snprintf(line1, sizeof(line1), "%s", title_short);
            lv_label_set_text(eye->content, line1);
            lv_obj_align(eye->content, LV_ALIGN_BOTTOM_MID, 0, -44);
            lv_label_set_text(eye->status_text, status_mood_caption(is_error, is_warn));
            lv_obj_set_style_text_color(eye->status_text, rgb(is_error ? s_theme->rose_rgb : s_theme->muted_rgb), LV_PART_MAIN);
            lv_obj_set_width(eye->status_text, 148);
            lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
            lv_obj_align(eye->status_text, LV_ALIGN_BOTTOM_MID, 0, -24);
            lv_obj_move_foreground(eye->content);
            lv_obj_move_foreground(eye->status_text);
        } else {
            lv_obj_set_width(eye->content, 166);
            lv_obj_set_style_text_font(eye->content, font_cjk(), LV_PART_MAIN);
            lv_obj_set_style_text_color(eye->content, rgb(s_theme->text_rgb), LV_PART_MAIN);
            snprintf(line1, sizeof(line1), "诊断\n%s\n%s", subtitle_short, hint_short);
            lv_label_set_text(eye->content, line1);
            lv_obj_align(eye->content, LV_ALIGN_CENTER, 0, -30);
            snprintf(bar_text,
                     sizeof(bar_text),
                     "%s · %s",
                     payload->scene_state[0] == '\0' ? "idle" : payload->scene_state,
                     is_error ? "ERROR" : (is_warn ? "WARN" : "OK"));
            lv_obj_set_width(eye->status_text, 172);
            lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
            lv_label_set_text(eye->status_text, bar_text);
            lv_obj_align(eye->status_text, LV_ALIGN_BOTTOM_MID, 0, -24);
        }
        lv_obj_set_width(eye->status_bar, 132);
        lv_obj_set_height(eye->status_bar, 8);
        lv_bar_set_value(eye->status_bar, is_error ? 100 : (is_warn ? 68 : 42), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
        lv_obj_set_style_bg_color(eye->status_bar, rgb(accent), LV_PART_INDICATOR);
        lv_obj_clear_flag(eye->status_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(eye->status_bar, LV_ALIGN_BOTTOM_MID, 0, -12);
    } else if (page == ATLAS_PAGE_VOICE) {
        char title_short[40];
        char subtitle_short[64];
        char hint_short[84];
        utf8_copy_chars(title_short,
                        sizeof(title_short),
                        payload->scene_title[0] == '\0' ? "我在听" : payload->scene_title,
                        5);
        utf8_copy_chars(subtitle_short,
                        sizeof(subtitle_short),
                        payload->scene_subtitle[0] == '\0' ? "等待语音" : payload->scene_subtitle,
                        9);
        utf8_copy_chars(hint_short,
                        sizeof(hint_short),
                        payload->scene_hint[0] == '\0' ? "说完自动识别" : payload->scene_hint,
                        13);
        lv_obj_set_width(eye->content, is_left ? 150 : 168);
        lv_obj_set_style_text_line_space(eye->content, 5, LV_PART_MAIN);
        lv_label_set_long_mode(eye->content, LV_LABEL_LONG_WRAP);
        if (is_left) {
            lv_obj_set_style_text_color(eye->content, rgb(s_theme->primary_rgb), LV_PART_MAIN);
            snprintf(line1,
                     sizeof(line1),
                     "%s\n%s",
                     title_short,
                     payload->scene_left_role[0] == '\0' ? "麦克风" : payload->scene_left_role);
            lv_label_set_text(eye->content, line1);
            lv_obj_align(eye->content, LV_ALIGN_CENTER, 0, -22);
            lv_label_set_text(eye->status_text, subtitle_short);
        } else {
            lv_obj_set_style_text_color(eye->content, rgb(s_theme->text_rgb), LV_PART_MAIN);
            snprintf(line1, sizeof(line1), "%s\n%s", subtitle_short, hint_short);
            lv_label_set_text(eye->content, line1);
            lv_obj_align(eye->content, LV_ALIGN_CENTER, 0, -24);
            lv_label_set_text(eye->status_text,
                              payload->scene_right_role[0] == '\0' ? "麦克风" : payload->scene_right_role);
        }
        lv_obj_set_width(eye->status_text, 158);
        lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
        lv_obj_align(eye->status_text, LV_ALIGN_BOTTOM_MID, 0, -27);
        lv_obj_set_width(eye->status_bar, 136);
        lv_obj_set_height(eye->status_bar, 10);
        lv_bar_set_value(eye->status_bar, is_left ? 56 : 34, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
        lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->primary_rgb), LV_PART_INDICATOR);
        lv_obj_clear_flag(eye->status_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(eye->status_bar, LV_ALIGN_BOTTOM_MID, 0, -13);
    } else if (page == ATLAS_PAGE_MUSIC || page == ATLAS_PAGE_STORY) {
        const bool story = page == ATLAS_PAGE_STORY;
        lv_obj_set_width(eye->content, 168);
        lv_obj_set_style_text_color(eye->content, rgb(story ? s_theme->amber_rgb : s_theme->primary_rgb), LV_PART_MAIN);
        lv_obj_set_style_text_line_space(eye->content, 5, LV_PART_MAIN);
        if (is_left) {
            lv_label_set_text(eye->content, story ? "讲故事\n准备中" : "音乐\n准备中");
            lv_obj_align(eye->content, LV_ALIGN_CENTER, 0, -20);
            lv_label_set_text(eye->status_text, story ? "Atlas Brain 故事技能" : "Atlas Brain 音乐技能");
        } else {
            lv_label_set_text(eye->content, story ? "等待\n故事内容" : "等待\n播放内容");
            lv_obj_align(eye->content, LV_ALIGN_CENTER, 0, -20);
            lv_label_set_text(eye->status_text, payload->pet_ip_valid ? payload->pet_ip : "Mac Brain");
        }
        lv_obj_set_width(eye->status_text, 160);
        lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
        lv_obj_align(eye->status_text, LV_ALIGN_BOTTOM_MID, 0, -24);
        lv_obj_set_width(eye->status_bar, 128);
        lv_obj_set_height(eye->status_bar, 8);
        lv_bar_set_value(eye->status_bar, story ? 45 : 60, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
        lv_obj_set_style_bg_color(eye->status_bar, rgb(story ? s_theme->amber_rgb : s_theme->primary_rgb), LV_PART_INDICATOR);
        lv_obj_clear_flag(eye->status_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(eye->status_bar, LV_ALIGN_BOTTOM_MID, 0, -12);
    } else if (page == ATLAS_PAGE_CLOCK) {
        char clock[16] = "";
        char date[40] = "";
        char status[32] = "";
        char clock_note[56] = "";
        uint8_t progress = 0;
        const bool wall_time_valid = format_clock_face(now_ms,
                                                       clock,
                                                       sizeof(clock),
                                                       date,
                                                       sizeof(date),
                                                       status,
                                                       sizeof(status),
                                                       &progress);
        const uint8_t live_progress = lively_progress(progress, now_ms, wall_time_valid);
        if (is_left) {
            snprintf(clock_note,
                     sizeof(clock_note),
                     "%s · %u%%",
                     wall_time_valid ? "桌面时钟" : "待校时",
                     (unsigned)live_progress);
            if (show_pet_head_keyframe(index, wall_time_valid ? "idle" : "think", 248, 0, 0)) {
                lv_obj_set_width(eye->content, 156);
                lv_obj_set_style_text_font(eye->content, &lv_font_montserrat_28, LV_PART_MAIN);
                lv_label_set_long_mode(eye->content, LV_LABEL_LONG_CLIP);
                lv_obj_set_style_text_color(eye->content, rgb(s_theme->text_rgb), LV_PART_MAIN);
                lv_label_set_text(eye->content, clock);
                lv_obj_align(eye->content, LV_ALIGN_BOTTOM_MID, 0, -42);
                lv_obj_set_width(eye->status_text, 158);
                lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
                lv_obj_set_style_text_font(eye->status_text, font_ui_small(), LV_PART_MAIN);
                lv_obj_set_style_text_color(eye->status_text,
                                            rgb(wall_time_valid ? s_theme->primary_rgb : s_theme->amber_rgb),
                                            LV_PART_MAIN);
                lv_label_set_text(eye->status_text, clock_note);
                lv_obj_align(eye->status_text, LV_ALIGN_TOP_MID, 0, 20);
                lv_obj_set_width(eye->status_bar, 126);
                lv_obj_set_height(eye->status_bar, 8);
                lv_bar_set_value(eye->status_bar, live_progress, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
                lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->primary_rgb), LV_PART_INDICATOR);
                lv_obj_clear_flag(eye->status_bar, LV_OBJ_FLAG_HIDDEN);
                lv_obj_align(eye->status_bar, LV_ALIGN_BOTTOM_MID, 0, -26);
                lv_obj_move_foreground(eye->content);
                lv_obj_move_foreground(eye->status_text);
                lv_obj_move_foreground(eye->status_bar);
                return;
            }

            lv_obj_set_width(eye->content, 218);
            lv_obj_set_style_text_font(eye->content, font_countdown(), LV_PART_MAIN);
            lv_obj_set_style_text_color(eye->content, rgb(s_theme->text_rgb), LV_PART_MAIN);
            lv_obj_set_style_text_line_space(eye->content, 0, LV_PART_MAIN);
            lv_label_set_long_mode(eye->content, LV_LABEL_LONG_CLIP);
            lv_label_set_text(eye->content, clock);
            lv_obj_align(eye->content, LV_ALIGN_CENTER, 0, -12);
            if (eye->progress_arc != NULL) {
                lv_obj_set_size(eye->progress_arc, 218, 218);
                lv_arc_set_value(eye->progress_arc, live_progress);
                lv_obj_set_style_arc_width(eye->progress_arc, 8, LV_PART_MAIN);
                lv_obj_set_style_arc_width(eye->progress_arc, 12, LV_PART_INDICATOR);
                lv_obj_set_style_arc_color(eye->progress_arc, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
                lv_obj_set_style_arc_color(eye->progress_arc, rgb(s_theme->primary_rgb), LV_PART_INDICATOR);
                lv_obj_clear_flag(eye->progress_arc, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_background(eye->progress_arc);
            }
            lv_obj_set_width(eye->status_text, 156);
            lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_color(eye->status_text, rgb(s_theme->muted_rgb), LV_PART_MAIN);
            lv_label_set_text(eye->status_text, date);
            lv_obj_align(eye->status_text, LV_ALIGN_CENTER, 0, 48);
        } else {
            lv_obj_add_flag(eye->content, LV_OBJ_FLAG_HIDDEN);
            render_quartz_clock(index, now_ms, live_progress);
            lv_obj_set_width(eye->status_text, 150);
            lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_color(eye->status_text,
                                        rgb(wall_time_valid ? s_theme->muted_rgb : s_theme->amber_rgb),
                                        LV_PART_MAIN);
            lv_obj_set_style_text_font(eye->status_text, font_ui_small(), LV_PART_MAIN);
            snprintf(clock_note,
                     sizeof(clock_note),
                     "%s  %s",
                     wall_time_valid ? "石英表" : "待校时",
                     status);
            lv_label_set_text(eye->status_text, clock_note);
            lv_obj_align(eye->status_text, LV_ALIGN_BOTTOM_MID, 0, -14);
            lv_obj_move_foreground(eye->status_text);
        }
    } else if (page == ATLAS_PAGE_CHAT) {
        const char *chat_text = payload->chat_text[0] == '\0' ? "暂无对话内容" : payload->chat_text;
        snprintf(line1, sizeof(line1), "对话\n%s", chat_text);
        lv_label_set_text(eye->content, line1);
        if (payload->pet_ip_valid && payload->pet_ip[0] != '\0') {
            snprintf(bar_text, sizeof(bar_text), "IP:%s", payload->pet_ip);
            lv_label_set_text(eye->status_text, bar_text);
        }
    } else if (page == ATLAS_PAGE_CALENDAR) {
        const char *title = payload->calendar_title[0] == '\0' ? "电子宠物日历" : payload->calendar_title;
        const char *note = payload->calendar_note[0] == '\0' ? "今日状态：待命" : payload->calendar_note;
        char title_short[40];
        char note_short[96];
        char month_day[16];
        char weekday[24];
        char calendar_hint[24];
        char calendar_badge[80];
        char month[8];
        char day[8];
        const bool calendar_synced = format_calendar_today(now_ms,
                                                           month_day,
                                                           sizeof(month_day),
                                                           weekday,
                                                           sizeof(weekday),
                                                           calendar_hint,
                                                           sizeof(calendar_hint));
        const uint8_t day_progress = day_progress_percent(now_ms);
        const uint8_t note_progress = calendar_synced ? (uint8_t)(28u + (day_progress / 2u)) :
                                                        pulse_percent(now_ms, 2200, 18, 60);
        split_month_day(month_day, month, sizeof(month), day, sizeof(day));
        utf8_copy_chars(title_short, sizeof(title_short), title, 8);
        utf8_copy_chars(note_short, sizeof(note_short), note, 16);
        lv_obj_set_style_text_font(eye->content, font_cjk(), LV_PART_MAIN);
        lv_obj_set_style_text_font(eye->status_text, font_ui_small(), LV_PART_MAIN);
        lv_obj_set_style_text_line_space(eye->content, 5, LV_PART_MAIN);
        lv_label_set_long_mode(eye->content, LV_LABEL_LONG_WRAP);
        if (is_left && show_pet_head_keyframe(index, "idle", 246, 0, 0)) {
            lv_obj_set_width(eye->content, is_left ? 152 : 160);
            if (is_left) {
                lv_obj_set_style_text_color(eye->content, rgb(s_theme->amber_rgb), LV_PART_MAIN);
                lv_obj_set_style_text_font(eye->content, &lv_font_montserrat_36, LV_PART_MAIN);
                snprintf(line1, sizeof(line1), "%s", day);
                lv_label_set_text(eye->content, line1);
                lv_obj_align(eye->content, LV_ALIGN_CENTER, 0, 34);
                lv_obj_set_width(eye->status_text, 150);
                lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
                lv_obj_set_style_text_color(eye->status_text, rgb(s_theme->muted_rgb), LV_PART_MAIN);
                snprintf(calendar_badge,
                         sizeof(calendar_badge),
                         "%s月 · %s",
                         month,
                         calendar_synced ? weekday : "待校时");
                lv_label_set_text(eye->status_text, calendar_badge);
                lv_obj_align(eye->status_text, LV_ALIGN_TOP_MID, 0, 18);
            } else {
                lv_obj_set_style_text_color(eye->content, rgb(s_theme->text_rgb), LV_PART_MAIN);
                lv_obj_set_style_text_font(eye->content, font_cjk(), LV_PART_MAIN);
                snprintf(line1, sizeof(line1), "今日安排\n%s", note_short);
                lv_label_set_text(eye->content, line1);
                lv_obj_align(eye->content, LV_ALIGN_BOTTOM_MID, 0, -42);
                snprintf(calendar_badge,
                         sizeof(calendar_badge),
                         "%s  %s",
                         calendar_hint,
                         title_short);
                lv_label_set_text(eye->status_text, calendar_badge);
                lv_obj_set_width(eye->status_text, 150);
                lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
                lv_obj_set_style_text_color(eye->status_text, rgb(s_theme->muted_rgb), LV_PART_MAIN);
                lv_obj_align(eye->status_text, LV_ALIGN_TOP_MID, 0, 18);
            }
            lv_obj_set_width(eye->status_bar, 124);
            lv_obj_set_height(eye->status_bar, 8);
            lv_bar_set_value(eye->status_bar, is_left ? day_progress : note_progress, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
            lv_obj_set_style_bg_color(eye->status_bar,
                                      rgb(is_left ? s_theme->amber_rgb : s_theme->positive_rgb),
                                      LV_PART_INDICATOR);
            lv_obj_clear_flag(eye->status_bar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(eye->status_bar, LV_ALIGN_BOTTOM_MID, 0, -24);
            lv_obj_move_foreground(eye->content);
            lv_obj_move_foreground(eye->status_text);
            lv_obj_move_foreground(eye->status_bar);
            return;
        }

        lv_obj_set_width(eye->content, 166);
        if (is_left) {
            lv_obj_set_style_text_font(eye->content, &lv_font_montserrat_36, LV_PART_MAIN);
            snprintf(line1, sizeof(line1), "%s", day);
            lv_label_set_text(eye->content, line1);
            lv_obj_set_style_text_color(eye->content, rgb(s_theme->amber_rgb), LV_PART_MAIN);
            snprintf(calendar_badge,
                     sizeof(calendar_badge),
                     "%s月 · %s",
                     month,
                     calendar_synced ? weekday : "待校时");
            lv_label_set_text(eye->status_text, calendar_badge);
        } else {
            snprintf(line1, sizeof(line1), "今日安排\n%s", note_short);
            lv_label_set_text(eye->content, line1);
            snprintf(calendar_badge,
                     sizeof(calendar_badge),
                     "%s  %s",
                     calendar_hint,
                     title_short);
            lv_label_set_text(eye->status_text, calendar_badge);
        }
        lv_obj_align(eye->content, LV_ALIGN_CENTER, 0, -18);
        lv_obj_set_width(eye->status_text, 158);
        lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
        lv_obj_align(eye->status_text, LV_ALIGN_BOTTOM_MID, 0, -28);
        lv_obj_set_width(eye->status_bar, 132);
        lv_obj_set_height(eye->status_bar, 8);
        lv_bar_set_value(eye->status_bar, is_left ? day_progress : note_progress, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
        lv_obj_set_style_bg_color(eye->status_bar,
                                  rgb(is_left ? s_theme->amber_rgb : s_theme->positive_rgb),
                                  LV_PART_INDICATOR);
        lv_obj_clear_flag(eye->status_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(eye->status_bar, LV_ALIGN_BOTTOM_MID, 0, -12);
    } else if (page == ATLAS_PAGE_POMODORO) {
        char remain[12];
        char task_short[40];
        char phase_badge[56];
        uint32_t remaining_ms = payload->pomodoro_remaining_ms;
        const uint16_t focus_minutes = payload->pomodoro_focus_minutes == 0 ? 25u : payload->pomodoro_focus_minutes;
        const uint16_t break_minutes = payload->pomodoro_break_minutes == 0 ? 5u : payload->pomodoro_break_minutes;
        if (!payload->pomodoro_running) {
            remaining_ms = (uint32_t)focus_minutes * 60u * 1000u;
        }
        format_countdown_mmss(remain, sizeof(remain), remaining_ms);
        const uint8_t progress_raw = payload->pomodoro_progress_percent > 100 ? 100 : payload->pomodoro_progress_percent;
        const uint8_t progress = lively_progress(progress_raw, now_ms, payload->pomodoro_running);
        const char *phase = pomodoro_phase_label(payload->pomodoro_running, payload->pomodoro_in_break);
        const uint32_t accent = pomodoro_accent_rgb(payload);
        const uint32_t total_ms = ((uint32_t)(payload->pomodoro_in_break ? break_minutes : focus_minutes) * 60u * 1000u);
        const uint32_t interval_ms = (total_ms > 0u) ? total_ms : 1u;
        const uint32_t used_ms = remaining_ms >= interval_ms ? 0u : (interval_ms - remaining_ms);
        const uint32_t used_minutes = used_ms / 60000u;
        const uint32_t total_minutes = interval_ms / 60000u;
        utf8_copy_chars(task_short,
                        sizeof(task_short),
                        payload->pomodoro_task_name[0] == '\0' ? "巡检任务" : payload->pomodoro_task_name,
                        7);
        if (is_left) {
            if (show_pet_head_keyframe(index,
                                       payload->pomodoro_running ? "listen" : "idle",
                                       248,
                                       0,
                                       0)) {
                lv_obj_set_width(eye->content, 156);
                lv_obj_set_style_text_font(eye->content, &lv_font_montserrat_28, LV_PART_MAIN);
                lv_label_set_long_mode(eye->content, LV_LABEL_LONG_CLIP);
                lv_obj_set_style_text_color(eye->content, rgb(s_theme->text_rgb), LV_PART_MAIN);
                lv_label_set_text(eye->content, remain);
                lv_obj_align(eye->content, LV_ALIGN_BOTTOM_MID, 0, -42);
                snprintf(phase_badge,
                         sizeof(phase_badge),
                         "%s · %u%%",
                         pomodoro_phase_short(payload->pomodoro_running, payload->pomodoro_in_break),
                         (unsigned)progress_raw);
                lv_obj_set_width(eye->status_text, 144);
                lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
                lv_obj_set_style_text_font(eye->status_text, font_ui_small(), LV_PART_MAIN);
                lv_obj_set_style_text_color(eye->status_text, rgb(accent), LV_PART_MAIN);
                lv_label_set_text(eye->status_text, phase_badge);
                lv_obj_align(eye->status_text, LV_ALIGN_TOP_MID, 0, 20);
                lv_obj_set_width(eye->status_bar, 126);
                lv_obj_set_height(eye->status_bar, 8);
                lv_bar_set_value(eye->status_bar, progress, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
                lv_obj_set_style_bg_color(eye->status_bar, rgb(accent), LV_PART_INDICATOR);
                lv_obj_clear_flag(eye->status_bar, LV_OBJ_FLAG_HIDDEN);
                lv_obj_align(eye->status_bar, LV_ALIGN_BOTTOM_MID, 0, -26);
                lv_obj_move_foreground(eye->content);
                lv_obj_move_foreground(eye->status_text);
                lv_obj_move_foreground(eye->status_bar);
                return;
            }

            lv_obj_set_width(eye->content, 214);
            lv_obj_set_style_text_font(eye->content, font_countdown(), LV_PART_MAIN);
            lv_obj_set_style_text_color(eye->content, rgb(s_theme->text_rgb), LV_PART_MAIN);
            lv_obj_set_style_text_line_space(eye->content, 0, LV_PART_MAIN);
            lv_label_set_long_mode(eye->content, LV_LABEL_LONG_CLIP);
            lv_label_set_text(eye->content, remain);
            lv_obj_align(eye->content, LV_ALIGN_CENTER, 0, -14);
            if (eye->progress_arc != NULL) {
                lv_obj_set_size(eye->progress_arc, 218, 218);
                lv_arc_set_value(eye->progress_arc, progress);
                lv_obj_set_style_arc_width(eye->progress_arc, 8, LV_PART_MAIN);
                lv_obj_set_style_arc_width(eye->progress_arc, 13, LV_PART_INDICATOR);
                lv_obj_set_style_arc_color(eye->progress_arc, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
                lv_obj_set_style_arc_color(eye->progress_arc, rgb(accent), LV_PART_INDICATOR);
                lv_obj_clear_flag(eye->progress_arc, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_background(eye->progress_arc);
            }
            snprintf(phase_badge,
                     sizeof(phase_badge),
                     "%s  %u%%",
                     pomodoro_phase_short(payload->pomodoro_running, payload->pomodoro_in_break),
                     (unsigned)progress_raw);
            lv_obj_set_width(eye->status_text, 156);
            lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_color(eye->status_text, rgb(accent), LV_PART_MAIN);
            lv_label_set_text(eye->status_text, phase_badge);
            lv_obj_align(eye->status_text, LV_ALIGN_CENTER, 0, 48);
        } else {
            lv_obj_set_style_bg_color(eye->screen, rgb(s_theme->panel_rgb), LV_PART_MAIN);
            if (eye->progress_arc != NULL) {
                lv_obj_set_size(eye->progress_arc, 220, 220);
                lv_arc_set_value(eye->progress_arc, progress);
                lv_obj_set_style_arc_width(eye->progress_arc, 8, LV_PART_MAIN);
                lv_obj_set_style_arc_width(eye->progress_arc, 12, LV_PART_INDICATOR);
                lv_obj_set_style_arc_color(eye->progress_arc, rgb(s_theme->muted_rgb), LV_PART_MAIN);
                lv_obj_set_style_arc_opa(eye->progress_arc, 110, LV_PART_MAIN);
                lv_obj_set_style_arc_color(eye->progress_arc, rgb(accent), LV_PART_INDICATOR);
                lv_obj_clear_flag(eye->progress_arc, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_background(eye->progress_arc);
            }
            lv_obj_set_width(eye->content, 154);
            lv_obj_set_style_text_font(eye->content, font_cjk(), LV_PART_MAIN);
            lv_obj_set_style_text_color(eye->content, rgb(s_theme->text_rgb), LV_PART_MAIN);
            lv_obj_set_style_text_line_space(eye->content, 5, LV_PART_MAIN);
            lv_label_set_long_mode(eye->content, LV_LABEL_LONG_WRAP);
            lv_obj_set_height(eye->content, LV_SIZE_CONTENT);
            lv_label_set_text_fmt(eye->content,
                                  "%s\n%s\n%u min",
                                  phase,
                                  task_short,
                                  (unsigned)total_minutes);
            lv_obj_align(eye->content, LV_ALIGN_CENTER, 0, -34);
            lv_obj_move_foreground(eye->content);
            snprintf(bar_text,
                     sizeof(bar_text),
                     "%s  %u/%u",
                     pomodoro_phase_short(payload->pomodoro_running, payload->pomodoro_in_break),
                     (unsigned)used_minutes,
                     (unsigned)total_minutes);
            lv_label_set_text(eye->status_text, bar_text);
            lv_obj_set_width(eye->status_text, 164);
            lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_color(eye->status_text, rgb(s_theme->muted_rgb), LV_PART_MAIN);
            lv_obj_set_width(eye->status_bar, 136);
            lv_obj_set_height(eye->status_bar, 11);
            lv_bar_set_value(eye->status_bar, progress, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->muted_rgb), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(eye->status_bar, 100, LV_PART_MAIN);
            lv_obj_set_style_bg_color(eye->status_bar, rgb(accent), LV_PART_INDICATOR);
            lv_obj_clear_flag(eye->status_bar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(eye->status_bar, LV_ALIGN_CENTER, 0, 42);
            lv_obj_align(eye->status_text, LV_ALIGN_CENTER, 0, 62);
            lv_obj_move_foreground(eye->status_bar);
            lv_obj_move_foreground(eye->status_text);
        }
    } else if (page == ATLAS_PAGE_PHOTO) {
        if (is_left && show_pet_head_keyframe(index, "think", 246, 0, 0)) {
            lv_obj_set_width(eye->content, 156);
            lv_obj_set_style_text_font(eye->content, font_cjk(), LV_PART_MAIN);
            lv_label_set_long_mode(eye->content, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_color(eye->content, rgb(s_theme->text_rgb), LV_PART_MAIN);
            lv_label_set_text(eye->content, "相册");
            lv_obj_align(eye->content, LV_ALIGN_BOTTOM_MID, 0, -42);
            lv_obj_set_width(eye->status_text, 150);
            lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_color(eye->status_text, rgb(s_theme->primary_rgb), LV_PART_MAIN);
            lv_label_set_text(eye->status_text, "照片入口");
            lv_obj_align(eye->status_text, LV_ALIGN_TOP_MID, 0, 20);
            lv_obj_set_width(eye->status_bar, 126);
            lv_obj_set_height(eye->status_bar, 8);
            lv_bar_set_value(eye->status_bar, pulse_percent(now_ms, 2600, 28, 74), LV_ANIM_OFF);
            lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
            lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->primary_rgb), LV_PART_INDICATOR);
            lv_obj_clear_flag(eye->status_bar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(eye->status_bar, LV_ALIGN_BOTTOM_MID, 0, -26);
            lv_obj_move_foreground(eye->content);
            lv_obj_move_foreground(eye->status_text);
            lv_obj_move_foreground(eye->status_bar);
            return;
        }
        lv_obj_set_width(eye->content, 164);
        lv_obj_set_style_text_font(eye->content, font_cjk(), LV_PART_MAIN);
        lv_obj_set_style_text_color(eye->content, rgb(is_left ? s_theme->primary_rgb : s_theme->text_rgb), LV_PART_MAIN);
        lv_obj_set_style_text_line_space(eye->content, 5, LV_PART_MAIN);
        lv_label_set_long_mode(eye->content, LV_LABEL_LONG_WRAP);
        lv_label_set_text(eye->content,
                          is_left ? "相册\n待接入" : "可先通过\nWeb/Brain\n下发图片");
        lv_obj_align(eye->content, LV_ALIGN_CENTER, 0, -22);
        lv_label_set_text(eye->status_text, is_left ? "本地占位页" : "资源接口待做");
        lv_obj_set_width(eye->status_text, 154);
        lv_label_set_long_mode(eye->status_text, LV_LABEL_LONG_CLIP);
        lv_obj_align(eye->status_text, LV_ALIGN_BOTTOM_MID, 0, -24);
        lv_obj_set_width(eye->status_bar, 128);
        lv_obj_set_height(eye->status_bar, 8);
        lv_bar_set_value(eye->status_bar, pulse_percent(now_ms, 2800, 22, 68), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->panel_2_rgb), LV_PART_MAIN);
        lv_obj_set_style_bg_color(eye->status_bar, rgb(s_theme->primary_rgb), LV_PART_INDICATOR);
        lv_obj_clear_flag(eye->status_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(eye->status_bar, LV_ALIGN_BOTTOM_MID, 0, -10);
    } else {
        lv_obj_add_flag(eye->content, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(eye->status_bar, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(eye->status_text, "");
        lv_obj_set_style_text_align(eye->content, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(eye->status_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        return;
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

bool atlas_page_from_name(const char *name, atlas_page_t *page)
{
    if (name == NULL || page == NULL) {
        return false;
    }
    for (atlas_page_t candidate = ATLAS_PAGE_EYES; candidate <= ATLAS_PAGE_POMODORO; ++candidate) {
        if (strcmp(name, atlas_page_name(candidate)) == 0) {
            *page = candidate;
            return true;
        }
    }
    return false;
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
                          uint32_t now_ms,
                          const atlas_display_payload_t *payload)
{
    if (s_theme == NULL) {
        s_theme = atlas_expression_default_theme();
    }

    atlas_display_payload_t local_payload = {0};
    payload_to_local(payload, &local_payload);

    atlas_eye_frame_t frame;
    atlas_expression_make_frame_with_theme(expression, motion, now_ms, audio_level, s_theme, &frame);

#if CONFIG_ATLAS_DISPLAY_WAVESHARE_LVGL
    if (s_lvgl_ready) {
        render_lvgl_eye(0, &frame.left, page, expression, now_ms, &local_payload);
        render_lvgl_eye(1, &frame.right, page, expression, now_ms, &local_payload);
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
