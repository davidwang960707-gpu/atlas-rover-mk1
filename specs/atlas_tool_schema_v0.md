# Atlas Tool Schema V0

版本：`atlas.tools.v0`  
状态：草案，兼容当前 `/api/tools/list`、`/api/tools/call`、`/mcp/tools/list`、`/mcp/tools/call`

## 1. 目标

统一 Web、语音、LLM、Atlas Brain、设备固件对“可执行能力”的描述方式。工具不是某个页面按钮，也不是某段关键词逻辑，而是平台能力契约。

## 2. 工具结构

```json
{
  "name": "atlas.page.show",
  "description": "切换设备页面",
  "target": "device",
  "risk": "display",
  "confirm_required": false,
  "timeout_ms": 2000,
  "inputSchema": {
    "type": "object",
    "properties": {
      "page": {
        "type": "string",
        "enum": ["eyes", "clock", "chat", "calendar", "pomodoro", "status"]
      }
    },
    "required": ["page"]
  },
  "outputSchema": {
    "type": "object",
    "properties": {
      "ok": {"type": "boolean"},
      "page": {"type": "string"}
    }
  },
  "offline_policy": "device_local"
}
```

## 3. 风险等级

| risk | 说明 | 是否默认需要确认 |
|---|---|---|
| `read` | 查询状态、天气、时间 | 否 |
| `display` | 切页面、切表情、展示文本 | 否 |
| `audio` | 播放 TTS、音乐、故事 | 否，音量过大时可提示 |
| `config` | 修改 Wi-Fi、Provider、设备名 | 是 |
| `ota` | 固件/资源升级 | 是 |
| `motion` | 电机、底盘、机械动作 | 是，当前桌面版默认关闭 |

## 4. 目标类型

| target | 说明 |
|---|---|
| `device` | 需要设备执行，例如切屏、播音、表情 |
| `brain` | Atlas Brain 本地执行，例如搜索、天气、角色、Provider |
| `provider` | 外部模型或服务，例如 LLM/ASR/TTS |
| `hybrid` | Brain 决策后设备执行，例如讲故事并切宠物表情 |

## 5. 核心工具集

### 5.1 页面与显示

| 工具 | 说明 |
|---|---|
| `atlas.page.show` | 切换页面 |
| `atlas.expression.set` | 设置表情 |
| `atlas.theme.set` | 设置双眼主题 |
| `atlas.ui.set_chat_mode` | 对话界面模式：文字、pet_head、eyes_only |

### 5.2 宠物

| 工具 | 说明 |
|---|---|
| `atlas.pet.set_state` | 设置宠物状态 |
| `atlas.pet.play_animation` | 播放宠物动画 |
| `atlas.pet.event` | 触摸、长时间不互动、讲故事、唱歌等事件 |

### 5.3 应用

| 工具 | 说明 |
|---|---|
| `atlas.clock.show` | 打开时钟 |
| `atlas.clock.sync` | 校准时间 |
| `atlas.pomodoro.start` | 开始番茄专注 |
| `atlas.pomodoro.stop` | 停止番茄 |
| `atlas.calendar.today` | 展示今日日历 |
| `atlas.weather.query` | 查询天气 |
| `atlas.story.tell` | 讲故事 |
| `atlas.music.play` | 播放音乐或歌唱 TTS |
| `atlas.chat` | 普通对话 |

### 5.4 诊断与 OTA

| 工具 | 说明 |
|---|---|
| `atlas.status.read` | 读取状态 |
| `atlas.selftest.run` | 运行自检 |
| `atlas.audio.opus_stream.status` | OPUS 状态 |
| `atlas.ota.check` | 检查固件/资源包 |

## 6. 调用返回

```json
{
  "ok": true,
  "name": "atlas.page.show",
  "result": {
    "page": "pomodoro"
  },
  "speak": {
    "enabled": true,
    "text": "好的，已经打开番茄专注。"
  },
  "ui": {
    "pet_state": "happy"
  }
}
```

失败：

```json
{
  "ok": false,
  "name": "atlas.weather.query",
  "error": {
    "code": "provider_not_configured",
    "message": "Weather provider not configured",
    "retryable": false
  }
}
```

## 7. 兼容策略

- 当前 Brain 和固件已有 `/api/tools/list`，可以继续返回 `atlas.tools.v0.desk_apps`。
- 新工具必须先出现在 Brain 侧，再决定是否需要设备侧实现。
- 设备不支持的工具必须返回明确错误，而不是静默成功。
- motion 工具在当前桌面版默认不注册或 `enabled=false`。
- 固件 common 层以 `atlas_common_tool_schema.*` 固化本地工具能力声明、参数摘要、结果码和兼容 JSON writer；HTTP route 可以复用同一份 schema 输出。
- 固件侧 `/api/tools`、`/api/tools/list`、`/mcp/tools/list` 返回同一份兼容工具表；`/api/tools/call`、`/mcp/tools/call` 保持旧字段 `ok/protocol/tool/result/page/expression/error`，并可兼容式新增 `error_code`。
- DualEye 本地主链路工具包括页面/表情/主题、`atlas.ui.set_chat_mode`、时钟、番茄、日历、宠物头状态/动画、Brain 离线状态、OPUS 状态和 OTA 检查；小车移动工具只能 `enabled=false` 或按安全策略降级。

## 8. 验收方法

```bash
curl http://127.0.0.1:8787/api/tools/list
curl -X POST http://127.0.0.1:8787/api/tools/call \
  -H 'Content-Type: application/json' \
  -d '{"name":"atlas.pomodoro.start","arguments":{"task_name":"测试","focus_minutes":1}}'
```

最小验收：

- 工具有 `name/description/inputSchema/risk/target`。
- 失败有结构化 error。
- 工具调用后设备状态可在 `/api/status/lite` 或 `/api/device/scene` 看到。
