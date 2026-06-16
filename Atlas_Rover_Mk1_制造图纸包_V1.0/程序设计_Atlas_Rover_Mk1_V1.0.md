# Atlas Rover Mk.1 V1.0 程序设计

## 目标

软件分成两个核心能力：

1. 双目表情程序：两块圆屏显示眼睛、表情和动画效果，并支持切换时钟、状态、语音、设置等多主题页面。
2. miniClaw/MimiClaw 语音交互：接入语音理解和自然语言指令，把“往前走一点”“停下”“显示时间”等话语转换成安全的 RoverIntent，再通过 UART 或本地驱动控制小车。

注意：运动类命令必须先经过本地安全意图层。miniClaw/MimiClaw 或 LLM 只能产生结构化意图，不能直接写 UART、不能绕过 STOP/超时/限速保护。

## 总体架构

```text
触摸/语音/Wi-Fi
    -> EventBus
    -> UI Shell + Eye Engine
    -> 本地命令词/miniClaw Adapter
    -> Intent Router + Safety Watchdog
    -> Rover Link(UART 或本地 DRV8833/PCA9685)
    -> 底盘板/电机驱动
```

## 固件模块划分

| 模块 | 职责 | 说明 |
| --- | --- | --- |
| app_main | 系统启动、任务创建、全局事件总线 | 初始化显示、触摸、音频、Wi-Fi、UART、NVS 配置 |
| ui_shell | 页面管理和主题切换 | 管理双目主页、时钟页、状态页、语音页、设置页 |
| eye_engine | 双眼渲染和表情动画 | 眨眼、瞳孔跟随、眼皮曲线、情绪过渡、低帧率省电 |
| theme_manager | 多主题资源管理 | 赛博蓝、黄铜复古、夜间低亮、调试高对比等主题 |
| voice_service | 语音入口 | 板载麦克风收音、唤醒/命令词、VAD、录音状态上报 |
| mimiclaw_adapter | MimiClaw 适配层 | 把自然语言/agent tool call 转成标准 RoverIntent，不直接驱动电机 |
| intent_router | 意图路由和安全裁剪 | 优先处理 STOP；限制速度/时长；低置信度要求确认 |
| rover_link | 底盘通信 | 通过 UART 文本协议向底盘板下发 AR1,MOVE/AR1,TURN/AR1,STOP/AR1,LIGHT |
| safety_watchdog | 安全看门狗 | 运动命令超时、串口断开、低电量、过流/异常复位时进入 STOP |
| storage | 本地配置 | 保存主题、音量、亮度、唤醒词开关、上次页面；不保存危险运动状态 |

## 多主题页面

| 页面 | 用途 | 显示内容 | 切换/备注 |
| --- | --- | --- | --- |
| 双目表情主页 | 默认页面 | 开心、思考、聆听、说话、惊讶、生气、困倦、睡眠、移动中、错误 | 低延迟渲染，适合巡游时常驻 |
| 时钟主题页 | 桌面陪伴/待机 | 双圆屏分别显示小时/分钟、模拟表盘、日期、电量、Wi-Fi | 触摸或语音“显示时间/回到眼睛”切换 |
| 语音交互页 | 聆听/思考/回答 | 左眼显示输入波形，右眼显示状态环；回答时恢复说话表情 | miniClaw/MimiClaw 进入思考时切换 Thinking |
| 小车状态页 | 调试和运行 | 速度、方向、底盘板在线、UART 延迟、电池电压、灯光模式 | 调试阶段常用，正式巡游可隐藏 |
| 主题设置页 | 触摸操作 | 主题、亮度、音量、表情风格、时钟样式、语音开关 | 长按或双击进入，避免误触 |
| 错误/安全页 | 异常状态 | 串口断开、低电量、底盘超时、语音未联网、miniClaw 不可用 | 必须提供 STOP 和回到主页 |

## 双目表情状态

| 状态 ID | 中文名 | 视觉效果 | 触发场景 |
| --- | --- | --- | --- |
| idle | 待机 | 轻微呼吸、随机眨眼、瞳孔慢速漂移 | 巡游空闲、桌面陪伴 |
| happy | 开心 | 上弧笑眼、蓝色高光增强 | 识别成功、完成指令 |
| listen | 聆听 | 瞳孔放大、外圈脉冲、低亮波形 | 唤醒词后录音 |
| thinking | 思考 | 眼睛向上/侧看、加载弧线 | miniClaw/MimiClaw 推理中 |
| speaking | 说话 | 眼皮随音量轻动、嘴形可用前灯辅助 | TTS 或提示音输出 |
| moving | 移动中 | 瞳孔朝运动方向偏移，边缘流光 | 前进/后退/转向 |
| surprised | 惊讶 | 圆眼放大、短闪 | 突发障碍/命令冲突 |
| angry | 生气 | 斜眼皮、红/橙警示 | 拒绝危险指令或错误 |
| sleepy | 困倦 | 半闭眼、低亮、慢眨眼 | 长时间待机 |
| error | 错误 | 警示图标/断线符号 | 底盘断开、低电量、语音服务异常 |

## miniClaw / MimiClaw 接入策略

这里把用户口中的 miniClaw 按两类兼容处理：

- MimiClaw：ESP32-S3 端 OpenClaw-like 方案，适合未来直接嵌入固件；但直接集成前必须确认 DualEye 实际 flash/PSRAM 是否满足目标版本需求。
- 外部宿主/调试桥：可作为端侧 MimiClaw 合并前的临时联调方式；DualEye 作为语音、表情和串口控制终端。

| 模式/层级 | 优先级 | 设计说明 |
| --- | --- | --- |
| 本地安全层 | 优先级最高 | STOP、前进、后退、左转、右转、显示时间、切换表情等固定命令先由本地规则解析，保证离线可用。 |
| MimiClaw 端侧模式 | 中期目标 | 若确认 DualEye 具备足够 flash/PSRAM，可把 MimiClaw/OpenClaw-like agent 编进 ESP-IDF 固件，用 tool call 调用 rover.move/eyes.set_expression。 |
| 外部宿主/调试桥模式 | 备选/增强 | 若暂时使用 Mac/本地宿主，DualEye 作为语音和显示终端，通过 Wi-Fi WebSocket/HTTP 与宿主通信，再由 DualEye 转 UART 控底盘。 |
| 云端/Telegram 模式 | 可选 | MimiClaw 原始思路支持 Telegram/LLM API；Mk.1 只把它作为远程调试/长文本理解，不把云端回答直接当运动命令。 |
| 工具调用白名单 | 必须 | 只开放 rover.move、rover.turn、rover.stop、eyes.set_expression、ui.set_page、lights.set_mode；不开放任意串口写入。 |
| 意图确认 | 必须 | 长距离、长时间、高速、离开桌面等指令必须二次确认；低置信度只回答澄清，不动车。 |

## 自然语言到小车指令

| 用户说法 | 标准指令 | 安全处理 |
| --- | --- | --- |
| 前进/往前走/向前一点 | AR1,MOVE,F,40,500 | 默认低速短时，避免从桌面冲出 |
| 后退/退一点 | AR1,MOVE,B,35,400 | 默认更低速，防止后方线束或障碍 |
| 左转/向左看/左拐 | AR1,TURN,L,30 | 角度需底盘板标定，可先按时间估算 |
| 右转/向右看/右拐 | AR1,TURN,R,30 | 角度需底盘板标定，可先按时间估算 |
| 停下/别动/急停 | AR1,STOP | 最高优先级，任何状态立即执行 |
| 开心一点/生气/睡觉 | EXPR,happy / EXPR,angry / EXPR,sleepy | 只改表情，不动底盘 |
| 显示时间/切换时钟 | PAGE,clock | 切换到时钟主题页 |
| 回到眼睛/主页 | PAGE,eyes | 回到双目表情主页 |

## 工具调用白名单

```text
rover.move(direction, speed_percent, duration_ms)
rover.turn(direction, angle_deg)
rover.stop()
eyes.set_expression(expression_id)
ui.set_page(page_id)
lights.set_mode(mode_id)
system.get_status()
```

所有工具调用先进入 `intent_router`，再由 `safety_watchdog` 裁剪。任何超过 1000 ms 的连续运动、超过 60% 的速度、或语义不明确的移动指令，都要求二次确认。

## 页面与表情联动规则

- 唤醒词触发：切到 `listen` 表情，语音页显示输入状态。
- miniClaw/MimiClaw 推理中：切到 `thinking`，前灯可低亮呼吸。
- 回答中：切到 `speaking`，眼皮或高光随音量变化。
- 小车移动中：切到 `moving`，瞳孔偏向运动方向。
- 收到 STOP：立即切 `idle` 或 `surprised`，底盘停车。
- 底盘离线/低电量：切 `error`，拒绝运动指令。
- 长时间无交互：切 `sleepy` 或时钟页。

## 里程碑

| 阶段 | 任务 | 验收标准 |
| --- | --- | --- |
| P0 | 双屏点亮和页面框架 | 双眼主页、时钟页、状态页能触摸/串口切换 |
| P1 | 表情引擎 | idle/happy/listen/thinking/speaking/moving/error 八个核心表情可平滑切换 |
| P2 | UART 底盘协议 | DualEye 能发 AR1,MOVE/AR1,TURN/AR1,STOP，底盘板能超时停车 |
| P3 | 本地语音命令 | 固定命令词可离线或弱联网触发 STOP/移动/页面切换 |
| P4 | miniClaw/MimiClaw 适配 | 自然语言转 RoverIntent，tool call 通过白名单和安全裁剪后下发 |
| P5 | 整车联调 | 语音、表情、灯光、底盘动作联动；低电量/断线/误识别进入安全状态 |

## 实施建议

第一版先做“表情 + 页面 + UART + 本地命令词”，确保小车能稳定听懂固定动作命令。
第二版再接 miniClaw/MimiClaw，把自然语言、记忆和工具调用叠上去。这样研发风险最低：就算 agent 不在线，STOP、前进、后退、左转、右转、显示时间这些核心功能仍可用。
