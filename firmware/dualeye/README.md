# Atlas Rover Mk.1 DualEye 固件 V0.6

这个目录是 `ESP32-S3-DualEye-Touch-LCD-1.28` 的 ESP-IDF 固件工程，目标芯片为 `esp32s3`。

V0.6 已经从单文件脚手架升级为可配网、可管理、可日常控制、可驱动双屏、可接收 MimiClaw 结构化意图，并带有电子宠物状态机的模块化程序：

- 双目表情参数模型：每块实体屏幕对应一只眼睛。
- 表情状态机：待机、开心、聆听、思考、说话、移动、好奇、困倦、惊讶、眨眼、爱心、爱钱、生气、充电、错误、大哭。
- UART 底盘协议：只发送 `AR1,` 开头的运动/停止指令。
- 双板职责划分：DualEye 负责 HMI/语音/意图，底盘板负责普通 N20 + DRV8833 的开环短时差速控制、限速和超时停车。
- 安全超时：DualEye 发出移动指令后会按动作时长延后补发 `AR1,STOP`，防止底盘板异常时持续移动。
- 语音事件接口：MimiClaw 可输出标准事件、tool-call 或 `atlas.mimiclaw.v1` 意图来驱动表情、页面、应用和底盘指令。
- 电子宠物状态机：维护心情值、能量值、好奇心；长时间不互动会困/睡觉，触摸会开心，移动后进入巡游状态，音乐/讲故事/对话有专属状态。
- NVS 配置：保存 Wi-Fi、LLM 模式、Base URL、Model、API Key 和安全限制。
- Wi-Fi 配网：无配置时开启 `AtlasRover-XXXX` SoftAP；有配置时优先连接路由器，失败后回落 APSTA。
- Web 管理页：手机/电脑访问设备 IP，可查看状态、保存配置、STOP、短时移动和测试文本意图。
- 配对码：启动时生成 6 位配对码；STOP 不需要配对码，移动和配置修改需要配对码。
- Web 入口拆分：`/app` 是日常应用页，`/admin` 是管理后台，根路径 `/` 默认进入应用页。
- Web 宠物控制：`/app` 已补电子宠物区块，可查看心情/能量/好奇心，并触发摸摸、玩耍、补能、休息、巡游、音乐、故事、对话状态。
- MimiClaw 适配层：当前支持本地关键词意图、LLM 配置状态，以及 `/api/mimiclaw/intent` 结构化意图执行入口。
- 主题同步：`classic`、`amber`、`mint`、`alert`、`night` 已从 Web 评审页同步到 `atlas_expression` palette。
- Waveshare 双屏后端：`atlas_display.c` 默认接入官方同款 GC9A01/LVGL 初始化；如果硬件初始化失败，会回退为串口日志渲染。
- Flash/PSRAM 配置：已按 DualEye 官方规格设置为 16MB Flash、8MB PSRAM 方向，并使用 4MB 应用分区。
- 开发演示：`main.c` 中 `ATLAS_ENABLE_DEV_EVENT_DEMO` 默认开启，只演示聆听/思考/说话/成功表情，不会发送移动指令；接入真实语音后可设为 `0`。

## 目录结构

```text
main/
  main.c                 程序入口和 FreeRTOS 任务
  atlas_expression.*     双眼表情参数帧
  atlas_display.*        双屏显示适配层，Waveshare GC9A01/LVGL + 日志回退
  atlas_rover_uart.*     AR1 UART 底盘协议
  atlas_voice.*          语音/MimiClaw 事件入口
  atlas_mimiclaw_intent.* MimiClaw tool-call / 结构化意图解析
  atlas_ui.*             页面、表情、运动、安全状态机
  atlas_pet.*            电子宠物心情/能量/好奇心状态机
  atlas_config.*         NVS 配置读写
  atlas_wifi.*           SoftAP/STA/APSTA 网络
  atlas_admin_http.*     Web 管理页和 REST API
  atlas_pairing.*        本地 6 位配对码
  atlas_llm_client.*     LLM 配置状态封装
  atlas_mimiclaw_adapter.* 文本/语音意图适配层
```

## 首次启动和管理页

1. 烧录后打开串口监视器，查看启动日志中的 6 位配对码。
2. 如果没有保存过 Wi-Fi，DualEye 会开启 `AtlasRover-XXXX` 热点。
3. 手机或电脑连接该热点，浏览器打开 `http://192.168.4.1`。
4. 默认入口 `/` 是用户应用页；后台设置入口是 `/admin`。
5. 在管理页保存 Wi-Fi、LLM/API 和安全设置。
6. 保存 Wi-Fi 后建议重启，设备会优先连接路由器；如果连接失败，会回落到 SoftAP/APSTA。

当前没有硬编码 `atlas-rover.local`，请优先使用页面或串口日志显示的 IP 地址。后续如果确认 ESP-IDF 工程可稳定接入 mDNS，再补本地域名。

## Web API 骨架

| 接口 | 方法 | 配对码 | 说明 |
|---|---|---|---|
| `/` / `/app` | GET | 否 | 用户应用页：表情、显示、移动、MimiClaw 应用入口 |
| `/admin` | GET | 否 | 管理后台：配网、API、安全和调试 |
| `/api/status` | GET | 否 | 查看 Wi-Fi、UI、电子宠物、LLM、安全状态，不返回 API Key 明文 |
| `/api/rover/stop` | POST | 否 | 立即发送 `AR1,STOP` |
| `/api/rover/move` | POST | 是 | 短时移动，受最大速度/最大时长限制 |
| `/api/app/expression` | POST | 是 | 切换双眼表情 |
| `/api/app/page` | POST | 是 | 切换显示页：双眼、时钟、闹钟、照片、状态等 |
| `/api/app/action` | POST | 是 | 触发应用动作：音乐、故事、陪聊、日历、番茄、闹钟；当前为 MimiClaw 占位入口 |
| `/api/pet/event` | POST | 是 | 触发电子宠物事件：`touch`、`play`、`feed`、`rest`、`patrol`、`music`、`story`、`chat` |
| `/api/config/wifi` | POST | 是 | 保存 Wi-Fi |
| `/api/config/llm` | POST | 是 | 保存 LLM 模式、Base URL、Model、API Key |
| `/api/config/safety` | POST | 是 | 保存是否允许移动、最大速度、最大时长 |
| `/api/config/ui` | POST | 是 | 保存主题、屏幕亮度、音量 |
| `/api/voice/text` | POST | 是 | 文本意图测试，进入 MimiClaw 适配层 |
| `/api/mimiclaw/intent` | POST | 是 | 接收 MimiClaw tool-call 或 `atlas.mimiclaw.v1` 意图，驱动表情、页面、应用和安全运动 |
| `/api/config/reset` | POST | 是 | 清除 Wi-Fi 和 LLM 配置 |
| `/api/system/reboot` | POST | 是 | 重启设备 |

安全默认值：

- `motion_enabled=true`，刚烧录后即可用 Web/语音做短时移动测试。
- `control_mode=manual`，默认进入手动模式，Web 方向按钮可以下达短时移动指令。
- 最大速度 `40%`，最大时长 `700 ms`。
- STOP 最高优先级，不需要配对码。
- Web 移动和配置修改仍需要 6 位配对码，避免同一局域网里的误触。
- API Key 只存 NVS，不打印、不进仓库；当前原型阶段尚未启用 NVS 加密，建议只放低风险测试 Key。

控制模式：

| 模式 | 允许的运动来源 | 说明 |
|---|---|---|
| `manual` | Web 手动控制 | 用户在应用页点前进/后退/转向；DualEye 裁剪速度和时长后通过 UART 下发 |
| `ai` | 语音 / MimiClaw / LLM | AI 产生结构化运动意图；DualEye 做 Safety Guard 后通过 UART 下发 |

无论哪种模式，底盘板看到的仍是 `AR1,` UART 指令；底盘板不直接访问 Web 管理页。

## UART 接线

DualEye 官方接口表确认 LCD1-Board SH1.0 14PIN 暴露 UART：

```text
DualEye LCD1 Pin10 UART_TXD -> XIAO D7 / GPIO20 / RX
DualEye LCD1 Pin9  UART_RXD <- XIAO D6 / GPIO21 / TX
DualEye LCD1 Pin2/Pin6 GND  <-> XIAO GND
```

注意事项：

- 这是 3.3 V TTL UART，不是 RS232。
- XIAO ESP32C3 是 3.3 V TTL，可与 DualEye UART 直连；若换成 5 V TTL 底盘板，进入 DualEye RX 前必须做分压或电平转换。
- XIAO 可由 5 V 升压支路供电，但电机供电不要从 DualEye 板取。
- 底盘板必须忽略所有不带 `AR1,` 前缀的串口内容，避免启动日志或乱码误触发电机。

## 双屏显示后端

官方资料和示例确认 DualEye 使用双 1.28 英寸 240x240 圆屏，ESP-IDF 示例为 `esp_lcd_gc9a01` + LVGL。当前固件已把官方示例里的板级参数收束到 `main/atlas_display.c`：

| 项目 | 左屏 | 右屏 | 共用 |
|---|---:|---:|---:|
| LCD 控制器 | GC9A01 | GC9A01 | SPI2 |
| 分辨率 | 240x240 | 240x240 | - |
| MOSI/SCLK/MISO | - | - | GPIO42 / GPIO41 / GPIO40 |
| DC | - | - | GPIO45 |
| CS | GPIO47 | GPIO38 | - |
| RST | GPIO48 | GPIO8 | - |
| 背光 | GPIO46 | GPIO39 | LEDC 0/1 |
| 触摸 I2C | GPIO10/11 | GPIO2/3 | CST816S，后续接入事件层 |

显示渲染仍然走我们自己的 `atlas_eye_pose_t`，不是直接复制官方 demo 的 UI。`atlas_expression_make_frame_with_theme()` 负责把表情、运动、音量和主题组合成左右眼参数；`atlas_display_render()` 再把左右眼分别画到两块实体屏。

主题 ID：

| ID | 中文名 | 说明 |
|---|---|---|
| `classic` | 经典蓝眼 | 默认开箱主题，黑底青蓝眼。 |
| `amber` | 琥珀巡航 | 更接近黄铜车架和复古巡航感。 |
| `mint` | 薄荷友好 | 更温和，适合陪伴/讲故事。 |
| `alert` | 红色警戒 | 错误、拒绝、急停更醒目。 |
| `night` | 低亮夜航 | 夜间低亮、柔蓝显示。 |

## 协议示例

```text
AR1,STOP
AR1,MOVE,F,40,500
AR1,MOVE,B,35,400
AR1,TURN,L,30,350
AR1,TURN,R,30,350
```

`MOVE` 和 `TURN` 都使用方向、速度百分比和持续时间。Mk.1 先按开环时间/PWM 标定，不承诺精确距离或精确角度。

建议底盘板 ACK：

```text
AR1,ACK,OK
AR1,ACK,BUSY
AR1,ACK,ERR
```

## 构建

在 VS Code ESP-IDF 插件中运行任务：

- `ESP-IDF: 构建 DualEye 固件`
- `ESP-IDF: 烧录 DualEye 固件`
- `ESP-IDF: 监视 DualEye 串口`

命令行也可以：

```bash
cd firmware/dualeye
export IDF_PATH="$HOME/.espressif/esp-idf-v5.5.2"
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.9_env"
. "$IDF_PATH/export.sh"
idf.py set-target esp32s3
idf.py build
```

底盘固件在 `firmware/chassis_xiao_esp32c3`，目标芯片 `esp32c3`。DualEye 与底盘板分别烧录，MimiClaw 接口层随 DualEye 固件构建。

如果本机设置了 SOCKS 代理，ESP-IDF component manager 可能需要 Python 依赖：

```bash
env -u http_proxy -u https_proxy -u HTTP_PROXY -u HTTPS_PROXY \
  "$IDF_PYTHON_ENV_PATH/bin/python" -m pip install PySocks
```

本地已经验证过一次：`idf.py build` 可以生成 `build/atlas_rover_dualeye.bin`，V0.6 大小约 `0x111b90`，4MB 应用分区剩余约 73%。

## 后续接入点

1. 真机屏幕：上电实测 GC9A01/LVGL 后端的旋转、左右眼方向、背光曲线和刷新稳定性。
2. 触摸：接入 CST816S 双路触摸事件，进入 `atlas_ui_handle_voice_intent()` 或新增 `atlas_ui_handle_touch_event()`。
3. MimiClaw：当前可通过 `/api/mimiclaw/intent` 提交 tool-call；下一步接真实 MimiClaw agent loop。
4. 音频：把麦克风 RMS/TTS 音量写入 `audio_level`，驱动聆听和说话表情脉冲。
5. 底盘 ACK：底盘板回传 `AR1,ACK,*`，DualEye 会更新状态或进入错误表情。
6. 真实 LLM：`atlas_llm_client.*` 目前只做配置状态；云端/宿主调用要先输出结构化意图，再进入本地 Safety Guard。
