# Atlas AOP1 Audio Frame V0

版本：`atlas.audio.aop1.v0`  
状态：草案，兼容当前 `/ws/audio` 与 `tools/simulate_opus_stream.py`

## 1. 目标

定义 Atlas OPUS/PCM 二进制音频帧格式，用于 DualEye 到 Atlas Brain 的持续音频流。它服务于低延迟 ASR、VAD、连续对话和后续 AEC 评估。

## 2. 传输

```text
WS /ws/audio
```

当前阶段 `/ws/audio` 独立于 `/ws/brain`，避免音频流和普通控制事件互相拖累。

## 3. 帧结构

AOP1 帧由固定头和 payload 构成：

| 字段 | 类型 | 说明 |
|---|---|---|
| `magic` | 4 bytes | 固定 `AOP1` |
| `version` | u8 | 当前为 1 |
| `header_len` | u8 | header 字节数 |
| `codec` | u8 | 1=OPUS, 2=PCM |
| `flags` | u8 | bit flags |
| `seq` | u32 | 连续帧序号 |
| `timestamp_ms` | u32 | 设备侧时间戳 |
| `sample_rate` | u16 | 默认 16000 |
| `frame_ms` | u16 | 默认 60 |
| `channels` | u8 | 默认 1 |
| `payload_len` | u32 | payload 字节数 |
| `rms` | u16 | 可选音量指标 |
| `peak` | u16 | 可选峰值 |

字段顺序以当前 `tools/simulate_opus_stream.py` 和固件 `atlas_opus_stream` 的实现为准；本文档固化语义，不在本轮强行改线上的二进制布局。

## 4. codec

| 值 | codec | 说明 |
|---|---|---|
| 1 | OPUS | 推荐主路径 |
| 2 | PCM | 调试或兼容路径 |

## 5. Brain 侧统计

Brain 收到帧后至少统计：

| 指标 | 说明 |
|---|---|
| `frames` | 收到帧数 |
| `sequence_gaps` | seq 缺口 |
| `payload_len_mismatches` | header 与实际 payload 不一致 |
| `duration_ms` | 累计音频时长 |
| `avg_payload_bytes` | 平均 payload 大小 |
| `vad_segments` | VAD 语音段数量 |
| `last_error` | 最近错误 |

## 6. 降级策略

| 场景 | 策略 |
|---|---|
| OPUS encoder 不可用 | 回退 WAV turn 或 PCM probe |
| Brain `/ws/audio` 不可达 | 不影响本地页面和普通 Brain session |
| 播放 TTS 中 | 设备可暂停发送或标记 muted |
| 丢帧 | Brain 统计，但不立刻断开 |

## 7. 验收方法

```bash
python3 tools/simulate_opus_stream.py --url ws://127.0.0.1:8787/ws/audio --duration-ms 1800
curl http://127.0.0.1:8787/api/audio/stream/status
```

真机验收：

```bash
curl -X POST http://DUALEYE_IP/api/audio/opus-stream/start
curl http://DUALEYE_IP/api/audio/opus-stream/status
curl http://127.0.0.1:8787/api/audio/stream/status
```

通过条件：

- 30 帧/1.8 秒附近统计正常。
- `sequence_gaps=0` 或极低。
- `payload_len_mismatches=0`。
- 播放 TTS 时不会误判为用户语音。

