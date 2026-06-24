# Atlas Brain Session V1

版本：`atlas.brain.session.v1`  
状态：草案，兼容当前 Atlas Brain `/ws/brain`

## 1. 目标

把 DualEye 与 Atlas Brain 的主交互链路统一到常驻 WebSocket 上。HTTP 继续保留为配置、自检、兜底、诊断，不再作为语音 turn 的主链路。

## 2. 连接

```text
WS /ws/brain
```

设备端由 `atlas_brain_ws_client` 主动连接 Atlas Brain。

## 3. 消息原则

| 类型 | 载体 | 说明 |
|---|---|---|
| 控制事件 | JSON text frame | hello、state、turn 事件、工具调用、错误 |
| 音频数据 | binary frame | WAV 或未来 OPUS chunk |
| OPUS 探针/真流 | `/ws/audio` 或 session 子通道 | 与普通控制事件隔离 |

## 4. 设备到 Brain

### 4.1 hello

```json
{
  "type": "hello",
  "protocol": "atlas.brain.session.v1",
  "device_id": "dualeye",
  "product_id": "atlas_dualeye_pet",
  "firmware": "0.14.x",
  "capabilities": {
    "audio_upload": "wav",
    "opus_stream": true,
    "tools": true,
    "display": "dual_round_240"
  }
}
```

### 4.2 state

```json
{
  "type": "state",
  "status": {
    "scene": {"page": "chat", "expression": "listen"},
    "audio_service": {"mode": "listening"},
    "ui": {"chat_mode": "pet_head"}
  }
}
```

### 4.3 turn.audio.begin

```json
{
  "type": "turn.audio.begin",
  "turn_id": "turn-001",
  "format": "wav",
  "sample_rate": 16000,
  "channels": 1,
  "bytes": 112044
}
```

随后发送 binary frame。Brain 必须支持设备 WebSocket 分片，不能假设一次 binary frame 就是完整 WAV。

### 4.4 turn.audio.end

```json
{
  "type": "turn.audio.end",
  "turn_id": "turn-001"
}
```

## 5. Brain 到设备

### 5.1 hello.ack

```json
{
  "type": "hello.ack",
  "protocol": "atlas.brain.session.v1",
  "session_id": "brain-session-001",
  "server_time_ms": 1780000000000
}
```

### 5.2 turn.result

```json
{
  "type": "turn.result",
  "turn_id": "turn-001",
  "asr_text": "打开番茄页面",
  "reply_text": "好的，已经打开番茄专注。",
  "pet_state": "happy",
  "page": "pomodoro",
  "tts_format": "wav",
  "tts_bytes": 38122
}
```

随后可发送 binary TTS 音频。

### 5.3 tool.call

```json
{
  "type": "tool.call",
  "tool_call_id": "tool-001",
  "name": "atlas.page.show",
  "arguments": {"page": "pomodoro"}
}
```

设备执行后返回：

```json
{
  "type": "tool.result",
  "tool_call_id": "tool-001",
  "ok": true,
  "result": {"page": "pomodoro"}
}
```

## 6. 错误格式

```json
{
  "type": "error",
  "stage": "tts",
  "code": "provider_timeout",
  "message": "TTS provider timeout",
  "retryable": true,
  "turn_id": "turn-001"
}
```

## 7. 降级策略

| 场景 | 要求 |
|---|---|
| Brain 连接失败 | 设备继续显示双眼、时钟、番茄、日历；语音显示 Brain 离线 |
| TTS 失败 | 页面/表情工具仍可执行；只跳过播报 |
| ASR 失败 | 保持监听状态并提示听不清 |
| LLM 失败 | 设备不进入异常文字页，展示可恢复状态 |
| WS 断开 | 自动重连，HTTP 配置页仍可访问 |

## 8. 验收方法

```bash
curl http://DUALEYE_IP/api/status/lite
curl http://127.0.0.1:8787/api/brain/session
```

最小验收：

- `brain_ws.connected=true`
- Brain 能收到 hello。
- WAV 分片上传可被 Brain 聚合。
- Brain 返回 `turn.result` 后设备能播放或软降级。

