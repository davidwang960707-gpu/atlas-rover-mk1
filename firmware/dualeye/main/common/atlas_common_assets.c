#include "common/atlas_common_assets.h"

#include <stdio.h>
#include <string.h>

static uint8_t count_true(bool value)
{
    return value ? 1u : 0u;
}

const char *atlas_common_assets_manifest_protocol(void)
{
    return ATLAS_COMMON_ASSET_MANIFEST_PROTOCOL;
}

const char *atlas_common_assets_resource_version(void)
{
    return ATLAS_COMMON_RESOURCE_VERSION;
}

const char *atlas_common_assets_font_version(void)
{
    return ATLAS_COMMON_FONT_VERSION;
}

bool atlas_common_assets_file_exists(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

void atlas_common_assets_probe(atlas_common_assets_probe_t *probe)
{
    if (probe == NULL) {
        return;
    }
    *probe = (atlas_common_assets_probe_t) {
        .eye_manifest = atlas_common_assets_file_exists(ATLAS_COMMON_ASSET_EYE_MANIFEST_PATH),
        .eye_pet_idle = atlas_common_assets_file_exists(ATLAS_COMMON_ASSET_EYE_PET_IDLE_LEFT_PATH),
        .eye_goggle_idle = atlas_common_assets_file_exists(ATLAS_COMMON_ASSET_EYE_GOGGLE_IDLE_LEFT_PATH),
        .eye_tomoe_idle = atlas_common_assets_file_exists(ATLAS_COMMON_ASSET_EYE_TOMOE_IDLE_LEFT_PATH),
        .eye_no_smoking_idle = atlas_common_assets_file_exists(ATLAS_COMMON_ASSET_EYE_NO_SMOKING_IDLE_LEFT_PATH),
        .pet_head_manifest = atlas_common_assets_file_exists(ATLAS_COMMON_ASSET_PET_HEAD_MANIFEST_PATH),
        .pet_head_idle = atlas_common_assets_file_exists(ATLAS_COMMON_ASSET_PET_HEAD_IDLE_PATH),
        .pet_head_speak = atlas_common_assets_file_exists(ATLAS_COMMON_ASSET_PET_HEAD_SPEAK_FRAME0_PATH),
        .pet_head_view = atlas_common_assets_file_exists(ATLAS_COMMON_ASSET_PET_HEAD_IDLE_YAW_L30_PATH) &&
                         atlas_common_assets_file_exists(ATLAS_COMMON_ASSET_PET_HEAD_THINK_YAW_R15_PATH),
        .pet_head_turn = atlas_common_assets_file_exists(ATLAS_COMMON_ASSET_PET_HEAD_TURN_C_TO_L30_FRAME0_PATH) &&
                         atlas_common_assets_file_exists(ATLAS_COMMON_ASSET_PET_HEAD_TURN_R30_TO_C_FRAME5_PATH),
    };
    probe->ok_count = count_true(probe->eye_manifest) +
                      count_true(probe->eye_pet_idle) +
                      count_true(probe->eye_goggle_idle) +
                      count_true(probe->eye_tomoe_idle) +
                      count_true(probe->eye_no_smoking_idle) +
                      count_true(probe->pet_head_manifest) +
                      count_true(probe->pet_head_idle) +
                      count_true(probe->pet_head_speak) +
                      count_true(probe->pet_head_view) +
                      count_true(probe->pet_head_turn);
}

bool atlas_common_assets_probe_pass(const atlas_common_assets_probe_t *probe, bool storage_ok)
{
    return storage_ok && probe != NULL && probe->ok_count >= 9u;
}

bool atlas_common_assets_probe_warn(const atlas_common_assets_probe_t *probe, bool storage_ok)
{
    return storage_ok && probe != NULL && probe->ok_count > 0u && probe->ok_count < 9u;
}

bool atlas_common_assets_theme_has_eye_assets(const char *theme_id)
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

bool atlas_common_assets_theme_uses_clockwise_rotation(const char *theme_id)
{
    return theme_id != NULL && strcmp(theme_id, "tomoe_spin") == 0;
}

void atlas_common_assets_lvgl_path_to_local(char *dst, size_t dst_size, const char *lvgl_src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (lvgl_src == NULL) {
        dst[0] = '\0';
        return;
    }

    const char *rel = strchr(lvgl_src, ':');
    rel = rel == NULL ? lvgl_src : rel + 1;
    while (*rel == '/') {
        ++rel;
    }
    snprintf(dst, dst_size, "%s/%s", ATLAS_COMMON_ASSET_MOUNT_PATH, rel);
}

size_t atlas_common_assets_eye_lvgl_path(char *dst,
                                         size_t dst_size,
                                         const char *theme_id,
                                         const char *state,
                                         const char *eye_name)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    return (size_t)snprintf(dst,
                            dst_size,
                            "%c:/atlas_eyes/%s/%s/%s.png",
                            ATLAS_COMMON_ASSET_LVGL_LETTER,
                            theme_id == NULL ? "" : theme_id,
                            state == NULL ? "idle" : state,
                            eye_name == NULL ? "left" : eye_name);
}

size_t atlas_common_assets_pet_head_keyframe_lvgl_path(char *dst, size_t dst_size, const char *state)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    return (size_t)snprintf(dst,
                            dst_size,
                            "%c:/atlas_pet_head/keyframes/%s.png",
                            ATLAS_COMMON_ASSET_LVGL_LETTER,
                            state == NULL || state[0] == '\0' ? "idle" : state);
}

size_t atlas_common_assets_pet_head_view_lvgl_path(char *dst,
                                                   size_t dst_size,
                                                   const char *state,
                                                   const char *view)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    return (size_t)snprintf(dst,
                            dst_size,
                            "%c:/atlas_pet_head/views/%s/%s.png",
                            ATLAS_COMMON_ASSET_LVGL_LETTER,
                            state == NULL || state[0] == '\0' ? "idle" : state,
                            view == NULL || view[0] == '\0' ? "yaw_c" : view);
}

size_t atlas_common_assets_pet_head_transition_lvgl_path(char *dst,
                                                         size_t dst_size,
                                                         const char *transition,
                                                         uint8_t frame)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    return (size_t)snprintf(dst,
                            dst_size,
                            "%c:/atlas_pet_head/transitions/%s/frame_%02u.png",
                            ATLAS_COMMON_ASSET_LVGL_LETTER,
                            transition == NULL ? "" : transition,
                            frame);
}

size_t atlas_common_assets_pet_head_animation_lvgl_path(char *dst,
                                                        size_t dst_size,
                                                        const char *animation,
                                                        uint8_t frame)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    return (size_t)snprintf(dst,
                            dst_size,
                            "%c:/atlas_pet_head/animations/%s/frame_%02u.png",
                            ATLAS_COMMON_ASSET_LVGL_LETTER,
                            animation == NULL ? "" : animation,
                            frame);
}
