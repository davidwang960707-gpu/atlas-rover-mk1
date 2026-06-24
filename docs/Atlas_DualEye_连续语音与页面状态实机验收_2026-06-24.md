# Atlas DualEye 连续语音与页面状态实机验收

日期：2026-06-24  
范围：固件体验线 P4，配合 Atlas Brain 实时 trace 脚本验证连续对话、页面切换、离线兜底和 OPUS 状态。

## 1. 验收目标

晚上真机遇到“连续语音又断了”“页面按钮没反应”“Brain 离线变异常页”“番茄或日历黑屏”时，优先通过 DualEye 端 JSON 判断归因，而不是只看屏幕猜。

核心接口：

```bash
curl http://DUALEYE_IP/api/status/lite
curl http://DUALEYE_IP/api/diagnostics/turn
curl http://DUALEYE_IP/api/audio/opus-stream/status
curl http://DUALEYE_IP/api/selftest
curl http://DUALEYE_IP/api/tools/list
```

## 2. 第一轮测试顺序

| 顺序 | 操作 | 主要接口 | 必看字段 | 通过标准 |
|---|---|---|---|---|
| 1 | 开机后不启动 Brain | `/api/status/lite` | `brain_ws.connected`、`experience.offline.brain_reason`、`experience.ui.local_apps_available` | Brain 离线是结构化状态，本地页面可用 |
| 2 | 切 `eyes -> clock -> pomodoro -> calendar -> chat` | `/api/status/lite` | `experience.ui.current_page`、`rendered_page`、`last_page_change_reason`、`left_screen/right_screen` | 每次切页都有 reason 和左右屏预期 |
| 3 | 切 `pet_head/text/eyes_only` | `/api/status/lite` | `ui.chat_mode`、`experience.ui.left_screen/right_screen`、`chat.text` | 三模式都能解释当前屏幕预期 |
| 4 | 启动 Brain，等待 WS | `/api/status/lite` | `brain_ws.connected`、`brain_ws.stage`、`experience.offline.brain_reason` | 在线后 `brain_ws.connected=true`，离线原因为空 |
| 5 | 连续语音 2-3 轮 | `/api/diagnostics/turn`、`/api/status/lite` | `voice_wake.reason`、`audio_service.busy`、`runtime.turns[0]`、`experience.voice.*` | 播放后 busy 回落，reason 可解释 |
| 6 | 启动 OPUS stream | `/api/audio/opus-stream/status` | `stream.running`、`connected`、`frames_sent`、`capture_failures`、`encode_failures` | Brain 在线后 frames 递增或失败可归因 |
| 7 | 跑自检收尾 | `/api/selftest` | `summary.fail`、`experience_voice`、`display_surfaces`、`offline_fallback` | `summary.fail=0`，warn 可解释 |

## 3. 连续语音字段

| 字段 | 看哪里 | 含义 | 异常归因 |
|---|---|---|---|
| `voice_wake.enabled` | `/api/status/lite`、`/api/diagnostics/turn` | 连续监听是否开启 | `false` 且需要连续对话时，检查启动参数或 Brain 配置 |
| `voice_wake.busy` | `/api/diagnostics/turn` | 唤醒 turn 是否处理中 | 长时间 `true` 表示 turn 卡住 |
| `voice_wake.mute_ms` | `/api/diagnostics/turn` | 播放/手动 UI 后静音剩余 | 递减是正常恢复；不变才可疑 |
| `voice_wake.reason` | 两个接口 | 最近唤醒/静音/失败原因 | `host bridge not configured`、`muted`、`ESP_*` 可直接归因 |
| `audio_service.mode` | 两个接口 | 音频服务状态 | 应在 turn 后回 `idle` 或 `monitoring` |
| `audio_service.busy` | 两个接口 | 录音/播放/任务是否占用 | 长时间 true 是卡住主线 |
| `audio_service.last_failure` | 两个接口 | 最近失败文本 | 非空时优先记录 |
| `experience.voice.playback_recovered` | 两个接口 | 播放后是否恢复 | `false` 时看 `recovery_reason` 和 `audio_service.busy` |
| `experience.voice.turn_count` | 两个接口 | 服务层 turn 计数 | 说话后不变说明 turn 未进入服务层 |
| `experience.voice.playback_count` | 两个接口 | 播放计数 | Brain 有回复但不增长，查 TTS/播放 |
| `experience.voice.job_error_count` | 两个接口 | turn job 错误数 | 增长时查 diagnostics 的 runtime error |
| `runtime.turns[0].played` | `/api/diagnostics/turn` | 最近 turn 是否成功播放 | `false` 时看 `play_error` |
| `runtime.turns[0].bridge_ok` | `/api/diagnostics/turn` | Brain turn 是否成功 | `false` 时看 Brain trace 和 `error` |

## 4. 页面与离线字段

| 字段 | 看哪里 | 含义 | 异常归因 |
|---|---|---|---|
| `experience.ui.current_page` | `/api/status/lite` | 应用层请求的页面 | 没变化说明按钮/工具请求没到 |
| `experience.ui.rendered_page` | `/api/status/lite` | scene 最终渲染页面 | 与 current 不同表示被运行态或离线 scene 覆盖 |
| `experience.ui.last_page_change_reason` | `/api/status/lite` | 最近切页来源 | `mobile nav page`、`tool call`、`manual action` |
| `experience.ui.left_screen/right_screen` | `/api/status/lite` | 双屏预期内容 | 单屏/黑屏时与实物比对 |
| `experience.ui.scene_severity` | `/api/status/lite` | 用户可感知严重度 | `warn/error` 时看 `scene` |
| `experience.offline.brain_reason` | `/api/status/lite` | Brain 离线原因 | 离线不应导致本地应用黑屏 |
| `apps.clock/calendar/pomodoro` | `/api/status/lite` | 本地应用状态 | Brain 离线时仍应可读 |
| `checks[].id=display_surfaces` | `/api/selftest` | 双屏预期和最近切页 | 现场复盘页面/单屏问题 |
| `checks[].id=offline_fallback` | `/api/selftest` | 离线兜底状态 | 应为 pass/warn，不应 fail |

## 5. OPUS / frames=0 归因

| 字段 | 看哪里 | 含义 | 异常归因 |
|---|---|---|---|
| `stream.running` | `/api/audio/opus-stream/status` | stream 任务是否启动 | `false` 表示还没 start |
| `stream.connected` | 同上 | WS 是否连上 Brain | `false` 先查 Brain URL/Wi-Fi |
| `stream.stage` | 同上 | 当前阶段 | `connecting` 多为网络/Brain 端 |
| `stream.frames_encoded` | 同上 | 编码帧数 | 0 且 connected=true 时查采集/编码 |
| `stream.frames_sent` | 同上 | 发送帧数 | 在线后应增长 |
| `stream.capture_failures` | 同上 | 采集失败 | 麦克风/I2S/音频硬件 |
| `stream.encode_failures` | 同上 | 编码失败 | OPUS/内存/资源 |
| `stream.send_failures` | 同上 | 发送失败 | Brain WS/网络 |
| `stream.muted_frames` | 同上 | 静音跳帧 | 播放后增长可正常 |

## 6. 常见失败判断

| 症状 | 先判断 | 下一步 |
|---|---|---|
| 连续语音断了 | `voice_wake.enabled`、`audio_service.busy`、`runtime.turns[0].phase` | 如果 turn_count 不增长，查唤醒；如果 bridge_ok=false，查 Brain；如果 played=false，查 TTS/播放 |
| 播放后不恢复 | `experience.voice.playback_recovered=false`、`audio_service.mode` | 看 `mute_ms` 是否递减；看 `last_failure` 和 `runtime.turns[0].play_error` |
| 页面按钮没反应 | `last_page_change_ms` 是否变化 | 不变查手机/PIN/网络；变化但 rendered_page 不变查 scene 覆盖 |
| Brain 离线变异常页 | `experience.offline.brain_reason`、`scene.severity`、`local_apps_available` | 本地应用应继续可切；若黑屏，查 display_surfaces 和资源 |
| 番茄/日历黑屏 | `current_page/rendered_page`、`left_screen/right_screen`、`apps.*` | 字段正确但屏幕黑，记录照片和串口，查屏幕/资源/字体 |
| pet_head 没出现 | `ui.chat_mode`、`left_screen=pet_head`、`eye_assets` | 如果页面不使用 chat mode，先切 chat/voice/music/story |
| OPUS frames=0 | `running/connected/frames_sent/failures` | 按第 5 节归因 |
| 自检 fail | `/api/selftest.summary.fail` | 先修 fail；Brain 离线和 WakeNet/AEC 未启用通常应是 warn |

## 7. 与 Brain trace 脚本配合

建议 JSONL 事件顺序：

1. `dualeye.status_lite.before_brain`
2. `dualeye.selftest.before_brain`
3. `dualeye.tools_list`
4. `dualeye.page_switch.eyes_clock_pomodoro_calendar_chat`
5. `brain.start`
6. `dualeye.status_lite.brain_online`
7. `dualeye.voice_turn.before`
8. `dualeye.diagnostics_turn.after`
9. `dualeye.opus_stream.status`
10. `dualeye.selftest.after`

每条问题至少附：

```text
endpoint:
timestamp:
current_page/rendered_page:
left_screen/right_screen:
brain_ws.connected/stage:
voice_wake.reason:
audio_service.mode/busy/last_failure:
runtime.turns[0]:
opus stream status:
照片或短视频:
```
