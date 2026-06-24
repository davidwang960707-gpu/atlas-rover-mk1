# Atlas 平台与应用分层隔离改造计划 V0.1

日期：2026-06-24  
目标：在不破坏当前 DualEye 可玩版本的前提下，逐步把平台、应用、通用固件、具体设备固件分层。

## 0. 核心原则

这一轮不做“大搬家式重构”。现有成果能跑、能烧、能演示，这是第一优先级。

平台化改造按下面顺序推进：

```text
先契约 -> 再适配层 -> 再抽 common -> 最后整理目录
```

任何一步如果导致下面能力不可用，都要回退：

- 手机能打开 DualEye 控制页。
- 双眼、时钟、番茄、日历、宠物页面能切换。
- Brain 离线时不黑屏。
- Atlas Brain 能启动。
- `/api/status/lite`、`/api/selftest`、`/ws/brain` 基本可用。

## 1. 分层目标

```text
平台层：协议、状态、自检、工具、音频、Brain session、资源 manifest
通用固件层：audio service、runtime、scene、brain client、asset loader
板卡层：DualEye 屏幕、音频 codec、触摸、Wi-Fi、分区
应用层：双眼、对话、宠物、时钟、番茄、日历、天气、故事、音乐
服务端层：Atlas Brain session、Provider、Tool Registry、OTA、Web routes
Web 层：TOC 用户端、管理端、验收页
```

## 2. 现状边界

### 2.1 固件

当前 `firmware/dualeye/main` 同时包含：

| 类型 | 当前文件 |
|---|---|
| 平台协议 | `atlas_admin_http.*`、`atlas_brain_ws_client.*`、`atlas_brain_intent.*` |
| 通用运行时 | `atlas_runtime.*`、`atlas_scene.*`、`atlas_audio_service.*`、`atlas_opus_stream.*` |
| 板卡适配 | `atlas_audio.*`、`atlas_display.*`、`atlas_wifi.*` |
| 应用能力 | `atlas_ui.*`、`atlas_expression.*`、`atlas_pet.*`、时钟/番茄/日历相关显示逻辑 |
| 暂停能力 | `atlas_rover_uart.*` |

短期保持目录不动，但在新代码中按上述边界命名和引用。

### 2.2 Atlas Brain

当前 `tools/atlas_brain_server.py` 仍承担较多职责：

| 类型 | 当前文件 |
|---|---|
| 服务入口 | `atlas_brain_server.py` |
| 平台模型 | `atlas_brain_core.py` |
| Provider | `atlas_brain_providers.py` |
| Runtime | `atlas_brain_runtime.py` |
| Web UI | `atlas_web_ui.py` |

下一步优先拆 Brain，而不是先拆固件。原因：Brain 更容易单元测试和 dry-run，不会影响烧录。

## 3. 安全迁移路线

### 阶段 A：契约冻结

本阶段已经新增：

```text
specs/atlas_device_status_v0.md
specs/atlas_selftest_v0.md
specs/atlas_brain_session_v1.md
specs/atlas_tool_schema_v0.md
specs/atlas_aop1_audio_frame_v0.md
specs/atlas_asset_manifest_v0.md
specs/atlas_board_profile_v0.md
```

要求：

- 新接口必须能对应到 specs。
- 旧接口不删。
- Web 和验收脚本优先按 specs 检查。

### 阶段 B：Brain 轻拆分

目标：不改路由行为，只拆内部模块。

建议顺序：

1. `tools/atlas_brain_tools.py`
   - 已完成第一刀：从 `atlas_brain_server.py` 拆出 `SkillRegistry`、角色/主题别名、pet state 映射、Tool Schema helper 和内置工具注册。
   - 主服务仍是唯一启动入口，`/api/tools/list`、`/api/tools/call`、`/mcp/tools/list`、`/mcp/tools/call` 路径不变。
   - OTA manifest 仍在主服务内，由主服务通过依赖注入传给工具注册器，避免工具模块反向 import 主服务。
2. `tools/atlas_brain_devices.py`
   - 已完成第一刀：拆出 DualEye 状态读取、配对重试、表单 POST、设备摘要、设备影子、意图下发、主题/对话模式切换、OPUS probe/stream 控制。
   - 主服务保留 `Bridge` 兼容外壳，现有工具、Web 路由和验收脚本不需要改调用方式。
3. `tools/atlas_brain_audio.py`
   - 已完成第一刀：拆出 TTS WAV 规范化与缓存、最近语音 turn 元信息、音频流诊断状态、AOP1/OPUS 帧解析、Ogg OPUS 封装、OPUS turn 解码、音频流模拟和音频响应压缩。
   - 下一刀再拆具体 HTTP/WS handler，让 `/turn/audio`、`/device/audio/wav`、`/ws/audio` 从主服务外壳转到音频 route/service。
4. `tools/atlas_brain_ota.py`
   - 拆出 manifest 和包扫描。
5. `tools/atlas_brain_routes.py`
   - 最后整理 HTTP/WS route 注册。

兼容要求：

- `tools/atlas_brain_server.py` 仍是唯一启动入口。
- `tools/start_atlas_brain_mimo.sh` 不变。
- `/admin`、`/devices`、`/api/tools/list`、`/ws/brain`、`/ws/audio` 路径不变。

### 阶段 C：固件 common 化标记

目标：先在文档和头文件里标记边界，不马上搬目录。

建议标记：

| 未来层 | 当前模块 |
|---|---|
| common/runtime | `atlas_runtime`、`atlas_scene` |
| common/audio | `atlas_audio_service`、`atlas_opus_stream`、`atlas_sr_probe` |
| common/brain_client | `atlas_brain_ws_client`、`atlas_brain_intent` |
| common/protocol | `status/selftest/tools/ota` 输出结构 |
| board/dualeye | `atlas_display`、`atlas_audio`、LCD/LVGL 初始化 |
| app/desktop_pet | `atlas_pet`、表情、页面应用 |

兼容要求：

- 不改 CMake 结构，除非必要。
- 不改 public 函数名，除非保留 wrapper。
- 每一步都编译。

### 阶段 D：Web 分层

短期 `atlas_web_ui.py` 继续输出页面，避免引入前端构建链路拖慢迭代。

中期迁移：

```text
web/atlas_console/app
web/atlas_console/admin
web/atlas_console/shared
```

迁移前提：

- Brain API 稳定。
- TOC App 视觉和主链路基本定型。
- 管理端功能不再频繁大改。

## 4. 平台和应用边界

### 4.1 平台不应该知道

平台层不应该写死：

- 土拨鼠这个具体 IP。
- DualEye 左右屏具体页面布局。
- MiMo 作为唯一 Provider。
- 鲁豫有约 Wi-Fi。
- Mac 某个固定 IP。
- 早期小车底盘运动逻辑。

### 4.2 应用不应该知道

应用层不应该知道：

- I2S pin。
- LCD 初始化。
- SPIFFS mount 细节。
- WebSocket 分片细节。
- OPUS encoder 参数细节。
- NVS key 的底层读写。

### 4.3 板卡层不应该知道

板卡层不应该知道：

- 番茄任务名称。
- 宠物心情值。
- LLM 角色。
- 天气城市。
- 讲故事风格。

## 5. 不破坏现有成果的护栏

每次平台化改造前后都跑：

```bash
python3 -m py_compile tools/atlas_brain_server.py tools/atlas_brain_core.py tools/atlas_brain_providers.py tools/atlas_brain_runtime.py tools/atlas_web_ui.py
python3 tools/atlas_brain_server.py --dry-run --host 127.0.0.1 --port 8787
python3 tools/check_atlas_preflash.py --brain-url http://127.0.0.1:8787 --skip-dualeye
```

固件改造后再跑：

```bash
cd firmware/dualeye
idf.py build
```

真机验收：

```text
GET /api/status/lite
GET /api/selftest
GET /api/diagnostics/turn
GET /api/tools/list
GET /api/audio/opus-stream/status
```

## 6. 本轮边界

本轮只做：

- 新增 specs 契约。
- 新增分层隔离改造计划。
- 不移动现有代码。
- 不改启动脚本。
- 不改固件行为。

## 7. 已完成记录

### 2026-06-24 Brain 工具注册拆分

- 新增 `tools/atlas_brain_tools.py`。
- 拆出 `SkillRegistry`、角色/主题别名、pet state 映射、Tool Schema helper 和内置工具注册。
- 验证：`py_compile` 通过；dry-run Brain 工具列表、工具调用、MCP 工具列表通过；`check_atlas_preflash.py --skip-dualeye` 通过。

### 2026-06-24 Brain 设备桥接拆分

- 新增 `tools/atlas_brain_devices.py`。
- 拆出 DualEye HTTP 访问、配对 PIN 同步、设备摘要、设备影子、UI 配置、Intent 下发、TTS 播放触发、OPUS probe/stream 控制。
- `tools/atlas_brain_server.py` 保留兼容代理方法，避免影响现有 Web、工具和验收脚本。
- 验证：`py_compile` 通过；dry-run Brain `/health`、`/api/devices`、`/api/device/live`、`atlas.ui.set_chat_mode` 通过；`check_atlas_preflash.py --skip-dualeye` 通过。

### 2026-06-24 Brain 音频基础层拆分

- 新增 `tools/atlas_brain_audio.py`。
- 拆出 TTS 缓存、WAV 规范化、音频 turn 元信息、OPUS/AOP1 编解码辅助、音频流状态仓库和响应压缩。
- 主服务继续保留 `/ws/brain`、`/ws/audio`、`/turn/audio`、`/device/audio/wav` 路由，避免影响真机链路。
- 验证：`py_compile` 通过；dry-run Brain 音频流模拟、TTS 缓存端点、预烧录检查通过。

下一轮才开始做 Brain 轻拆分，而且每拆一个模块都保留 `atlas_brain_server.py` 的启动兼容。

## 7. 下一步建议

下一轮优先做设备桥接拆分：

1. 从 `atlas_brain_server.py` 中拆 `tools/atlas_brain_devices.py`。
2. 迁移设备状态读取、DualEye intent/form 调用、设备摘要、设备 live/scene 相关函数。
3. 保持 `Bridge` 对外方法名不变，主服务只调用薄代理。
4. 跑 `py_compile`、dry-run、`check_atlas_preflash.py --skip-dualeye`。
5. 再拆 `atlas_brain_audio.py` 和 `atlas_brain_ota.py`。

这样能继续把平台核心从服务端沉淀出来，同时不影响固件实机体验。

## 8. 已完成记录

### 2026-06-24：Brain 工具系统第一刀

变更：

- 新增 `tools/atlas_brain_tools.py`。
- `atlas_brain_server.py` 改为通过 `build_builtin_skill_registry(...)` 构建工具注册表。
- 工具模块只依赖 Bridge-like 对象，不反向 import 主服务。
- 清理主服务内旧的 `SkillRegistry` 和内联 `build_skill_registry` 死代码。

验证：

```text
python3 -m py_compile tools/atlas_brain_server.py tools/atlas_brain_tools.py tools/atlas_brain_core.py tools/atlas_brain_providers.py tools/atlas_brain_runtime.py tools/atlas_web_ui.py
python3 tools/atlas_brain_server.py --dry-run --host 127.0.0.1 --port 8799
curl http://127.0.0.1:8799/api/tools/list
curl -X POST http://127.0.0.1:8799/api/tools/call ...
python3 tools/check_atlas_preflash.py --brain-url http://127.0.0.1:8799 --skip-dualeye
```

结果：

- Brain-only 预检 `PASS=10 WARN=0 FAIL=0`。
- 工具表仍返回 `atlas.tools.v0.desk_apps`。
- 默认工具数量仍为 23。
- `atlas.pomodoro.start` dry-run 调用通过。
- `8799` 仅为临时验证端口，验证后已停止。
