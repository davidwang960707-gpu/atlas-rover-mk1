# Atlas 端到端实测与 xiaozhi 追赶记录

日期：2026-06-22  
设备：Waveshare ESP32-S3-DualEye-Touch-LCD-1.28  
DualEye 局域网地址：`192.168.3.60`  
Mac Brain 局域网地址：`192.168.3.53:8787`

## 1. 本轮实测结论

当前 Wi-Fi 条件下，主链路已经可以闭环：

| 项目 | 结果 | 记录 |
|---|---|---|
| DualEye 状态接口 | 通过 | `/api/status` 正常返回，STA IP 为 `192.168.3.60` |
| DualEye App / Admin 页面 | 通过 | `/app`、`/admin` 均 200，加载时间约 0.08 秒 |
| Mac Brain Health | 通过 | LLM、ASR、TTS 均已启用，默认天气城市为济南 |
| 文本对话 + TTS + DualEye 播放 | 通过 | `dualeye_play.ok=true`，TTS WAV 已推送播放 |
| 板载麦克风语音 turn | 通过 | 录音、ASR、兜底回复、TTS、播放均成功 |
| 连续监听启动/停止 | 通过 | 开启后 `audio_service.mode=monitoring`，关闭后恢复 |
| 天气技能 | 通过 | 默认济南可查，返回温度、天气、风速 |
| 日历技能 | 通过 | 能切换日历页并下发今天日期 |
| 番茄技能 | 通过 | 能启动 25 分钟任务，也能停止 |
| OPUS 60ms 模拟 | 通过 | Mac Brain 可模拟 30 帧，仍不是 DualEye 真 OPUS |
| SR/VAD 探针 | 通过 | 门限触发模拟正常，WakeNet/AEC 尚未硬上 |

## 2. 本轮修复

### 2.1 弱语音不再静默

问题：DualEye 录到“嗯。”、“啊。”这类弱输入时，Mac Brain 之前会把它当成 `voice_ignored`，没有回复、没有播报，用户感受像“卡死”。

修复：当设备语音 turn 开启 `speak=true` 时，弱输入会触发可听见的兜底回复：

```text
我在呢，刚才没听清楚。你可以再说一遍吗？
```

实测结果：

```json
{
  "ok": true,
  "asr_text": "嗯。",
  "reply": "我在呢，刚才没听清楚。你可以再说一遍吗？",
  "tts_ready": true,
  "played": true,
  "play_error": "ESP_OK"
}
```

### 2.2 设备语音 turn 不再误判 502

问题：ASR、LLM、TTS 都成功时，如果“同步推送页面/表情到 DualEye”失败，Mac Brain 会把整个设备语音 turn 返回 502，DualEye 只看到 `bridge failed`。

修复：设备发起语音 turn 时，只要已有回复或 TTS 可播放，就判定 turn 成功；同时保留 `device_intent_ok` 诊断字段，方便后续排查页面/表情同步问题。

### 2.3 Web 返回体不再塞巨大 base64 音频

问题：`/text`、`/audio`、`/skill`、`/speak` 之前会把整段 WAV base64 放进 JSON，单次返回可能几十万字符，Web 控制台容易卡。

修复：音频缓存到 Mac Brain，接口只返回短链接：

```json
{
  "tts_url": "http://192.168.3.53:8787/tts/latest.wav",
  "tts_cached": {
    "ready": true,
    "bytes": 112684
  },
  "tts": {
    "audio_url_omitted": true
  }
}
```

实测：一次带播报文本请求返回体约 `1486 bytes`，而不是几十万字符。

## 3. 与 xiaozhi 的当前差距

xiaozhi 的强项是实时语音 IoT 基础设施：OPUS 流、WebSocket/MQTT+UDP、设备状态机、WakeNet/AEC、MCP 工具、OTA 和管理台。

Atlas 当前已经超过普通 Demo 的部分：

| 方向 | 当前优势 |
|---|---|
| 双眼表达 | 多主题、双屏分工、宠物 IP、表情页是 Atlas 差异化核心 |
| 桌面应用 | 时钟、日历、番茄、天气、故事、音乐入口更适合桌面机器人 |
| 本地可控 | Mac Brain 让 API Key、模型、诊断留在本机，调试效率高 |
| 失败解释 | `/api/status`、`/api/device/selftest`、`/acceptance`、turn 日志已能定位多数问题 |

仍需追赶的部分：

| 优先级 | 要补强的事 | 原因 |
|---|---|---|
| P0 | 把 `atlas_audio_service` 继续做成真服务队列 | 避免 HTTP handler 扛录音/播放/监听，减少卡死 |
| P1 | WebSocket Brain 主链路 | 为流式音频、事件、工具调用打基础 |
| P2 | DualEye 真 OPUS 60ms 帧 | 降低延迟，接近 xiaozhi 的连续对话体验 |
| P3 | WakeNet/AEC 资源验证 | 解决免按键唤醒和播报自激 |
| P4 | Tool Schema V0 / 类 MCP | 让 LLM 稳定控制页面、表情、番茄、日历、天气 |
| P5 | OTA manifest 到真 OTA | 烧录频繁之后再做安全升级 |

## 4. 下一步

下一轮优先做两件事：

1. 继续把应用页做得像“机器人应用”，不是调试屏：时钟、日历、番茄要更美观、更动态、更可解释。
2. 把 P1/P2 往前推：设备与 Brain 的长连接事件通道继续实化，OPUS 从模拟推进到 DualEye 端真实探针。

## 5. 2026-06-22 晚间补强记录

本轮目标有两个：

1. 时钟、日历、番茄屏幕要更像真正应用，而不是调试页。
2. OPUS 从 Mac 模拟推进到 DualEye 真机链路。

### 5.1 圆屏应用页改造

固件文件：`firmware/dualeye/main/atlas_display.c`

已完成：

| 页面 | 左屏 | 右屏 | 设计意图 |
|---|---|---|---|
| 桌面时钟 | 大号数字时间 + 宠物主题状态 | 圆形石英表 + 秒针/分针/时针 | 左屏负责“可读”，右屏负责“像物件” |
| 日历 | 月日大字 + 宠物状态图层 | 今日标题/事项摘要 | 从状态输出变成“今日卡片” |
| 番茄专注 | 倒计时 + 宠物陪伴 + 阶段/进度 | 阶段、任务名、时长 | 明确区分 FOCUS/BREAK/READY，减少调试味 |

注意：这部分已编译通过，但当前 USB 串口未进入下载模式，所以新视觉还没有成功刷入并肉眼验收。

### 5.2 DualEye 真机 OPUS 探针

Mac Brain 文件：`tools/mimiclaw_bridge_macos.py`

新增接口：

| 接口 | 方向 | 作用 |
|---|---|---|
| `POST /api/device/opus-probe` | Mac Brain -> DualEye | 触发 DualEye 板载麦克风采样，并在板端编码 60ms OPUS 帧 |
| `GET /api/audio/stream/status` | Web/验收页 -> Mac Brain | 查看最近 OPUS 探针或流式音频统计 |
| `/acceptance` 按钮“OPUS 真机探针” | 人工验收 | 一键验证真机 OPUS 编码链路 |

实测命令：

```bash
curl --noproxy '*' -sS --max-time 30 \
  -X POST http://127.0.0.1:8787/api/device/opus-probe \
  -H 'Content-Type: application/json' \
  --data '{"duration_ms":1800}'
```

实测结果：

```json
{
  "ok": true,
  "stage": "P2_opus_60ms_probe",
  "probe": {
    "encoder_ready": true,
    "requested_ms": 1800,
    "frame_ms": 60,
    "sample_rate": 16000,
    "frame_samples": 960,
    "frames_requested": 30,
    "frames_encoded": 30,
    "encoded_bytes": 4290,
    "avg_packet_bytes": 143,
    "last_error": "ESP_OK"
  }
}
```

结论：OPUS 已经从“模拟统计”推进到“DualEye 真机 PCM -> 60ms OPUS 编码探针”。它还不是正式连续推流，下一步要把这条探针改造成 WebSocket 二进制帧持续上行。

### 5.3 编译与烧录状态

固件编译命令：

```bash
idf.py -C "/Users/macbook/Documents/Atlas One/firmware/dualeye" build
```

结果：

| 项目 | 结果 |
|---|---|
| ESP-IDF build | 通过 |
| 产物 | `firmware/dualeye/build/atlas_rover_dualeye.bin` |
| 应用大小 | `0x20abe0` |
| App 分区剩余 | 约 66% |

烧录握手失败：

```text
A fatal error occurred: Failed to connect to ESP32-S3: No serial data received.
```

判断：不是固件包问题，而是 DualEye 当前没有进入 ESP32-S3 下载模式。下一次实刷时需要按住 BOOT，再点 RESET 或重新插 USB，终端开始 `Connecting...` 后松开 BOOT。

## 6. 对标小智后的明确差距

参考：

- `78/xiaozhi-esp32`：官方 README 写明支持 ESP-SR 离线唤醒、WebSocket 或 MQTT+UDP、OPUS、流式 ASR+LLM+TTS、设备端/云端 MCP、自定义字体/表情/聊天背景等能力。
- `xinnan-tech/xiaozhi-esp32-server`：官方 README 写明服务端支持 MQTT+UDP、WebSocket、MCP 接入点、声纹识别、知识库、管理后台、插件扩展等能力。

### 6.1 Atlas 已经具备或接近具备

| 能力 | 当前状态 |
|---|---|
| 双屏应用体验 | Atlas 有明显差异化：双眼主题、宠物 IP、时钟/日历/番茄双屏分工 |
| Mac Brain | 可作为本地大脑，接入 LLM/ASR/TTS/天气/工具/验收页 |
| 整段 WAV turn | 已可用，适合短期实机体验 |
| OPUS | 已到 DualEye 真机编码探针阶段 |
| 类 MCP 工具 | 已有工具表和工具调用雏形，可控制页面/表情/番茄/日历 |
| 管理端/应用端分层 | 已有 `/admin`、`/app`、`/acceptance` 和设备列表雏形 |

### 6.2 仍然落后小智的地方

| 优先级 | 差距 | 当前影响 |
|---|---|---|
| P0 | 音频服务还没有完全脱离 HTTP handler | 连续语音仍可能被播放、录音、网络请求互相影响 |
| P1 | Brain 主链路还不是稳定 WebSocket 长连接 | 设备状态、事件、音频没有统一会话通道 |
| P2 | OPUS 还停在探针，不是持续二进制上行 | 不能达到小智那种流式低延迟体验 |
| P3 | WakeNet/AEC 未上真模型 | 还不能自然免按键唤醒，也不能稳定压制播报自激 |
| P4 | 工具/技能 Schema 还不够严谨 | LLM 控页面和应用时还需要更多约束、回执和失败解释 |
| P5 | OTA 仍是 manifest/包管理雏形 | 还不能替代手工 USB 烧录 |

## 7. 下一步执行方案

### P0：先把烧录版本变成可用版本

目标：下一次刷进去后，用户可以直接体验，不再像调试固件。

要做：

1. 修复 USB 下载握手或改用明确的手动 BOOT 流程。
2. 刷入本轮已编译包。
3. 用 `/acceptance` 逐项验收：状态、自检、OPUS 真机探针、工具表、Brain health。
4. 肉眼验收：时钟、日历、番茄、宠物主题是否两屏都亮、中文是否完整、布局是否像应用。

### P1：把 OPUS 探针改成真流

目标：DualEye 端每 60ms 编码一帧 OPUS，通过 WebSocket 持续发给 Mac Brain。

要做：

1. DualEye 增加 `atlas_opus_uplink_task`。
2. 每帧携带 `seq`、`timestamp_ms`、`sample_rate`、`frame_ms`、`payload_bytes`。
3. Mac Brain `/ws/audio` 接收真实二进制帧，不再只统计模拟帧。
4. 接入 VAD 后，做到静音不发或低频心跳。

## 8. P2 OPUS 真流实现记录

日期：2026-06-22 夜间

### 8.1 已完成

DualEye 固件：

| 文件 | 内容 |
|---|---|
| `firmware/dualeye/main/atlas_opus_stream.h` | 新增 `atlas_opus_stream_start/stop/get_status/write_status_json` |
| `firmware/dualeye/main/atlas_opus_stream.c` | 新增持续 WebSocket 上行任务，板端采集 PCM、编码 60ms OPUS、发送 AOP1 二进制帧 |
| `firmware/dualeye/main/atlas_admin_http.c` | 新增 `/api/audio/opus-stream/start`、`/stop`、`/status`，并更新 capabilities/selftest |
| `firmware/dualeye/main/idf_component.yml` | 增加 `espressif/esp_websocket_client: ^1.7.0` |

Mac Brain：

| 文件 | 内容 |
|---|---|
| `tools/mimiclaw_bridge_macos.py` | `/ws/audio` 支持解析 AOP1 帧头，统计 seq、丢帧、payload、麦克风能量 |
| `tools/mimiclaw_bridge_macos.py` | 新增 `/api/device/opus-stream/start`、`/stop`、`/status` 代理接口 |
| `tools/mimiclaw_bridge_macos.py` | `/acceptance` 增加“OPUS 真流 1.8s”和流状态验收 |

### 8.2 AOP1 二进制帧格式

每个 WebSocket binary message：

```text
32 字节 AOP1 header + OPUS payload
```

Header：

| 字节 | 字段 |
|---|---|
| 0-3 | magic：`AOP1` |
| 4 | version：`1` |
| 5 | header_len：`32` |
| 6 | flags |
| 7 | channels |
| 8-11 | seq |
| 12-15 | timestamp_ms |
| 16-17 | sample_rate |
| 18-19 | frame_ms |
| 20-21 | payload_len |
| 22 | mic_level |
| 23 | reserved |
| 24-27 | mic_rms |
| 28-31 | mic_peak |

### 8.3 本地模拟验收结果

Mac Brain 临时运行在 `127.0.0.1:8788`，用原始 WebSocket 客户端模拟 ESP32 客户端发送 12 个 AOP1 OPUS 帧。

结果：

```json
{
  "stage": "P2_dualeye_ws_opus_stream",
  "frames": 12,
  "bytes": 1716,
  "wire_bytes": 2100,
  "atlas_frames": 12,
  "legacy_binary_frames": 0,
  "sequence_gaps": 0,
  "last_seq": 12,
  "last_packet_bytes": 143,
  "estimated_audio_ms": 720
}
```

结论：Mac Brain 的 WebSocket 握手、AOP1 帧解析、chunk ack、end ack、状态记录都通过。

### 8.4 固件编译结果

命令：

```bash
idf.py -C "/Users/macbook/Documents/Atlas One/firmware/dualeye" build
```

结果：

| 项目 | 结果 |
|---|---|
| build | 通过 |
| app bin | `firmware/dualeye/build/atlas_rover_dualeye.bin` |
| app size | `0x211420` |
| app 分区剩余 | 约 66% |

### 8.5 实机烧录后验收顺序

1. 启动 Mac Brain。
2. 打开 `http://127.0.0.1:8787/acceptance`。
3. 点“OPUS 真流 1.8s”。
4. 查看 `/api/audio/stream/status`：
   - `last_stream.stage = P2_dualeye_ws_opus_stream`
   - `atlas_frames > 0`
   - `sequence_gaps = 0`
   - `last_packet_bytes > 0`
5. 再点“停止真流”，确认 DualEye `/api/audio/opus-stream/status` 进入 `done/stopped/idle` 类状态。

### 8.6 下一步

P2 真流目前只做“传输和统计”，下一步进入产品化语音链路：

1. Mac Brain 对 OPUS payload 做解码或转发到支持流式 ASR 的服务。
2. DualEye 加 VAD，静音时不发 OPUS payload，只发低频状态心跳。
3. 播放 TTS 时自动暂停或 mute OPUS 上行，避免自激。
4. 把 WebSocket turn 状态接进 `idle -> listening -> recording -> thinking -> speaking -> listening` 会话状态机。

### P2：把会话状态机补完整

目标：连续语音不再“听一次断一次”。

状态：

```text
idle -> listening -> recording -> thinking -> speaking -> listening
```

要做：

1. 播放期间自动 mute mic，播放结束恢复监听。
2. 每个 turn 输出明确失败原因：ASR 空、LLM 超时、TTS 失败、播放失败、网络断开。
3. Web 控制端展示最近 10 个 turn，不再只给“没反应”的体感。

### P3：WakeNet/AEC 资源验证

目标：接近小智的免按键语音体验，但不牺牲当前稳定性。

要做：

1. 单独分支引入 ESP-SR 组件。
2. 测堆内存、PSRAM、CPU 占用、LCD 刷新是否受影响。
3. 先做 WakeNet，不急着上完整 AEC。
4. 如果资源紧张，保留 Web/触摸唤醒 + VAD 连续监听作为 V1 方案。

### P4：应用工具化

目标：时钟、日历、番茄不只是页面，而是可以被 mimiClaw/LLM 稳定调用的应用。

要做：

1. `clock.set_timezone`、`clock.sync_now`、`clock.show_theme`
2. `calendar.show_today`、`calendar.add_event`、`calendar.next_event`
3. `pomodoro.start`、`pomodoro.pause`、`pomodoro.resume`、`pomodoro.stop`
4. 每个工具都返回 `ok/error/page/expression/speech_hint`。

### P5：再追 OTA

目标：减少频繁插 USB 烧录。

要做：

1. 固件内 OTA client 读取 Mac Brain manifest。
2. 支持版本指纹、包 hash、回滚策略。
3. 先做局域网 OTA，再考虑公网。

## 9. 晚间加固包：0.12.5-opus-brain

本轮目标：先把“能刷进去并可诊断”做扎实，再继续追 xiaozhi 的流式体验。

已完成：

| 模块 | 结果 |
|---|---|
| 固件版本指纹 | 升级为 `0.12.5-opus-brain` |
| `atlas_audio_service` | 音频服务 worker 优先使用 PSRAM 栈，状态 JSON 暴露 `psram_stack` |
| 连续语音监听 | `atlas_voice_wake` 优先使用 PSRAM 栈，状态 JSON 暴露 `psram_stack/mute_ms/reason` |
| OPUS 真流诊断 | `/api/audio/opus-stream/status` 增加 `free_internal_heap/free_psram_heap` |
| Mac Brain 验收报告 | 新增 `/api/acceptance/report`，支持 `skip_device=1` |
| AOP1 WebSocket 模拟 | `tools/simulate_opus_stream.py` 默认发送 AOP1 二进制帧 |
| 烧录前脚本 | `tools/check_atlas_preflash.py` 支持 Brain-only 检查，并接受 `opus_streaming=true` |

本地验证：

| 项目 | 结果 |
|---|---|
| Python 语法检查 | 通过：`mimiclaw_bridge_macos.py`、`check_atlas_preflash.py`、`simulate_opus_stream.py` |
| DualEye 固件编译 | 通过 |
| app bin 大小 | `0x211780` |
| app 分区剩余 | 约 66% |
| Brain-only 加固检查 | 通过：`PASS=6 WARN=0 FAIL=0` |
| AOP1 WebSocket 模拟 | 通过：`atlas_frames=12`、`bytes=1716`、`sequence_gaps=0`、`legacy_binary_frames=0` |

当前阻塞：

| 项目 | 现象 | 处理 |
|---|---|---|
| USB 烧录 | Mac 当前没有 `/dev/cu.usbmodem*` 或 `/dev/cu.usbserial*` | 需要重新插入 DualEye，必要时按住 BOOT 再点 RESET 进下载模式 |
| Wi-Fi 访问 | `192.168.3.60`、`192.168.3.53`、`192.168.4.1` 均不可达 | 刷入前先恢复 USB；刷入后再做局域网验收 |

真机刷入后验收标准：

1. `/api/status` 看到 `fingerprint.firmware_version = 0.12.5-opus-brain`。
2. `/api/selftest`：`fail=0`。
3. `/api/capabilities`：`brain_channel.opus_streaming=true`。
4. Mac Brain `/api/acceptance/report`：设备相关 required 项 `fail=0`。
5. 点 `/acceptance` 的“OPUS 真流 1.8s”，确认：
   - `last_stream.stage = P2_dualeye_ws_opus_stream`
   - `atlas_frames > 0`
   - `sequence_gaps = 0`
   - DualEye `stream.stage` 不再是 `task_create_failed` 或 `stack overflow`。

## 10. 80 分加固包：0.13.0-runtime-80

本轮目标：在不假装已经超过 xiaozhi 的前提下，把 Atlas 当前版本推进到“能烧录、能诊断、能客观验收”的 80 分线。

已完成：

| 模块 | 结果 |
|---|---|
| 固件版本指纹 | 升级为 `0.13.0-runtime-80` |
| Brain runtime | 新增 `tools/atlas_brain_runtime.py`，按 session 记录状态、事件、OPUS 帧健康和语音段 |
| 评分接口 | 新增 `/api/runtime/score`，返回当前实测分和 ready score |
| WebSocket OPUS | `/ws/audio` 接入 runtime，统计 `atlas_frames/sequence_gaps/payload_len_mismatches/speech_segments` |
| 固件 OPUS 稳定性 | 播放 mute 期间跳过采集，新增 `muted_frames/capture_failures/encode_failures` |
| 验收入口 | `/acceptance` 和 `/admin` 增加 runtime/score 按钮 |

本地验证：

| 项目 | 结果 |
|---|---|
| Python 语法检查 | 通过 |
| DualEye 固件编译 | 通过 |
| app bin 大小 | `0x2118d0` |
| app 分区剩余 | 约 66% |
| Brain-only 加固检查 | `PASS=7 WARN=0 FAIL=0` |
| AOP1 WebSocket 模拟 | 通过：30 帧、1.8 秒、`sequence_gaps=0`、`payload_len_mismatches=0` |
| runtime score | 当前 92/100；未配置 LLM/ASR/TTS Provider 扣 8 分 |

下一次实机测试：

1. 刷入后确认 `/api/status` 里的 `fingerprint.firmware_version = 0.13.0-runtime-80`。
2. 跑 `/api/selftest`，目标 `fail=0`。
3. Mac Brain 打开 `/acceptance`，点“OPUS 真流 1.8s”。
4. 如果失败，优先看 DualEye `/api/audio/opus-stream/status` 的 `stage`、`muted_frames`、`capture_failures`、`encode_failures`、`send_failures`。
