#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    ATLAS_RUNTIME_STATE_IDLE = 0,
    ATLAS_RUNTIME_STATE_LISTENING,
    ATLAS_RUNTIME_STATE_RECORDING,
    ATLAS_RUNTIME_STATE_TRANSCRIBING,
    ATLAS_RUNTIME_STATE_THINKING,
    ATLAS_RUNTIME_STATE_TOOL_RUNNING,
    ATLAS_RUNTIME_STATE_SPEAKING,
    ATLAS_RUNTIME_STATE_COOLDOWN,
    ATLAS_RUNTIME_STATE_ERROR,
} atlas_runtime_state_t;

void atlas_runtime_init(void);
atlas_runtime_state_t atlas_runtime_get_state(void);
const char *atlas_runtime_state_name(atlas_runtime_state_t state);
const char *atlas_runtime_get_reason(void);
uint32_t atlas_runtime_get_state_changed_ms(void);
void atlas_runtime_set_state(atlas_runtime_state_t state, const char *reason, uint32_t now_ms);

void atlas_runtime_turn_begin(const char *kind, uint16_t duration_ms, uint32_t now_ms);
void atlas_runtime_turn_note_audio(size_t wav_len,
                                   uint8_t mic_level,
                                   uint32_t mic_rms,
                                   uint32_t mic_peak,
                                   uint32_t now_ms);
void atlas_runtime_turn_note_bridge(int bridge_status,
                                    bool bridge_ok,
                                    const char *asr_text,
                                    const char *reply,
                                    const char *error,
                                    uint32_t now_ms);
void atlas_runtime_turn_note_tts(bool tts_ready,
                                 size_t tts_wav_len,
                                 esp_err_t play_err,
                                 uint32_t now_ms);
void atlas_runtime_turn_finish(bool ok, const char *error, uint32_t now_ms);
size_t atlas_runtime_write_json(char *dst, size_t dst_size);
