#include "atlas_pet.h"

#include <string.h>
#include <strings.h>

#define ATLAS_PET_SLEEPY_AFTER_MS (3u * 60u * 1000u)
#define ATLAS_PET_SLEEP_AFTER_MS (8u * 60u * 1000u)
#define ATLAS_PET_DECAY_STEP_MS (30u * 1000u)

static uint8_t clamp_metric_i16(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return (uint8_t)value;
}

static void add_metric(uint8_t *metric, int delta)
{
    if (metric == NULL) {
        return;
    }
    *metric = clamp_metric_i16((int)*metric + delta);
}

static void begin_phase(atlas_pet_state_t *pet,
                        atlas_pet_phase_t phase,
                        uint32_t now_ms,
                        uint32_t duration_ms,
                        bool counts_as_interaction)
{
    if (pet == NULL) {
        return;
    }

    pet->phase = phase;
    pet->phase_until_ms = duration_ms == 0 ? 0 : now_ms + duration_ms;
    if (counts_as_interaction) {
        pet->last_interaction_ms = now_ms;
        pet->asleep = false;
    }
}

void atlas_pet_init(atlas_pet_state_t *pet)
{
    if (pet == NULL) {
        return;
    }

    *pet = (atlas_pet_state_t) {
        .phase = ATLAS_PET_PHASE_IDLE,
        .mood = 72,
        .energy = 82,
        .curiosity = 58,
        .asleep = false,
        .last_interaction_ms = 0,
        .last_decay_ms = 0,
        .phase_until_ms = 0,
    };
    strlcpy(pet->asset_id, "pet/programmer", sizeof(pet->asset_id));
}

void atlas_pet_tick(atlas_pet_state_t *pet, uint32_t now_ms)
{
    if (pet == NULL) {
        return;
    }

    if (pet->last_decay_ms == 0) {
        pet->last_decay_ms = now_ms;
    }

    while (now_ms - pet->last_decay_ms >= ATLAS_PET_DECAY_STEP_MS) {
        pet->last_decay_ms += ATLAS_PET_DECAY_STEP_MS;
        add_metric(&pet->energy, -1);
        add_metric(&pet->curiosity, 2);
        if (now_ms - pet->last_interaction_ms > ATLAS_PET_SLEEPY_AFTER_MS) {
            add_metric(&pet->mood, -1);
        }
    }

    const uint32_t idle_ms = now_ms - pet->last_interaction_ms;
    if (pet->energy <= 8 || idle_ms >= ATLAS_PET_SLEEP_AFTER_MS) {
        pet->asleep = true;
        pet->phase = ATLAS_PET_PHASE_SLEEPING;
        pet->phase_until_ms = 0;
        return;
    }
    if (pet->energy <= 20) {
        pet->asleep = false;
        pet->phase = ATLAS_PET_PHASE_LOW_ENERGY;
        pet->phase_until_ms = 0;
        return;
    }
    if (idle_ms >= ATLAS_PET_SLEEPY_AFTER_MS) {
        pet->asleep = false;
        pet->phase = ATLAS_PET_PHASE_SLEEPY;
        pet->phase_until_ms = 0;
        return;
    }

    if (pet->phase_until_ms != 0 && now_ms >= pet->phase_until_ms) {
        pet->phase = ATLAS_PET_PHASE_IDLE;
        pet->phase_until_ms = 0;
        pet->asleep = false;
    }

    if (pet->phase == ATLAS_PET_PHASE_IDLE && pet->curiosity >= 88) {
        add_metric(&pet->curiosity, -10);
        begin_phase(pet, ATLAS_PET_PHASE_CURIOUS, now_ms, 3500, false);
    }
}

void atlas_pet_handle_event(atlas_pet_state_t *pet, atlas_pet_event_t event, uint32_t now_ms)
{
    if (pet == NULL) {
        return;
    }

    switch (event) {
    case ATLAS_PET_EVENT_TOUCH:
        add_metric(&pet->mood, 12);
        if (pet->energy <= 10u || pet->phase == ATLAS_PET_PHASE_SLEEPY || pet->phase == ATLAS_PET_PHASE_SLEEPING) {
            add_metric(&pet->energy, 30);
        } else {
            add_metric(&pet->energy, -1);
        }
        add_metric(&pet->curiosity, 5);
        begin_phase(pet, ATLAS_PET_PHASE_HAPPY, now_ms, 6000, true);
        break;
    case ATLAS_PET_EVENT_PLAY:
        add_metric(&pet->mood, 9);
        add_metric(&pet->energy, -4);
        add_metric(&pet->curiosity, 8);
        begin_phase(pet, ATLAS_PET_PHASE_HAPPY, now_ms, 5500, true);
        break;
    case ATLAS_PET_EVENT_FEED:
        add_metric(&pet->mood, 3);
        add_metric(&pet->energy, 18);
        add_metric(&pet->curiosity, -2);
        begin_phase(pet, ATLAS_PET_PHASE_HAPPY, now_ms, 4500, true);
        break;
    case ATLAS_PET_EVENT_VOICE_LISTEN:
        add_metric(&pet->mood, 2);
        add_metric(&pet->curiosity, 7);
        begin_phase(pet, ATLAS_PET_PHASE_CURIOUS, now_ms, 4500, true);
        break;
    case ATLAS_PET_EVENT_THINK:
        add_metric(&pet->curiosity, 4);
        begin_phase(pet, ATLAS_PET_PHASE_CURIOUS, now_ms, 4000, true);
        break;
    case ATLAS_PET_EVENT_SPEAK:
        add_metric(&pet->mood, 4);
        add_metric(&pet->energy, -2);
        begin_phase(pet, ATLAS_PET_PHASE_CHAT, now_ms, 7000, true);
        break;
    case ATLAS_PET_EVENT_PATROL:
        add_metric(&pet->mood, 5);
        add_metric(&pet->energy, -8);
        add_metric(&pet->curiosity, 7);
        begin_phase(pet, ATLAS_PET_PHASE_PATROL, now_ms, 6500, true);
        break;
    case ATLAS_PET_EVENT_MUSIC:
        add_metric(&pet->mood, 8);
        add_metric(&pet->energy, -3);
        add_metric(&pet->curiosity, 2);
        begin_phase(pet, ATLAS_PET_PHASE_MUSIC, now_ms, 10000, true);
        break;
    case ATLAS_PET_EVENT_STORY:
        add_metric(&pet->mood, 7);
        add_metric(&pet->energy, -4);
        add_metric(&pet->curiosity, 6);
        begin_phase(pet, ATLAS_PET_PHASE_STORY, now_ms, 10000, true);
        break;
    case ATLAS_PET_EVENT_CHAT:
        add_metric(&pet->mood, 5);
        add_metric(&pet->energy, -3);
        add_metric(&pet->curiosity, 4);
        begin_phase(pet, ATLAS_PET_PHASE_CHAT, now_ms, 8000, true);
        break;
    case ATLAS_PET_EVENT_STOP:
        add_metric(&pet->energy, 1);
        begin_phase(pet, ATLAS_PET_PHASE_IDLE, now_ms, 0, true);
        break;
    case ATLAS_PET_EVENT_REST:
        add_metric(&pet->mood, 2);
        add_metric(&pet->energy, 15);
        add_metric(&pet->curiosity, -4);
        begin_phase(pet, ATLAS_PET_PHASE_SLEEPING, now_ms, 8000, true);
        pet->asleep = true;
        break;
    case ATLAS_PET_EVENT_CHARGE:
        add_metric(&pet->mood, 2);
        add_metric(&pet->energy, 20);
        begin_phase(pet, ATLAS_PET_PHASE_IDLE, now_ms, 0, true);
        break;
    case ATLAS_PET_EVENT_ERROR:
        add_metric(&pet->mood, -10);
        add_metric(&pet->energy, -3);
        begin_phase(pet,
                    pet->energy <= 20 ? ATLAS_PET_PHASE_LOW_ENERGY : ATLAS_PET_PHASE_CURIOUS,
                    now_ms,
                    4500,
                    true);
        break;
    case ATLAS_PET_EVENT_INTERACTION:
    default:
        add_metric(&pet->mood, 2);
        add_metric(&pet->curiosity, 2);
        begin_phase(pet, ATLAS_PET_PHASE_CURIOUS, now_ms, 3000, true);
        break;
    }
}

const char *atlas_pet_phase_name(atlas_pet_phase_t phase)
{
    switch (phase) {
    case ATLAS_PET_PHASE_HAPPY:
        return "happy";
    case ATLAS_PET_PHASE_CURIOUS:
        return "curious";
    case ATLAS_PET_PHASE_SLEEPY:
        return "sleepy";
    case ATLAS_PET_PHASE_SLEEPING:
        return "sleeping";
    case ATLAS_PET_PHASE_PATROL:
        return "patrol";
    case ATLAS_PET_PHASE_MUSIC:
        return "music";
    case ATLAS_PET_PHASE_STORY:
        return "story";
    case ATLAS_PET_PHASE_CHAT:
        return "chat";
    case ATLAS_PET_PHASE_LOW_ENERGY:
        return "low_energy";
    case ATLAS_PET_PHASE_IDLE:
    default:
        return "idle";
    }
}

const char *atlas_pet_phase_label_zh(atlas_pet_phase_t phase)
{
    switch (phase) {
    case ATLAS_PET_PHASE_HAPPY:
        return "开心";
    case ATLAS_PET_PHASE_CURIOUS:
        return "好奇";
    case ATLAS_PET_PHASE_SLEEPY:
        return "困了";
    case ATLAS_PET_PHASE_SLEEPING:
        return "睡觉";
    case ATLAS_PET_PHASE_PATROL:
        return "巡游";
    case ATLAS_PET_PHASE_MUSIC:
        return "听音乐";
    case ATLAS_PET_PHASE_STORY:
        return "讲故事";
    case ATLAS_PET_PHASE_CHAT:
        return "对话";
    case ATLAS_PET_PHASE_LOW_ENERGY:
        return "低能量";
    case ATLAS_PET_PHASE_IDLE:
    default:
        return "待机";
    }
}

const char *atlas_pet_event_name(atlas_pet_event_t event)
{
    switch (event) {
    case ATLAS_PET_EVENT_TOUCH:
        return "touch";
    case ATLAS_PET_EVENT_PLAY:
        return "play";
    case ATLAS_PET_EVENT_FEED:
        return "feed";
    case ATLAS_PET_EVENT_VOICE_LISTEN:
        return "voice_listen";
    case ATLAS_PET_EVENT_THINK:
        return "think";
    case ATLAS_PET_EVENT_SPEAK:
        return "speak";
    case ATLAS_PET_EVENT_PATROL:
        return "patrol";
    case ATLAS_PET_EVENT_MUSIC:
        return "music";
    case ATLAS_PET_EVENT_STORY:
        return "story";
    case ATLAS_PET_EVENT_CHAT:
        return "chat";
    case ATLAS_PET_EVENT_STOP:
        return "stop";
    case ATLAS_PET_EVENT_REST:
        return "rest";
    case ATLAS_PET_EVENT_CHARGE:
        return "charge";
    case ATLAS_PET_EVENT_ERROR:
        return "error";
    case ATLAS_PET_EVENT_INTERACTION:
    default:
        return "interaction";
    }
}

bool atlas_pet_event_from_name(const char *name, atlas_pet_event_t *event)
{
    if (name == NULL || event == NULL) {
        return false;
    }

    for (atlas_pet_event_t candidate = ATLAS_PET_EVENT_INTERACTION;
         candidate <= ATLAS_PET_EVENT_ERROR;
         ++candidate) {
        if (strcmp(name, atlas_pet_event_name(candidate)) == 0) {
            *event = candidate;
            return true;
        }
    }

    if (strcasecmp(name, "move") == 0 || strcasecmp(name, "cruise") == 0) {
        *event = ATLAS_PET_EVENT_PATROL;
        return true;
    }
    if (strcasecmp(name, "wake") == 0 || strcasecmp(name, "awake") == 0 || strcmp(name, "唤醒") == 0) {
        *event = ATLAS_PET_EVENT_TOUCH;
        return true;
    }
    if (strcasecmp(name, "sleep") == 0) {
        *event = ATLAS_PET_EVENT_REST;
        return true;
    }
    if (strcasecmp(name, "talk") == 0) {
        *event = ATLAS_PET_EVENT_CHAT;
        return true;
    }
    return false;
}

atlas_expression_t atlas_pet_expression(const atlas_pet_state_t *pet)
{
    if (pet == NULL) {
        return ATLAS_EXPR_IDLE;
    }

    switch (pet->phase) {
    case ATLAS_PET_PHASE_HAPPY:
        return pet->mood >= 92 ? ATLAS_EXPR_LOVE : ATLAS_EXPR_HAPPY;
    case ATLAS_PET_PHASE_CURIOUS:
        return ATLAS_EXPR_CURIOUS;
    case ATLAS_PET_PHASE_SLEEPY:
    case ATLAS_PET_PHASE_SLEEPING:
    case ATLAS_PET_PHASE_LOW_ENERGY:
        return ATLAS_EXPR_SLEEPY;
    case ATLAS_PET_PHASE_PATROL:
        return ATLAS_EXPR_MOVING;
    case ATLAS_PET_PHASE_MUSIC:
    case ATLAS_PET_PHASE_STORY:
    case ATLAS_PET_PHASE_CHAT:
        return ATLAS_EXPR_SPEAKING;
    case ATLAS_PET_PHASE_IDLE:
    default:
        if (pet->mood < 18) {
            return ATLAS_EXPR_CRY;
        }
        if (pet->curiosity > 82) {
            return ATLAS_EXPR_CURIOUS;
        }
        return ATLAS_EXPR_IDLE;
    }
}

bool atlas_pet_phase_is_character_active(const atlas_pet_state_t *pet)
{
    if (pet == NULL) {
        return false;
    }
    return pet->phase != ATLAS_PET_PHASE_IDLE ||
           pet->mood < 18 ||
           pet->curiosity > 82;
}
