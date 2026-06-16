# Atlas Rover Mk.1 DualEye 固件 V0.1

这个目录是 `ESP32-S3-DualEye-Touch-LCD-1.28` 的 ESP-IDF 固件工程，目标芯片为 `esp32s3`。

V0.1 已经从单文件脚手架升级为模块化程序：

- 双目表情参数模型：每块实体屏幕对应一只眼睛。
- 表情状态机：待机、开心、聆听、思考、说话、移动、好奇、困倦、惊讶、眨眼、拒绝、充电、错误。
- UART 底盘协议：只发送 `AR1,` 开头的运动/停止指令。
- 双板职责划分：DualEye 负责 HMI/语音/意图，底盘板负责电机闭环/限速/超时停车/DRV8833。
- 安全超时：DualEye 发出移动指令后会在约 700 ms 后主动补发 `AR1,STOP`。
- 语音事件接口：miniClaw/MimiClaw 后续只需要输出标准事件即可驱动表情和底盘指令。
- 显示适配占位：当前用日志输出每只眼的参数，真机双屏绘制后续接入 `atlas_display.c`。
- Flash 配置：已按 DualEye 官方规格设置为 16MB；PSRAM 等真机显示示例接入时再启用。
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
```

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
