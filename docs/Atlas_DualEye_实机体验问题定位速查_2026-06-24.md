# Atlas DualEye 实机体验问题定位速查

日期：2026-06-24  
范围：固件体验线 P2，面向手机连设备后的现场排障。  
原则：先看 `/api/status/lite`，再看专项接口；先判断请求是否到设备，再判断页面是否被 scene/离线兜底覆盖。

## 1. 快速入口

把 `DUALEYE_IP` 替换为设备 IP：

```bash
curl http://DUALEYE_IP/api/status/lite
curl http://DUALEYE_IP/api/diagnostics/turn
curl http://DUALEYE_IP/api/selftest
curl http://DUALEYE_IP/api/tools/list
curl http://DUALEYE_IP/api/audio/opus-stream/status
```

最先看这几个字段：

| 目的 | 字段 |
|---|---|
| 当前应用页 | `ui.page`、`experience.ui.current_page` |
| 最终渲染页 | `scene.page`、`experience.ui.rendered_page` |
| 左右屏预期 | `experience.ui.left_screen`、`experience.ui.right_screen`、`experience.ui.display_screens` |
| 最近切页来源 | `experience.ui.last_page_change_reason`、`experience.ui.last_manual_override_reason` |
| Brain 离线 | `brain_ws.connected`、`experience.offline.brain_reason`、`voice.offline_reason` |
| 语音恢复 | `experience.voice.playback_recovered`、`runtime.reason`、`audio_service.last_failure` |
| 工具调用 | `/api/tools/call` 返回的 `ok`、`error_code`、`result.page` |
| 自检 | `/api/selftest.summary.fail`、`checks[].id/status/detail` |

## 2. 症状速查表

| 症状 | 先看接口 | 关键字段 | 可能原因 | 下一步操作 |
|---|---|---|---|---|
| 页面切换按钮无效 | `/api/status/lite` | `experience.ui.last_manual_override_reason`、`last_page_change_ms`、`current_page`、`rendered_page` | 手机请求未到设备；PIN/认证失败；scene 因语音/离线覆盖手动页 | 先重新点一次页面按钮，再刷新 status；若 `last_manual_override_ms` 不变，检查手机是否连到 DualEye AP/STA 和 PIN |
| 工具切页无效 | `/api/tools/call`，再 `/api/status/lite` | `error_code`、`result.page`、`experience.ui.last_page_change_reason=tool call` | tool 名或参数错误；工具返回失败；页面被运行态覆盖 | 先确认 `/api/tools/list` 有该工具；失败时按 `error_code` 修参数 |
| Brain 离线 | `/api/status/lite` | `brain_ws.connected=false`、`experience.offline.brain_reason`、`voice.offline_reason` | Mac Brain 未启动；Brain URL 错；Wi-Fi 未连；Provider 未配置 | 本地双眼/时钟/番茄/日历应仍可切；再检查 `llm.configured`、`wifi.sta_connected`、`brain_ws.url` |
| 语音播报后不恢复 | `/api/diagnostics/turn` | `experience.voice.playback_recovered`、`recovery_reason`、`runtime.state/reason`、`audio_service.busy` | TTS 播放后 audio service 未回 idle/monitoring；连续监听被 mute；Brain turn 失败 | 若 `audio_service.busy=true` 持续不变，记录 diagnostics；若 `mute_ms` 递减，等待恢复 |
| 宠物头没出现 | `/api/status/lite`，`/api/selftest` | `ui.chat_mode`、`experience.ui.left_screen=pet_head`、`pet.asset_id`、`eye_assets` | chat mode 不是 `pet_head`；pet_head 资源缺失；当前页面不使用 chat mode | 调 `atlas.ui.set_chat_mode` 为 `pet_head`；若 selftest 资源 warn/fail，检查 SPIFFS 资源包 |
| text/eyes_only 显示不对 | `/api/status/lite` | `ui.chat_mode`、`experience.ui.left_screen/right_screen`、`chat.text` | 模式未保存；页面不是 chat/voice/music/story；scene 覆盖 | 切到 chat 页后再切 chat mode；比对 `current_page` 与 `rendered_page` |
| 时钟单屏或黑屏 | `/api/status/lite` | `experience.ui.left_screen=analog_clock`、`right_screen=digital_clock`、`apps.clock.synced` | 页面没有切到 clock；时钟未校准；屏幕硬件/背光问题 | 调 `/api/app/action action=clock.sync` 或工具 `atlas.clock.sync`；若预期双屏但实机单屏，记录串口/拍照 |
| 番茄单屏或黑屏 | `/api/status/lite` | `experience.ui.left_screen=pomodoro_timer`、`right_screen=pomodoro_task`、`apps.pomodoro.running` | 页面没有切到 pomodoro；任务状态未更新；scene 覆盖 | 调 `atlas.pomodoro.show` 或 `atlas.pomodoro.start`；刷新 status 确认 progress |
| 日历单屏或黑屏 | `/api/status/lite` | `experience.ui.left_screen=calendar_title`、`right_screen=calendar_note`、`calendar.title/note` | 页面未切到 calendar；日历文本为空；中文字体 fallback 问题 | 调 `atlas.calendar.today` 或 `atlas.calendar.set_note`；若字段有值但屏幕空，记录资源/字体自检 |
| Wi-Fi 配置失败 | `/api/status/lite`，`/api/wifi/scan` | `wifi.sta_connected`、`wifi.ap_started`、`wifi.ap_ssid`、`experience.offline.wifi_connected` | 手机没连设备 AP；SSID/密码错误；路由器距离远 | 先用 `/api/wifi/scan` 确认能扫到目标 SSID；STA 失败时应保留 AP 配网 |
| 只有 AP 无 STA | `/api/status/lite` | `wifi.mode`、`sta_connected=false`、`ap_started=true` | 无有效 Wi-Fi 配置或 STA 连接失败 | 用手机连 `wifi.ap_ssid`，重新写 `/api/config/wifi` |
| OPUS frames=0 | `/api/audio/opus-stream/status`，`/api/audio/opus-probe` | `audio_stream.running`、`connected`、`frames_sent`、`capture_failures`、`encode_failures`、`stage` | stream 未启动；Brain/WS 未连；采集或编码失败 | 先跑 `/api/audio/opus-probe`；再 start stream；Brain 离线时 frames 可能为 0 |
| 自检 fail | `/api/selftest` | `summary.fail`、失败的 `checks[].id/detail` | 资源、音频硬件、OTA 分区、内存或工具表异常 | 先处理 fail；Brain 离线、WakeNet/AEC 未启用通常应是 warn 而非 fail |
| 小车 motion 不可用 | `/api/tools/list`，`/api/selftest` | `rover_motion=false`、`motion_boundary`、`error_code=not_supported` | 桌面宠物固件按策略禁用 motion | 不作为本轮问题；不要把 motion 恢复为主功能 |

## 3. 推荐第一轮真机顺序

1. 打开 `/api/status/lite`，确认 `ok=true`、`experience` 存在、`display_screens=2`。
2. 不启动 Mac Brain，切双眼、时钟、番茄、日历、chat 三模式，确认本地降级可用。
3. 用 `/api/tools/call` 驱动 `atlas.show_page`、`atlas.ui.set_chat_mode`、`atlas.pomodoro.show/start`、`atlas.calendar.today`。
4. 跑 `/api/selftest`，确认 `summary.fail=0`，重点看 `display_surfaces`、`offline_fallback`、`experience_tools`。
5. 跑 `/api/audio/opus-probe` 和 `/api/audio/opus-stream/status`，记录 frames/capture/encode 状态。
6. 启动 Mac Brain，再看 `brain_ws.connected` 和一次语音 turn 的 `/api/diagnostics/turn`。
7. 最后测 Wi-Fi AP fallback：断 STA 后确认 `ap_started=true`，手机仍能进入控制页。

## 4. 记录模板

```text
固件 commit:
bin size:
DualEye IP:
Brain online/offline:
status.lite experience:
selftest summary:
失败症状:
关键字段:
照片/串口日志:
下一步:
```
