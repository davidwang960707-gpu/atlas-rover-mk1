# Atlas DualEye 实机 Trace 字段契约

日期：2026-06-24  
范围：固件体验线 P3，配合服务端实时 trace/JSONL 脚本做真机联调。  
原则：trace 脚本只依赖本文列出的稳定字段；新增字段只能兼容追加，旧字段不得重命名或删除。

## 1. Trace 采集建议

建议每 1-2 秒采样一次：

```bash
curl http://DUALEYE_IP/api/status/lite
curl http://DUALEYE_IP/api/diagnostics/turn
curl http://DUALEYE_IP/api/audio/opus-stream/status
curl http://DUALEYE_IP/api/selftest
curl http://DUALEYE_IP/api/tools/list
```

JSONL 建议每行至少记录：

```json
{"ts":"2026-06-24T20:00:00+08:00","endpoint":"/api/status/lite","ok":true,"body":{}}
```

## 2. `/api/status/lite`

用途：手机端和 trace 的主状态源，优先用于判断页面、左右屏预期、Brain 离线、本地应用状态。

| 字段 | 含义 | 变化预期 |
|---|---|---|
| `ok` | HTTP JSON 是否由固件正常返回 | 应为 `true` |
| `firmware` / `fingerprint.*` | 固件、资源、工具表版本 | 单次烧录后稳定 |
| `ui.page` | 应用层当前请求页面 | 工具/手机按钮切页后变化 |
| `scene.page` | scene resolver 最终渲染页面 | 可被语音、离线、运行态覆盖 |
| `experience.ui.current_page` | 与 `ui.page` 对齐的快速字段 | 切页后应更新 |
| `experience.ui.rendered_page` | 与 `scene.page` 对齐的快速字段 | 判断“按钮已到但被覆盖” |
| `experience.ui.left_screen` | 左屏预期内容，如 `pet_head`、`analog_clock` | 当前页面或 chat mode 变化后更新 |
| `experience.ui.right_screen` | 右屏预期内容，如 `short_text`、`digital_clock` | 当前页面或 chat mode 变化后更新 |
| `experience.ui.display_screens` | 固件预期屏幕数量 | DualEye 应为 `2` |
| `experience.ui.last_page_change_reason` | 最近页面变化来源 | 例：`mobile nav page`、`tool call`、`manual action` |
| `experience.ui.last_page_change_ms` | 最近页面变化毫秒时间戳 | 切页成功后递增 |
| `experience.voice.playback_recovered` | 最近一次播放/turn 后是否恢复到非 busy | 语音播放后应回到 `true` |
| `experience.voice.recovery_reason` | 恢复或卡住原因 | 异常时用于定位 |
| `experience.offline.brain_reason` | Brain 离线原因 | Brain 在线时为空 |
| `wifi.sta_connected` / `wifi.ap_started` | STA/AP 配网状态 | Wi-Fi 断开时 AP 应仍可用 |
| `brain_ws.connected` / `brain_ws.stage` | Brain WebSocket 状态 | Brain 启动后应连上 |
| `apps.clock/calendar/pomodoro` | 本地桌面应用状态 | Brain 离线时仍可读 |
| `audio_stream.frames_sent` | OPUS 发送帧数摘要 | 开始 stream 后递增 |

判读规则：

- `current_page` 变了但 `rendered_page` 没变：页面请求到了，但被 scene/运行态覆盖。
- `last_page_change_ms` 不变：手机按钮或工具调用大概率没有到设备，先看网络、PIN、HTTP 返回。
- `left_screen/right_screen` 是固件预期，不是像素采样；实机单屏亮时用它判断是否硬件/背光/资源问题。

## 3. `/api/diagnostics/turn`

用途：语音 turn 和播放恢复的专项诊断。trace 中在每次语音前后采样。

| 字段 | 含义 | 变化预期 |
|---|---|---|
| `voice_wake.enabled` | 连续监听开关 | 打开连续监听后为 `true` |
| `voice_wake.busy` | 唤醒任务是否正在处理 turn | turn 中短暂为 `true` |
| `voice_wake.mute_ms` | 播放/手动操作后的监听静音剩余 | 播放后递减到 0 |
| `voice_wake.reason` | 最近监听原因 | 例：`idle`、`level`、`muted`、错误名 |
| `experience.voice.playback_recovered` | 播放后恢复状态 | turn 完成后应为 `true` |
| `experience.voice.recovery_reason` | runtime 恢复/失败原因 | 卡住时优先看这里 |
| `experience.ui.*` | 同 status/lite 的页面与左右屏摘要 | 用于判断语音是否覆盖页面 |
| `scene.severity` | 用户可感知严重度 | Brain 离线多为 `warn` |
| `audio_service.busy/job_running` | 音频服务是否仍占用 | 长时间 true 说明卡住 |
| `audio_service.last_failure` | 最近音频失败说明 | 失败时必须非空或可解释 |
| `runtime.state/reason` | 运行态状态机 | turn 后应回 `idle` 或 `monitoring` 相关原因 |

## 4. `/api/audio/opus-stream/status`

用途：定位 OPUS 60ms 上行链路、frames=0、编码/采集/发送失败。

| 字段 | 含义 | 变化预期 |
|---|---|---|
| `ok` | status wrapper 是否正常 | 应为 `true` |
| `protocol` | OPUS stream 协议 | 固定 `atlas.opus.stream.v0` |
| `binary_header` | 二进制帧头 | 固定 `AOP1` |
| `stream.running` | stream 任务是否运行 | start 后为 `true` |
| `stream.connected` | WebSocket 是否已连接 | Brain 在线且握手后为 `true` |
| `stream.stage` | 当前阶段 | 例：`idle`、`connecting`、`opus_streaming` |
| `stream.frame_ms` | 单帧时长 | 应为 `60` |
| `stream.sample_rate` | 采样率 | 应为 `16000` |
| `stream.frames_encoded` | 已编码帧数 | 采集编码成功后递增 |
| `stream.frames_sent` | 已发送帧数 | 连接成功后递增 |
| `stream.capture_failures` | 采集失败数 | 大于 0 时看音频硬件 |
| `stream.encode_failures` | OPUS 编码失败数 | 大于 0 时看编码资源/内存 |
| `stream.send_failures` | WS 发送失败数 | 大于 0 时看 Brain/网络 |
| `stream.muted_frames` | 播放/静音期间跳过帧 | TTS 后可能增长 |
| `stream.last_error` | 最近 ESP 错误名 | `ESP_OK` 表示无最近错误 |

frames=0 判读：

- `running=false`：尚未启动 stream。
- `running=true` 且 `connected=false`：Brain WS/网络问题。
- `connected=true` 且 `capture_failures>0`：麦克风采集问题。
- `connected=true` 且 `encode_failures>0`：OPUS 编码或内存问题。
- `muted_frames` 增长：播放恢复期间静音跳帧，不一定是错误。

## 5. `/api/selftest`

用途：开局和问题复现后读一次，判断 fail/warn 类别。

| 字段 | 含义 | 变化预期 |
|---|---|---|
| `summary.fail` | 阻断核心体验的问题数 | 晚上首轮应为 `0` |
| `summary.warn` | 可降级问题数 | Brain 离线、WakeNet/AEC 未启用可以是 warn |
| `checks[].id=eye_assets` | 眼睛/宠物头资源 | 资源缺失会影响 pet_head/主题 |
| `checks[].id=audio_hw` | 麦克风/喇叭硬件 | fail 会影响语音 |
| `checks[].id=brain_ws` | Brain WS 期望状态 | Brain 未启动时不应造成本地页面 fail |
| `checks[].id=experience_voice` | 连续语音可观测性 | 应能看到 reason/recovery |
| `checks[].id=experience_ui_modes` | chat 三模式与应用状态 | 应为 pass |
| `checks[].id=display_surfaces` | 当前双屏预期与最近切页原因 | 单屏/黑屏优先看 detail |
| `checks[].id=offline_fallback` | 离线兜底 | 应为 pass/warn，不应 fail |
| `checks[].id=experience_tools` | 工具调用覆盖 | 应为 pass |
| `checks[].id=motion_boundary` | motion 禁用边界 | 桌面版应保持 disabled/not_supported |

## 6. `/api/tools/list`

用途：服务端 trace 在调用工具前确认固件侧能力边界。

| 字段 | 含义 | 变化预期 |
|---|---|---|
| `ok` | 工具表返回正常 | 应为 `true` |
| `protocol` | 工具表版本 | 当前为 `atlas.tools.v0.desk_apps` |
| `schema_protocol` | schema 协议 | 当前为 `atlas.tool.schema.v0` |
| `tool_count` | 工具数量 | 固件升级时可增加，不应突然为 0 |
| `call_endpoint` | HTTP tool call 路径 | `/api/tools/call` |
| `mcp_call_endpoint` | MCP-like call 路径 | `/mcp/tools/call` |
| `capabilities.display` | 页面/表情/主题能力 | 应为 `true` |
| `capabilities.pet_head` | 宠物头能力 | 应为 `true` |
| `capabilities.audio_opus_stream` | OPUS 状态能力 | 应为 `true` |
| `capabilities.rover_motion` | 小车运动 | 桌面版必须为 `false` |
| `tools[].name` | 工具名 | 主链路至少包含页面、chat mode、clock、pomodoro、calendar、pet、audio、selftest |
| `tools[].enabled` | 工具是否可执行 | motion 工具应为 `false` |
| `result_codes` | 失败码集合 | trace 解析 `error_code` 时使用 |

主链路工具：

- `atlas.show_page`
- `atlas.set_expression`
- `atlas.set_theme`
- `atlas.ui.set_chat_mode`
- `atlas.clock.show` / `atlas.clock.sync`
- `atlas.calendar.today` / `atlas.calendar.set_note`
- `atlas.pomodoro.show` / `atlas.pomodoro.start`
- `atlas.pet.set_state` / `atlas.pet.play_animation`
- `atlas.brain.offline_status`
- `atlas.audio.opus_stream.status`
- `atlas.selftest.run`

## 7. 最小 JSON 样例

### 7.1 Brain 离线但本地页面可用

```json
{
  "endpoint": "/api/status/lite",
  "ok": true,
  "ui": {"page": "clock", "chat_mode": "pet_head"},
  "scene": {"page": "clock", "severity": "warn", "state": "brain_offline"},
  "experience": {
    "protocol": "atlas.dualeye.experience.v0",
    "ui": {
      "current_page": "clock",
      "rendered_page": "clock",
      "left_screen": "analog_clock",
      "right_screen": "digital_clock",
      "display_screens": 2,
      "local_apps_available": true
    },
    "offline": {
      "brain_online": false,
      "brain_reason": "connect_failed",
      "wifi_connected": true
    }
  },
  "brain_ws": {"connected": false, "stage": "connect_failed"},
  "apps": {
    "clock": {"enabled": true, "synced": true},
    "calendar": {"enabled": true},
    "pomodoro": {"enabled": true}
  }
}
```

判读：Brain 离线，但 `current_page/rendered_page=clock`，`local_apps_available=true`，本地页面应继续显示。若实机黑屏，优先查屏幕/资源/背光，而不是 Brain。

### 7.2 页面从 eyes 切到 pomodoro

```json
{
  "endpoint": "/api/status/lite",
  "ok": true,
  "ui": {"page": "pomodoro", "expression": "thinking"},
  "scene": {"page": "pomodoro", "severity": "ok"},
  "experience": {
    "ui": {
      "current_page": "pomodoro",
      "rendered_page": "pomodoro",
      "last_page_change_page": "pomodoro",
      "last_page_change_reason": "tool call",
      "last_page_change_ms": 84210,
      "left_screen": "pomodoro_timer",
      "right_screen": "pomodoro_task",
      "display_screens": 2
    }
  },
  "apps": {
    "pomodoro": {
      "enabled": true,
      "running": true,
      "task": "focus",
      "progress_percent": 3,
      "remaining_ms": 1450000
    }
  }
}
```

判读：工具或按钮请求已到达设备，scene 没有覆盖页面。若屏幕仍是 eyes，记录照片和串口；若 `current_page=pomodoro` 但 `rendered_page` 不是 pomodoro，则查语音/离线/运行态覆盖。

### 7.3 OPUS frames=0 排查样例

```json
{
  "endpoint": "/api/audio/opus-stream/status",
  "ok": true,
  "protocol": "atlas.opus.stream.v0",
  "binary_header": "AOP1",
  "stream": {
    "running": true,
    "connected": false,
    "stage": "connecting",
    "frame_ms": 60,
    "sample_rate": 16000,
    "frames_encoded": 0,
    "frames_sent": 0,
    "send_failures": 0,
    "capture_failures": 0,
    "encode_failures": 0,
    "muted_frames": 0,
    "last_error": "ESP_OK"
  }
}
```

判读：stream 已启动但未连接 Brain，所以 frames=0 先查 Brain URL、Wi-Fi、`brain_ws.connected`。如果 `connected=true` 仍 frames=0，再看 `capture_failures/encode_failures/muted_frames`。

## 8. 推荐联调顺序

1. 启动 trace 脚本，先采 `/api/status/lite`、`/api/selftest`、`/api/tools/list`。
2. Brain 不启动，切 `eyes -> clock -> pomodoro -> calendar -> chat`，确认 `last_page_change_ms` 和 `left_screen/right_screen` 随操作变化。
3. 启动 Brain，等待 `brain_ws.connected=true`，再采一次 `/api/status/lite`。
4. 跑一次语音 turn，前后采 `/api/diagnostics/turn`，确认 `audio_service.busy` 回落、`playback_recovered` 可解释。
5. 启动 OPUS stream，采 `/api/audio/opus-stream/status`，确认 `running/connected/frames_sent`。
6. 如果出现失败，把对应 endpoint 的 JSONL 行、照片和串口日志放到同一条问题记录里。
