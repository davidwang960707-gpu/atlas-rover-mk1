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
    ATLAS_EXPR_LOVE,
    ATLAS_EXPR_MONEY,
    ATLAS_EXPR_ANGRY,
    ATLAS_EXPR_CHARGING,
    ATLAS_EXPR_ERROR,
    ATLAS_EXPR_CRY,
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

typedef enum {
    ATLAS_THEME_CLASSIC = 0,
    ATLAS_THEME_AMBER,
    ATLAS_THEME_MINT,
    ATLAS_THEME_ALERT,
    ATLAS_THEME_NIGHT,
    ATLAS_THEME_COUNT,
} atlas_theme_t;

typedef struct {
    const char *id;
    const char *name_zh;
    uint32_t bg_rgb;
    uint32_t panel_rgb;
    uint32_t panel_2_rgb;
    uint32_t stage_bg_rgb;
    uint32_t eye_bg_rgb;
    uint32_t line_rgb;
    uint32_t primary_rgb;
    uint32_t positive_rgb;
    uint32_t danger_rgb;
    uint32_t amber_rgb;
    uint32_t rose_rgb;
    uint32_t tear_rgb;
    uint32_t text_rgb;
    uint32_t muted_rgb;
} atlas_theme_palette_t;

typedef struct {
    atlas_eye_pose_t left;
    atlas_eye_pose_t right;
} atlas_eye_frame_t;

const char *atlas_expression_name(atlas_expression_t expression);
bool atlas_expression_from_name(const char *name, atlas_expression_t *expression);
const char *atlas_motion_name(atlas_motion_t motion);
const atlas_theme_palette_t *atlas_expression_theme_palette(atlas_theme_t theme);
const atlas_theme_palette_t *atlas_expression_theme_palette_by_id(const char *theme_id);
const atlas_theme_palette_t *atlas_expression_default_theme(void);
atlas_theme_t atlas_expression_theme_from_id(const char *theme_id);
const char *atlas_expression_theme_id(atlas_theme_t theme);
bool atlas_expression_theme_is_valid(const char *theme_id);

void atlas_expression_make_frame(atlas_expression_t expression,
                                 atlas_motion_t motion,
                                 uint32_t now_ms,
                                 uint8_t audio_level,
                                 atlas_eye_frame_t *frame);

void atlas_expression_make_frame_with_theme(atlas_expression_t expression,
                                            atlas_motion_t motion,
                                            uint32_t now_ms,
                                            uint8_t audio_level,
                                            const atlas_theme_palette_t *theme,
                                            atlas_eye_frame_t *frame);
