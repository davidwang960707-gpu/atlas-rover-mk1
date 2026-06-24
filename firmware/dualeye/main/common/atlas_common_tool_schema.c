#include "common/atlas_common_tool_schema.h"

#include <stdio.h>
#include <string.h>

static const atlas_common_tool_descriptor_t s_tools[] = {
    {"atlas.show_page", "display", "device", true},
    {"atlas.set_expression", "display", "device", true},
    {"atlas.set_theme", "display", "device", true},
    {"atlas.role.switch", "display", "device", true},
    {"atlas.ui.set_chat_mode", "display", "device", true},
    {"atlas.clock.show", "display", "device", true},
    {"atlas.clock.sync", "config", "device", true},
    {"atlas.clock.status", "read", "device", true},
    {"atlas.calendar.show", "display", "device", true},
    {"atlas.calendar.today", "display", "device", true},
    {"atlas.calendar.set_note", "display", "device", true},
    {"atlas.pomodoro.show", "display", "device", true},
    {"atlas.pomodoro.start", "display", "device", true},
    {"atlas.pomodoro.stop", "display", "device", true},
    {"atlas.pomodoro.reset", "display", "device", true},
    {"atlas.pomodoro.status", "read", "device", true},
    {"atlas.pet.set_state", "display", "device", true},
    {"atlas.pet.play_animation", "display", "device", true},
    {"atlas.pet.event", "display", "device", true},
    {"atlas.status.read", "read", "device", true},
    {"atlas.selftest.run", "read", "device", true},
    {"atlas.brain.offline_status", "read", "device", true},
    {"atlas.audio.opus_stream.status", "read", "device", true},
    {"atlas.ota.check", "ota", "device", true},
    {"atlas.rover.move", "motion", "device", false},
    {"atlas.rover.stop", "motion", "device", false},
};

static void json_escape(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    size_t out = 0;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    for (size_t i = 0; src[i] != '\0' && out + 1 < dst_size; ++i) {
        const char c = src[i];
        if ((c == '"' || c == '\\') && out + 2 < dst_size) {
            dst[out++] = '\\';
            dst[out++] = c;
        } else if (c == '\n' && out + 2 < dst_size) {
            dst[out++] = '\\';
            dst[out++] = 'n';
        } else if (c == '\r' && out + 2 < dst_size) {
            dst[out++] = '\\';
            dst[out++] = 'r';
        } else if (c == '\t' && out + 2 < dst_size) {
            dst[out++] = '\\';
            dst[out++] = 't';
        } else {
            dst[out++] = c;
        }
    }
    dst[out] = '\0';
}

const char *atlas_common_tool_schema_version(void)
{
    return ATLAS_COMMON_TOOL_SCHEMA_VERSION;
}

const char *atlas_common_tool_schema_protocol(void)
{
    return ATLAS_COMMON_TOOL_SCHEMA_PROTOCOL;
}

const atlas_common_tool_descriptor_t *atlas_common_tool_schema_descriptors(size_t *count)
{
    if (count != NULL) {
        *count = atlas_common_tool_schema_tool_count();
    }
    return s_tools;
}

size_t atlas_common_tool_schema_tool_count(void)
{
    return sizeof(s_tools) / sizeof(s_tools[0]);
}

bool atlas_common_tool_schema_has_tool(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < atlas_common_tool_schema_tool_count(); ++i) {
        if (strcmp(name, s_tools[i].name) == 0) {
            return true;
        }
    }
    return false;
}

atlas_common_tool_result_t atlas_common_tool_result_from_esp_err(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return ATLAS_COMMON_TOOL_RESULT_OK;
    case ESP_ERR_INVALID_ARG:
        return ATLAS_COMMON_TOOL_RESULT_INVALID_ARGUMENT;
    case ESP_ERR_NOT_FOUND:
        return ATLAS_COMMON_TOOL_RESULT_NOT_FOUND;
    case ESP_ERR_NOT_SUPPORTED:
        return ATLAS_COMMON_TOOL_RESULT_NOT_SUPPORTED;
    case ESP_ERR_INVALID_STATE:
        return ATLAS_COMMON_TOOL_RESULT_INVALID_STATE;
    case ESP_ERR_NOT_FINISHED:
        return ATLAS_COMMON_TOOL_RESULT_REQUIRES_CONFIRMATION;
    default:
        return ATLAS_COMMON_TOOL_RESULT_FAILED;
    }
}

const char *atlas_common_tool_result_code(atlas_common_tool_result_t result)
{
    switch (result) {
    case ATLAS_COMMON_TOOL_RESULT_OK:
        return "ok";
    case ATLAS_COMMON_TOOL_RESULT_INVALID_ARGUMENT:
        return "invalid_argument";
    case ATLAS_COMMON_TOOL_RESULT_NOT_FOUND:
        return "tool_not_found";
    case ATLAS_COMMON_TOOL_RESULT_NOT_SUPPORTED:
        return "not_supported";
    case ATLAS_COMMON_TOOL_RESULT_INVALID_STATE:
        return "invalid_state";
    case ATLAS_COMMON_TOOL_RESULT_REQUIRES_CONFIRMATION:
        return "confirmation_required";
    default:
        return "execution_failed";
    }
}

const char *atlas_common_tool_result_code_from_esp_err(esp_err_t err)
{
    return atlas_common_tool_result_code(atlas_common_tool_result_from_esp_err(err));
}

size_t atlas_common_tool_schema_write_list_json(char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }

    return (size_t)snprintf(
        dst,
        dst_size,
        "{"
        "\"ok\":true,"
        "\"protocol\":\"" ATLAS_COMMON_TOOL_SCHEMA_VERSION "\","
        "\"schema_protocol\":\"" ATLAS_COMMON_TOOL_SCHEMA_PROTOCOL "\","
        "\"mcp_like\":true,"
        "\"tool_count\":%u,"
        "\"call_endpoint\":\"/api/tools/call\","
        "\"mcp_call_endpoint\":\"/mcp/tools/call\","
        "\"capabilities\":{\"display\":true,\"desk_apps\":true,\"pet_head\":true,\"brain_offline_status\":true,\"audio_opus_stream\":true,\"ota_check\":true,\"rover_motion\":false},"
        "\"result_codes\":[\"ok\",\"invalid_argument\",\"tool_not_found\",\"not_supported\",\"invalid_state\",\"confirmation_required\",\"execution_failed\"],"
        "\"tools\":["
        "{\"name\":\"atlas.show_page\",\"description\":\"切换 DualEye 页面\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"aliases\":[\"atlas_show_page\",\"display.show_page\"],\"inputSchema\":{\"type\":\"object\",\"properties\":{\"page\":{\"type\":\"string\",\"enum\":[\"eyes\",\"clock\",\"status\",\"voice\",\"music\",\"story\",\"chat\",\"calendar\",\"pomodoro\"]}},\"required\":[\"page\"]}},"
        "{\"name\":\"atlas.set_expression\",\"description\":\"切换表情\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"aliases\":[\"atlas_set_expression\",\"eyes.set_expression\"],\"inputSchema\":{\"type\":\"object\",\"properties\":{\"expression\":{\"type\":\"string\"}},\"required\":[\"expression\"]}},"
        "{\"name\":\"atlas.set_theme\",\"description\":\"切换并保存双眼主题\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"theme\":{\"type\":\"string\"}},\"required\":[\"theme\"]}},"
        "{\"name\":\"atlas.role.switch\",\"description\":\"联动角色、主题、表情和页面\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"role\":{\"type\":\"string\",\"enum\":[\"pet\",\"raptor\",\"mecha\",\"goggle\"]}},\"required\":[\"role\"]}},"
        "{\"name\":\"atlas.ui.set_chat_mode\",\"description\":\"切换对话界面模式\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"chat_mode\":{\"type\":\"string\",\"enum\":[\"pet_head\",\"text\",\"eyes_only\"]}},\"required\":[\"chat_mode\"]}},"
        "{\"name\":\"atlas.clock.show\",\"description\":\"打开桌面时钟\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.clock.sync\",\"description\":\"校准时钟\",\"risk\":\"config\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"epoch_ms\":{\"type\":\"integer\"}}}},"
        "{\"name\":\"atlas.clock.status\",\"description\":\"显示时钟状态\",\"risk\":\"read\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.calendar.show\",\"description\":\"打开日历\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.calendar.today\",\"description\":\"显示今日日历\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"title\":{\"type\":\"string\"},\"note\":{\"type\":\"string\"}}}},"
        "{\"name\":\"atlas.calendar.set_note\",\"description\":\"设置日历便签\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"title\":{\"type\":\"string\"},\"note\":{\"type\":\"string\"}},\"required\":[\"note\"]}},"
        "{\"name\":\"atlas.pomodoro.show\",\"description\":\"打开番茄专注\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.pomodoro.start\",\"description\":\"开始番茄专注\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"task_name\":{\"type\":\"string\"},\"focus_minutes\":{\"type\":\"integer\"},\"break_minutes\":{\"type\":\"integer\"}}}},"
        "{\"name\":\"atlas.pomodoro.stop\",\"description\":\"停止番茄专注\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.pomodoro.reset\",\"description\":\"重置番茄专注\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.pomodoro.status\",\"description\":\"显示番茄状态\",\"risk\":\"read\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.pet.set_state\",\"description\":\"设置宠物头视觉状态\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"state\":{\"type\":\"string\",\"enum\":[\"idle\",\"listen\",\"think\",\"speak\",\"happy\",\"sleepy\",\"surprised\",\"sing\"]},\"text\":{\"type\":\"string\"}},\"required\":[\"state\"]}},"
        "{\"name\":\"atlas.pet.play_animation\",\"description\":\"播放宠物头动画\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"animation\":{\"type\":\"string\",\"enum\":[\"blink\",\"speak\",\"sing\",\"laugh\",\"think\"]},\"text\":{\"type\":\"string\"}},\"required\":[\"animation\"]}},"
        "{\"name\":\"atlas.pet.event\",\"description\":\"触发电子宠物事件\",\"risk\":\"display\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"aliases\":[\"atlas_pet_event\",\"pet.event\"],\"inputSchema\":{\"type\":\"object\",\"properties\":{\"event\":{\"type\":\"string\"}},\"required\":[\"event\"]}},"
        "{\"name\":\"atlas.status.read\",\"description\":\"读取本地状态摘要\",\"risk\":\"read\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.selftest.run\",\"description\":\"运行设备自检摘要\",\"risk\":\"read\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.brain.offline_status\",\"description\":\"读取 Brain 离线降级状态\",\"risk\":\"read\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.audio.opus_stream.status\",\"description\":\"读取 OPUS 真流状态\",\"risk\":\"read\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.ota.check\",\"description\":\"读取固件包/OTA manifest\",\"risk\":\"ota\",\"target\":\"device\",\"enabled\":true,\"offline_policy\":\"device_local\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.rover.move\",\"description\":\"小车移动，当前桌面版禁用\",\"risk\":\"motion\",\"target\":\"device\",\"enabled\":false,\"confirm_required\":true,\"disabled_reason\":\"rover_motion_disabled_for_dualeye_desktop\",\"inputSchema\":{\"type\":\"object\"}},"
        "{\"name\":\"atlas.rover.stop\",\"description\":\"小车停止，当前桌面版不作为主链路\",\"risk\":\"motion\",\"target\":\"device\",\"enabled\":false,\"confirm_required\":true,\"disabled_reason\":\"rover_motion_disabled_for_dualeye_desktop\",\"inputSchema\":{\"type\":\"object\"}}"
        "],"
        "\"notes\":\"固件侧 Tool Schema V0 执行桌面显示、宠物头、桌面应用、Brain 离线状态和诊断工具；运动工具保留 disabled 能力声明。\""
        "}",
        (unsigned)atlas_common_tool_schema_tool_count());
}

size_t atlas_common_tool_schema_write_call_result_json(char *dst,
                                                       size_t dst_size,
                                                       const char *tool,
                                                       const char *result,
                                                       const char *page,
                                                       const char *expression,
                                                       esp_err_t err)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }

    char safe_tool[128];
    char safe_result[240];
    char safe_page[48];
    char safe_expression[48];
    const char *error_name = err == ESP_OK ? "" : esp_err_to_name(err);
    char safe_error[64];
    json_escape(safe_tool, sizeof(safe_tool), tool);
    json_escape(safe_result, sizeof(safe_result), result);
    json_escape(safe_page, sizeof(safe_page), page);
    json_escape(safe_expression, sizeof(safe_expression), expression);
    json_escape(safe_error, sizeof(safe_error), error_name);

    return (size_t)snprintf(dst,
                            dst_size,
                            "{\"ok\":%s,\"protocol\":\"" ATLAS_COMMON_TOOL_SCHEMA_VERSION "\","
                            "\"tool\":\"%s\",\"result\":\"%s\",\"page\":\"%s\",\"expression\":\"%s\","
                            "\"error\":\"%s\",\"error_code\":\"%s\"}",
                            err == ESP_OK ? "true" : "false",
                            safe_tool,
                            safe_result,
                            safe_page,
                            safe_expression,
                            safe_error,
                            atlas_common_tool_result_code_from_esp_err(err));
}
