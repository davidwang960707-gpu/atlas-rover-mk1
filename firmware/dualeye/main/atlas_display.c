#include "atlas_display.h"

#include "esp_log.h"

static const char *TAG = "atlas_display";

static uint32_t s_last_render_log_ms;

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
    default:
        return "unknown";
    }
}

esp_err_t atlas_display_init(void)
{
    /*
     * V0.1 keeps the hardware display backend as a single adapter.
     * Replace this file with the Waveshare/LVGL panel init once the board is
     * connected; the rest of the UI state machine can remain unchanged.
     */
    ESP_LOGI(TAG, "display backend: log renderer placeholder, dual 240x240 eye frames");
    return ESP_OK;
}

void atlas_display_render(atlas_page_t page,
                          atlas_expression_t expression,
                          atlas_motion_t motion,
                          uint8_t audio_level,
                          uint32_t now_ms)
{
    atlas_eye_frame_t frame;
    atlas_expression_make_frame(expression, motion, now_ms, audio_level, &frame);

    if (now_ms - s_last_render_log_ms < 1000) {
        return;
    }
    s_last_render_log_ms = now_ms;

    ESP_LOGI(TAG,
             "page=%s expr=%s motion=%s L{x=%d y=%d iris=%u pupil=%u lid=%u/%u effect=%s visible=%d} "
             "R{x=%d y=%d iris=%u pupil=%u lid=%u/%u effect=%s visible=%d}",
             atlas_page_name(page),
             atlas_expression_name(expression),
             atlas_motion_name(motion),
             frame.left.look_x,
             frame.left.look_y,
             frame.left.iris_scale,
             frame.left.pupil_scale,
             frame.left.top_lid,
             frame.left.bottom_lid,
             effect_name(frame.left.effect),
             frame.left.visible,
             frame.right.look_x,
             frame.right.look_y,
             frame.right.iris_scale,
             frame.right.pupil_scale,
             frame.right.top_lid,
             frame.right.bottom_lid,
             effect_name(frame.right.effect),
             frame.right.visible);
}
