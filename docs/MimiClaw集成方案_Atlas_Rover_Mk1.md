# MimiClaw 集成方案 - Atlas Rover Mk.1

版本：V0.2

## 1. 核对结论

本项目优先适配的是 `memovai/mimiclaw`，不是 MiniClaw。

MimiClaw 是一个完整的 ESP32-S3 AI assistant 固件：纯 C / ESP-IDF / FreeRTOS，包含 Wi-Fi、Telegram/Feishu/WebSocket/串口 CLI、Agent loop、LLM provider、tool calling、SPIFFS skills、memory、cron 等能力。它要求 ESP32-S3、16MB Flash、8MB PSRAM，这与 DualEye 的硬件方向接近。

MiniClaw 不能作为 MimiClaw 的端侧替代。它最多只能作为外部电脑宿主或调试桥，真正要跑在 DualEye/ESP32-S3 上的“大脑”应按 MimiClaw 方案走。

参考来源：

- https://github.com/memovai/mimiclaw
- https://github.com/memovai/mimiclaw/blob/main/docs/ARCHITECTURE.md

## 2. 烧录策略

同一块 ESP32-S3 不能同时烧录两份互相独立的固件。

如果采用端侧 MimiClaw 模式，最终必须是一份合并后的 DualEye 固件：

```text
DualEye 显示/触摸/语音/底盘安全
  + MimiClaw Agent loop / tools / memory / skills
  -> 编译成一份 atlas_rover_dualeye.bin
  -> 烧录到 ESP32-S3-DualEye-Touch-LCD-1.28
```

不能这样做：

```text
先烧 DualEye 固件
再烧 MimiClaw 固件
```

第二次烧录会覆盖第一次固件。

Mk.1 当前采用两阶段：

| 阶段 | 目标 | 说明 |
|---|---|---|
| V0.5 | DualEye 侧 MimiClaw 接口层 | 提供 `/api/mimiclaw/intent`，可接收 Mimiclaw tool-call/结构化意图，驱动表情、页面、应用和安全运动。 |
| V0.6+ | 端侧 MimiClaw 工程合并 | 把 MimiClaw 的 agent loop、tool registry、SPIFFS skills/memory、LLM 配置合进 DualEye 固件。 |

## 3. 当前已完成的 DualEye 接口

DualEye 固件侧新增：

- `atlas_mimiclaw_intent.*`
- `/api/mimiclaw/intent`
- 管理页 MimiClaw 结构化意图测试框

接口支持两种 JSON：

### 3.1 Mimiclaw tool-call 风格

```json
{
  "tool": "atlas_set_expression",
  "input": {
    "expression": "happy"
  }
}
```

```json
{
  "tool": "atlas_rover_move",
  "input": {
    "direction": "forward",
    "speed": 35,
    "duration_ms": 600
  }
}
```

### 3.2 Atlas Rover 结构化意图风格

```json
{
  "version": "atlas.mimiclaw.v1",
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

## 4. MimiClaw 侧建议工具

后续把 MimiClaw 合进 DualEye 固件时，应在 MimiClaw `tool_registry` 中增加 Atlas Rover 工具：

| Tool | 作用 |
|---|---|
| `atlas_set_expression` | 切换双眼表情。 |
| `atlas_show_page` | 切换功能页面。 |
| `atlas_app_action` | 触发音乐、故事、对话、日历、番茄等应用入口。 |
| `atlas_pet_event` | 触发电子宠物事件，例如摸摸、玩耍、补能、休息、巡游状态、音乐、故事、对话。 |
| `atlas_rover_move` | 短时移动，必须经 DualEye Safety Guard。 |
| `atlas_rover_stop` | 立即停止，最高优先级。 |
| `atlas_status_report` | 查询当前 UI、电子宠物、Wi-Fi、UART、底盘安全状态。 |

这些工具不直接写电机 GPIO，不直接写 UART。它们统一进入 DualEye 的 UI/安全层，再由 DualEye 决定是否发 `AR1,` UART 给底盘板。
电子宠物事件也不直接移动底盘；`patrol` 只切换宠物状态和表情，真正车轮运动仍必须走 `atlas_rover_move`。

## 5. Safety Guard

所有运动都必须满足：

- `motion_enabled=true`
- `control_mode=ai`
- speed 不超过 DualEye 配置上限
- duration 不超过 DualEye 配置上限
- STOP 永远可用
- 低置信度、连续运动、含糊移动指令要求确认或拒绝

底盘板仍是最后一道保护：限速、超时停车、电机驱动保护都在底盘板侧执行。

## 6. 用户 Web 端

Web 端分两类：

- `/app`：用户日常应用页，切换表情、页面、电子宠物、音乐、故事、对话、日历、番茄、小车移动。
- `/admin`：管理后台，配置 Wi-Fi/API/安全/主题，并提供 MimiClaw 结构化意图测试框。

MimiClaw 端侧合并完成前，`/admin` 的测试框可用于模拟 Mimiclaw tool call，提前验证 DualEye 执行链路。
`/api/status` 会返回 `pet` 对象，包括 `phase`、中文状态、心情、能量、好奇心、是否睡着和 SD 卡资源 ID。`/api/pet/event` 可供 Web、手机端或 Mimiclaw 调用。

## 7. 下一步工程合并清单

1. 把 MimiClaw `message_bus`、`agent_loop`、`llm_proxy`、`tool_registry`、`skill_loader`、`memory` 按组件方式迁入 DualEye 工程。
2. 合并分区表：保留 16MB Flash，参考 MimiClaw OTA + SPIFFS 布局，同时给 DualEye app 留足空间。
3. 合并 PSRAM/TLS/WebSocket 配置。
4. 注册 Atlas Rover tools。
5. 把 `integrations/mimiclaw/spiffs_data/skills/atlas-rover-control.md` 放入 MimiClaw SPIFFS。
6. 真机验证：屏幕刷新、Wi-Fi/TLS、Agent loop、SPIFFS、UART 底盘控制是否互相抢内存或阻塞。
