#include "atlas_expression.h"

#include <stddef.h>
#include <string.h>

static const atlas_theme_palette_t s_theme_palettes[ATLAS_THEME_COUNT] = {
    [ATLAS_THEME_CLASSIC] = {
        .id = "classic",
        .name_zh = "经典蓝眼",
        .bg_rgb = 0x07080Au,
        .panel_rgb = 0x111318u,
        .panel_2_rgb = 0x0D0F13u,
        .stage_bg_rgb = 0x07080Au,
        .eye_bg_rgb = 0x07080Au,
        .line_rgb = 0xA66C2Au,
        .primary_rgb = 0x3FC9FFu,
        .positive_rgb = 0x5FE1B4u,
        .danger_rgb = 0xFF6B4Bu,
        .amber_rgb = 0xF5DC96u,
        .rose_rgb = 0xFF6AA9u,
        .tear_rgb = 0x6ED7FFu,
        .text_rgb = 0xEFE9DFu,
        .muted_rgb = 0x9C958Cu,
    },
    [ATLAS_THEME_AMBER] = {
        .id = "amber",
        .name_zh = "琥珀巡航",
        .bg_rgb = 0x0B0906u,
        .panel_rgb = 0x15120Du,
        .panel_2_rgb = 0x100D09u,
        .stage_bg_rgb = 0x090807u,
        .eye_bg_rgb = 0x090706u,
        .line_rgb = 0xCC8742u,
        .primary_rgb = 0xFFC15Fu,
        .positive_rgb = 0x70D7FFu,
        .danger_rgb = 0xFF6D4Fu,
        .amber_rgb = 0xFFC15Fu,
        .rose_rgb = 0xFF6AA9u,
        .tear_rgb = 0x70D7FFu,
        .text_rgb = 0xFFF1DCu,
        .muted_rgb = 0xC0AA8Fu,
    },
    [ATLAS_THEME_MINT] = {
        .id = "mint",
        .name_zh = "薄荷友好",
        .bg_rgb = 0x06100Fu,
        .panel_rgb = 0x0D1917u,
        .panel_2_rgb = 0x091310u,
        .stage_bg_rgb = 0x06100Fu,
        .eye_bg_rgb = 0x04100Eu,
        .line_rgb = 0x8F9F7Au,
        .primary_rgb = 0x62F0B5u,
        .positive_rgb = 0xB8F477u,
        .danger_rgb = 0xFF7066u,
        .amber_rgb = 0xE2D478u,
        .rose_rgb = 0xFF6AA9u,
        .tear_rgb = 0x62F0B5u,
        .text_rgb = 0xEAFFF6u,
        .muted_rgb = 0x9FC2B3u,
    },
    [ATLAS_THEME_ALERT] = {
        .id = "alert",
        .name_zh = "红色警戒",
        .bg_rgb = 0x0D0808u,
        .panel_rgb = 0x171011u,
        .panel_2_rgb = 0x110B0Cu,
        .stage_bg_rgb = 0x0D0808u,
        .eye_bg_rgb = 0x0B0707u,
        .line_rgb = 0xB57C3Bu,
        .primary_rgb = 0xFF7B61u,
        .positive_rgb = 0xFFD36Au,
        .danger_rgb = 0xFF4F45u,
        .amber_rgb = 0xFFC45Fu,
        .rose_rgb = 0xFF6AA9u,
        .tear_rgb = 0xFF7B61u,
        .text_rgb = 0xFFF0EAu,
        .muted_rgb = 0xC29B93u,
    },
    [ATLAS_THEME_NIGHT] = {
        .id = "night",
        .name_zh = "低亮夜航",
        .bg_rgb = 0x05080Du,
        .panel_rgb = 0x0C1118u,
        .panel_2_rgb = 0x080D13u,
        .stage_bg_rgb = 0x05080Du,
        .eye_bg_rgb = 0x03060Au,
        .line_rgb = 0x7C826Eu,
        .primary_rgb = 0x88BFFFu,
        .positive_rgb = 0x74DCB2u,
        .danger_rgb = 0xFF6C6Cu,
        .amber_rgb = 0xD6C784u,
        .rose_rgb = 0xFF6AA9u,
        .tear_rgb = 0x88BFFFu,
        .text_rgb = 0xDFE9F7u,
        .muted_rgb = 0x93A0ADu,
    },
};

static int16_t triangle_wave(uint32_t now_ms, uint32_t period_ms, int16_t amplitude)
{
    if (period_ms == 0) {
        return 0;
    }

    const uint32_t phase = now_ms % period_ms;
    const uint32_t half = period_ms / 2;
    if (phase < half) {
        return (int16_t)((phase * 2 * amplitude) / half - amplitude);
    }
    return (int16_t)(amplitude - (((phase - half) * 2 * amplitude) / half));
}

static atlas_eye_pose_t base_eye(uint32_t color)
{
    return (atlas_eye_pose_t) {
        .look_x = 0,
        .look_y = 0,
        .iris_scale = 100,
        .pupil_scale = 100,
        .top_lid = 0,
        .bottom_lid = 0,
        .brow_tilt = 0,
        .accent_rgb = color,
        .effect = ATLAS_EYE_EFFECT_NONE,
        .visible = true,
    };
}

const char *atlas_expression_name(atlas_expression_t expression)
{
    static const char *names[ATLAS_EXPR_COUNT] = {
        [ATLAS_EXPR_IDLE] = "idle",
        [ATLAS_EXPR_HAPPY] = "happy",
        [ATLAS_EXPR_LISTEN] = "listen",
        [ATLAS_EXPR_THINKING] = "thinking",
        [ATLAS_EXPR_SPEAKING] = "speaking",
        [ATLAS_EXPR_MOVING] = "moving",
        [ATLAS_EXPR_CURIOUS] = "curious",
        [ATLAS_EXPR_SLEEPY] = "sleepy",
        [ATLAS_EXPR_SURPRISED] = "surprised",
        [ATLAS_EXPR_WINK] = "wink",
        [ATLAS_EXPR_LOVE] = "love",
        [ATLAS_EXPR_MONEY] = "money",
        [ATLAS_EXPR_ANGRY] = "angry",
        [ATLAS_EXPR_CHARGING] = "charging",
        [ATLAS_EXPR_ERROR] = "error",
        [ATLAS_EXPR_CRY] = "cry",
    };

    if (expression < 0 || expression >= ATLAS_EXPR_COUNT || names[expression] == NULL) {
        return "unknown";
    }
    return names[expression];
}

bool atlas_expression_from_name(const char *name, atlas_expression_t *expression)
{
    if (name == NULL || expression == NULL) {
        return false;
    }

    for (atlas_expression_t candidate = ATLAS_EXPR_IDLE; candidate < ATLAS_EXPR_COUNT; ++candidate) {
        if (strcmp(name, atlas_expression_name(candidate)) == 0) {
            *expression = candidate;
            return true;
        }
    }
    return false;
}

const char *atlas_motion_name(atlas_motion_t motion)
{
    switch (motion) {
    case ATLAS_MOTION_FORWARD:
        return "forward";
    case ATLAS_MOTION_BACKWARD:
        return "backward";
    case ATLAS_MOTION_LEFT:
        return "left";
    case ATLAS_MOTION_RIGHT:
        return "right";
    case ATLAS_MOTION_NONE:
    default:
        return "none";
    }
}

const atlas_theme_palette_t *atlas_expression_theme_palette(atlas_theme_t theme)
{
    if (theme < 0 || theme >= ATLAS_THEME_COUNT) {
        return &s_theme_palettes[ATLAS_THEME_CLASSIC];
    }
    return &s_theme_palettes[theme];
}

atlas_theme_t atlas_expression_theme_from_id(const char *theme_id)
{
    if (theme_id == NULL || theme_id[0] == '\0' || strcmp(theme_id, "atlas_blue") == 0) {
        return ATLAS_THEME_CLASSIC;
    }
    for (atlas_theme_t theme = ATLAS_THEME_CLASSIC; theme < ATLAS_THEME_COUNT; ++theme) {
        if (strcmp(theme_id, s_theme_palettes[theme].id) == 0) {
            return theme;
        }
    }
    return ATLAS_THEME_CLASSIC;
}

const atlas_theme_palette_t *atlas_expression_theme_palette_by_id(const char *theme_id)
{
    return atlas_expression_theme_palette(atlas_expression_theme_from_id(theme_id));
}

const atlas_theme_palette_t *atlas_expression_default_theme(void)
{
    return &s_theme_palettes[ATLAS_THEME_CLASSIC];
}

const char *atlas_expression_theme_id(atlas_theme_t theme)
{
    return atlas_expression_theme_palette(theme)->id;
}

bool atlas_expression_theme_is_valid(const char *theme_id)
{
    if (theme_id == NULL || theme_id[0] == '\0') {
        return false;
    }
    if (strcmp(theme_id, "atlas_blue") == 0) {
        return true;
    }
    for (atlas_theme_t theme = ATLAS_THEME_CLASSIC; theme < ATLAS_THEME_COUNT; ++theme) {
        if (strcmp(theme_id, s_theme_palettes[theme].id) == 0) {
            return true;
        }
    }
    return false;
}

void atlas_expression_make_frame(atlas_expression_t expression,
                                 atlas_motion_t motion,
                                 uint32_t now_ms,
                                 uint8_t audio_level,
                                 atlas_eye_frame_t *frame)
{
    atlas_expression_make_frame_with_theme(expression,
                                           motion,
                                           now_ms,
                                           audio_level,
                                           atlas_expression_default_theme(),
                                           frame);
}

void atlas_expression_make_frame_with_theme(atlas_expression_t expression,
                                            atlas_motion_t motion,
                                            uint32_t now_ms,
                                            uint8_t audio_level,
                                            const atlas_theme_palette_t *theme,
                                            atlas_eye_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    if (theme == NULL) {
        theme = atlas_expression_default_theme();
    }

    frame->left = base_eye(theme->primary_rgb);
    frame->right = base_eye(theme->primary_rgb);

    const int16_t breathe = triangle_wave(now_ms, 3800, 4);
    const int16_t pulse = triangle_wave(now_ms, 900, 14);
    const int16_t talk = triangle_wave(now_ms, 340, 9);
    const int16_t audio_boost = (int16_t)((audio_level > 100 ? 100 : audio_level) / 5);

    switch (expression) {
    case ATLAS_EXPR_IDLE:
        frame->left.look_x = -4;
        frame->right.look_x = 4;
        frame->left.iris_scale = (uint16_t)(100 + breathe);
        frame->right.iris_scale = (uint16_t)(100 + breathe);
        break;

    case ATLAS_EXPR_HAPPY:
        frame->left = base_eye(theme->positive_rgb);
        frame->right = base_eye(theme->positive_rgb);
        frame->left.look_y = -10;
        frame->right.look_y = -10;
        frame->left.iris_scale = 72;
        frame->right.iris_scale = 72;
        frame->left.top_lid = 70;
        frame->right.top_lid = 70;
        frame->left.bottom_lid = 58;
        frame->right.bottom_lid = 58;
        break;

    case ATLAS_EXPR_LISTEN:
        frame->left.iris_scale = (uint16_t)(108 + pulse / 2);
        frame->right.iris_scale = (uint16_t)(108 + pulse / 2);
        frame->left.effect = ATLAS_EYE_EFFECT_PULSE;
        frame->right.effect = ATLAS_EYE_EFFECT_PULSE;
        break;

    case ATLAS_EXPR_THINKING:
        frame->left.look_x = -18;
        frame->left.look_y = -16;
        frame->right.look_x = 18;
        frame->right.look_y = -16;
        frame->left.iris_scale = 92;
        frame->right.iris_scale = 92;
        frame->left.top_lid = 34;
        frame->right.top_lid = 34;
        frame->left.effect = ATLAS_EYE_EFFECT_SCAN;
        frame->right.effect = ATLAS_EYE_EFFECT_SCAN;
        break;

    case ATLAS_EXPR_SPEAKING:
        frame->left.iris_scale = (uint16_t)(100 + talk + audio_boost);
        frame->right.iris_scale = (uint16_t)(100 + talk + audio_boost);
        frame->left.effect = ATLAS_EYE_EFFECT_TALK;
        frame->right.effect = ATLAS_EYE_EFFECT_TALK;
        break;

    case ATLAS_EXPR_MOVING:
        frame->left.iris_scale = 96;
        frame->right.iris_scale = 96;
        frame->left.top_lid = 20;
        frame->right.top_lid = 20;
        if (motion == ATLAS_MOTION_FORWARD) {
            frame->left.look_y = -18;
            frame->right.look_y = -18;
        } else if (motion == ATLAS_MOTION_BACKWARD) {
            frame->left.look_y = 22;
            frame->right.look_y = 22;
        } else if (motion == ATLAS_MOTION_LEFT) {
            frame->left.look_x = -34;
            frame->right.look_x = -34;
        } else if (motion == ATLAS_MOTION_RIGHT) {
            frame->left.look_x = 34;
            frame->right.look_x = 34;
        }
        break;

    case ATLAS_EXPR_CURIOUS:
        frame->left.look_x = -8;
        frame->left.look_y = -8;
        frame->left.iris_scale = 118;
        frame->right.look_x = 10;
        frame->right.look_y = 10;
        frame->right.iris_scale = 86;
        frame->right.top_lid = 34;
        break;

    case ATLAS_EXPR_SLEEPY:
        frame->left.look_y = 18;
        frame->right.look_y = 18;
        frame->left.iris_scale = 78;
        frame->right.iris_scale = 78;
        frame->left.top_lid = 78;
        frame->right.top_lid = 78;
        frame->left.bottom_lid = 34;
        frame->right.bottom_lid = 34;
        break;

    case ATLAS_EXPR_SURPRISED:
        frame->left.iris_scale = 128;
        frame->right.iris_scale = 128;
        frame->left.pupil_scale = 55;
        frame->right.pupil_scale = 55;
        break;

    case ATLAS_EXPR_WINK:
        frame->left = base_eye(theme->positive_rgb);
        frame->left.visible = false;
        frame->left.top_lid = 100;
        frame->left.bottom_lid = 100;
        frame->right.iris_scale = 105;
        frame->right.look_x = 8;
        break;

    case ATLAS_EXPR_LOVE:
        frame->left = base_eye(theme->rose_rgb);
        frame->right = base_eye(theme->rose_rgb);
        frame->left.iris_scale = (uint16_t)(116 + pulse / 3);
        frame->right.iris_scale = (uint16_t)(116 + pulse / 3);
        frame->left.pupil_scale = 76;
        frame->right.pupil_scale = 76;
        frame->left.look_y = -6;
        frame->right.look_y = -6;
        frame->left.effect = ATLAS_EYE_EFFECT_PULSE;
        frame->right.effect = ATLAS_EYE_EFFECT_PULSE;
        break;

    case ATLAS_EXPR_MONEY:
        frame->left = base_eye(theme->amber_rgb);
        frame->right = base_eye(theme->amber_rgb);
        frame->left.iris_scale = 86;
        frame->right.iris_scale = 86;
        frame->left.pupil_scale = 62;
        frame->right.pupil_scale = 62;
        frame->left.look_x = -12;
        frame->right.look_x = 12;
        frame->left.effect = ATLAS_EYE_EFFECT_SCAN;
        frame->right.effect = ATLAS_EYE_EFFECT_SCAN;
        break;

    case ATLAS_EXPR_ANGRY:
        frame->left = base_eye(theme->danger_rgb);
        frame->right = base_eye(theme->danger_rgb);
        frame->left.iris_scale = 88;
        frame->right.iris_scale = 88;
        frame->left.top_lid = 70;
        frame->right.top_lid = 70;
        frame->left.brow_tilt = 18;
        frame->right.brow_tilt = -18;
        frame->left.look_x = 8;
        frame->right.look_x = -8;
        break;

    case ATLAS_EXPR_CHARGING:
        frame->left = base_eye(theme->amber_rgb);
        frame->right = base_eye(theme->amber_rgb);
        frame->left.look_y = 8;
        frame->right.look_y = 8;
        frame->left.effect = ATLAS_EYE_EFFECT_CHARGE;
        frame->right.effect = ATLAS_EYE_EFFECT_CHARGE;
        break;

    case ATLAS_EXPR_ERROR:
        frame->left = base_eye(theme->danger_rgb);
        frame->right = base_eye(theme->danger_rgb);
        frame->left.iris_scale = 92;
        frame->right.iris_scale = 92;
        frame->left.top_lid = 45;
        frame->right.top_lid = 45;
        frame->left.bottom_lid = 45;
        frame->right.bottom_lid = 45;
        frame->left.effect = ATLAS_EYE_EFFECT_ERROR;
        frame->right.effect = ATLAS_EYE_EFFECT_ERROR;
        break;

    case ATLAS_EXPR_CRY:
        frame->left = base_eye(theme->tear_rgb);
        frame->right = base_eye(theme->tear_rgb);
        frame->left.look_y = 24;
        frame->right.look_y = 24;
        frame->left.iris_scale = 72;
        frame->right.iris_scale = 72;
        frame->left.top_lid = 42;
        frame->right.top_lid = 42;
        frame->left.bottom_lid = 28;
        frame->right.bottom_lid = 28;
        frame->left.effect = ATLAS_EYE_EFFECT_TALK;
        frame->right.effect = ATLAS_EYE_EFFECT_TALK;
        break;

    case ATLAS_EXPR_COUNT:
    default:
        break;
    }
}
