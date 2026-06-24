# Atlas DualEye 三线体验实机测试矩阵

日期：2026-06-24  
范围：固件体验线 P1，面向晚上 DualEye 真机验收。  
固件目标：语音连续对话、双目宠物/应用体验、Brain 离线兜底可以通过屏幕和接口明确判断。

## 1. 测试前准备

1. 烧录本轮固件后，确认设备能进入本地页面，不出现黑屏或通用 `ESP_ERR_*` 异常文字页。
2. 记录 DualEye IP，后文用 `DUALEYE_IP` 代替。
3. Mac Brain 可先不启动。第一轮先验证离线兜底，再启动 Brain 做在线语音。
4. 所有接口先读 lite，再按需要读 full：

```bash
curl http://DUALEYE_IP/api/status/lite
curl http://DUALEYE_IP/api/diagnostics/turn
curl http://DUALEYE_IP/api/selftest
curl http://DUALEYE_IP/api/tools/list
```

## 2. 第一轮建议顺序

| 顺序 | 项目 | 目的 |
|---|---|---|
| 1 | `/api/status/lite` | 确认 `experience`、`scene`、`ui.chat_mode`、`apps`、`brain_ws`、`wifi` 可读 |
| 2 | Brain 离线页面 | 确认离线是结构化状态，本地双眼/时钟/番茄/日历/宠物头仍可切 |
| 3 | chat mode 三态 | 确认 `pet_head`、`text`、`eyes_only` 屏幕呈现符合预期 |
| 4 | 工具调用 | 用 `/api/tools/call` 驱动页面、表情、主题、番茄、日历、pet_head |
| 5 | 自检 | 确认 `/api/selftest.summary.fail=0`，体验检查项存在 |
| 6 | OPUS 探针 | 确认 60ms OPUS 探针接口可返回结构化状态 |
| 7 | 启动 Brain | 验证 WebSocket 在线、语音 turn、播放后恢复 |
| 8 | Wi-Fi 未连/AP fallback | 确认 AP 模式和本地 UI 兜底仍可用 |

## 3. 体验矩阵

| ID | 场景 | 操作 | 屏幕预期 | 接口字段预期 | 通过标准 |
|---|---|---|---|---|---|
| V1 | 语音连续监听空闲 | `curl /api/status/lite`，再打开连续监听 | 语音页或宠物页保持可用，不黑屏 | `experience.voice.continuous_enabled`、`voice_wake.reason`、`audio_service.continuous_enabled` 可读 | 能判断监听开关和最近原因 |
| V2 | TTS 播放后恢复 | 触发一次 `/api/voice/turn?async=1` 或 Brain 语音回复 | 播放后回到宠物/双眼/监听状态 | `experience.voice.playback_recovered`、`experience.voice.recovery_reason`、`runtime.reason` 有恢复线索 | 不停在 busy/异常文字页 |
| V3 | 连续监听失败 | 断 Brain 或禁用 Provider 后触发语音 | 显示 Brain 离线/等待配置一类状态 | `/api/diagnostics/turn.experience.voice.continuous_reason`、`runtime.reason`、`audio_service.last_failure` 可定位 | 失败原因结构化可读 |
| U1 | `pet_head` 模式 | 工具调用 `atlas.ui.set_chat_mode` 为 `pet_head` | 左屏宠物头，右屏短文本 | `ui.chat_mode=pet_head`，`experience.ui.chat_mode=pet_head`，`pet.phase` 可读 | 宠物头不丢透明底，不黑屏 |
| U2 | `text` 模式 | 工具调用 `atlas.ui.set_chat_mode` 为 `text` | 双屏文字或文字主导呈现 | `ui.chat_mode=text`，`chat.text` 可读 | 文字不变异常页 |
| U3 | `eyes_only` 模式 | 工具调用 `atlas.ui.set_chat_mode` 为 `eyes_only` | 简单双眼表情 | `ui.chat_mode=eyes_only`，`ui.expression` 可读 | 只显示眼睛表情也能返回状态 |
| A1 | 时钟 | 工具或 Web 控制端切到时钟 | 双屏时钟/日期正常 | `apps.clock.enabled=true`，`apps.clock.time/date` 可读 | 离线也可显示 |
| A2 | 番茄 | 调用番茄开始/暂停工具 | 番茄状态和宠物表情联动 | `apps.pomodoro.running/progress_percent/remaining_ms` 可读 | 状态变化不依赖 Brain |
| A3 | 日历 | 设置或展示日历 | 日历标题/备注正常 | `apps.calendar.enabled/title/note` 可读 | 中文字体 fallback 正常 |
| A4 | 故事/音乐体验 | 触发故事/音乐页或表情 | 宠物状态进入 speak/sing/think 等 | `scene.severity` 非 error，`pet.phase`、`ui.expression` 可读 | 不进入通用异常页 |
| O1 | Brain 离线 | 关闭 Mac Brain 或断开 WS | 屏幕仍能切本地应用，语音页提示离线/等待配置 | `brain_ws.connected=false`，`experience.offline.brain_reason` 非空，`voice.offline_reason` 可读 | 离线是 warn/结构化状态 |
| O2 | Provider 未配置 | 清空或禁用 Provider 配置后查看状态 | 本地页面可用，语音显示等待配置 | `llm.configured=false` 或 `voice.offline_reason` 有原因 | 无异常文字页 |
| O3 | Wi-Fi 未连/AP 模式 | 让 STA 不可用，进入 AP fallback | AP 配网页和本地 UI 可见 | `wifi.sta_connected=false`，`wifi.ap_started=true`，`experience.offline.wifi_connected=false` | 手机端仍能配网 |
| T1 | 工具列表 | `curl /api/tools/list` 和 `/mcp/tools/list` | 无屏幕变化要求 | 工具包含 display/chat/clock/pomodoro/calendar/pet_head/audio/OTA；motion disabled | 工具表不缺主链路 |
| T2 | 工具调用成功 | `/api/tools/call` 调页面/主题/表情/pet_head | 屏幕响应工具结果 | 返回 `ok=true`、`error_code=ok`、状态可在 `/api/status/lite` 看到 | 工具能驱动 UI |
| T3 | 工具调用失败 | 传入非法 tool 或非法参数 | 屏幕不进异常页 | 返回 `ok=false`、结构化 `error_code` | 失败可诊断 |
| P1 | OPUS 探针 | `curl /api/audio/opus-probe` | 无屏幕变化要求 | 返回 OPUS/60ms/采集编码状态 | 有结构化失败原因 |
| S1 | 自检 | `curl /api/selftest` | 无屏幕变化要求 | `summary.fail=0`，存在 `experience_voice`、`experience_ui_modes`、`offline_fallback`、`experience_tools` | 自检能指导下一步 |

## 4. 工具调用样例

工具列表：

```bash
curl http://DUALEYE_IP/api/tools/list
curl http://DUALEYE_IP/mcp/tools/list
```

切换 chat mode：

```bash
curl -X POST http://DUALEYE_IP/api/tools/call \
  -H 'Content-Type: application/json' \
  -d '{"pin":"PAIRING_PIN","name":"atlas.ui.set_chat_mode","arguments":{"chat_mode":"pet_head"}}'
```

切换页面/表情：

```bash
curl -X POST http://DUALEYE_IP/api/tools/call \
  -H 'Content-Type: application/json' \
  -d '{"pin":"PAIRING_PIN","name":"atlas.show_page","arguments":{"page":"chat"}}'

curl -X POST http://DUALEYE_IP/api/tools/call \
  -H 'Content-Type: application/json' \
  -d '{"pin":"PAIRING_PIN","name":"atlas.set_expression","arguments":{"expression":"happy"}}'
```

查看诊断：

```bash
curl http://DUALEYE_IP/api/status/lite
curl http://DUALEYE_IP/api/diagnostics/turn
curl http://DUALEYE_IP/api/selftest
```

## 5. 判定规则

| 级别 | 说明 |
|---|---|
| P0 必过 | 不黑屏、不进入通用异常文字页，本地双眼/时钟/番茄/日历/pet_head 可切 |
| P0 必过 | Brain 离线、Provider 未配置、Wi-Fi 未连都有结构化 reason |
| P0 必过 | `/api/tools/call` 成功和失败都返回结构化 `error_code` |
| P1 可带回 | OPUS 或 WakeNet/AEC 真机资源不足，但接口必须给出明确原因 |
| P1 可带回 | 宠物动画帧率或表情映射不够细，但不能影响主页面可用 |

## 6. 需要记录的真机信息

- `/api/status/lite.experience`
- `/api/status/lite.scene`
- `/api/status/lite.audio_service`
- `/api/status/lite.brain_ws`
- `/api/diagnostics/turn.experience`
- `/api/selftest.summary` 和 `checks`
- `atlas_rover_dualeye.bin` 大小
- 串口中首个 voice turn 的失败或恢复日志
