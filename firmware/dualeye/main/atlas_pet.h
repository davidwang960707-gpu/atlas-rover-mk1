#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "atlas_expression.h"

typedef enum {
    ATLAS_PET_PHASE_IDLE = 0,
    ATLAS_PET_PHASE_HAPPY,
    ATLAS_PET_PHASE_CURIOUS,
    ATLAS_PET_PHASE_SLEEPY,
    ATLAS_PET_PHASE_SLEEPING,
    ATLAS_PET_PHASE_PATROL,
    ATLAS_PET_PHASE_MUSIC,
    ATLAS_PET_PHASE_STORY,
    ATLAS_PET_PHASE_CHAT,
    ATLAS_PET_PHASE_LOW_ENERGY,
} atlas_pet_phase_t;

typedef enum {
    ATLAS_PET_EVENT_INTERACTION = 0,
    ATLAS_PET_EVENT_TOUCH,
    ATLAS_PET_EVENT_PLAY,
    ATLAS_PET_EVENT_FEED,
    ATLAS_PET_EVENT_VOICE_LISTEN,
    ATLAS_PET_EVENT_THINK,
    ATLAS_PET_EVENT_SPEAK,
    ATLAS_PET_EVENT_PATROL,
    ATLAS_PET_EVENT_MUSIC,
    ATLAS_PET_EVENT_STORY,
    ATLAS_PET_EVENT_CHAT,
    ATLAS_PET_EVENT_STOP,
    ATLAS_PET_EVENT_REST,
    ATLAS_PET_EVENT_CHARGE,
    ATLAS_PET_EVENT_ERROR,
} atlas_pet_event_t;

typedef struct {
    atlas_pet_phase_t phase;
    uint8_t mood;
    uint8_t energy;
    uint8_t curiosity;
    bool asleep;
    uint32_t last_interaction_ms;
    uint32_t last_decay_ms;
    uint32_t phase_until_ms;
    char asset_id[24];
} atlas_pet_state_t;

void atlas_pet_init(atlas_pet_state_t *pet);
void atlas_pet_tick(atlas_pet_state_t *pet, uint32_t now_ms);
void atlas_pet_handle_event(atlas_pet_state_t *pet, atlas_pet_event_t event, uint32_t now_ms);

const char *atlas_pet_phase_name(atlas_pet_phase_t phase);
const char *atlas_pet_phase_label_zh(atlas_pet_phase_t phase);
const char *atlas_pet_event_name(atlas_pet_event_t event);
bool atlas_pet_event_from_name(const char *name, atlas_pet_event_t *event);
atlas_expression_t atlas_pet_expression(const atlas_pet_state_t *pet);
bool atlas_pet_phase_is_character_active(const atlas_pet_state_t *pet);
