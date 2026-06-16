#include "atlas_expression.h"

#include <stddef.h>

#define RGB_CYAN   0x3FC9FFu
#define RGB_MINT   0x5FE1B4u
#define RGB_AMBER  0xF5DC96u
#define RGB_RED    0xFF6B4Bu

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
        [ATLAS_EXPR_ANGRY] = "angry",
        [ATLAS_EXPR_CHARGING] = "charging",
        [ATLAS_EXPR_ERROR] = "error",
    };

    if (expression < 0 || expression >= ATLAS_EXPR_COUNT || names[expression] == NULL) {
        return "unknown";
    }
    return names[expression];
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

void atlas_expression_make_frame(atlas_expression_t expression,
                                 atlas_motion_t motion,
                                 uint32_t now_ms,
                                 uint8_t audio_level,
                                 atlas_eye_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    frame->left = base_eye(RGB_CYAN);
    frame->right = base_eye(RGB_CYAN);

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
        frame->left = base_eye(RGB_MINT);
        frame->right = base_eye(RGB_MINT);
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
        frame->left = base_eye(RGB_MINT);
        frame->left.visible = false;
        frame->left.top_lid = 100;
        frame->left.bottom_lid = 100;
        frame->right.iris_scale = 105;
        frame->right.look_x = 8;
        break;

    case ATLAS_EXPR_ANGRY:
        frame->left = base_eye(RGB_RED);
        frame->right = base_eye(RGB_RED);
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
        frame->left = base_eye(RGB_AMBER);
        frame->right = base_eye(RGB_AMBER);
        frame->left.look_y = 8;
        frame->right.look_y = 8;
        frame->left.effect = ATLAS_EYE_EFFECT_CHARGE;
        frame->right.effect = ATLAS_EYE_EFFECT_CHARGE;
        break;

    case ATLAS_EXPR_ERROR:
        frame->left = base_eye(RGB_RED);
        frame->right = base_eye(RGB_RED);
        frame->left.iris_scale = 92;
        frame->right.iris_scale = 92;
        frame->left.top_lid = 45;
        frame->right.top_lid = 45;
        frame->left.bottom_lid = 45;
        frame->right.bottom_lid = 45;
        frame->left.effect = ATLAS_EYE_EFFECT_ERROR;
        frame->right.effect = ATLAS_EYE_EFFECT_ERROR;
        break;

    case ATLAS_EXPR_COUNT:
    default:
        break;
    }
}
