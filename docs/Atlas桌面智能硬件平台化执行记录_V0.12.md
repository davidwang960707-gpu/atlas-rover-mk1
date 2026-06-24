# Atlas 桌面智能硬件平台化执行记录 V0.12

日期：2026-06-22  
方向：本轮先把实体车动态能力放一放，优先完成 DualEye 语音/音频/状态诊断和 Atlas Brain 平台化后端。

## 1. 本轮产品边界

当前实体先按“桌面智能硬件 / 电子宠物伴侣”推进，不再把 N20 底盘动态移动作为本轮验收项。

保留：

- 双眼主题、表情、时钟、番茄、日历、对话、故事、音乐入口。
- DualEye 板载 ES7210 麦克风、ES8311/外放、WAV turn。
- Mac 侧 Atlas Brain/MimiClaw/MiMo ASR/LLM/TTS。
- STOP 和底盘旧接口作为后续安全联调入口。

暂停：

- Web 手动移动作为用户主功能。
- 语音/LLM 自动生成小车移动指令。
- Mac Brain 默认注册 `atlas.rover.move` / `atlas.rover.stop` 技能。
- 运动相关 UI 作为主要应用入口。

需要恢复底盘技能时，显式打开：

```bash
export ATLAS_ENABLE_ROVER_SKILLS=1
```

固件端动态底盘构建开关默认为关闭：

```c
#define ATLAS_ROVER_MOTION_BUILD_ENABLED 0
```

## 2. 固件端完成项

### 2.1 P0：稳定 WAV turn

新增 `atlas_runtime.*`，记录最近语音 turn 的完整状态：

- `idle / listening / recording / transcribing / thinking / speaking / cooldown / error`
- 录音时长、WAV 字节数、麦克风 level/RMS/Peak。
- Mac Bridge HTTP 状态、ASR 文本、回复文本、Bridge 错误。
- TTS 是否 ready、WAV 字节数、播放是否成功、播放错误。

新增接口：

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/api/diagnostics/turn` | 查看最近语音 turn、连续监听状态、mute 剩余时间和失败原因 |
| `GET` | `/api/system/info` | 查看固件构建、芯片、heap/PSRAM、资源版本、音频和 runtime 状态 |

`/api/status` 也增加了：

- `runtime`
- `features.rover_motion`
- `safety.motion_enabled=false`

### 2.2 播放期间 mute 证据化

原本已有播放期间 `voice_wake_mute_for()`，本轮把它和 runtime 诊断串起来。后续遇到“播报后杂音/误触发/连续监听断掉”时，优先看：

```text
/api/diagnostics/turn
runtime.turns[0].phase
runtime.turns[0].play_error
voice_wake.mute_ms
voice_wake.reason
```

### 2.3 动态底盘降级

默认构建下：

- `atlas_config_motion_supported()` 返回 false。
- 即使 NVS 里历史保存过 `motion_enabled=true`，加载后也会强制关闭。
- `/api/capabilities` 返回 `motion_supported=false` 和 `disabled_reason=desk_companion_build`。
- `/app` 和 `/admin` 上的动态底盘入口显示为“本版暂停”。

STOP 保留，方便未来接底盘时仍可作为安全入口。

## 3. Atlas Brain 服务端完成项

### 3.1 平台化设备模型

新增设备接口：

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/api/devices` | 返回设备列表，目前默认一台 DualEye |
| `GET` | `/devices` | 设备列表页面 |
| `GET` | `/devices/<device_id>/app` | 单设备 App 操作页面 |

新的分层：

| 页面 | 角色 |
|---|---|
| `/devices` | 平台设备列表 |
| `/devices/dualeye/app` | 单设备日常操作页 |
| `/admin` | 平台管理端/智控台：Provider、技能、会话、诊断、OTA manifest |

### 3.2 动态底盘技能默认不注册

默认情况下，`SkillRegistry` 不注册：

- `atlas.rover.move`
- `atlas.rover.stop`

自然语言里出现前进、后退、左转、右转、停止时，服务端会转成普通聊天回复：

```text
这一版先不做动态移动，我会优先把语音、表情和桌面应用做好。
```

如果后续要恢复底盘技能，设置：

```bash
ATLAS_ENABLE_ROVER_SKILLS=1
```

### 3.3 仍保留的技能

默认注册：

```text
atlas.show_page
atlas.set_expression
atlas.set_theme
atlas.role.switch
atlas.pomodoro.start
atlas.pomodoro.stop
atlas.calendar.today
atlas.weather.query
atlas.web_search
atlas.music.play
atlas.story.tell
atlas.chat
atlas.ota.check
```

## 4. P0-P5 路线更新

| 阶段 | 当前状态 | 下一步 |
|---|---|---|
| P0 `atlas_audio_service` 真服务化 | 已完成。语音 turn、OPUS 探针等长操作进入 audio service worker，状态包含 worker/job/last_failure，播放后自动回到监听判断 | 真机回归连续语音是否还会卡死、断链 |
| P1 WebSocket Brain JSON 长连接 | 已完成。固件 `/api/brain/ws` 支持 hello/status/recent_events/ping，turn 过程会写入 Brain event ring | 下一轮把 Mac Brain 侧也升级为主动订阅设备事件 |
| P2 DualEye 真实 OPUS 60ms 帧 PoC | 已完成第一步。固件新增 `atlas_opus_stream.*` 和 `/api/audio/opus-probe`，用真实 ES7210 PCM 编码 60ms OPUS 帧并统计包大小 | 下一轮把探针输出接到 WebSocket Brain，而不是只返回统计 |
| P3 WakeNet/AEC 资源验证 | 已完成探针。`/api/sr/status` 返回 ESP-SR 头文件、模型分区、OPUS 支持、堆内存和 AEC 风险，不直接启用 WakeNet/AEC | 晚上实机看模型分区与 PSRAM 余量，再决定是否引入 ESP-SR 组件 |
| P4 Tool Schema V0 / 类 MCP | 已完成。Mac Brain `SkillRegistry` 输出 `inputSchema/outputSchema/risk/target/confirm_required`，新增 `/api/tools/list`、`/api/tools/call`、`/mcp/tools/list`、`/mcp/tools/call` | 下一轮让 LLM/MimiClaw 优先走 schema 工具调用，不再靠关键词猜 |
| P5 OTA manifest + 包管理 | 已完成包清单阶段。`/ota/manifest` 和 `/api/ota/packages` 扫描本地 build 产物，输出 size、sha256、flash offset 和缺失项 | 真 OTA 仍需 OTA 分区、回滚策略和签名验证，当前仍用 USB 烧录 |

## 5. 验证结果

Python 服务端语法检查：

```text
python3 -m py_compile tools/mimiclaw_bridge_macos.py tools/check_mimiclaw_network_macos.py tools/preview_dualeye_admin.py
通过
```

DualEye 固件构建：

```text
idf.py build
通过
atlas_rover_dualeye.bin binary size 0x1deca0
6MB app partition 剩余 0x421360 bytes，约 69%
```

上面是 V0.12 第一轮构建结果；第三轮 P3/P4 推进后的最新构建结果见下方。

V0.12 第二轮补充：

- `atlas_audio_service.*` 已加入固件，统一记录录音、播放、连续监听、mute 窗口和最近错误。
- `/api/status`、`/api/system/info`、`/api/diagnostics/turn` 已包含 `audio_service`。
- 固件新增 `/api/brain/ws`，当前定位是 JSON 事件 WebSocket PoC，不承载音频流。
- Mac Brain 新增 `tools/atlas_brain_core.py`，抽象 Device、Provider、Protocol、App 和 PlatformBackend。
- Mac Brain 新增 `/api/platform`、`/api/providers`、`/api/protocols`、`/api/brain/events`。
- 第二轮 ESP-IDF 构建通过，`atlas_rover_dualeye.bin` 大小 `0x1e0dc0`，6MB app 分区剩余 `0x41f240`，仍约 69%。

V0.12 第三轮补充：

- P3：Mac Brain 新增 `/ws/audio`，可接收模拟 OPUS/PCM 二进制帧，默认按 60ms 一帧统计。
- P3：新增 `/api/audio/stream/status` 与 `/api/audio/stream/simulate`。
- P3：新增 `tools/simulate_opus_stream.py`，可在 Mac 端模拟 WebSocket 音频流。
- P4：固件新增 `atlas_sr_probe.*` 与 `/api/sr/status`，明确当前仍是 `energy_gate_vad` fallback。
- P4：Mac Brain 新增 `/api/sr/status` 与 `/api/sr/simulate`。
- 第三轮 ESP-IDF 构建通过，`atlas_rover_dualeye.bin` 大小 `0x1e1140`，6MB app 分区剩余 `0x41eec0`，仍约 69%。

V0.12 第四轮补充：

- 新增 `atlas_scene.*` 场景解析层，把 UI 页面、runtime、audio service、Wi-Fi、Mac Brain 配置统一合成为一个 `scene` 快照。
- 双眼显示不再只看 `page/expression`：`listening / recording / transcribing / thinking / speaking / cooldown / wifi_config / brain_offline / audio_test / error` 都有明确的页面、表情、标题、副标题和恢复提示。
- `/api/status`、`/api/diagnostics/turn` 新增 `scene` 字段；屏幕显示和 Web 状态读取用同一套判断。
- LVGL 补齐真正的 `status` 与 `voice` 页面：左屏偏情绪/状态，右屏偏链路/原因/提示；音乐、故事页面也有基础状态页，不再只是普通眼睛页。
- Mac Brain 平台层识别 DualEye `scene`，`/api/devices`、`/devices`、`/devices/<id>/app` 显示当前场景、异常级别、runtime/audio mode 和连续监听状态。
- Mac Brain 新增 `/api/device/scene`，设备离线时也返回结构化“设备离线”快照，不再让管理页硬吃 502。
- 第四轮 ESP-IDF 构建通过，`atlas_rover_dualeye.bin` 大小 `0x1e33a0`，6MB app 分区剩余 `0x41cc60`，仍约 69%。

V0.12 第五轮补充：

- P0：`atlas_audio_service` 进入真 worker 模式，`/api/status` 能看到 `worker_started/job_running/job_count/job_error_count/last_job_ms/last_failure`。
- P1：固件 `/api/brain/ws` 升级为 `atlas.brain.events.v1` JSON 长连接，支持 `hello/status/recent_events/ping`，语音 turn 会记录 `turn.started/transcribing/thinking/speaking/played/failed`。
- P2：固件新增 `atlas_opus_stream.*` 与 `/api/audio/opus-probe`，实际采集板载麦克风 PCM，按 16k/mono/60ms 编码 OPUS，返回帧数、总字节、最小/最大/平均包大小和麦克风电平。
- P3：`/api/sr/status` 改为 `P3_resource_probe`，输出 ESP-SR/OPUS 头文件、模型分区、WakeNet/AEC 当前启用状态、堆内存和测试矩阵。
- P4：Mac Brain `SkillRegistry` 补 Tool Schema V0，`/api/tools/list`、`/api/tools/call`、`/mcp/tools/list`、`/mcp/tools/call` 可用于 MimiClaw/LLM 稳定发现和调用页面、表情、主题、番茄、日历、天气、搜索、音乐、故事、对话、OTA 检查。
- P5：Mac Brain `/ota/manifest`、`/api/ota/packages` 输出本地固件包管理清单，包含 bootloader、partition table、app、SPIFFS storage 的大小、sha256 与烧录偏移。
- 最新 ESP-IDF 构建通过，`atlas_rover_dualeye.bin` 大小 `0x208290`，6MB app 分区剩余 `0x3f7d70`，约 66%。
- Mac Brain dry-run 通过：`/api/tools/list`、`/mcp/tools/list`、`/api/tools/call`、`/ota/manifest`、`/api/ota/packages` 均可用。

V0.12.1 烧录前加固包：

- DualEye 新增稳定版本指纹 `0.12.1-preflash-hardening`，并在 `/api/status`、`/api/capabilities`、`/api/system/info` 返回 `fingerprint`。
- DualEye 新增 `/api/selftest` 轻量自检，不自动播放/录音；检查 SPIFFS、关键 PNG 资源、Wi-Fi、Mac Brain 配置、板载音频、audio service worker、Brain WS、OPUS probe、SR probe、Tool Schema、内存和动态底盘边界。
- DualEye 管理台新增“一键自检”和“系统信息”按钮。
- Mac Brain 新增 `/api/device/selftest`、`/api/device/system-info` 和 `/acceptance` 烧录验收页。
- 新增 `tools/check_atlas_preflash.py`，支持 `--skip-dualeye`，可在设备离线时先验证 Mac Brain、工具表和 OTA 包清单。
- 最新 ESP-IDF 构建通过，`atlas_rover_dualeye.bin` 大小 `0x209aa0`，6MB app 分区剩余 `0x3f6560`，约 66%。
- Python 编译与 Mac Brain dry-run 验收通过。
- 发布候选检查确认：当前本机 `127.0.0.1:8787` 仍是旧版 Mac Brain，缺少 `/api/tools/list` 与 `/api/ota/packages`。烧录前需要重启 Mac Brain 到当前代码；新版 dry-run 服务已通过全部 Brain 侧验收。

当前仍未完成：

- DualEye 端 OPUS 只完成编码探针，尚未把音频 chunk 真实推到 Brain WebSocket。
- 端侧 WakeNet、自定义唤醒词、AEC。
- 播放期间真实回声消除；目前仍依赖 mute/门限规避。
- 真 OTA；当前只有 manifest 和包哈希，仍需 USB 烧录。

ESP-IDF 环境：

```text
/Users/macbook/.espressif/esp-idf-v5.5.2/export.sh
```

## 6. 烧录后重点验证

1. 打开 `/api/selftest`，确认 `summary.fail=0`，记录 `fingerprint.firmware_version`。
2. 打开 `/api/system/info`，确认 `motion_supported=false`、资源版本和 runtime 正常。
3. 打开 `/api/status`，确认新增 `scene` 字段；配网、Mac Brain 未配置、语音监听、播放中、异常时 `scene.label/title/severity` 应跟屏幕一致。
4. 打开 `/api/diagnostics/turn`，确认 `scene`、`runtime`、`audio_service` 三组信息互相匹配。
5. 在 Mac 上运行 `python3 tools/check_atlas_preflash.py --dualeye-url http://设备IP --brain-url http://127.0.0.1:8787`。
6. 在 Mac Brain 打开 `/acceptance`，确认 DualEye 自检、工具表和 OTA manifest 可见。
7. 在 `/app` 里执行板载语音对话，结束后看 `/api/diagnostics/turn` 是否有 ASR、reply、TTS、play 状态。
8. 打开 `/devices/dualeye/app`，测试文本对话、角色切换、番茄、日历、天气。
8. 说“前进一下”，确认不会发动态底盘指令，而是返回本版暂停动态移动的提示。
