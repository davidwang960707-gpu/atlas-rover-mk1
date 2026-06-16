#include "atlas_admin_http.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "atlas_llm_client.h"
#include "atlas_mimiclaw_adapter.h"
#include "atlas_pairing.h"
#include "atlas_wifi.h"

static const char *TAG = "atlas_admin";

typedef struct {
    atlas_config_t *config;
    atlas_ui_state_t *ui_state;
    atlas_admin_now_ms_fn_t now_ms;
    httpd_handle_t server;
} atlas_admin_ctx_t;

static atlas_admin_ctx_t s_ctx;

static bool is_motion_event(atlas_voice_event_t event)
{
    return event == ATLAS_VOICE_EVENT_MOVE_FORWARD ||
           event == ATLAS_VOICE_EVENT_MOVE_BACKWARD ||
           event == ATLAS_VOICE_EVENT_TURN_LEFT ||
           event == ATLAS_VOICE_EVENT_TURN_RIGHT;
}

static bool expression_from_name(const char *name, atlas_expression_t *expression)
{
    if (name == NULL || expression == NULL) {
        return false;
    }
    for (atlas_expression_t candidate = ATLAS_EXPR_IDLE; candidate < ATLAS_EXPR_COUNT; ++candidate) {
        if (strcmp(name, atlas_expression_name(candidate)) == 0) {
            *expression = candidate;
            return true;
        }
    }
    return false;
}

static bool page_from_name(const char *name, atlas_page_t *page)
{
    if (name == NULL || page == NULL) {
        return false;
    }
    if (strcmp(name, "eyes") == 0) {
        *page = ATLAS_PAGE_EYES;
    } else if (strcmp(name, "clock") == 0) {
        *page = ATLAS_PAGE_CLOCK;
    } else if (strcmp(name, "status") == 0) {
        *page = ATLAS_PAGE_STATUS;
    } else if (strcmp(name, "voice") == 0) {
        *page = ATLAS_PAGE_VOICE;
    } else if (strcmp(name, "settings") == 0) {
        *page = ATLAS_PAGE_SETTINGS;
    } else if (strcmp(name, "alarm") == 0) {
        *page = ATLAS_PAGE_ALARM;
    } else if (strcmp(name, "photo") == 0) {
        *page = ATLAS_PAGE_PHOTO;
    } else if (strcmp(name, "music") == 0) {
        *page = ATLAS_PAGE_MUSIC;
    } else if (strcmp(name, "story") == 0) {
        *page = ATLAS_PAGE_STORY;
    } else if (strcmp(name, "chat") == 0) {
        *page = ATLAS_PAGE_CHAT;
    } else if (strcmp(name, "calendar") == 0) {
        *page = ATLAS_PAGE_CALENDAR;
    } else if (strcmp(name, "pomodoro") == 0) {
        *page = ATLAS_PAGE_POMODORO;
    } else {
        return false;
    }
    return true;
}

static uint32_t now_ms(void)
{
    return s_ctx.now_ms == NULL ? 0 : s_ctx.now_ms();
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void url_decode(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    size_t out = 0;
    for (size_t i = 0; i < src_len && out + 1 < dst_size; ++i) {
        if (src[i] == '+') {
            dst[out++] = ' ';
        } else if (src[i] == '%' && i + 2 < src_len) {
            const int hi = hex_value(src[i + 1]);
            const int lo = hex_value(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[out++] = (char)((hi << 4) | lo);
                i += 2;
            }
        } else {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
}

static bool form_get_value(const char *body, const char *key, char *out, size_t out_size)
{
    if (body == NULL || key == NULL || out == NULL || out_size == 0) {
        return false;
    }

    const size_t key_len = strlen(key);
    const char *cursor = body;
    while (*cursor != '\0') {
        const char *pair_end = strchr(cursor, '&');
        if (pair_end == NULL) {
            pair_end = cursor + strlen(cursor);
        }
        const char *equals = memchr(cursor, '=', (size_t)(pair_end - cursor));
        if (equals != NULL && (size_t)(equals - cursor) == key_len && strncmp(cursor, key, key_len) == 0) {
            url_decode(out, out_size, equals + 1, (size_t)(pair_end - equals - 1));
            return true;
        }
        cursor = *pair_end == '&' ? pair_end + 1 : pair_end;
    }

    out[0] = '\0';
    return false;
}

static esp_err_t read_body(httpd_req_t *req, char *body, size_t body_size)
{
    if (body == NULL || body_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t received = 0;
    const size_t target = req->content_len < body_size - 1 ? req->content_len : body_size - 1;
    while (received < target) {
        const int ret = httpd_req_recv(req, body + received, target - received);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }
    body[received] = '\0';
    return ESP_OK;
}

static bool authorize_body(const char *body)
{
    char pin[16];
    if (!form_get_value(body, "pin", pin, sizeof(pin))) {
        return false;
    }
    return atlas_pairing_authorize_pin(pin);
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{
    char json[160];
    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"%s\"}", message);
    httpd_resp_set_status(req, status);
    return send_json(req, json);
}

static void json_escape(char *dst, size_t dst_size, const char *src)
{
    size_t out = 0;
    if (dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    for (size_t i = 0; src[i] != '\0' && out + 1 < dst_size; ++i) {
        if (src[i] == '"' || src[i] == '\\') {
            if (out + 2 >= dst_size) {
                break;
            }
            dst[out++] = '\\';
            dst[out++] = src[i];
        } else if ((unsigned char)src[i] >= 0x20) {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
}

static esp_err_t app_handler(httpd_req_t *req)
{
    static const char *html =
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Atlas Rover</title>"
        "<style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;background:#101114;color:#f2eee7}"
        "main{max-width:820px;margin:0 auto;padding:16px}.top{display:flex;justify-content:space-between;gap:12px;align-items:center}.badge{padding:6px 10px;border:1px solid #5fe1b4;color:#5fe1b4}"
        "section{margin:12px 0;padding:12px;border:1px solid #41474f;background:#171a20}h1{margin:6px 0 2px;font-size:28px}h2{font-size:17px;margin:0 0 10px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(138px,1fr));gap:8px}"
        "button,input{font:inherit;border:1px solid #6f7780;background:#202631;color:#f2eee7;padding:11px;border-radius:6px}button{cursor:pointer}button.primary{border-color:#3fc9ff}button.warn{border-color:#ff6b4b;color:#ffb2a0}"
        ".pad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:360px;margin:0 auto}.pad .wide{grid-column:1/4}.status{white-space:pre-wrap;color:#b9c2cc;font-size:13px}</style></head>"
        "<body><main><div class=\"top\"><div><h1>Atlas Rover</h1><div id=\"mode\" class=\"badge\">连接中</div></div><a href=\"/admin\" style=\"color:#f5dc96\">管理后台</a></div>"
        "<section><h2>配对</h2><div class=\"grid\"><input id=\"pin\" inputmode=\"numeric\" placeholder=\"6 位配对码\"><button onclick=\"savePin()\">保存配对码</button><button class=\"warn\" onclick=\"stopNow()\">STOP</button></div></section>"
        "<section><h2>模式</h2><div class=\"grid\"><button class=\"primary\" onclick=\"setMode('manual')\">手动模式</button><button onclick=\"setMode('ai')\">AI 模式</button><button onclick=\"refresh()\">刷新状态</button></div><div id=\"status\" class=\"status\">加载中...</div></section>"
        "<section><h2>双眼表情</h2><div class=\"grid\"><button onclick=\"expr('happy')\">开心</button><button onclick=\"expr('thinking')\">思考</button><button onclick=\"expr('listen')\">聆听</button><button onclick=\"expr('speaking')\">说话</button><button onclick=\"expr('sleepy')\">困倦</button><button onclick=\"expr('wink')\">眨眼</button></div></section>"
        "<section><h2>显示</h2><div class=\"grid\"><button onclick=\"page('eyes')\">双眼</button><button onclick=\"page('clock')\">时钟</button><button onclick=\"page('status')\">状态</button><button onclick=\"page('voice')\">语音</button><button onclick=\"page('music')\">音乐</button><button onclick=\"page('story')\">故事</button><button onclick=\"page('chat')\">对话</button><button onclick=\"page('calendar')\">日历</button><button onclick=\"page('pomodoro')\">番茄</button><button onclick=\"page('photo')\">照片</button></div></section>"
        "<section><h2>移动</h2><div class=\"pad\"><button></button><button onclick=\"move('F')\">前进</button><button></button><button onclick=\"move('L')\">左转</button><button class=\"warn\" onclick=\"stopNow()\">停止</button><button onclick=\"move('R')\">右转</button><button class=\"wide\" onclick=\"move('B')\">后退</button></div></section>"
        "<section><h2>MimiClaw 应用</h2><div class=\"grid\"><button onclick=\"act('music')\">听音乐</button><button onclick=\"act('story')\">讲故事</button><button onclick=\"act('chat')\">陪我说话</button><button onclick=\"act('calendar')\">看日历</button><button onclick=\"act('pomodoro')\">番茄专注</button><button onclick=\"act('alarm')\">设置闹钟</button></div></section>"
        "</main><script>"
        "const enc=encodeURIComponent,$=id=>document.getElementById(id);let st=null;$('pin').value=localStorage.getItem('atlas_pin')||'';function savePin(){localStorage.setItem('atlas_pin',$('pin').value);alert('已保存')}"
        "async function post(u,b=''){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});const t=await r.text();try{return JSON.parse(t)}catch(e){return {raw:t}}}"
        "function pinBody(){return `pin=${enc($('pin').value)}`}"
        "async function refresh(){st=await (await fetch('/api/status')).json();$('mode').textContent=`${st.safety.control_mode==='ai'?'AI 模式':'手动模式'} · ${st.ui.page} · ${st.ui.expression}`;$('status').textContent=JSON.stringify(st,null,2)}"
        "async function setMode(m){const speed=st?st.safety.max_speed_percent:40,dur=st?st.safety.max_duration_ms:700;alert(JSON.stringify(await post('/api/config/safety',`${pinBody()}&motion_enabled=1&control_mode=${m}&max_speed=${speed}&max_duration=${dur}`)));refresh()}"
        "async function stopNow(){alert(JSON.stringify(await post('/api/rover/stop')));refresh()}"
        "async function move(d){alert(JSON.stringify(await post('/api/rover/move',`${pinBody()}&dir=${d}&speed=30&duration=500`)));refresh()}"
        "async function expr(e){alert(JSON.stringify(await post('/api/app/expression',`${pinBody()}&expression=${e}`)));refresh()}"
        "async function page(p){alert(JSON.stringify(await post('/api/app/page',`${pinBody()}&page=${p}`)));refresh()}"
        "async function act(a){alert(JSON.stringify(await post('/api/app/action',`${pinBody()}&action=${a}`)));refresh()}refresh();setInterval(refresh,5000);</script></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, html);
}

static esp_err_t admin_handler(httpd_req_t *req)
{
    static const char *html =
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Atlas Rover 管理台</title>"
        "<style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;background:#0b0d10;color:#efe9df}"
        "main{max-width:920px;margin:0 auto;padding:18px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px}"
        "section{border:1px solid #8a632f;padding:14px;background:#11151b}button,input,select{font:inherit;padding:9px;border-radius:6px;border:1px solid #8a632f;background:#151922;color:#efe9df}"
        "button{cursor:pointer}button.stop{background:#7a261f;border-color:#ff6b4b}button.primary{border-color:#3fc9ff}.row{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0}"
        "label{display:grid;gap:4px;margin:8px 0;color:#bdb6ad}code{display:block;white-space:pre-wrap;background:#07080a;padding:10px;border:1px solid #26313d;color:#5fe1b4}</style></head>"
        "<body><main><h1>Atlas Rover Mk.1 管理台</h1><p><a href=\"/app\" style=\"color:#f5dc96\">进入应用页</a></p>"
        "<p>先跑起来，再跑稳，再变好看。STOP 不需要配对码；移动和配置修改需要 DualEye 日志/屏幕上的 6 位配对码。</p>"
        "<section><h2>状态</h2><code id=\"status\">加载中...</code><div class=\"row\"><button class=\"stop\" onclick=\"stopNow()\">STOP</button><button onclick=\"refresh()\">刷新</button></div></section>"
        "<div class=\"grid\"><section><h2>手动控制</h2><label>配对码<input id=\"pin\" inputmode=\"numeric\" placeholder=\"6 位配对码\"></label>"
        "<label>速度 %<input id=\"speed\" type=\"number\" min=\"1\" max=\"80\" value=\"30\"></label><label>时长 ms<input id=\"duration\" type=\"number\" min=\"100\" max=\"2000\" value=\"500\"></label>"
        "<div class=\"row\"><button onclick=\"move('F')\">前进</button><button onclick=\"move('B')\">后退</button><button onclick=\"move('L')\">左转</button><button onclick=\"move('R')\">右转</button></div></section>"
        "<section><h2>Wi-Fi 配网</h2><label>SSID<input id=\"ssid\"></label><label>密码<input id=\"wifi_pass\" type=\"password\"></label><button class=\"primary\" onclick=\"saveWifi()\">保存 Wi-Fi</button></section>"
        "<section><h2>大模型/API</h2><label>模式<select id=\"llm_mode\"><option value=\"off\">关闭</option><option value=\"host\">电脑宿主 MiniClaw</option><option value=\"cloud\">云端大模型</option><option value=\"embedded\">端侧 MimiClaw</option></select></label>"
        "<label>Provider<input id=\"provider\" value=\"openai_compatible\"></label><label>Base URL<input id=\"base_url\"></label><label>Model<input id=\"model\"></label><label>API Key<input id=\"api_key\" type=\"password\" placeholder=\"留空则不更新\"></label>"
        "<button class=\"primary\" onclick=\"saveLlm()\">保存 API 设置</button></section>"
        "<section><h2>安全</h2><label><input id=\"motion_enabled\" type=\"checkbox\"> 允许运动</label><label>控制模式<select id=\"control_mode\"><option value=\"manual\">手动模式：Web 控制</option><option value=\"ai\">AI 模式：语音/MimiClaw</option></select></label><label>最大速度 %<input id=\"max_speed\" type=\"number\" min=\"1\" max=\"80\" value=\"40\"></label>"
        "<label>最大时长 ms<input id=\"max_duration\" type=\"number\" min=\"100\" max=\"2000\" value=\"700\"></label><button class=\"primary\" onclick=\"saveSafety()\">保存安全设置</button></section>"
        "<section><h2>界面/主题</h2><label>主题<select id=\"ui_theme\"><option value=\"classic\">经典蓝眼</option><option value=\"amber\">琥珀巡航</option><option value=\"mint\">薄荷友好</option><option value=\"alert\">红色警戒</option><option value=\"night\">低亮夜航</option></select></label>"
        "<label>屏幕亮度 %<input id=\"brightness\" type=\"number\" min=\"0\" max=\"100\" value=\"70\"></label><label>音量 %<input id=\"volume\" type=\"number\" min=\"0\" max=\"100\" value=\"60\"></label><button class=\"primary\" onclick=\"saveUi()\">保存界面设置</button></section>"
        "<section><h2>文本意图测试</h2><label>文本<input id=\"voice_text\" placeholder=\"forward / stop / left\"></label><button onclick=\"sendText()\">发送到意图层</button></section>"
        "<section><h2>系统</h2><div class=\"row\"><button onclick=\"resetCfg()\">清除 Wi-Fi/API</button><button onclick=\"reboot()\">重启</button></div></section></div></main>"
        "<script>"
        "const enc=encodeURIComponent;function pin(){return document.getElementById('pin').value}"
        "async function post(u,b=''){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});const t=await r.text();try{return JSON.parse(t)}catch(e){return {raw:t}}}"
        "async function refresh(){const s=await (await fetch('/api/status')).json();document.getElementById('status').textContent=JSON.stringify(s,null,2);"
        "document.getElementById('max_speed').value=s.safety.max_speed_percent;document.getElementById('max_duration').value=s.safety.max_duration_ms;document.getElementById('motion_enabled').checked=s.safety.motion_enabled;document.getElementById('control_mode').value=s.safety.control_mode||'manual';"
        "document.getElementById('ui_theme').value=s.ui.theme||'classic';document.getElementById('brightness').value=s.ui.brightness;document.getElementById('volume').value=s.ui.volume;"
        "document.getElementById('llm_mode').value=s.llm.mode||'off';document.getElementById('provider').value=s.llm.provider||'';document.getElementById('base_url').value=s.llm.base_url||'';document.getElementById('model').value=s.llm.model||''}"
        "async function stopNow(){alert(JSON.stringify(await post('/api/rover/stop')));refresh()}"
        "async function move(d){const b=`pin=${enc(pin())}&dir=${d}&speed=${enc(speed.value)}&duration=${enc(duration.value)}`;alert(JSON.stringify(await post('/api/rover/move',b)));refresh()}"
        "async function saveWifi(){const b=`pin=${enc(pin())}&ssid=${enc(ssid.value)}&password=${enc(wifi_pass.value)}`;alert(JSON.stringify(await post('/api/config/wifi',b)));refresh()}"
        "async function saveLlm(){const b=`pin=${enc(pin())}&mode=${enc(llm_mode.value)}&provider=${enc(provider.value)}&base_url=${enc(base_url.value)}&model=${enc(model.value)}&api_key=${enc(api_key.value)}`;alert(JSON.stringify(await post('/api/config/llm',b)));api_key.value='';refresh()}"
        "async function saveSafety(){const b=`pin=${enc(pin())}&motion_enabled=${motion_enabled.checked?1:0}&control_mode=${enc(control_mode.value)}&max_speed=${enc(max_speed.value)}&max_duration=${enc(max_duration.value)}`;alert(JSON.stringify(await post('/api/config/safety',b)));refresh()}"
        "async function saveUi(){const b=`pin=${enc(pin())}&theme=${enc(ui_theme.value)}&brightness=${enc(brightness.value)}&volume=${enc(volume.value)}`;alert(JSON.stringify(await post('/api/config/ui',b)));refresh()}"
        "async function sendText(){const b=`pin=${enc(pin())}&text=${enc(voice_text.value)}`;alert(JSON.stringify(await post('/api/voice/text',b)));refresh()}"
        "async function resetCfg(){if(confirm('清除 Wi-Fi/API 配置？'))alert(JSON.stringify(await post('/api/config/reset',`pin=${enc(pin())}`)))}"
        "async function reboot(){if(confirm('重启设备？'))alert(JSON.stringify(await post('/api/system/reboot',`pin=${enc(pin())}`)))}refresh();setInterval(refresh,4000);</script></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, html);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    atlas_wifi_status_t wifi;
    atlas_wifi_get_status(&wifi);

    atlas_llm_status_t llm;
    atlas_llm_client_get_status(s_ctx.config, &llm);

    char base_url[ATLAS_LLM_BASE_URL_MAX * 2];
    char mode[ATLAS_LLM_MODE_MAX * 2];
    char model[ATLAS_LLM_MODEL_MAX * 2];
    char provider[ATLAS_LLM_PROVIDER_MAX * 2];
    char ui_theme[ATLAS_UI_THEME_MAX * 2];
    json_escape(base_url, sizeof(base_url), llm.base_url);
    json_escape(mode, sizeof(mode), llm.mode);
    json_escape(model, sizeof(model), llm.model);
    json_escape(provider, sizeof(provider), llm.provider);
    json_escape(ui_theme, sizeof(ui_theme), s_ctx.config->ui.theme);

    char json[1700];
    snprintf(json,
             sizeof(json),
             "{"
             "\"ok\":true,"
             "\"pairing_hint\":\"see DualEye screen or serial log\","
             "\"ui\":{\"page\":\"%s\",\"expression\":\"%s\",\"motion\":\"%s\",\"moving\":%s,\"last_ack\":%d,"
             "\"theme\":\"%s\",\"brightness\":%u,\"volume\":%u},"
             "\"wifi\":{\"mode\":\"%s\",\"sta_connected\":%s,\"sta_ip\":\"%s\",\"ap_started\":%s,\"ap_ip\":\"%s\",\"ap_ssid\":\"%s\"},"
             "\"llm\":{\"mode\":\"%s\",\"label\":\"%s\",\"provider\":\"%s\",\"base_url\":\"%s\",\"model\":\"%s\",\"configured\":%s,\"api_key_set\":%s},"
             "\"safety\":{\"motion_enabled\":%s,\"control_mode\":\"%s\",\"max_speed_percent\":%u,\"max_duration_ms\":%u}"
             "}",
             atlas_page_name(s_ctx.ui_state->page),
             atlas_expression_name(s_ctx.ui_state->expression),
             atlas_motion_name(s_ctx.ui_state->motion),
             s_ctx.ui_state->moving ? "true" : "false",
             (int)s_ctx.ui_state->last_ack,
             ui_theme,
             s_ctx.config->ui.brightness,
             s_ctx.config->ui.volume,
             atlas_wifi_mode_name(wifi.mode),
             wifi.sta_connected ? "true" : "false",
             wifi.sta_ip,
             wifi.ap_started ? "true" : "false",
             wifi.ap_ip,
             wifi.ap_ssid,
             mode,
             atlas_llm_client_mode_label(llm.mode),
             provider,
             base_url,
             model,
             llm.configured ? "true" : "false",
             llm.api_key_set ? "true" : "false",
             s_ctx.config->safety.motion_enabled ? "true" : "false",
             s_ctx.config->safety.control_mode,
             s_ctx.config->safety.max_speed_percent,
             s_ctx.config->safety.max_duration_ms);

    return send_json(req, json);
}

static esp_err_t app_expression_handler(httpd_req_t *req)
{
    char body[160];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char expression_name[24] = "";
    (void)form_get_value(body, "expression", expression_name, sizeof(expression_name));
    atlas_expression_t expression = ATLAS_EXPR_IDLE;
    if (!expression_from_name(expression_name, &expression)) {
        return send_error(req, "400 Bad Request", "bad expression");
    }

    s_ctx.ui_state->page = ATLAS_PAGE_EYES;
    s_ctx.ui_state->expression = expression;
    s_ctx.ui_state->audio_level = expression == ATLAS_EXPR_SPEAKING ? 58 : 0;
    s_ctx.ui_state->last_event_ms = now_ms();
    return send_json(req, "{\"ok\":true,\"app\":\"expression\"}");
}

static esp_err_t app_page_handler(httpd_req_t *req)
{
    char body[160];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char page_name[24] = "";
    (void)form_get_value(body, "page", page_name, sizeof(page_name));
    atlas_page_t page = ATLAS_PAGE_EYES;
    if (!page_from_name(page_name, &page)) {
        return send_error(req, "400 Bad Request", "bad page");
    }

    s_ctx.ui_state->page = page;
    if (page == ATLAS_PAGE_VOICE) {
        s_ctx.ui_state->expression = ATLAS_EXPR_LISTEN;
        s_ctx.ui_state->audio_level = 24;
    } else if (page == ATLAS_PAGE_MUSIC || page == ATLAS_PAGE_STORY || page == ATLAS_PAGE_CHAT) {
        s_ctx.ui_state->expression = page == ATLAS_PAGE_CHAT ? ATLAS_EXPR_LISTEN : ATLAS_EXPR_SPEAKING;
        s_ctx.ui_state->audio_level = page == ATLAS_PAGE_CHAT ? 28 : 58;
    } else if (page == ATLAS_PAGE_CALENDAR || page == ATLAS_PAGE_POMODORO ||
               page == ATLAS_PAGE_ALARM || page == ATLAS_PAGE_PHOTO) {
        s_ctx.ui_state->expression = ATLAS_EXPR_CURIOUS;
        s_ctx.ui_state->audio_level = 0;
    } else if (!s_ctx.ui_state->moving) {
        s_ctx.ui_state->expression = ATLAS_EXPR_IDLE;
        s_ctx.ui_state->audio_level = 0;
    }
    s_ctx.ui_state->last_event_ms = now_ms();
    return send_json(req, "{\"ok\":true,\"app\":\"page\"}");
}

static esp_err_t app_action_handler(httpd_req_t *req)
{
    char body[160];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char action[24] = "";
    (void)form_get_value(body, "action", action, sizeof(action));
    const uint32_t ts = now_ms();

    if (s_ctx.ui_state->moving) {
        (void)atlas_ui_stop(s_ctx.ui_state, ts);
    }

    if (strcmp(action, "music") == 0) {
        s_ctx.ui_state->page = ATLAS_PAGE_MUSIC;
        s_ctx.ui_state->expression = ATLAS_EXPR_SPEAKING;
        s_ctx.ui_state->audio_level = 64;
    } else if (strcmp(action, "story") == 0) {
        s_ctx.ui_state->page = ATLAS_PAGE_STORY;
        s_ctx.ui_state->expression = ATLAS_EXPR_SPEAKING;
        s_ctx.ui_state->audio_level = 58;
    } else if (strcmp(action, "chat") == 0) {
        s_ctx.ui_state->page = ATLAS_PAGE_CHAT;
        s_ctx.ui_state->expression = ATLAS_EXPR_LISTEN;
        s_ctx.ui_state->audio_level = 28;
    } else if (strcmp(action, "calendar") == 0) {
        s_ctx.ui_state->page = ATLAS_PAGE_CALENDAR;
        s_ctx.ui_state->expression = ATLAS_EXPR_CURIOUS;
        s_ctx.ui_state->audio_level = 0;
    } else if (strcmp(action, "pomodoro") == 0) {
        s_ctx.ui_state->page = ATLAS_PAGE_POMODORO;
        s_ctx.ui_state->expression = ATLAS_EXPR_THINKING;
        s_ctx.ui_state->audio_level = 0;
    } else if (strcmp(action, "alarm") == 0) {
        s_ctx.ui_state->page = ATLAS_PAGE_ALARM;
        s_ctx.ui_state->expression = ATLAS_EXPR_CURIOUS;
        s_ctx.ui_state->audio_level = 0;
    } else {
        return send_error(req, "400 Bad Request", "bad action");
    }

    s_ctx.ui_state->last_event_ms = ts;
    return send_json(req, "{\"ok\":true,\"app\":\"action\",\"note\":\"mimiclaw placeholder\"}");
}

static esp_err_t stop_handler(httpd_req_t *req)
{
    (void)req;
    esp_err_t err = atlas_ui_stop(s_ctx.ui_state, now_ms());
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"action\":\"stop\"}");
}

static esp_err_t move_handler(httpd_req_t *req)
{
    char body[256];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }
    if (!atlas_config_motion_allowed(s_ctx.config)) {
        return send_error(req, "423 Locked", "motion disabled");
    }
    if (!atlas_config_manual_control_allowed(s_ctx.config)) {
        return send_error(req, "409 Conflict", "manual mode required");
    }

    char dir[8] = "";
    char speed_s[8] = "";
    char duration_s[8] = "";
    (void)form_get_value(body, "dir", dir, sizeof(dir));
    (void)form_get_value(body, "speed", speed_s, sizeof(speed_s));
    (void)form_get_value(body, "duration", duration_s, sizeof(duration_s));

    atlas_voice_event_t event = ATLAS_VOICE_EVENT_NONE;
    if (strcmp(dir, "F") == 0) {
        event = ATLAS_VOICE_EVENT_MOVE_FORWARD;
    } else if (strcmp(dir, "B") == 0) {
        event = ATLAS_VOICE_EVENT_MOVE_BACKWARD;
    } else if (strcmp(dir, "L") == 0) {
        event = ATLAS_VOICE_EVENT_TURN_LEFT;
    } else if (strcmp(dir, "R") == 0) {
        event = ATLAS_VOICE_EVENT_TURN_RIGHT;
    } else {
        return send_error(req, "400 Bad Request", "bad direction");
    }

    atlas_voice_intent_t intent = atlas_voice_intent_from_event(event);
    const int requested_speed = atoi(speed_s);
    const int requested_duration = atoi(duration_s);
    if (requested_speed > 0) {
        intent.speed = (uint8_t)requested_speed;
    }
    if (requested_duration > 0) {
        intent.duration_ms = (uint16_t)requested_duration;
    }
    if (intent.speed > s_ctx.config->safety.max_speed_percent) {
        intent.speed = s_ctx.config->safety.max_speed_percent;
    }
    if (intent.duration_ms > s_ctx.config->safety.max_duration_ms) {
        intent.duration_ms = s_ctx.config->safety.max_duration_ms;
    }

    const esp_err_t err = atlas_ui_handle_voice_intent(s_ctx.ui_state, intent, now_ms());
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"action\":\"move\"}");
}

static esp_err_t save_wifi_handler(httpd_req_t *req)
{
    char body[320];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char ssid[ATLAS_WIFI_SSID_MAX] = "";
    char password[ATLAS_WIFI_PASSWORD_MAX] = "";
    (void)form_get_value(body, "ssid", ssid, sizeof(ssid));
    (void)form_get_value(body, "password", password, sizeof(password));
    if (ssid[0] == '\0') {
        return send_error(req, "400 Bad Request", "ssid required");
    }

    esp_err_t err = atlas_config_save_wifi(ssid, password);
    if (err == ESP_OK) {
        strlcpy(s_ctx.config->wifi_ssid, ssid, sizeof(s_ctx.config->wifi_ssid));
        strlcpy(s_ctx.config->wifi_password, password, sizeof(s_ctx.config->wifi_password));
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"saved\":\"wifi\",\"note\":\"reboot to connect STA\"}");
}

static esp_err_t save_llm_handler(httpd_req_t *req)
{
    char body[640];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    atlas_llm_config_t llm = s_ctx.config->llm;
    (void)form_get_value(body, "mode", llm.mode, sizeof(llm.mode));
    (void)form_get_value(body, "provider", llm.provider, sizeof(llm.provider));
    (void)form_get_value(body, "base_url", llm.base_url, sizeof(llm.base_url));
    (void)form_get_value(body, "model", llm.model, sizeof(llm.model));

    char api_key[ATLAS_LLM_API_KEY_MAX] = "";
    if (form_get_value(body, "api_key", api_key, sizeof(api_key)) && api_key[0] != '\0') {
        strlcpy(llm.api_key, api_key, sizeof(llm.api_key));
    }

    esp_err_t err = atlas_config_save_llm(&llm);
    if (err == ESP_OK) {
        s_ctx.config->llm = llm;
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"saved\":\"llm\"}");
}

static esp_err_t save_safety_handler(httpd_req_t *req)
{
    char body[320];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char value[16] = "";
    atlas_safety_config_t safety = s_ctx.config->safety;
    if (form_get_value(body, "motion_enabled", value, sizeof(value))) {
        safety.motion_enabled = strcmp(value, "1") == 0 || strcmp(value, "true") == 0;
    }
    if (form_get_value(body, "control_mode", value, sizeof(value))) {
        strlcpy(safety.control_mode, value, sizeof(safety.control_mode));
    }
    if (form_get_value(body, "max_speed", value, sizeof(value))) {
        safety.max_speed_percent = (uint8_t)atoi(value);
    }
    if (form_get_value(body, "max_duration", value, sizeof(value))) {
        safety.max_duration_ms = (uint16_t)atoi(value);
    }

    esp_err_t err = atlas_config_save_safety(&safety);
    if (err == ESP_OK) {
        (void)atlas_config_load(s_ctx.config);
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"saved\":\"safety\"}");
}

static esp_err_t save_ui_handler(httpd_req_t *req)
{
    char body[256];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char value[32] = "";
    atlas_ui_config_t ui = s_ctx.config->ui;
    if (form_get_value(body, "theme", value, sizeof(value))) {
        strlcpy(ui.theme, value, sizeof(ui.theme));
    }
    if (form_get_value(body, "brightness", value, sizeof(value))) {
        ui.brightness = (uint8_t)atoi(value);
    }
    if (form_get_value(body, "volume", value, sizeof(value))) {
        ui.volume = (uint8_t)atoi(value);
    }

    esp_err_t err = atlas_config_save_ui(&ui);
    if (err == ESP_OK) {
        (void)atlas_config_load(s_ctx.config);
        atlas_display_set_theme(s_ctx.config->ui.theme);
        atlas_display_set_brightness(s_ctx.config->ui.brightness);
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"saved\":\"ui\"}");
}

static esp_err_t voice_text_handler(httpd_req_t *req)
{
    char body[320];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }

    char text[160] = "";
    (void)form_get_value(body, "text", text, sizeof(text));
    atlas_mimiclaw_result_t result = atlas_mimiclaw_resolve_text(s_ctx.config, text);
    if (is_motion_event(result.intent.event)) {
        if (!atlas_config_motion_allowed(s_ctx.config)) {
            return send_error(req, "423 Locked", "motion disabled");
        }
        if (!atlas_config_ai_control_allowed(s_ctx.config)) {
            return send_error(req, "409 Conflict", "ai mode required");
        }
    }

    const esp_err_t err = atlas_ui_handle_voice_intent(s_ctx.ui_state, result.intent, now_ms());
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }

    char json[220];
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"source\":\"%s\",\"used_llm\":%s,\"event\":\"%s\"}",
             atlas_mimiclaw_source_name(result.source),
             result.used_llm ? "true" : "false",
             atlas_voice_event_name(result.intent.event));
    return send_json(req, json);
}

static esp_err_t reset_handler(httpd_req_t *req)
{
    char body[128];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }
    esp_err_t err = atlas_config_reset_network_and_llm();
    if (err == ESP_OK) {
        (void)atlas_config_load(s_ctx.config);
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"cleared\":\"network_llm\",\"note\":\"reboot recommended\"}");
}

static esp_err_t reboot_handler(httpd_req_t *req)
{
    char body[128];
    ESP_RETURN_ON_ERROR(read_body(req, body, sizeof(body)), TAG, "read body failed");
    if (!authorize_body(body)) {
        return send_error(req, "403 Forbidden", "pairing required");
    }
    (void)send_json(req, "{\"ok\":true,\"rebooting\":true}");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

static esp_err_t register_uri(httpd_handle_t server, const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    const httpd_uri_t route = {
        .uri = uri,
        .method = method,
        .handler = handler,
        .user_ctx = NULL,
    };
    return httpd_register_uri_handler(server, &route);
}

esp_err_t atlas_admin_http_start(atlas_config_t *config,
                                 atlas_ui_state_t *ui_state,
                                 atlas_admin_now_ms_fn_t now_ms_fn)
{
    if (config == NULL || ui_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_ctx.config = config;
    s_ctx.ui_state = ui_state;
    s_ctx.now_ms = now_ms_fn;

    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.server_port = 80;
    http_config.stack_size = 8192;
    http_config.max_uri_handlers = 20;

    ESP_RETURN_ON_ERROR(httpd_start(&s_ctx.server, &http_config), TAG, "httpd_start failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/", HTTP_GET, app_handler), TAG, "route / failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/app", HTTP_GET, app_handler), TAG, "route app failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/admin", HTTP_GET, admin_handler), TAG, "route admin failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/status", HTTP_GET, status_handler), TAG, "route status failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/rover/stop", HTTP_POST, stop_handler), TAG, "route stop failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/rover/move", HTTP_POST, move_handler), TAG, "route move failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/app/expression", HTTP_POST, app_expression_handler), TAG, "route app expression failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/app/page", HTTP_POST, app_page_handler), TAG, "route app page failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/app/action", HTTP_POST, app_action_handler), TAG, "route app action failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/config/wifi", HTTP_POST, save_wifi_handler), TAG, "route wifi failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/config/llm", HTTP_POST, save_llm_handler), TAG, "route llm failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/config/safety", HTTP_POST, save_safety_handler), TAG, "route safety failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/config/ui", HTTP_POST, save_ui_handler), TAG, "route ui failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/voice/text", HTTP_POST, voice_text_handler), TAG, "route voice failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/config/reset", HTTP_POST, reset_handler), TAG, "route reset failed");
    ESP_RETURN_ON_ERROR(register_uri(s_ctx.server, "/api/system/reboot", HTTP_POST, reboot_handler), TAG, "route reboot failed");

    ESP_LOGI(TAG, "admin HTTP server started on port 80");
    return ESP_OK;
}
