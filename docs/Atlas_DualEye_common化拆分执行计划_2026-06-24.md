# Atlas DualEye common 化拆分执行计划

日期：2026-06-24
分支：`codex/atlas-firmware-common-split`
范围：仅固件 worktree `/Users/macbook/Documents/Atlas-One-Firmware`

## 拆分原则

- 先保留现有 public wrapper，再把内部实现迁到 `common/`，避免一次性改坏双眼、时钟、番茄、日历、宠物头和 Brain 离线降级。
- 与 Brain 共享的字段先落到 `specs/` 或本文档；新增字段只做兼容扩展，不删除旧字段。
- 固件 common 层不直接依赖 DualEye board/audio/display 设备实现，设备应用层通过 backend callback 或 wrapper 绑定。
- 每轮至少跑 `cd firmware/dualeye && idf.py build`，真机验收再跑 `tools/check_atlas_preflash.py`。

## P0-P5 顺序

### P0 audio service 真服务化

目标：把音频任务调度、状态机、mute 窗口、计数和 JSON 输出变成可复用 common service。

本轮边界：

- 新增 `firmware/dualeye/main/common/atlas_common_audio_service.*`。
- `atlas_audio_service.*` 保留旧 API，作为 DualEye app wrapper。
- common 层通过 `measure_mic_fn`、`capture_wav_fn`、`play_wav_pcm_fn` backend callback 访问设备音频。
- `/api/status`、`/api/status/lite`、`/api/selftest`、`/api/voice/turn`、`/api/audio/opus-probe` 和 OPUS stream 状态字段保持兼容。

下一步验收：

- 构建通过。
- 自检里的 `audio_service.initialized`、`worker_started` 仍为 true。
- 异步 voice turn 和 OPUS probe 继续走同一个 worker。

### P1 常驻 Brain WebSocket session

目标：把 `atlas_brain_ws_client` 拆成 common session runtime 和 DualEye event adapter。

本轮边界：

- 新增 `firmware/dualeye/main/common/atlas_common_brain_session.*`。
- `atlas_brain_ws_client.*` 保留旧 API，作为 DualEye app wrapper。
- common 层负责 base URL 转换、health probe、connect/reconnect、hello/ping、turn WAV request、TTS binary receive 和状态 JSON。
- app wrapper 通过 callback 提供 Wi-Fi ready、runtime state、device id、protocol 和 Brain base URL。
- 保留 `/api/brain/ws`、`/api/brain/events`、`brain_ws` status 字段；`atlas.brain.session.v1` payload 兼容当前 Brain。
- Brain 离线时仅更新 `brain_ws.stage=brain_offline/waiting_wifi`；双眼、时钟、番茄、日历等本地页面继续由 scene/UI 层显示，不因 session 断开切到异常页。

### P2 OPUS 60ms 真机链路

目标：把 OPUS probe 与持续推流入口拆成 common audio stream adapter。

本轮边界：

- 新增 `firmware/dualeye/main/common/atlas_common_opus_stream.*`。
- `atlas_opus_stream.*` 保留旧 API，作为 DualEye app wrapper。
- common 层固化 AOP1 header、60ms frame、OPUS encoder、probe statistics、WebSocket uplink、sequence、send failure、mute statistics 和状态 JSON。
- app wrapper 通过 callback 提供 PCM capture、播放 mute 状态、audio service 阶段通知、device id 和 task name。
- 保留 `/api/audio/opus-probe`、`/api/audio/opus-stream/start|stop|status` 与 `atlas.audio.stream.v0`；Brain 侧 `/api/device/opus-*` 代理语义不需要固件改路由。
- Brain 离线不影响本地 UI；OPUS stream start 仍只返回 host bridge 未配置/连接失败状态，不改双眼、时钟、番茄、日历本地页面降级规则。

### P3 selftest/status/capabilities common 层

目标：先把设备自检、状态摘要和能力声明中的通用状态语义抽出来，后续再把 ESP-SR/WakeNet/AEC 资源检查挂到同一套 contract。

本轮边界：

- 新增 `firmware/dualeye/main/common/atlas_common_device_status.*`。
- common 层定义 `atlas.device.status.v0`、`atlas.selftest.v0`、`atlas.capabilities.v0` 协议标识，以及 `pass/warn/fail`、selftest summary、`ready_to_flash` 判断。
- `/api/selftest` 复用 common summary/count helper，但不删除、不重命名现有 JSON 字段。
- `/api/status`、`/api/status/lite`、`/api/capabilities`、`/api/system/info` 保持现有 route 和字段语义；后续只能兼容式新增协议字段。
- Brain 离线、Wi-Fi 未连接、WakeNet/AEC 未启用等异常仍以结构化 `warn/fail` 暴露，UI 本地页面继续由 scene/UI 层降级显示双眼、时钟、番茄和日历，不切异常文字页或黑屏。

下一步验收：

- 构建通过。
- `/api/selftest.summary` 数量与旧逻辑一致，Brain WS 离线仍为 warn 而非 fail。
- 后续 P3b 可把 `/api/sr/status` 的 WakeNet/AEC model partition 与 heap 风险也接入 `atlas_common_device_status`。

### P4 UI/page/app state common 层

目标：把页面状态、chat mode 默认值、应用页/手动页/持久页分类，以及 Brain 离线时的显示降级策略入口抽成可复用 common contract。

本轮边界：

- 新增 `firmware/dualeye/main/common/atlas_common_ui_state.*`。
- common 层定义 `atlas.ui.state.v0`、`pet_head/text/eyes_only` chat mode、`pet_head` 默认值、手动页面保留窗口和 transient 回眼睛页窗口。
- `atlas_ui.c` 复用 common 的持久页和 chat mode 规范化；双眼、时钟、番茄、日历状态字段仍由原 wrapper 维护。
- `atlas_scene.c` 复用 common 的应用页/手动页判断；Brain 离线仍走结构化 `brain_offline` scene，不改 HTTP API 字段，不切黑屏。
- `atlas_display.c` 复用 common 的 chat mode 和宠物头入口判断；渲染资源路径、宠物头动画、时钟/番茄/日历渲染逻辑保持原位。

下一步验收：

- 构建通过。
- `/api/status`、`/api/status/lite` 的 `ui/chat/apps/scene` 字段保持兼容。
- Brain 未配置或离线时仍显示结构化 Brain 离线页，本地双眼、时钟、番茄、日历和宠物头可继续切换。

### P5 Tool Schema V0 adapter

目标：把工具表和工具调用从 HTTP handler 中拆成 schema adapter。

计划：

- common 层定义 tool descriptor、JSON list writer、call dispatcher。
- app 层注册 eyes、clock、calendar、pomodoro、status、audio、ota 等 DualEye tool handlers。
- 固件只更新 `specs/atlas_tool_schema_v0.md` 的兼容扩展，不直接改 Brain 实现。

### 后续 OTA manifest/包管理接口

目标：把 OTA manifest/status/packages/apply 从 HTTP handler 中拆成 manifest adapter。

计划：

- common 层负责 package metadata、version/hash/offset 字段输出和兼容 JSON。
- app 层负责 ESP-IDF app OTA apply、storage/model/partition USB-only 状态说明。
- 保留 `/api/ota/status`、`/api/ota/manifest`、`/api/ota/packages`、`/api/ota/apply`。

## 模块边界图

```mermaid
flowchart LR
    CommonAudio["common/audio_service\nworker, state, mute, JSON"]
    DualEyeAudio["DualEye audio wrapper\natlas_audio backend"]
    Http["admin_http routes"]
    Opus["opus probe/stream"]
    Scene["scene/ui/status"]

    Http --> DualEyeAudio
    Opus --> DualEyeAudio
    Scene --> DualEyeAudio
    DualEyeAudio --> CommonAudio
    CommonAudio --> DualEyeAudio
```

## 当前兼容承诺

- 旧 C 符号 `atlas_audio_service_*` 全部保留。
- 旧 enum 宏 `ATLAS_AUDIO_SERVICE_MODE_*` 全部保留。
- 旧 JSON 字段 `initialized/worker_started/mode/busy/job_running/continuous_enabled/muted/*_count/last_*` 全部保留。
- common 层新增能力暂不改变 HTTP contract。
