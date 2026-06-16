# Atlas Rover Mk.1 DualEye 固件 V0.3

这个目录是 `ESP32-S3-DualEye-Touch-LCD-1.28` 的 ESP-IDF 固件工程，目标芯片为 `esp32s3`。

V0.3 已经从单文件脚手架升级为可配网、可管理、可日常控制的模块化程序：

- 双目表情参数模型：每块实体屏幕对应一只眼睛。
- 表情状态机：待机、开心、聆听、思考、说话、移动、好奇、困倦、惊讶、眨眼、拒绝、充电、错误。
- UART 底盘协议：只发送 `AR1,` 开头的运动/停止指令。
- 双板职责划分：DualEye 负责 HMI/语音/意图，底盘板负责电机闭环/限速/超时停车/DRV8833。
- 安全超时：DualEye 发出移动指令后会在约 700 ms 后主动补发 `AR1,STOP`。
- 语音事件接口：miniClaw/MimiClaw 后续只需要输出标准事件即可驱动表情和底盘指令。
- NVS 配置：保存 Wi-Fi、LLM 模式、Base URL、Model、API Key 和安全限制。
- Wi-Fi 配网：无配置时开启 `AtlasRover-XXXX` SoftAP；有配置时优先连接路由器，失败后回落 APSTA。
- Web 管理页：手机/电脑访问设备 IP，可查看状态、保存配置、STOP、短时移动和测试文本意图。
- 配对码：启动时生成 6 位配对码；STOP 不需要配对码，移动和配置修改需要配对码。
- Web 入口拆分：`/app` 是日常应用页，`/admin` 是管理后台，根路径 `/` 默认进入应用页。
- MimiClaw 适配层：当前先做本地关键词意图和 LLM 配置状态，真实云端/宿主调用后续接入。
- 显示适配占位：当前用日志输出每只眼的参数，真机双屏绘制后续接入 `atlas_display.c`。
- Flash 配置：已按 DualEye 官方规格设置为 16MB，并使用 4MB 应用分区；PSRAM 等真机显示示例接入时再启用。
- 开发演示：`main.c` 中 `ATLAS_ENABLE_DEV_EVENT_DEMO` 默认开启，只演示聆听/思考/说话/成功表情，不会发送移动指令；接入真实语音后可设为 `0`。

## 目录结构

```text
main/
  main.c                 程序入口和 FreeRTOS 任务
  atlas_expression.*     双眼表情参数帧
  atlas_display.*        双屏显示适配层，当前是日志渲染器
  atlas_rover_uart.*     AR1 UART 底盘协议
  atlas_voice.*          语音/miniClaw 事件入口
  atlas_ui.*             页面、表情、运动、安全状态机
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
| `/api/status` | GET | 否 | 查看 Wi-Fi、UI、LLM、安全状态，不返回 API Key 明文 |
| `/api/rover/stop` | POST | 否 | 立即发送 `AR1,STOP` |
| `/api/rover/move` | POST | 是 | 短时移动，受最大速度/最大时长限制 |
| `/api/app/expression` | POST | 是 | 切换双眼表情 |
| `/api/app/page` | POST | 是 | 切换显示页：双眼、时钟、闹钟、照片、状态等 |
| `/api/app/action` | POST | 是 | 触发应用动作：音乐、故事、陪聊、闹钟；当前为 MimiClaw 占位入口 |
| `/api/config/wifi` | POST | 是 | 保存 Wi-Fi |
| `/api/config/llm` | POST | 是 | 保存 LLM 模式、Base URL、Model、API Key |
| `/api/config/safety` | POST | 是 | 保存是否允许移动、最大速度、最大时长 |
| `/api/voice/text` | POST | 是 | 文本意图测试，进入 MimiClaw 适配层 |
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
DualEye LCD1 Pin10 UART_TXD -> 底盘板 RX
DualEye LCD1 Pin9  UART_RXD <- 底盘板 TX
DualEye LCD1 Pin2/Pin6 GND  <-> 底盘板 GND
```

注意事项：

- 这是 3.3 V TTL UART，不是 RS232。
- 若底盘板 TX 是 5 V TTL，进入 DualEye RX 前必须做分压或电平转换。
- 底盘板如需 5 V 逻辑供电，可以另接 5 V，但电机供电不要从 DualEye 板取。
- 底盘板必须忽略所有不带 `AR1,` 前缀的串口内容，避免启动日志或乱码误触发电机。

## 协议示例

```text
AR1,STOP
AR1,MOVE,F,40,500
AR1,MOVE,B,35,400
AR1,TURN,L,30
AR1,TURN,R,30
```

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
cd "/Users/macbook/Documents/Atlas One/firmware/dualeye"
export IDF_PATH="$HOME/.espressif/esp-idf-v5.5.2"
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.9_env"
. "$IDF_PATH/export.sh"
idf.py set-target esp32s3
idf.py build
```

## 后续接入点

1. 真机屏幕：替换 `main/atlas_display.c`，接入 Waveshare 官方 ESP-IDF/LVGL 双屏初始化。
2. 触摸：触摸事件进入 `atlas_ui_handle_voice_intent()` 或新增 `atlas_ui_handle_touch_event()`。
3. miniClaw/MimiClaw：把语音理解结果映射成 `atlas_voice_event_t`，不要直接散落发送 UART。
4. 音频：把麦克风 RMS/TTS 音量写入 `audio_level`，驱动聆听和说话表情脉冲。
5. 底盘 ACK：底盘板回传 `AR1,ACK,*`，DualEye 会更新状态或进入错误表情。
6. 真实 LLM：`atlas_llm_client.*` 目前只做配置状态；云端/宿主调用要先输出结构化意图，再进入本地 Safety Guard。
