# Atlas Brain 改造记录

日期：2026-06-23

版本：`0.14.3-atlas-brain`

## 结论

项目主线正式收敛为：

```text
DualEye 设备端 + Atlas Brain / Mac 桥 + MiMo Provider + Atlas Tool Schema
```

旧 Agent 命名不再作为产品主线。历史接口和脚本只保留兼容入口，避免旧命令、旧固件和历史调试记录立刻失效。

## 本轮完成

| 项目 | 结果 |
|---|---|
| 主服务入口 | 旧桥接 wrapper 已移除；主入口统一为 `tools/atlas_brain_server.py` |
| 网络自检 | 旧网络检查 wrapper 已移除；入口统一为 `tools/check_atlas_brain_network_macos.py` |
| 启动脚本 | `tools/start_atlas_brain_mimo.sh` 改为启动 `tools/atlas_brain_server.py` |
| Brain 会话 | Mac 侧提供 `/api/brain/session` 和 `/ws/brain`；DualEye 固件新增主动 `atlas_brain_ws_client`，STA 联网后常驻连接 Mac Brain `/ws/brain` 并发送 hello/ping/state |
| 语音 turn 主链路 | DualEye `/api/voice/turn` 不再 POST HTTP WAV；改为在常驻 `/ws/brain` 内发送 JSON `turn.audio.begin` + binary WAV，Mac Brain 用同一条 WS 返回 `turn.result` 和 binary WAV TTS |
| Brain 离线降级 | `/ws/brain` 未连接时，语音能力返回 `brain_offline`，屏幕不进入异常页；双眼、时钟、日历、番茄等本地页面继续显示 |
| 音频流边界 | `/ws/audio` 保持承载 AOP1/60ms OPUS 二进制帧；控制事件不和音频帧混在一起 |
| 设备意图接口 | DualEye 主入口为 `/api/intent`，别名为 `/api/brain/intent`；旧命名路由已从活跃固件移除 |
| Web 文案 | DualEye 管理页、README、预览脚本和预检提示统一改为 Atlas Brain / Mac 桥口径 |
| 模块拆分 | 新增 `tools/atlas_brain_providers.py`，把 LLM/ASR/TTS/macOS TTS fallback 从主服务拆出 |
| 调用策略 | Atlas Brain 优先调用 `/api/intent`，新固件别名回退到 `/api/brain/intent` |
| MiMo Provider | `.atlas-brain.env` 已作为本地密钥入口；新增 `tools/check_atlas_providers.py`，可真实验证 LLM/TTS/ASR 链路 |
| 编译验证 | ESP-IDF 构建通过，`atlas_rover_dualeye.bin` 大小 `0x239bf0`，5MB app slot 剩余 `0x2c6410` |

## 当前架构口径

```text
DualEye
  - 显示、音频采集/播放、配网、轻量本地安全、结构化 intent 执行
  - 提供 /api/intent、/api/brain/intent、/api/tools/list、/api/tools/call、/api/audio/opus-stream/*
  - 主动常驻连接 Mac Brain /ws/brain；语音 turn 走 WS JSON + binary WAV，Brain 离线时只降级语音能力

Atlas Brain / Mac
  - ASR / LLM / TTS Provider
  - 设备会话、WS 语音 turn、binary TTS 下行、工具调用、OPUS 流接收、运行时评分、验收页、OTA 包
  - 提供 /ws/brain、/ws/audio、/api/tools/call、/api/runtime、/api/brain/session

MiMo Provider
  - 当前默认模型来源
  - API Key 留在 Mac 侧，不写入源码
```

## 对标 xiaozhi 后仍需推进

| 优先级 | 任务 | 说明 |
|---|---|---|
| P0 | DualEye 常驻连接 `/ws/brain` | 代码已完成主动客户端、WS 语音 turn、binary TTS 下行和离线降级；仍需烧录后实机确认 connected/stage |
| P1 | OPUS 真流接流式 ASR | 现在能接收并统计 AOP1 帧，下一步接入真正流式 ASR |
| P2 | 流式 TTS / 播放打断 | 本轮已完成 WS binary WAV 下行；真正边生成边播和用户打断仍需继续 |
| P3 | Tool ActionResponse | 工具结果需要统一表达是否播报、是否继续请求 LLM、是否切页面、是否失败 |
| P4 | Atlas Brain 模块拆分 | Provider 已拆出；后续继续拆 tools、sessions、ota、web routes |
| P5 | OTA 生产化 | 补签名、回滚、升级后健康确认和版本兼容策略 |

## 验收命令

```bash
python3 tools/atlas_brain_server.py --dry-run --host 127.0.0.1 --port 8787
python3 tools/simulate_opus_stream.py --url ws://127.0.0.1:8787/ws/audio --duration-ms 1800
python3 tools/check_atlas_providers.py --brain-url http://127.0.0.1:8787
python3 tools/check_atlas_preflash.py --brain-url http://127.0.0.1:8787 --skip-dualeye
```

本轮已验证：Python 语法检查通过；ESP-IDF 编译通过；`/api/brain/session` 返回 `atlas.brain.session.v1`；`/ws/brain` 本地协议测试通过 `hello -> ack turn.audio.begin -> turn.result ws_brain_binary`；`/ws/audio` OPUS 模拟流 30 帧通过；MiMo LLM 真实返回 `llm_chat`；MiMo TTS 生成 16k WAV；MiMo ASR 可从该 WAV 识别出“你好，我是阿特拉斯”；Provider 自检 `PASS`；预检 `PASS=10 WARN=0 FAIL=0`；跑流和 Provider 自检后 runtime score `100/100`，ready score `100/100`。DualEye 主动 `/ws/brain` 需要烧录后看 `/api/status/lite` 的 `brain_ws.connected` 实机确认。

新增会话端点：

```text
GET /api/brain/session
WS  /ws/brain
WS  /ws/audio
```
