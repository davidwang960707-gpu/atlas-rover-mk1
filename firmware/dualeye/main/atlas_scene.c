#include "atlas_scene.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "atlas_display.h"

#define ATLAS_WIFI_CONFIG_BOOT_HINT_MS 9000u
#define ATLAS_MANUAL_PAGE_OVERRIDE_MS 8000u

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    strlcpy(dst, src == NULL ? "" : src, dst_size);
}

static bool is_app_page(atlas_page_t page)
{
    return page == ATLAS_PAGE_CLOCK ||
           page == ATLAS_PAGE_MUSIC ||
           page == ATLAS_PAGE_STORY ||
           page == ATLAS_PAGE_CHAT ||
           page == ATLAS_PAGE_CALENDAR ||
           page == ATLAS_PAGE_POMODORO;
}

static bool is_manual_display_page(atlas_page_t page)
{
    return page == ATLAS_PAGE_EYES ||
           page == ATLAS_PAGE_CLOCK ||
           page == ATLAS_PAGE_STATUS ||
           page == ATLAS_PAGE_VOICE ||
           is_app_page(page);
}

static bool runtime_state_can_be_overridden(atlas_runtime_state_t runtime_state)
{
    return runtime_state == ATLAS_RUNTIME_STATE_IDLE ||
           runtime_state == ATLAS_RUNTIME_STATE_LISTENING ||
           runtime_state == ATLAS_RUNTIME_STATE_THINKING ||
           runtime_state == ATLAS_RUNTIME_STATE_TOOL_RUNNING ||
           runtime_state == ATLAS_RUNTIME_STATE_COOLDOWN ||
           runtime_state == ATLAS_RUNTIME_STATE_ERROR;
}

static bool host_bridge_missing(const atlas_config_t *config)
{
    return config != NULL &&
           strcmp(config->llm.mode, "host") == 0 &&
           config->llm.base_url[0] == '\0';
}

static bool starts_with(const char *text, const char *prefix)
{
    return text != NULL && prefix != NULL && strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool contains_text_ci(const char *text, const char *needle)
{
    if (text == NULL || needle == NULL || needle[0] == '\0') {
        return false;
    }

    const size_t needle_len = strlen(needle);
    for (size_t i = 0; text[i] != '\0'; ++i) {
        size_t j = 0;
        while (j < needle_len && text[i + j] != '\0' &&
               tolower((unsigned char)text[i + j]) == tolower((unsigned char)needle[j])) {
            ++j;
        }
        if (j == needle_len) {
            return true;
        }
    }
    return false;
}

static void friendly_error_text(const char *raw, char *subtitle, size_t subtitle_size, char *hint, size_t hint_size)
{
    const char *reason = raw == NULL ? "" : raw;
    if (starts_with(reason, "ESP_ERR_HTTP_CONNECT") ||
        starts_with(reason, "ESP_ERR_HTTP") ||
        contains_text_ci(reason, "connect") ||
        contains_text_ci(reason, "econn") ||
        contains_text_ci(reason, "ehost") ||
        contains_text_ci(reason, "enet")) {
        copy_text(subtitle, subtitle_size, "没有连上 Mac Brain");
        copy_text(hint, hint_size, "确认电脑服务和同一 Wi-Fi");
    } else if (starts_with(reason, "ESP_ERR_TIMEOUT") || contains_text_ci(reason, "timeout")) {
        copy_text(subtitle, subtitle_size, "等待回复超时");
        copy_text(hint, hint_size, "看管理端最近一轮日志");
    } else if (starts_with(reason, "ESP_ERR_NO_MEM")) {
        copy_text(subtitle, subtitle_size, "内存暂时不够用");
        copy_text(hint, hint_size, "重启后再试或关闭长任务");
    } else if (starts_with(reason, "ESP_ERR_") || starts_with(reason, "HTTP ") || starts_with(reason, "HTTP_")) {
        copy_text(subtitle, subtitle_size, "底层链路返回错误");
        copy_text(hint, hint_size, "管理端保留完整错误码");
    } else if (reason[0] != '\0') {
        copy_text(subtitle, subtitle_size, "最近一轮链路失败");
        copy_text(hint, hint_size, "管理端可查看完整链路");
    } else {
        copy_text(subtitle, subtitle_size, "最近一轮对话没完成");
        copy_text(hint, hint_size, "稍后会自动恢复监听");
    }
}

static void scene_set(atlas_scene_snapshot_t *scene,
                      atlas_scene_kind_t kind,
                      atlas_page_t page,
                      atlas_expression_t expression,
                      uint8_t audio_level,
                      bool overlay,
                      bool needs_attention,
                      const char *title,
                      const char *subtitle,
                      const char *hint,
                      const char *left_role,
                      const char *right_role,
                      const char *severity)
{
    if (scene == NULL) {
        return;
    }
    scene->kind = kind;
    scene->page = page;
    scene->expression = expression;
    scene->audio_level = audio_level;
    scene->overlay = overlay;
    scene->needs_attention = needs_attention;
    copy_text(scene->state, sizeof(scene->state), atlas_scene_kind_name(kind));
    copy_text(scene->title, sizeof(scene->title), title);
    copy_text(scene->subtitle, sizeof(scene->subtitle), subtitle);
    copy_text(scene->hint, sizeof(scene->hint), hint);
    copy_text(scene->left_role, sizeof(scene->left_role), left_role);
    copy_text(scene->right_role, sizeof(scene->right_role), right_role);
    copy_text(scene->severity, sizeof(scene->severity), severity == NULL ? "info" : severity);
}

const char *atlas_scene_kind_name(atlas_scene_kind_t kind)
{
    switch (kind) {
    case ATLAS_SCENE_BOOT:
        return "boot";
    case ATLAS_SCENE_WIFI_CONFIG:
        return "wifi_config";
    case ATLAS_SCENE_IDLE:
        return "idle";
    case ATLAS_SCENE_MONITORING:
        return "monitoring";
    case ATLAS_SCENE_LISTENING:
        return "listening";
    case ATLAS_SCENE_RECORDING:
        return "recording";
    case ATLAS_SCENE_TRANSCRIBING:
        return "transcribing";
    case ATLAS_SCENE_THINKING:
        return "thinking";
    case ATLAS_SCENE_TOOL_RUNNING:
        return "tool_running";
    case ATLAS_SCENE_SPEAKING:
        return "speaking";
    case ATLAS_SCENE_COOLDOWN:
        return "cooldown";
    case ATLAS_SCENE_APP:
        return "app";
    case ATLAS_SCENE_AUDIO_TEST:
        return "audio_test";
    case ATLAS_SCENE_BRAIN_OFFLINE:
        return "brain_offline";
    case ATLAS_SCENE_MOVING:
        return "moving";
    case ATLAS_SCENE_CHARGING:
        return "charging";
    case ATLAS_SCENE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

const char *atlas_scene_kind_label_zh(atlas_scene_kind_t kind)
{
    switch (kind) {
    case ATLAS_SCENE_BOOT:
        return "启动中";
    case ATLAS_SCENE_WIFI_CONFIG:
        return "配网中";
    case ATLAS_SCENE_IDLE:
        return "待机";
    case ATLAS_SCENE_MONITORING:
        return "连续监听";
    case ATLAS_SCENE_LISTENING:
        return "我在听";
    case ATLAS_SCENE_RECORDING:
        return "录音中";
    case ATLAS_SCENE_TRANSCRIBING:
        return "识别中";
    case ATLAS_SCENE_THINKING:
        return "思考中";
    case ATLAS_SCENE_TOOL_RUNNING:
        return "执行技能";
    case ATLAS_SCENE_SPEAKING:
        return "正在说";
    case ATLAS_SCENE_COOLDOWN:
        return "收尾中";
    case ATLAS_SCENE_APP:
        return "应用页";
    case ATLAS_SCENE_AUDIO_TEST:
        return "音频测试";
    case ATLAS_SCENE_BRAIN_OFFLINE:
        return "大脑离线";
    case ATLAS_SCENE_MOVING:
        return "巡游";
    case ATLAS_SCENE_CHARGING:
        return "充电";
    case ATLAS_SCENE_ERROR:
        return "异常";
    default:
        return "未知";
    }
}

void atlas_scene_resolve(const atlas_ui_state_t *ui,
                         const atlas_config_t *config,
                         const atlas_wifi_status_t *wifi,
                         const atlas_audio_status_t *audio,
                         const atlas_audio_service_status_t *audio_service,
                         atlas_runtime_state_t runtime_state,
                         const char *runtime_reason,
                         uint32_t now_ms,
                         atlas_scene_snapshot_t *scene)
{
    if (scene == NULL) {
        return;
    }

    const atlas_page_t requested_page = ui == NULL ? ATLAS_PAGE_EYES : ui->page;
    const atlas_expression_t requested_expr = ui == NULL ? ATLAS_EXPR_IDLE : ui->expression;
    const uint8_t requested_audio = ui == NULL ? 0 : ui->audio_level;
    const bool recent_manual_page =
        ui != NULL &&
        ui->last_event_ms != 0 &&
        now_ms >= ui->last_event_ms &&
        now_ms - ui->last_event_ms <= ATLAS_MANUAL_PAGE_OVERRIDE_MS &&
        is_manual_display_page(requested_page) &&
        runtime_state_can_be_overridden(runtime_state);
    char subtitle[72];
    char hint[96];

    scene_set(scene,
              ATLAS_SCENE_IDLE,
              requested_page,
              requested_expr,
              requested_audio,
              false,
              false,
              "待机",
              "电子宠物在线",
              "可以说话、切换主题或打开应用页",
              "情绪状态",
              "运行状态",
              "info");

    if (audio != NULL && audio->initialized && (!audio->input_ready || !audio->output_ready)) {
        scene_set(scene,
                  ATLAS_SCENE_AUDIO_TEST,
                  ATLAS_PAGE_STATUS,
                  ATLAS_EXPR_ERROR,
                  requested_audio,
                  true,
                  true,
                  "音频未就绪",
                  !audio->input_ready ? "麦克风链路未就绪" : "外放链路未就绪",
                  "在管理端运行麦克风/喇叭测试",
                  "硬件状态",
                  "测试结果",
                  "warn");
        return;
    }

    if (audio_service != NULL && audio_service->mode == ATLAS_AUDIO_SERVICE_MODE_ERROR) {
        friendly_error_text(audio_service->last_action, subtitle, sizeof(subtitle), hint, sizeof(hint));
        scene_set(scene,
                  ATLAS_SCENE_ERROR,
                  ATLAS_PAGE_STATUS,
                  ATLAS_EXPR_ERROR,
                  requested_audio,
                  true,
                  true,
                  "音频异常",
                  subtitle,
                  hint,
                  "情绪状态",
                  "诊断摘要",
                  "error");
        return;
    }

    if (host_bridge_missing(config)) {
        scene_set(scene,
                  ATLAS_SCENE_BRAIN_OFFLINE,
                  ATLAS_PAGE_STATUS,
                  ATLAS_EXPR_ERROR,
                  0,
                  true,
                  true,
                  "大脑离线",
                  "Mac Brain 地址未配置",
                  "管理端填写 Mac 局域网 http://IP:8787",
                  "情绪状态",
                  "桥接状态",
                  "error");
        return;
    }

    const bool wifi_ap_fallback = wifi != NULL && !wifi->sta_connected && wifi->ap_started;
    const bool no_user_event_yet = ui == NULL || ui->last_event_ms == 0;
    if (wifi_ap_fallback && no_user_event_yet && now_ms < ATLAS_WIFI_CONFIG_BOOT_HINT_MS) {
        snprintf(subtitle, sizeof(subtitle), "热点 %s", wifi->ap_ssid[0] == '\0' ? "AtlasRover" : wifi->ap_ssid);
        snprintf(hint, sizeof(hint), "访问 %s 配网或管理", wifi->ap_ip[0] == '\0' ? "192.168.4.1" : wifi->ap_ip);
        scene_set(scene,
                  ATLAS_SCENE_WIFI_CONFIG,
                  ATLAS_PAGE_STATUS,
                  ATLAS_EXPR_CURIOUS,
                  0,
                  true,
                  true,
                  "等待配网",
                  subtitle,
                  hint,
                  "情绪状态",
                  "配网提示",
                  "warn");
        return;
    }

    if (ui != NULL && ui->moving) {
        scene_set(scene,
                  ATLAS_SCENE_MOVING,
                  ATLAS_PAGE_EYES,
                  ATLAS_EXPR_MOVING,
                  requested_audio,
                  true,
                  false,
                  "巡游中",
                  "底盘指令执行中",
                  "安全超时会自动 STOP",
                  "情绪状态",
                  "运动状态",
                  "info");
        return;
    }

    if (ui != NULL && ui->charging) {
        scene_set(scene,
                  ATLAS_SCENE_CHARGING,
                  requested_page,
                  ATLAS_EXPR_CHARGING,
                  requested_audio,
                  false,
                  false,
                  "充电中",
                  "能量恢复",
                  "保持外放和录音测试低频进行",
                  "情绪状态",
                  "电量状态",
                  "info");
    }

    if (recent_manual_page) {
        if (is_app_page(requested_page)) {
            scene_set(scene,
                      ATLAS_SCENE_APP,
                      requested_page,
                      requested_expr,
                      requested_audio,
                      false,
                      false,
                      atlas_scene_kind_label_zh(ATLAS_SCENE_APP),
                      atlas_page_name(requested_page),
                      "手动切换优先显示",
                      "应用情绪",
                      "应用内容",
                      "info");
        }
        return;
    }

    switch (runtime_state) {
    case ATLAS_RUNTIME_STATE_LISTENING:
        scene_set(scene,
                  ATLAS_SCENE_LISTENING,
                  ATLAS_PAGE_VOICE,
                  ATLAS_EXPR_LISTEN,
                  requested_audio > 0 ? requested_audio : 24,
                  true,
                  false,
                  "我在听",
                  "等待你的语音",
                  "说完后会自动进入识别和思考",
                  "情绪状态",
                  "麦克风电平",
                  "info");
        return;
    case ATLAS_RUNTIME_STATE_RECORDING:
        scene_set(scene,
                  ATLAS_SCENE_RECORDING,
                  ATLAS_PAGE_VOICE,
                  ATLAS_EXPR_LISTEN,
                  requested_audio > 0 ? requested_audio : 34,
                  true,
                  false,
                  "录音中",
                  "正在采集板载麦克风",
                  "请保持 1 米内说话",
                  "情绪状态",
                  "输入电平",
                  "info");
        return;
    case ATLAS_RUNTIME_STATE_TRANSCRIBING:
        scene_set(scene,
                  ATLAS_SCENE_TRANSCRIBING,
                  ATLAS_PAGE_STATUS,
                  ATLAS_EXPR_THINKING,
                  requested_audio,
                  true,
                  false,
                  "识别中",
                  "正在把语音发给 Mac Brain",
                  "ASR 完成后会进入理解",
                  "识别状态",
                  "Brain 通道",
                  "info");
        return;
    case ATLAS_RUNTIME_STATE_THINKING:
        scene_set(scene,
                  ATLAS_SCENE_THINKING,
                  ATLAS_PAGE_STATUS,
                  ATLAS_EXPR_THINKING,
                  requested_audio,
                  true,
                  false,
                  "思考中",
                  runtime_reason == NULL || runtime_reason[0] == '\0' ? "等待大模型回复" : runtime_reason,
                  "如果超过 10 秒，请查看 Mac Brain 日志",
                  "情绪状态",
                  "LLM 状态",
                  "info");
        return;
    case ATLAS_RUNTIME_STATE_TOOL_RUNNING:
        scene_set(scene,
                  ATLAS_SCENE_TOOL_RUNNING,
                  ATLAS_PAGE_STATUS,
                  ATLAS_EXPR_THINKING,
                  requested_audio,
                  true,
                  false,
                  "执行技能",
                  runtime_reason == NULL || runtime_reason[0] == '\0' ? "正在调用工具" : runtime_reason,
                  "天气、番茄、日历、表情切换都会走这里",
                  "情绪状态",
                  "工具状态",
                  "info");
        return;
    case ATLAS_RUNTIME_STATE_SPEAKING:
        scene_set(scene,
                  ATLAS_SCENE_SPEAKING,
                  ATLAS_PAGE_CHAT,
                  ATLAS_EXPR_SPEAKING,
                  requested_audio > 0 ? requested_audio : 58,
                  true,
                  false,
                  "正在说",
                  "TTS 播放中",
                  "播放期间会抑制麦克风误触发",
                  "情绪状态",
                  "字幕/回复",
                  "info");
        return;
    case ATLAS_RUNTIME_STATE_COOLDOWN:
        scene_set(scene,
                  ATLAS_SCENE_COOLDOWN,
                  requested_page == ATLAS_PAGE_CHAT ? ATLAS_PAGE_CHAT : ATLAS_PAGE_EYES,
                  ATLAS_EXPR_HAPPY,
                  0,
                  true,
                  false,
                  "收尾中",
                  "刚刚完成一轮对话",
                  "马上恢复监听或待机",
                  "情绪状态",
                  "链路状态",
                  "info");
        return;
    case ATLAS_RUNTIME_STATE_ERROR:
        friendly_error_text(runtime_reason, subtitle, sizeof(subtitle), hint, sizeof(hint));
        scene_set(scene,
                  ATLAS_SCENE_ERROR,
                  ATLAS_PAGE_STATUS,
                  ATLAS_EXPR_ERROR,
                  0,
                  true,
                  true,
                  "链路异常",
                  subtitle,
                  hint,
                  "情绪状态",
                  "失败原因",
                  "error");
        return;
    case ATLAS_RUNTIME_STATE_IDLE:
    default:
        break;
    }

    if (audio_service != NULL &&
        audio_service->continuous_enabled &&
        audio_service->mode == ATLAS_AUDIO_SERVICE_MODE_MONITORING &&
        requested_page == ATLAS_PAGE_VOICE) {
        scene_set(scene,
                  ATLAS_SCENE_MONITORING,
                  ATLAS_PAGE_VOICE,
                  ATLAS_EXPR_LISTEN,
                  requested_audio > 0 ? requested_audio : 18,
                  true,
                  false,
                  "连续监听",
                  audio_service->muted ? "播放静音保护中" : "等待语音触发",
                  "说一句完整指令或问题",
                  "情绪状态",
                  "麦克风门限",
                  "info");
        return;
    }

    if (is_app_page(requested_page)) {
        scene_set(scene,
                  ATLAS_SCENE_APP,
                  requested_page,
                  requested_expr,
                  requested_audio,
                  false,
                  false,
                  atlas_scene_kind_label_zh(ATLAS_SCENE_APP),
                  atlas_page_name(requested_page),
                  "应用页由 Web/语音技能切换",
                  "应用情绪",
                  "应用内容",
                  "info");
    }
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
        } else if (ch >= 0x20) {
            dst[(*used)++] = (char)ch;
        }
    }
    dst[*used] = '\0';
}

static void append_json_string(char *dst, size_t dst_size, size_t *used, const char *text)
{
    if (dst == NULL || dst_size == 0 || used == NULL || *used >= dst_size) {
        return;
    }
    int wrote = snprintf(dst + *used, dst_size - *used, "\"");
    if (wrote > 0) {
        *used += (size_t)wrote;
    }
    json_escape_append(dst, dst_size, text, used);
    wrote = snprintf(dst + *used, dst_size - *used, "\"");
    if (wrote > 0) {
        *used += (size_t)wrote;
    }
    if (*used >= dst_size) {
        *used = dst_size - 1;
    }
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

size_t atlas_scene_write_json(const atlas_scene_snapshot_t *scene, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    if (scene == NULL) {
        strlcpy(dst, "{}", dst_size);
        return strlen(dst);
    }

    size_t used = 0;
    appendf(dst, dst_size, &used, "{\"state\":");
    append_json_string(dst, dst_size, &used, scene->state);
    appendf(dst, dst_size, &used, ",\"label\":");
    append_json_string(dst, dst_size, &used, atlas_scene_kind_label_zh(scene->kind));
    appendf(dst, dst_size, &used, ",\"title\":");
    append_json_string(dst, dst_size, &used, scene->title);
    appendf(dst, dst_size, &used, ",\"subtitle\":");
    append_json_string(dst, dst_size, &used, scene->subtitle);
    appendf(dst, dst_size, &used, ",\"hint\":");
    append_json_string(dst, dst_size, &used, scene->hint);
    appendf(dst, dst_size, &used, ",\"left_role\":");
    append_json_string(dst, dst_size, &used, scene->left_role);
    appendf(dst, dst_size, &used, ",\"right_role\":");
    append_json_string(dst, dst_size, &used, scene->right_role);
    appendf(dst,
            dst_size,
            &used,
            ",\"severity\":\"%s\",\"page\":\"%s\",\"expression\":\"%s\",\"audio_level\":%u,"
            "\"overlay\":%s,\"needs_attention\":%s}",
            scene->severity,
            atlas_page_name(scene->page),
            atlas_expression_name(scene->expression),
            scene->audio_level,
            scene->overlay ? "true" : "false",
            scene->needs_attention ? "true" : "false");
    if (used >= dst_size) {
        used = dst_size - 1;
    }
    dst[used] = '\0';
    return used;
}
