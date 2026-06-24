#include "atlas_runtime.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ATLAS_RUNTIME_TURN_HISTORY 6
#define ATLAS_RUNTIME_TEXT_MAX 128
#define ATLAS_RUNTIME_STATUS_TURN_LIMIT 2
#define ATLAS_RUNTIME_TURN_JSON_MAX 1150

typedef struct {
    uint32_t seq;
    char turn_id[28];
    char kind[24];
    char phase[24];
    bool ok;
    bool finished;
    uint32_t started_ms;
    uint32_t updated_ms;
    uint32_t finished_ms;
    uint16_t duration_ms;
    size_t wav_len;
    uint8_t mic_level;
    uint32_t mic_rms;
    uint32_t mic_peak;
    int bridge_status;
    bool bridge_ok;
    bool tts_ready;
    bool played;
    size_t tts_wav_len;
    esp_err_t play_err;
    char asr_text[ATLAS_RUNTIME_TEXT_MAX];
    char reply[ATLAS_RUNTIME_TEXT_MAX];
    char error[ATLAS_RUNTIME_TEXT_MAX];
} atlas_runtime_turn_t;

static atlas_runtime_state_t s_state = ATLAS_RUNTIME_STATE_IDLE;
static char s_state_reason[64] = "boot";
static uint32_t s_state_changed_ms;
static uint32_t s_turn_seq;
static atlas_runtime_turn_t s_turns[ATLAS_RUNTIME_TURN_HISTORY];

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strlcpy(dst, src, dst_size);
}

static atlas_runtime_turn_t *current_turn(void)
{
    if (s_turn_seq == 0) {
        return NULL;
    }
    return &s_turns[(s_turn_seq - 1u) % ATLAS_RUNTIME_TURN_HISTORY];
}

static void json_escape_append(char *dst, size_t dst_size, const char *src, size_t *used)
{
    if (dst == NULL || dst_size == 0 || used == NULL || *used >= dst_size) {
        return;
    }
    const char *text = src == NULL ? "" : src;
    for (size_t i = 0; text[i] != '\0' && *used + 1 < dst_size; ++i) {
        const unsigned char ch = (unsigned char)text[i];
        if ((ch == '"' || ch == '\\') && *used + 2 < dst_size) {
            dst[(*used)++] = '\\';
            dst[(*used)++] = (char)ch;
        } else if (ch == '\n' && *used + 2 < dst_size) {
            dst[(*used)++] = '\\';
            dst[(*used)++] = 'n';
        } else if (ch == '\r' && *used + 2 < dst_size) {
            dst[(*used)++] = '\\';
            dst[(*used)++] = 'r';
        } else if (ch == '\t' && *used + 2 < dst_size) {
            dst[(*used)++] = '\\';
            dst[(*used)++] = 't';
        } else if (ch >= 0x20) {
            dst[(*used)++] = (char)ch;
        }
    }
    dst[*used] = '\0';
}

static void appendf(char *dst, size_t dst_size, size_t *used, const char *fmt, ...)
{
    if (dst == NULL || dst_size == 0 || used == NULL || *used >= dst_size) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    const int wrote = vsnprintf(dst + *used, dst_size - *used, fmt, args);
    va_end(args);
    if (wrote < 0) {
        return;
    }
    const size_t available = dst_size - *used;
    if ((size_t)wrote >= available) {
        *used = dst_size - 1;
    } else {
        *used += (size_t)wrote;
    }
}

static void append_json_string(char *dst, size_t dst_size, size_t *used, const char *text)
{
    appendf(dst, dst_size, used, "\"");
    json_escape_append(dst, dst_size, text, used);
    appendf(dst, dst_size, used, "\"");
}

static void write_turn_json(char *dst, size_t dst_size, const atlas_runtime_turn_t *turn)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (turn == NULL) {
        return;
    }
    size_t used = 0;
    appendf(dst,
            dst_size,
            &used,
            "{\"seq\":%" PRIu32 ",\"turn_id\":",
            turn->seq);
    append_json_string(dst, dst_size, &used, turn->turn_id);
    appendf(dst, dst_size, &used, ",\"kind\":");
    append_json_string(dst, dst_size, &used, turn->kind);
    appendf(dst,
            dst_size,
            &used,
            ",\"phase\":\"%s\",\"ok\":%s,\"finished\":%s,"
            "\"started_ms\":%" PRIu32 ",\"updated_ms\":%" PRIu32 ",\"finished_ms\":%" PRIu32 ","
            "\"duration_ms\":%" PRIu16 ",\"wav_bytes\":%u,"
            "\"mic_level\":%" PRIu8 ",\"mic_rms\":%" PRIu32 ",\"mic_peak\":%" PRIu32 ","
            "\"bridge_status\":%d,\"bridge_ok\":%s,\"tts_ready\":%s,\"tts_bytes\":%u,\"played\":%s,"
            "\"play_error\":\"%s\",",
            turn->phase,
            turn->ok ? "true" : "false",
            turn->finished ? "true" : "false",
            turn->started_ms,
            turn->updated_ms,
            turn->finished_ms,
            turn->duration_ms,
            (unsigned)turn->wav_len,
            turn->mic_level,
            turn->mic_rms,
            turn->mic_peak,
            turn->bridge_status,
            turn->bridge_ok ? "true" : "false",
            turn->tts_ready ? "true" : "false",
            (unsigned)turn->tts_wav_len,
            turn->played ? "true" : "false",
            esp_err_to_name(turn->play_err));
    appendf(dst, dst_size, &used, "\"asr_text\":");
    append_json_string(dst, dst_size, &used, turn->asr_text);
    appendf(dst, dst_size, &used, ",\"reply\":");
    append_json_string(dst, dst_size, &used, turn->reply);
    appendf(dst, dst_size, &used, ",\"error\":");
    append_json_string(dst, dst_size, &used, turn->error);
    appendf(dst, dst_size, &used, "}");

    if (used + 1 >= dst_size) {
        used = 0;
        appendf(dst,
                dst_size,
                &used,
                "{\"seq\":%" PRIu32 ",\"turn_id\":",
                turn->seq);
        append_json_string(dst, dst_size, &used, turn->turn_id);
        appendf(dst,
                dst_size,
                &used,
                ",\"kind\":");
        append_json_string(dst, dst_size, &used, turn->kind);
        appendf(dst,
                dst_size,
                &used,
                ",\"phase\":\"%s\",\"ok\":%s,\"finished\":%s,\"bridge_status\":%d,"
                "\"played\":%s,\"play_error\":\"%s\",\"error\":",
                turn->phase,
                turn->ok ? "true" : "false",
                turn->finished ? "true" : "false",
                turn->bridge_status,
                turn->played ? "true" : "false",
                esp_err_to_name(turn->play_err));
        append_json_string(dst, dst_size, &used, turn->error);
        appendf(dst, dst_size, &used, ",\"compact\":true}");
    }
}

static void turn_set_phase(atlas_runtime_turn_t *turn, const char *phase, uint32_t now_ms)
{
    if (turn == NULL) {
        return;
    }
    copy_text(turn->phase, sizeof(turn->phase), phase);
    turn->updated_ms = now_ms;
}

void atlas_runtime_init(void)
{
    memset(s_turns, 0, sizeof(s_turns));
    s_state = ATLAS_RUNTIME_STATE_IDLE;
    copy_text(s_state_reason, sizeof(s_state_reason), "boot");
    s_state_changed_ms = 0;
    s_turn_seq = 0;
}

atlas_runtime_state_t atlas_runtime_get_state(void)
{
    return s_state;
}

const char *atlas_runtime_state_name(atlas_runtime_state_t state)
{
    switch (state) {
    case ATLAS_RUNTIME_STATE_IDLE:
        return "idle";
    case ATLAS_RUNTIME_STATE_LISTENING:
        return "listening";
    case ATLAS_RUNTIME_STATE_RECORDING:
        return "recording";
    case ATLAS_RUNTIME_STATE_TRANSCRIBING:
        return "transcribing";
    case ATLAS_RUNTIME_STATE_THINKING:
        return "thinking";
    case ATLAS_RUNTIME_STATE_TOOL_RUNNING:
        return "tool_running";
    case ATLAS_RUNTIME_STATE_SPEAKING:
        return "speaking";
    case ATLAS_RUNTIME_STATE_COOLDOWN:
        return "cooldown";
    case ATLAS_RUNTIME_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

const char *atlas_runtime_get_reason(void)
{
    return s_state_reason;
}

uint32_t atlas_runtime_get_state_changed_ms(void)
{
    return s_state_changed_ms;
}

void atlas_runtime_set_state(atlas_runtime_state_t state, const char *reason, uint32_t now_ms)
{
    s_state = state;
    copy_text(s_state_reason, sizeof(s_state_reason), reason == NULL ? "" : reason);
    s_state_changed_ms = now_ms;
}

void atlas_runtime_turn_begin(const char *kind, uint16_t duration_ms, uint32_t now_ms)
{
    s_turn_seq++;
    atlas_runtime_turn_t *turn = current_turn();
    if (turn == NULL) {
        return;
    }
    memset(turn, 0, sizeof(*turn));
    turn->seq = s_turn_seq;
    snprintf(turn->turn_id, sizeof(turn->turn_id), "turn-%" PRIu32, s_turn_seq);
    copy_text(turn->kind, sizeof(turn->kind), kind == NULL ? "voice_turn" : kind);
    copy_text(turn->phase, sizeof(turn->phase), "begin");
    turn->started_ms = now_ms;
    turn->updated_ms = now_ms;
    turn->duration_ms = duration_ms;
    turn->play_err = ESP_ERR_NOT_FOUND;
    atlas_runtime_set_state(ATLAS_RUNTIME_STATE_LISTENING, "turn begin", now_ms);
}

void atlas_runtime_turn_note_audio(size_t wav_len,
                                   uint8_t mic_level,
                                   uint32_t mic_rms,
                                   uint32_t mic_peak,
                                   uint32_t now_ms)
{
    atlas_runtime_turn_t *turn = current_turn();
    if (turn == NULL) {
        return;
    }
    turn->wav_len = wav_len;
    turn->mic_level = mic_level;
    turn->mic_rms = mic_rms;
    turn->mic_peak = mic_peak;
    turn_set_phase(turn, "audio_captured", now_ms);
}

void atlas_runtime_turn_note_bridge(int bridge_status,
                                    bool bridge_ok,
                                    const char *asr_text,
                                    const char *reply,
                                    const char *error,
                                    uint32_t now_ms)
{
    atlas_runtime_turn_t *turn = current_turn();
    if (turn == NULL) {
        return;
    }
    turn->bridge_status = bridge_status;
    turn->bridge_ok = bridge_ok;
    copy_text(turn->asr_text, sizeof(turn->asr_text), asr_text);
    copy_text(turn->reply, sizeof(turn->reply), reply);
    if (error != NULL && error[0] != '\0') {
        copy_text(turn->error, sizeof(turn->error), error);
    }
    turn_set_phase(turn, bridge_ok ? "bridge_ok" : "bridge_error", now_ms);
}

void atlas_runtime_turn_note_tts(bool tts_ready,
                                 size_t tts_wav_len,
                                 esp_err_t play_err,
                                 uint32_t now_ms)
{
    atlas_runtime_turn_t *turn = current_turn();
    if (turn == NULL) {
        return;
    }
    turn->tts_ready = tts_ready;
    turn->tts_wav_len = tts_wav_len;
    turn->play_err = play_err;
    turn->played = play_err == ESP_OK;
    turn_set_phase(turn, turn->played ? "played" : "play_error", now_ms);
}

void atlas_runtime_turn_finish(bool ok, const char *error, uint32_t now_ms)
{
    atlas_runtime_turn_t *turn = current_turn();
    if (turn == NULL) {
        return;
    }
    turn->ok = ok;
    turn->finished = true;
    turn->finished_ms = now_ms;
    turn->updated_ms = now_ms;
    copy_text(turn->phase, sizeof(turn->phase), ok ? "done" : "error");
    if (error != NULL && error[0] != '\0') {
        copy_text(turn->error, sizeof(turn->error), error);
    }
    atlas_runtime_set_state(ok ? ATLAS_RUNTIME_STATE_IDLE : ATLAS_RUNTIME_STATE_ERROR,
                            ok ? "turn done" : turn->error,
                            now_ms);
}

size_t atlas_runtime_write_json(char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    size_t used = 0;
    appendf(dst, dst_size, &used,
            "{\"state\":\"%s\",\"state_reason\":",
            atlas_runtime_state_name(s_state));
    append_json_string(dst, dst_size, &used, s_state_reason);
    const uint32_t total = s_turn_seq < ATLAS_RUNTIME_TURN_HISTORY ? s_turn_seq : ATLAS_RUNTIME_TURN_HISTORY;
    const uint32_t emit_limit = total < ATLAS_RUNTIME_STATUS_TURN_LIMIT ? total : ATLAS_RUNTIME_STATUS_TURN_LIMIT;
    appendf(dst,
            dst_size,
            &used,
            ",\"state_changed_ms\":%" PRIu32 ",\"turn_seq\":%" PRIu32
            ",\"turn_history_size\":%" PRIu32 ",\"turns_omitted\":%" PRIu32 ",\"turns\":[",
            s_state_changed_ms,
            s_turn_seq,
            total,
            total > emit_limit ? total - emit_limit : 0);

    bool first = true;
    uint32_t emitted = 0;
    uint32_t dropped_for_space = 0;
    char turn_json[ATLAS_RUNTIME_TURN_JSON_MAX];
    for (uint32_t i = 0; i < emit_limit; ++i) {
        const uint32_t seq = s_turn_seq - i;
        const atlas_runtime_turn_t *turn = &s_turns[(seq - 1u) % ATLAS_RUNTIME_TURN_HISTORY];
        if (turn->seq == 0) {
            continue;
        }
        write_turn_json(turn_json, sizeof(turn_json), turn);
        const size_t turn_len = strlen(turn_json);
        const size_t comma_len = first ? 0u : 1u;
        const size_t close_reserve = 96u;
        if (turn_len == 0 || used + comma_len + turn_len + close_reserve >= dst_size) {
            dropped_for_space = emit_limit - i;
            break;
        }
        appendf(dst, dst_size, &used, "%s%s", first ? "" : ",", turn_json);
        first = false;
        emitted++;
    }
    appendf(dst,
            dst_size,
            &used,
            "],\"turns_emitted\":%" PRIu32 ",\"turns_dropped_for_space\":%" PRIu32 "}",
            emitted,
            dropped_for_space);
    return used;
}
