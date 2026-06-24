#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATLAS_COMMON_ASSET_MANIFEST_PROTOCOL "atlas.asset.manifest.v0"
#define ATLAS_COMMON_PET_HEAD_PROTOCOL "atlas.pet_head.v0"
#define ATLAS_COMMON_EYE_THEME_PROTOCOL "atlas.eye_theme.v0"
#define ATLAS_COMMON_ASSET_PACK_ID "dualeye-assets"
#define ATLAS_COMMON_RESOURCE_VERSION "dualeye-assets-v0.6-pet-head-yaw"
#define ATLAS_COMMON_FONT_VERSION "atlas_font_zh_16_3500"
#define ATLAS_COMMON_ASSET_MOUNT_PATH "/spiffs"
#define ATLAS_COMMON_ASSET_PARTITION_LABEL "storage"
#define ATLAS_COMMON_ASSET_LVGL_LETTER 'A'

#define ATLAS_COMMON_ASSET_EYE_MANIFEST_PATH "/spiffs/atlas_eyes/manifest.json"
#define ATLAS_COMMON_ASSET_EYE_PET_IDLE_LEFT_PATH "/spiffs/atlas_eyes/pet/idle/left.png"
#define ATLAS_COMMON_ASSET_EYE_GOGGLE_IDLE_LEFT_PATH "/spiffs/atlas_eyes/goggle/idle/left.png"
#define ATLAS_COMMON_ASSET_EYE_TOMOE_IDLE_LEFT_PATH "/spiffs/atlas_eyes/tomoe_spin/idle/left.png"
#define ATLAS_COMMON_ASSET_EYE_NO_SMOKING_IDLE_LEFT_PATH "/spiffs/atlas_eyes/no_smoking/idle/left.png"
#define ATLAS_COMMON_ASSET_PET_HEAD_MANIFEST_PATH "/spiffs/atlas_pet_head/manifest.json"
#define ATLAS_COMMON_ASSET_PET_HEAD_IDLE_PATH "/spiffs/atlas_pet_head/keyframes/idle.png"
#define ATLAS_COMMON_ASSET_PET_HEAD_SPEAK_FRAME0_PATH "/spiffs/atlas_pet_head/animations/speak/frame_00.png"
#define ATLAS_COMMON_ASSET_PET_HEAD_IDLE_YAW_L30_PATH "/spiffs/atlas_pet_head/views/idle/yaw_l30.png"
#define ATLAS_COMMON_ASSET_PET_HEAD_THINK_YAW_R15_PATH "/spiffs/atlas_pet_head/views/think/yaw_r15.png"
#define ATLAS_COMMON_ASSET_PET_HEAD_TURN_C_TO_L30_FRAME0_PATH "/spiffs/atlas_pet_head/transitions/turn_yaw_c_to_yaw_l30/frame_00.png"
#define ATLAS_COMMON_ASSET_PET_HEAD_TURN_R30_TO_C_FRAME5_PATH "/spiffs/atlas_pet_head/transitions/turn_yaw_r30_to_yaw_c/frame_05.png"

typedef struct {
    bool eye_manifest;
    bool eye_pet_idle;
    bool eye_goggle_idle;
    bool eye_tomoe_idle;
    bool eye_no_smoking_idle;
    bool pet_head_manifest;
    bool pet_head_idle;
    bool pet_head_speak;
    bool pet_head_view;
    bool pet_head_turn;
    uint8_t ok_count;
} atlas_common_assets_probe_t;

const char *atlas_common_assets_manifest_protocol(void);
const char *atlas_common_assets_resource_version(void);
const char *atlas_common_assets_font_version(void);

bool atlas_common_assets_file_exists(const char *path);
void atlas_common_assets_probe(atlas_common_assets_probe_t *probe);
bool atlas_common_assets_probe_pass(const atlas_common_assets_probe_t *probe, bool storage_ok);
bool atlas_common_assets_probe_warn(const atlas_common_assets_probe_t *probe, bool storage_ok);

bool atlas_common_assets_theme_has_eye_assets(const char *theme_id);
bool atlas_common_assets_theme_uses_clockwise_rotation(const char *theme_id);

void atlas_common_assets_lvgl_path_to_local(char *dst, size_t dst_size, const char *lvgl_src);
size_t atlas_common_assets_eye_lvgl_path(char *dst,
                                         size_t dst_size,
                                         const char *theme_id,
                                         const char *state,
                                         const char *eye_name);
size_t atlas_common_assets_pet_head_keyframe_lvgl_path(char *dst, size_t dst_size, const char *state);
size_t atlas_common_assets_pet_head_view_lvgl_path(char *dst,
                                                   size_t dst_size,
                                                   const char *state,
                                                   const char *view);
size_t atlas_common_assets_pet_head_transition_lvgl_path(char *dst,
                                                         size_t dst_size,
                                                         const char *transition,
                                                         uint8_t frame);
size_t atlas_common_assets_pet_head_animation_lvgl_path(char *dst,
                                                        size_t dst_size,
                                                        const char *animation,
                                                        uint8_t frame);
