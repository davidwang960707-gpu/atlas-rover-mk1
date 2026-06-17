# Atlas Rover Mk.1 开发环境配置

## 本机状态

已检测到：

- VS Code App: `/Applications/Visual Studio Code.app`
- ESP-IDF: `~/.espressif/esp-idf-v5.5.2`
- ESP-IDF Python: `~/.espressif/python_env/idf5.5_py3.9_env`
- CMake / Ninja / SDL2: 已安装
- 目标芯片：ESP32-S3

## 打开工作区

```bash
./scripts/open_vscode.sh
```

如果你想在终端里直接用 `code` 命令，打开 VS Code 后执行：

```text
Cmd+Shift+P -> Shell Command: Install 'code' command in PATH
```

## VS Code 任务

打开工作区后按：

```text
Cmd+Shift+P -> Tasks: Run Task
```

常用任务：

| 任务 | 用途 |
|---|---|
| `Atlas: 检查开发环境` | 检查 ESP-IDF、CMake、Ninja、SDL2、串口 |
| `Atlas: 构建 Mac 双眼模拟器` | 编译 Mac 可视化预览器 |
| `Atlas: 运行 Mac 双眼模拟器` | 打开双眼表情/页面预览窗口 |
| `Atlas: 启动 Web 双眼预览服务器` | 用浏览器或 VS Code Live Preview 预览 `simulator_web/index.html` |
| `ESP-IDF: 构建 DualEye 固件` | 编译 ESP32-S3 固件骨架 |
| `ESP-IDF: 烧录 DualEye 固件` | 烧录到 DualEye |
| `ESP-IDF: 监视 DualEye 串口` | 查看串口日志 |

## 开发路线

## VS Code 内置预览

已安装 Microsoft Live Preview 插件。使用方法：

1. 打开 `simulator_web/index.html`。
2. 右上角点击预览按钮，或执行 `Cmd+Shift+P -> Live Preview: Show Preview`。
3. 预览会在 VS Code 右侧打开，保存文件后自动刷新。

这个 Web 预览用于快速调双眼表情、页面切换和 `AR1` 指令映射；最终显示、触摸、音频和 UART 仍需烧录到 DualEye 真机验证。

## 开发路线

1. 先用 `simulator_mac` 调双眼表情、时钟、语音页和状态页。
2. 固件侧先保持 `AR1,` UART 协议稳定。
3. 真机联调时，DualEye 只发结构化指令，底盘板负责电机安全。

DualEye 到底盘板接线：

```text
DualEye LCD1 Pin10 UART_TXD -> XIAO ESP32C3 D7 / GPIO20 / RX
DualEye LCD1 Pin9  UART_RXD <- XIAO ESP32C3 D6 / GPIO21 / TX
DualEye LCD1 Pin2/Pin6 GND  <-> XIAO ESP32C3 GND
```

底盘板只解析带前缀的命令：

```text
AR1,MOVE,F,40,500
AR1,TURN,L,35,350
AR1,STOP
```

## 官方资料

- ESP-IDF VS Code Extension: https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/installation.html
- LVGL PC Simulator: https://lvgl.io/docs/open/9.1/integration/ide/pc-simulator
- Waveshare DualEye: https://docs.waveshare.com/ESP32-S3-DualEye-Touch-LCD-1.28
