# MiniClaw Skills 与 Agent 设计 - Atlas Rover Mk.1

版本：V0.1
目标：让 MiniClaw/MimiClaw 成为 Atlas Rover 的“大脑”，但不绕过 DualEye 和底盘板的本地安全保护。

## 1. 结论

需要在 MiniClaw/MimiClaw 中预置 Skills 或 Agent。

DualEye 侧的 `atlas_mimiclaw_adapter.*` 只是“接口适配层”，负责把外部大脑输出转换成固件内部可执行的 `atlas_voice_intent_t`、页面切换、表情切换和应用动作。MiniClaw/MimiClaw 侧还必须有一套固定的机器人技能定义，否则 LLM/Agent 可能会输出自由文本，DualEye 就很难稳定、安全地执行。

正确分层是：

```text
用户语音/文本
  -> MiniClaw/MimiClaw Agent
  -> 结构化 AtlasBrainIntent
  -> DualEye atlas_mimiclaw_adapter
  -> DualEye UI / 表情 / 应用 / Safety Guard
  -> UART 底盘板
  -> DRV8833 / 电机
```

MiniClaw/MimiClaw 只能输出结构化意图，不能直接输出 UART 字符串，不能直接驱动电机。

## 2. 预置 Agent 建议

| Agent | 角色 | 说明 |
|---|---|---|
| `atlas_rover_brain` | 总控大脑 | 接收用户语音/文本，判断是聊天、显示、应用、运动还是状态查询。 |
| `atlas_expression_director` | 表情导演 | 根据情绪、动作、语音状态切换双眼表情，如 happy、thinking、love、cry。 |
| `atlas_app_director` | 功能界面导演 | 切换 eyes、clock、music、story、chat、calendar、pomodoro、status 等页面。 |
| `atlas_motion_planner` | 运动意图规划 | 把“往前走一点”“转过来”变成短时、低速的运动意图。 |
| `atlas_safety_guard` | 安全审查 | 在 MiniClaw 侧先做第一层拒绝/澄清；DualEye 和底盘板仍做最终兜底。 |
| `atlas_companion` | 陪伴应用 | 负责讲故事、聊天、听音乐、番茄钟、日历提醒等偏应用能力。 |

Mk.1 阶段可以先实现一个总控 Agent，把其他 Agent 当作内部提示词/工具分组；等功能变多后再拆成多 Agent。

## 3. 预置 Skills 建议

### 3.1 表情与屏幕

| Skill | 参数 | 用途 |
|---|---|---|
| `eyes.set_expression` | `expression`、`duration_ms` | 切换双眼表情。 |
| `display.show_page` | `page` | 切换功能页面。 |
| `display.set_theme` | `theme` | 切换 classic、amber、mint、alert、night。 |
| `display.set_brightness` | `brightness` | 调整屏幕亮度。 |
| `status.report` | 无或 `scope` | 展示电量、Wi-Fi、UART、底盘安全状态。 |

### 3.2 机器人移动

| Skill | 参数 | 用途 |
|---|---|---|
| `rover.move` | `direction`、`speed`、`duration_ms` | 短时移动。必须限速、限时。 |
| `rover.stop` | 无 | 立即停止。任何模式都允许。 |
| `rover.set_mode` | `manual` / `ai` | 切换 Web 手动模式或 AI 语音模式。 |

运动类 Skill 的参数必须保守：

- `speed` 默认 30-40%，最高不得超过 DualEye 安全配置。
- `duration_ms` 默认 300-800 ms，最高不得超过 DualEye 安全配置。
- “一直走”“跟着我走”“巡逻”等连续行为，Mk.1 默认要求二次确认或拒绝。
- `stop` 优先级最高，不需要二次确认。

### 3.3 语音与陪伴应用

| Skill | 参数 | 用途 |
|---|---|---|
| `audio.speak` | `text`、`tone` | TTS 回复，同时触发 speaking 表情。 |
| `music.play` | `query`、`source` | 播放音乐或进入音乐页。 |
| `story.tell` | `topic`、`age`、`style` | 讲故事。 |
| `chat.reply` | `message` | 普通对话。 |
| `calendar.show` | `date` | 展示日历/日程。 |
| `calendar.add_reminder` | `title`、`time` | 新增提醒。 |
| `pomodoro.start` | `minutes` | 开始番茄钟。 |
| `pomodoro.stop` | 无 | 停止番茄钟。 |

这些 Skill 可以先只切页面和表情，真实音乐源、TTS、日历存储后续再逐步接入。

## 4. 标准输出协议

MiniClaw/MimiClaw 建议输出统一 JSON，暂命名为 `AtlasBrainIntent`：

```json
{
  "version": "atlas.intent.v1",
  "confidence": 0.92,
  "expression": "happy",
  "page": "music",
  "action": {
    "name": "music.play",
    "args": {
      "query": "轻松一点的音乐"
    }
  },
  "motion": null,
  "speech": "好呀，我给你放一首轻松一点的歌。",
  "safety": {
    "requires_confirmation": false,
    "reason": ""
  }
}
```

运动意图示例：

```json
{
  "version": "atlas.intent.v1",
  "confidence": 0.88,
  "expression": "moving",
  "page": "eyes",
  "action": null,
  "motion": {
    "direction": "forward",
    "speed": 35,
    "duration_ms": 600
  },
  "speech": "我往前走一点。",
  "safety": {
    "requires_confirmation": false,
    "reason": ""
  }
}
```

需要确认的意图示例：

```json
{
  "version": "atlas.intent.v1",
  "confidence": 0.63,
  "expression": "thinking",
  "page": "voice",
  "action": null,
  "motion": null,
  "speech": "你是想让我持续往前走，还是只往前挪一点？",
  "safety": {
    "requires_confirmation": true,
    "reason": "运动时长不明确"
  }
}
```

## 5. DualEye 侧执行映射

DualEye 收到 `AtlasBrainIntent` 后按以下顺序处理：

1. 如果有 `speech`，进入 speaking 或 thinking 表情。
2. 如果有 `expression`，调用表情状态机。
3. 如果有 `page`，切换功能页面。
4. 如果有 `action`，进入对应应用动作入口。
5. 如果有 `motion`，先经过 DualEye Safety Guard，再通过 UART 发给底盘板。
6. 如果 `requires_confirmation=true`，不执行运动，只显示确认/澄清状态。

DualEye 侧仍保留本地兜底：

- STOP 永远可用。
- 运动必须受 `motion_enabled`、`control_mode`、最大速度、最大时长限制。
- AI 模式下接收语音/MiniClaw 运动意图；手动模式下 Web 方向按钮可用。
- 底盘板还要做超时停车和电机保护。

## 6. Mk.1 最小实现顺序

1. MiniClaw 先预置 `atlas_rover_brain` 单 Agent。
2. 先实现这些 Skills：`eyes.set_expression`、`display.show_page`、`rover.move`、`rover.stop`、`audio.speak`。
3. DualEye 新增 `AtlasBrainIntent` JSON 解析入口。
4. `/api/voice/text` 从“关键词测试”升级为“MiniClaw 返回结构化意图测试”。
5. 再补音乐、讲故事、日历、番茄钟等应用 Skill。

## 7. 开发边界

- MiniClaw/MimiClaw 是大脑，不是电机驱动层。
- DualEye 是脸、交互屏、语音入口和安全网关。
- 底盘板是运动执行器和最后一道电机安全保护。
- Agent 可以建议动作，但最终执行权必须经过 DualEye 和底盘板。
