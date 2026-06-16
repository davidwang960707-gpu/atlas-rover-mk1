# Atlas Rover Mk.1

一个还在长身体的桌面巡游机器人原型。

它现在的样子，大概是：铜丝焊接车架、Waveshare ESP32-S3 DualEye 双圆屏、双板 UART 控制、N20/DRV8833 底盘、语音交互、双目表情，还有一点点“我想活起来”的执念。

我们知道它还很粗糙：机械结构没经过足够多次打样，DualEye 真屏驱动还没接上，miniClaw/MimiClaw 语音链路也还在搭骨架。这个仓库不是“成品展示柜”，更像一个公开的工作台。欢迎大神路过时顺手指点，尤其欢迎指出不合理、不安全、不优雅的地方。
<img width="2506" height="2348" alt="image" src="https://github.com/user-attachments/assets/82bc6d5b-bc66-4e5b-b9d4-6708805ef747" />


## 我们想做什么

Atlas Rover Mk.1 想成为一个小而完整的桌面机器人实验平台：

- 双实体圆屏，每块屏幕显示一只眼睛。
- DualEye 负责表情、页面、语音、触摸和高层意图。
- 底盘板负责电机、DRV8833、限速、闭环和超时停车。
- DualEye 与底盘板通过 3.3 V TTL UART 通信。
- 车架尽量保留黄铜/铜丝手工 DIY 的质感。

核心原则很朴素：先跑起来，再跑稳，再变好看。

## 当前状态

| 模块 | 状态 |
|---|---|
| 制造图纸包 | V1.0 草案已整理 |
| 采购清单 | 已有中文清单和规格复审 |
| 双目表情方案 | 已有 V0.5，按“一屏一眼”设计并补充页面视觉和候选界面推荐 |
| Web 表情预览 | 已可在 VS Code Live Preview 中查看，支持 5 套主题、16 个表情、桌面时钟、语音输入和状态页评审 |
| DualEye 固件 | V0.3 可编译，已包含表情状态机、UART 协议、安全超时、NVS 配置、SoftAP/STA、应用页和管理后台 |
| Web 应用/管理界面 | V0.3 已拆分 `/app` 日常应用页和 `/admin` 管理后台，支持表情、显示、移动、MimiClaw 应用占位和配置管理 |
| 真机双屏显示 | 待接入 Waveshare 官方 ESP-IDF/LVGL 示例 |
| 语音 miniClaw/MimiClaw | 接口已预留，待正式接入 |
| 底盘闭环控制 | 待底盘板固件实现 |

## 仓库结构

```text
.
├── Atlas_Rover_Mk1_制造图纸包_V1.0/   # 图纸、装配、接线、采购和复审文档
├── docs/                              # 设计说明、开发环境、路线图、进展日志
├── firmware/dualeye/                  # ESP-IDF DualEye 固件
├── simulator_web/                     # 双目表情 Web 预览
├── simulator_mac/                     # SDL 桌面模拟器骨架
├── scripts/                           # 环境检查、VS Code 打开、同步脚本
└── tools/                             # 图纸包/采购表生成与校验工具
```

## 快速看效果

Web 表情预览适合先看双目状态机和交互感觉：

```bash
cd "simulator_web"
python3 -m http.server 5173
```

然后打开：

```text
http://127.0.0.1:5173
```

也可以在 VS Code 里使用 Live Preview 打开 `simulator_web/index.html`。

## 编译 DualEye 固件

```bash
cd "firmware/dualeye"
export IDF_PATH="$HOME/.espressif/esp-idf-v5.5.2"
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.9_env"
. "$IDF_PATH/export.sh"
idf.py set-target esp32s3
idf.py build
```

当前 V0.3 已通过本地构建，并按 DualEye 官方规格配置为 16MB Flash；固件使用自定义分区表，应用分区为 4MB，方便后续接入屏幕、音频和 Web 页面。PSRAM 暂未开启，等接入官方显示示例时再确认模式。

烧录后：

- 没有 Wi-Fi 配置时，DualEye 会开启 `AtlasRover-XXXX` 热点，手机/电脑连接后访问 `http://192.168.4.1`。
- 串口日志会打印 6 位配对码；STOP 不需要配对码，移动和配置修改需要配对码。
- Web 管理页可以配置 Wi-Fi、LLM 模式、Base URL、Model、API Key 和移动安全限制。
- API Key 只保存在设备 NVS，不写入源码、不提交到 GitHub、不在日志中输出。

## 最想请教的问题

如果你刚好擅长下面任一方向，非常欢迎开 issue 指点：

- ESP32-S3 DualEye 双屏 LVGL 初始化和双 240x240 圆屏刷新策略。
- ES8311/ES7210 音频链路、麦克风 RMS、TTS 播放与表情联动。
- 小车底盘闭环、电机限速、超时停车、DRV8833 接法。
- 铜丝/黄铜车架的焊接结构强度和可维护性。
- 适合桌面巡游机器人的安全策略。
- 更好的双目表情状态机和动画参数设计。

我们会认真看，也会把采纳过程记在进展日志里。

## 进展记录

- [项目进展日志](docs/项目进展日志.md)
- [开发路线图](docs/开发路线图.md)

## 同步方式

本地整理后可以运行：

```bash
./scripts/sync_github.sh "说明这次推进了什么"
```

脚本会检查状态、提交并推送到 GitHub。自动化同步暂时不做，避免把未检查的草稿或本地杂物推上公开仓库。

## 许可证

代码部分暂按 MIT License 开放。硬件图纸、采购清单和制造文档仍是早期草案，欢迎讨论后再切到更合适的硬件/文档许可证。

## 一句实话

这是一个边学边做的机器人项目。我们不怕被指出问题，怕的是问题一直藏在桌子底下。欢迎拍砖，越具体越好。
