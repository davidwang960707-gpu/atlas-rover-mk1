#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ATLAS_EXPR_IDLE = 0,
    ATLAS_EXPR_HAPPY,
    ATLAS_EXPR_LISTEN,
    ATLAS_EXPR_THINKING,
    ATLAS_EXPR_SPEAKING,
    ATLAS_EXPR_MOVING,
    ATLAS_EXPR_CURIOUS,
    ATLAS_EXPR_SLEEPY,
    ATLAS_EXPR_SURPRISED,
    ATLAS_EXPR_WINK,
    ATLAS_EXPR_ANGRY,
    ATLAS_EXPR_CHARGING,
    ATLAS_EXPR_ERROR,
    ATLAS_EXPR_COUNT,
} atlas_expression_t;

typedef enum {
    ATLAS_MOTION_NONE = 0,
    ATLAS_MOTION_FORWARD,
    ATLAS_MOTION_BACKWARD,
    ATLAS_MOTION_LEFT,
    ATLAS_MOTION_RIGHT,
} atlas_motion_t;

typedef enum {
    ATLAS_EYE_EFFECT_NONE = 0,
    ATLAS_EYE_EFFECT_PULSE,
    ATLAS_EYE_EFFECT_SCAN,
    ATLAS_EYE_EFFECT_TALK,
    ATLAS_EYE_EFFECT_CHARGE,
    ATLAS_EYE_EFFECT_ERROR,
} atlas_eye_effect_t;

typedef struct {
    int16_t look_x;         // -100 left, 0 center, 100 right
    int16_t look_y;         // -100 up, 0 center, 100 down
    uint16_t iris_scale;    // 100 = normal
    uint16_t pupil_scale;   // 100 = normal
    uint8_t top_lid;        // 0 open, 100 closed
    uint8_t bottom_lid;     // 0 open, 100 closed
    int16_t brow_tilt;      // degrees
    uint32_t accent_rgb;    // 0xRRGGBB
    atlas_eye_effect_t effect;
    bool visible;
} atlas_eye_pose_t;

typedef struct {
    atlas_eye_pose_t left;
    atlas_eye_pose_t right;
} atlas_eye_frame_t;

const char *atlas_expression_name(atlas_expression_t expression);
const char *atlas_motion_name(atlas_motion_t motion);

void atlas_expression_make_frame(atlas_expression_t expression,
                                 atlas_motion_t motion,
                                 uint32_t now_ms,
                                 uint8_t audio_level,
                                 atlas_eye_frame_t *frame);
