# MimiClaw 集成方案 - Atlas Rover Mk.1

版本：V0.3

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

Mk.1 当前采用“方案 2：Mac 桥接优先”：

| 阶段 | 目标 | 说明 |
|---|---|---|
| 当前 | DualEye 侧 MimiClaw 接口层 | 提供 `/api/mimiclaw/intent`，可接收 Mimiclaw tool-call/结构化意图，驱动表情、页面、应用和安全运动。 |
| 当前 | Mac 桥接服务 | Mac 运行 `tools/mimiclaw_bridge_macos.py`，把 MimiClaw/LLM/文本指令转成 DualEye intent，并自动补配对码。 |
| 后续 | 端侧 MimiClaw 工程合并 | 如果确认内存、分区和网络栈都稳定，再评估把 MimiClaw agent loop、tool registry、SPIFFS skills/memory 合进 DualEye 固件。 |

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
| `atlas_app_action` | 触发时钟、音乐、故事、对话、日历、番茄等应用入口。 |
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

## 7. Mac 桥接运行方式

### 7.1 推荐网络拓扑

推荐让三端都在同一个家用 Wi-Fi：

```text
家用 Wi-Fi / 路由器
  ├─ DualEye：STA IP，例如 192.168.31.123
  ├─ Mac：运行 tools/mimiclaw_bridge_macos.py，例如 192.168.31.20:8787
  └─ 手机/电脑：访问 DualEye Web 和 Mac bridge
```

流程：

1. 首次用 DualEye SoftAP `http://192.168.4.1/admin` 保存家用 Wi-Fi。
2. 重启 DualEye，读取 `/api/status` 或串口日志中的 `wifi.sta_ip`。
3. Mac 连接同一个 Wi-Fi，运行桥接时把 `--dualeye-url` 指向 DualEye STA IP。
4. Web `/app` 的桥接地址、`/admin` 的 Base URL 都填 Mac 的局域网地址，例如 `http://192.168.31.20:8787`。

这条链路同时满足：

- Mac 能访问 DualEye。
- 手机/电脑能访问 Mac bridge。
- Mac 保持互联网能力，能跑 MimiClaw/云端 LLM。

### 7.2 救援/离线拓扑

没有路由器时，可以让 Mac 和手机都连接 DualEye 热点：

```text
DualEye SoftAP：192.168.4.1
  ├─ Mac：192.168.4.x，运行 bridge
  └─ 手机：192.168.4.x，访问 DualEye 和 Mac bridge
```

此时：

```bash
python3 tools/mimiclaw_bridge_macos.py --dualeye-url http://192.168.4.1
```

Web 里桥接地址填 `http://Mac的192.168.4.x地址:8787`。这个模式适合临时测试显示/意图转发，但 Mac 可能没有互联网，云端 LLM 能力会受影响。

### 7.3 常见网络坑

- 手机连 DualEye 热点、Mac 连家用 Wi-Fi：手机通常访问不到 Mac bridge。
- Web 里填 `127.0.0.1:8787` 后用手机打开：访问的是手机自己，不是 Mac。
- 公司/酒店 Wi-Fi 有客户端隔离：DualEye、Mac、手机看似同 Wi-Fi，但彼此不能访问。
- macOS 防火墙或 VPN 拦截 Python 入站：手机访问 Mac bridge 会超时。
- DualEye 配上 Wi-Fi 后是 STA 模式；只有 STA 失败才回落 APSTA 救援。以 `/api/status` 的 `wifi.mode` 和 `sta_ip/ap_ip` 为准。

Mac 自检：

```bash
python3 tools/check_mimiclaw_network_macos.py \
  --dualeye-url http://192.168.31.123 \
  --bridge-url http://127.0.0.1:8787
```

### 7.4 运行命令

在 Mac 上运行：

```bash
python3 tools/mimiclaw_bridge_macos.py --dualeye-url http://192.168.4.1
```

如果 DualEye 已经连入家里路由器，也可以改成它在路由器里的 IP：

```bash
python3 tools/mimiclaw_bridge_macos.py --dualeye-url http://192.168.31.123
```

桥接服务默认监听：

```text
http://127.0.0.1:8787
http://<Mac局域网IP>:8787
```

常用测试：

```bash
curl -X POST http://127.0.0.1:8787/text \
  -H 'Content-Type: application/json' \
  -d '{"text":"开始 25 分钟番茄，任务是固件测试"}'

curl -X POST http://127.0.0.1:8787/intent \
  -H 'Content-Type: application/json' \
  -d '{"tool":"atlas_show_page","input":{"page":"clock"}}'
```

桥接支持的第一批文本指令：

- “时钟 / 几点 / clock” -> `atlas_show_page(page=clock)`
- “开始 25 分钟番茄，任务是 xxx” -> `atlas_pomodoro` + `atlas_show_page(page=pomodoro)`
- “日历 / 今天 / calendar” -> `atlas_calendar` + `atlas_show_page(page=calendar)`
- “开心 / 聆听 / 睡觉” -> `atlas_set_expression`
- “前进 / 后退 / 左转 / 右转 / 停止” -> `atlas_rover_move` / `atlas_rover_stop`
- “音乐 / 讲故事 / 普通聊天文本” -> `atlas_app_action` 或 `atlas_chat`

注意：运动类指令仍要求 DualEye 安全配置允许移动，并且 `control_mode=ai`。Web 手动方向键则要求 `control_mode=manual`。

## 8. 下一步工程合并清单

1. 先用 Mac 桥接稳定跑通 MimiClaw/LLM 文本到 DualEye intent 的链路。
2. 把 MimiClaw `message_bus`、`agent_loop`、`llm_proxy`、`tool_registry`、`skill_loader`、`memory` 按组件方式迁入 DualEye 工程。
3. 合并分区表：保留 16MB Flash，参考 MimiClaw OTA + SPIFFS 布局，同时给 DualEye app 留足空间。
4. 合并 PSRAM/TLS/WebSocket 配置。
5. 注册 Atlas Rover tools。
6. 把 `integrations/mimiclaw/spiffs_data/skills/atlas-rover-control.md` 放入 MimiClaw SPIFFS。
7. 真机验证：屏幕刷新、Wi-Fi/TLS、Agent loop、SPIFFS、UART 底盘控制是否互相抢内存或阻塞。
