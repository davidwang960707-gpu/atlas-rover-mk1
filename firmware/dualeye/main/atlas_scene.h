#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atlas_audio.h"
#include "atlas_audio_service.h"
#include "atlas_config.h"
#include "atlas_runtime.h"
#include "atlas_ui.h"
#include "atlas_wifi.h"

typedef enum {
    ATLAS_SCENE_BOOT = 0,
    ATLAS_SCENE_WIFI_CONFIG,
    ATLAS_SCENE_IDLE,
    ATLAS_SCENE_MONITORING,
    ATLAS_SCENE_LISTENING,
    ATLAS_SCENE_RECORDING,
    ATLAS_SCENE_TRANSCRIBING,
    ATLAS_SCENE_THINKING,
    ATLAS_SCENE_TOOL_RUNNING,
    ATLAS_SCENE_SPEAKING,
    ATLAS_SCENE_COOLDOWN,
    ATLAS_SCENE_APP,
    ATLAS_SCENE_AUDIO_TEST,
    ATLAS_SCENE_BRAIN_OFFLINE,
    ATLAS_SCENE_MOVING,
    ATLAS_SCENE_CHARGING,
    ATLAS_SCENE_ERROR,
} atlas_scene_kind_t;

typedef struct {
    atlas_scene_kind_t kind;
    atlas_page_t page;
    atlas_expression_t expression;
    uint8_t audio_level;
    bool overlay;
    bool needs_attention;
    char state[28];
    char title[48];
    char subtitle[72];
    char hint[96];
    char left_role[40];
    char right_role[40];
    char severity[16];
} atlas_scene_snapshot_t;

const char *atlas_scene_kind_name(atlas_scene_kind_t kind);
const char *atlas_scene_kind_label_zh(atlas_scene_kind_t kind);

void atlas_scene_resolve(const atlas_ui_state_t *ui,
                         const atlas_config_t *config,
                         const atlas_wifi_status_t *wifi,
                         const atlas_audio_status_t *audio,
                         const atlas_audio_service_status_t *audio_service,
                         atlas_runtime_state_t runtime_state,
                         const char *runtime_reason,
                         uint32_t now_ms,
                         atlas_scene_snapshot_t *scene);

size_t atlas_scene_write_json(const atlas_scene_snapshot_t *scene, char *dst, size_t dst_size);
