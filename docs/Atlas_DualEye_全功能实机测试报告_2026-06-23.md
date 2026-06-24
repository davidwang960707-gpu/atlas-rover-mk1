# Atlas DualEye 全功能实机测试报告

日期：2026-06-23  
测试对象：Waveshare ESP32-S3-DualEye-Touch-LCD-1.28  
固件版本：`0.14.7-acceptance`  
资源版本：`dualeye-assets-v0.5-pet-head`  
字体版本：`atlas_font_zh_16_3500`  
Atlas Brain：`http://127.0.0.1:8787` / 局域网 `http://192.168.3.53:8787`  
DualEye：`http://192.168.3.60`

## 结论

本轮已完成烧录、重启、联机、离线降级、Web 控制、Provider、OPUS 真流和预烧录加固检查。按当前工程验收规则，设备达到“可继续实机体验测试”的状态。

## 23:56 追加实测与修复

本轮从用户主链路、界面和语音对话重新验收，发现并修复了一个真实链路问题：

| 项目 | 结果 | 说明 |
|---|---:|---|
| Mac Brain Provider | 通过 | `LLM/ASR/TTS=true`，模型为 `xiaomi/mimo-v2.5-pro`、`mimo-v2.5-asr`、`mimo-v2.5-tts` |
| 预检 | 通过 | `PASS=16 WARN=0 FAIL=0` |
| Web TOC 控制台 | 通过 | 浏览器实点“发送”后回复“收到！”，TTS 自动播报成功，控制台 0 JS error |
| Web 按钮语义 | 已修复 | 原“只转文字”实际是浏览器麦克风 ASR，已拆成“只发文字”和“语音转文字” |
| 自然语言意图 | 已修复 | “用一句话提醒我该睡觉了”不再误判为切换困困表情，改为普通聊天回复 |
| 主题/页面/应用技能 | 通过 | `atlas.role.switch`、`atlas.clock.show`、`atlas.calendar.today`、`atlas.pomodoro.start`、`atlas.pet.play_animation` 均返回 `ok=true` |
| 文本对话 + TTS + DualEye 播放 | 通过 | TTS WAV 约 `194604 bytes`，DualEye `/api/audio/play-url` 播放成功 |
| 天气 | 通过 | 默认城市济南，返回“山东济南现在阴，气温24.8℃，风速11.2 km/h。” |
| 板载语音 turn | 已修复后通过 | DualEye 上传 `57644 bytes` WAV，ASR 返回“嗯。”，TTS 返回 `128044 bytes`，DualEye 播放成功 |
| OPUS turn | 通过 | 31 帧、0 丢帧、0 payload mismatch，Ogg 封装和 WAV 解码成功 |
| DualEye 手机页 | 通过 | `scene.state=idle`，`needs_attention=false`，不再停在“链路异常”页 |

### 关键修复

1. **Brain WebSocket WAV 多帧接收**  
   问题：DualEye 板载语音通过 `/ws/brain` 上传 WAV 时，ESP WebSocket 底层会按约 `2048 bytes` 分片发送；Brain 之前按“单帧完整 WAV”校验，导致 `wav size mismatch: expected 112044, got 2048`。  
   修复：`tools/atlas_brain_server.py` 在收到 `turn.audio.begin` 后按声明的 `bytes` 累积多个 binary frame，收满后再校验 RIFF 并进入 ASR/LLM/TTS。

2. **Web 控制台文字/语音入口拆分**  
   问题：“只转文字”按钮会请求浏览器麦克风权限，在桌面自动化或用户未授权时像是按钮失效。  
   修复：新增“只发文字”用于输入框文本不播报；原麦克风 ASR 改名“语音转文字”。

3. **意图规则收窄**  
   问题：“提醒我该睡觉了”被关键词“睡/困”误判为切换困困表情。  
   修复：表情切换只在“切换/显示/变成/表情/眼睛”等明确视觉控制语境下触发，普通情绪表达走聊天。

4. **设备配置口径清理**  
   旧 NVS 配置里的 provider 名称从 `mimiclaw_mac_bridge` 更新为 `atlas_brain_mac`；当前实际架构统一按 Atlas Brain / Mac 桥接口径。

### 当前留置状态

- 连续语音监听：**已关闭**。原因是测试环境噪声会触发能量门限，容易反复进入“没听清楚”的 turn；板载语音 turn 本身已经通过。需要体验连续对话时，可在手机页点击“开启连续对话”。
- 手机页唯一浏览器错误是 `/favicon.ico` 404，不影响功能。
- OPUS turn 的“无语音”测试会返回 ASR 空文本或“嗯。”，这是测试环境没有人说完整指令造成的业务结果；链路层已通过。

### 截图证据

- Web TOC 控制台：[atlas_brain_device_app_after_user_flow.png](</Users/macbook/Documents/Atlas One/output/playwright/atlas_brain_device_app_after_user_flow.png>)
- DualEye 手机页修复后：[dualeye_device_app_after_ws_fix.png](</Users/macbook/Documents/Atlas One/output/playwright/dualeye_device_app_after_ws_fix.png>)
- 验收页：[atlas_acceptance_page.png](</Users/macbook/Documents/Atlas One/output/playwright/atlas_acceptance_page.png>)

| 项目 | 结果 | 证据 |
|---|---:|---|
| ESP-IDF 编译 | 通过 | `atlas_rover_dualeye.bin` 大小 `0x23b7c0`，5MB app slot 剩余约 55% |
| App 分区烧录 | 通过 | esptool 写入 `0x100000`，hash verified |
| DualEye 预检 | 通过 | `PASS=16 WARN=0 FAIL=0` |
| DualEye 自检 | 通过 | `PASS=15 WARN=1 FAIL=0`，唯一 warn 是 WakeNet/AEC 未正式启用 |
| Brain 常驻 WS | 通过 | `brain_ws.connected=true`，断开后可自动重连 |
| Brain 离线降级 | 通过 | Brain 停止后设备仍显示本地页面，`voice.available=false`，原因 `brain_offline` |
| Web/手机端页面切换 | 通过 | `eyes/photo/clock/calendar/pomodoro` 接口均返回 200 |
| 表情切换 | 通过 | `blink` 已兼容到固件表达式映射 |
| 电子宠物工具 | 通过 | `atlas.pet.set_state` 返回 accepted |
| LLM | 通过 | 小米 MiMo Pro 返回中文结果 |
| TTS | 通过 | 生成并拉取 WAV，最近一次 `81964 bytes` |
| ASR | 通过 | 最近一次识别返回“你好，我是阿特拉西。” |
| ASR 异常收敛 | 通过 | 小概率 SSL EOF 已改为重试并返回 JSON 错误，不再断开 HTTP |
| OPUS 真机流 | 通过 | 31 帧，约 1860ms，0 丢包，0 payload mismatch，设备状态 `done` |
| OPUS 语音 Turn | 通过 | `/api/device/opus-turn/start` 可将 OPUS packet 封装 Ogg、解码 WAV 并进入 ASR |
| Runtime Score | 通过 | `100/100`，注意这是工程验收规则，不代表产品无短板 |

## 本轮发现并修复的问题

| 问题 | 原因 | 修复 |
|---|---|---|
| Brain 离线时设备可能进入异常页或重连抖动 | 固件直接创建 WS client，未先做健康探测 | `atlas_brain_ws_client` 增加 `/health` preflight，失败进入 `brain_offline`，本地页面继续运行 |
| `/api/app/expression` 的 `blink` 控制失败 | capabilities 暴露 `blink`，表达式解析未接受 | `atlas_expression_from_name("blink")` 映射到 `wink` |
| capabilities 暴露 `photo`，但页面切换返回 501/空白 | 固件缺少 photo 页面分支 | 增加 photo 占位页，避免黑屏和契约不一致 |
| Brain 对话成功但设备离线时误判失败 | send_text 把设备同步失败等同于 LLM 失败 | 返回 `device_sync_ok/device_sync_warning`，对话结果本身保持成功 |
| OPUS 成功发送后状态仍显示 `muted` | 最后一轮碰到 mute 会覆盖 stage | 任务结束时如果已有成功帧，最终状态归一为 `done` |
| Provider ASR 偶发网络 EOF 使 Web 端“没反应” | `/asr` handler 未捕获 Provider 异常 | ASR 增加一次重试，并把异常返回成结构化 JSON |
| 预检脚本误报 Brain tool call 失败 | 请求改成 `atlas.ota.check`，校验仍期待旧工具 | 校验逻辑改为检查 OTA manifest 协议 |

## 关键测试记录

### 固件与设备状态

- 固件指纹：`0.14.7-acceptance`
- Build tag：`atlas-dualeye-Jun 23 2026 22:40:15`
- Wi-Fi：STA 已连接，IP `192.168.3.60`；AP 同时开启 `AtlasRover-E235`
- 音频：`input_ready=true`，`output_ready=true`，volume `90`
- Brain WS：`connected=true`，stage `connected`
- Voice：`available=true`，transport `brain_ws_binary`

### OPUS 真流

最终测试文件：`/tmp/atlas_opus_final_0147_retry.json`

```json
{
  "ok": true,
  "brain_frames": 31,
  "brain_atlas_frames": 31,
  "brain_seq_gaps": 0,
  "brain_payload_len_mismatches": 0,
  "brain_estimated_audio_ms": 1860,
  "device_stage": "done",
  "device_frames_sent": 31,
  "device_muted_frames": 0,
  "device_last_error": "ESP_OK"
}
```

### OPUS 语音 Turn

补充测试文件：`/tmp/atlas_opus_turn_alias_test.json`

```json
{
  "ok": true,
  "frames": 31,
  "packet_cache_count": 31,
  "decode": {
    "ok": true,
    "wav_bytes": 59390,
    "ogg_bytes": 5403,
    "packets": 31
  },
  "turn": {
    "ok": true,
    "asr_text": "嗯。",
    "tts_ready": false
  }
}
```

说明：这次自动测试环境里没有对着设备说完整指令，ASR 只识别到“嗯。”，所以没有生成有效回复和 TTS；但 OPUS packet -> Ogg Opus -> WAV -> ASR 的链路已跑通。

### Provider

最终测试文件：`/tmp/atlas_provider_final_0147_retry.txt`

- LLM：通过，回答“等于2呀！”
- TTS：通过，生成 WAV `81964 bytes`
- ASR：通过，返回“你好，我是阿特拉西。”
- Provider 配置：MiMo LLM / ASR / TTS 均启用；API Key 已在本机环境配置，报告不记录密钥。

### 离线降级

测试方法：停止 Mac Brain，等待设备自动重连逻辑进入下一轮。  
结果：

- DualEye `/api/status/lite` 仍可访问。
- UI 回到本地 `eyes` 页面，未黑屏，未进入异常文字页。
- `brain_ws.connected=false`
- `brain_ws.stage=brain_offline`
- `voice.available=false`
- `voice.offline_reason=brain_offline`

随后重启 Brain，设备自动恢复：

- `brain_ws.connected=true`
- `voice.available=true`

## 当前真实能力边界

1. 当前语音主链路已经从零散 HTTP 推进到 DualEye 常驻 Brain WebSocket，具备产品化基础。
2. OPUS 现在已具备 DualEye 到 Mac Brain 的真实 60ms 二进制帧上行，并完成“流结束后封装 Ogg -> 解码 WAV -> ASR”的结束式 turn PoC；还没有升级为边录边识别、边生成边播报的低延迟流式 ASR/TTS 产品链路。
3. WakeNet 模型资源已经能探测和初始化，但正式常驻唤醒与 AEC 还没有启用；当前仍以能量门限/VAD 和播放期间 mute 作为保护。
4. 时钟、日历、番茄、聊天、电子宠物页面接口可用；美术观感仍需要肉眼评审继续打磨。
5. OTA manifest、包管理、app OTA 入口已具备；bootloader、partition table、SR model、SPIFFS storage 变化仍建议 USB 全量刷。
6. 这版已明显高于早期“调试页 + 易断链”的状态，但离 xiaozhi 那种完整连续语音产品仍差流式 ASR/TTS、WakeNet/AEC、长期压测和多设备平台能力。

## 复盘

这轮最有价值的改动不是又加了一个功能，而是把几个“用户会觉得心累”的断点收住了：Brain 离线不再把设备拖进异常页，Provider 出错不再表现成 Web 端没反应，OPUS 成功传输后状态不再误导，页面能力列表和实际页面实现开始一致。

下一轮最应该做的是把“结束式 OPUS turn”推进成“低延迟连续对话”：边录边切 VAD 语音段、边送 ASR，回答期间明确状态和动画，播报期间做可靠的自激抑制，再做 20 到 30 分钟连续对话压测。只有这一步过了，才真正接近一个能拿给别人玩的桌面机器人。
