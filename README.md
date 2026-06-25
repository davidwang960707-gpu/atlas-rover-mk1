# Atlas Rover Mk.1

一个还在长身体的桌面智能硬件 / 桌面巡游机器人原型。

它现在的样子，大概是：铜丝焊接车架、Waveshare ESP32-S3 DualEye 双圆屏、语音交互、双目表情、电子宠物、番茄/时钟/日历应用，还有一点点“我想活起来”的执念。N20/DRV8833 底盘方案先保留为后续可选模块，这一版实体优先把桌面智能硬件本体做稳。

我们知道它还很粗糙：机械结构没经过足够多次打样，DualEye 真屏驱动已经接入但还没充分真机标定，Atlas Brain / Mac 桥的语音链路也还在继续打磨。这个仓库不是“成品展示柜”，更像一个公开的工作台。欢迎大神路过时顺手指点，尤其欢迎指出不合理、不安全、不优雅的地方。
<img width="2506" height="2348" alt="image" src="https://github.com/user-attachments/assets/82bc6d5b-bc66-4e5b-b9d4-6708805ef747" />


## 我们想做什么

Atlas Rover Mk.1 想成为一个小而完整的桌面机器人实验平台：

- 双实体圆屏，每块屏幕显示一只眼睛。
- DualEye 负责表情、页面、语音、触摸和高层意图。
- Seeed XIAO ESP32C3 底盘板负责普通 N20 电机、DRV8833、限速、开环短时差速控制和超时停车；当前软件构建默认暂停动态底盘，先做静态桌面伴侣体验。
- DualEye 与底盘板通过 3.3 V TTL UART 通信，后续恢复动态版本时再启用。
- 车架尽量保留黄铜/铜丝手工 DIY 的质感。

核心原则很朴素：先跑起来，再跑稳，再变好看。

## 当前状态

| 模块 | 状态 |
|---|---|
| 制造图纸包 | V1.0 草案已整理 |
| 采购清单 | 已有中文清单和规格复审 |
| 双目表情方案 | 已有 V0.6，按“一屏一眼”设计并补充页面视觉、应用能力映射和候选界面推荐 |
| Web 表情预览 | 已可在 VS Code Live Preview 中查看，支持 5 套主题、16 个表情、时钟/状态/语音/音乐/故事/对话/日历/番茄页评审 |
| DualEye 固件 | `0.14.7-acceptance` 已实机烧录验收，接入 Waveshare GC9A01/LVGL 双屏、SPIFFS 主题资源、3500 常用汉字、时钟/日历/番茄/电子宠物页面、三种对话界面 `pet_head/text/eyes_only`、2.5D 土拨鼠头关键帧/动画、语音 turn runtime 诊断、`atlas_audio_service`、主动 Atlas Brain `/ws/brain` 常驻会话、WS binary TTS 下行、Brain 离线本地页面降级、真实 OPUS 60ms 上行流、ESP-SR WakeNet 模型资源探针和完整自检/OTA 包接口 |
| Web 应用/管理界面 | DualEye `/app` 日常控制页和 `/admin` 本地管理后台已可用；Mac 侧 Atlas Brain 新增 `atlas.ui.set_chat_mode`、`atlas.pet.set_state`、`atlas.pet.play_animation` 工具，设备 App 页会同步真实主题、页面、表情和对话界面模式，并按“一个设备一个 App 页、管理端平台化”收敛；`/app` 已补“小鲅 X1”开机动画预览 |
| 真机双屏显示 | 已接 Waveshare 官方同款 GC9A01/LVGL 初始化，4 套核心眼睛主题、新增主题资源、pet_head 和 `boot/xiaoba_x1` 开机动画已内置；番茄/时钟/日历页面仍会继续打磨视觉 |
| 语音 Atlas Brain/MiMo | DualEye 通过 Atlas Brain / Mac 桥接对接 MiMo LLM/ASR/TTS；Atlas Brain 已拆出 `atlas_brain_providers.py` Provider 层，支持会话状态、Provider 诊断、技能系统、角色切换、天气、联网搜索骨架和平台化设备列表；`/api/device/opus-turn/start` 已可把 DualEye OPUS packet 封装 Ogg、解码 WAV 并进入 ASR |
| 底盘开环控制 | XIAO ESP32C3 底盘固件 V0.1 已补齐但本轮默认不启用动态移动；服务端默认不注册 rover 移动技能，固件 `motion_supported=false`，后续动态版再恢复 |

## 仓库结构

```text
.
├── Atlas_Rover_Mk1_制造图纸包_V1.0/   # 图纸、装配、接线、采购和复审文档
├── docs/                              # 设计说明、开发环境、路线图、进展日志
├── firmware/dualeye/                  # ESP-IDF DualEye 固件
├── firmware/chassis_xiao_esp32c3/     # ESP-IDF XIAO ESP32C3 底盘固件
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

当前 `0.14.7-acceptance` 已通过本地构建并完成实机 app-only 烧录验收，按 DualEye 官方规格配置为 16MB Flash、PSRAM、双 OTA app 分区、ESP-SR `model` 分区和 4MB `storage` 资源分区；SPIFFS 资源分区内置双眼 PNG、2.5D 土拨鼠头资源和 3500 常用汉字压缩字库，`model` 分区打包 `wn9_nihaoxiaozhi_tts` 作为 WakeNet 资源验证模型。构建结果：`atlas_rover_dualeye.bin` 大小 `0x23b7c0`，5MB app slot 剩余约 55%；`storage` 资源目录已压缩到约 3.6MB，可进入 4MB 分区。

这版分区表已变化，首次升级必须全量烧录，不能只刷 app：

```bash
cd "firmware/dualeye"
idf.py flash
```

烧录前可先跑：

```bash
python3 tools/atlas_brain_server.py --dry-run --host 127.0.0.1 --port 8787
python3 tools/simulate_opus_stream.py --url ws://127.0.0.1:8787/ws/audio --duration-ms 1800
python3 tools/check_atlas_providers.py --brain-url http://127.0.0.1:8787
python3 tools/check_atlas_preflash.py --brain-url http://127.0.0.1:8787 --skip-dualeye
```

烧录后：

- 没有 Wi-Fi 配置时，DualEye 会开启 `AtlasRover-XXXX` 热点，手机/电脑连接后访问 `http://192.168.4.1`。
- 串口日志会打印 6 位配对码；配置修改需要配对码。STOP 保留为后续底盘安全入口。
- Web 管理页可以配置 Wi-Fi、LLM 模式、Base URL、Model、API Key、音频和界面设置；本轮动态底盘默认暂停。
- Atlas Brain 的 API Key 只保存在本地 `.atlas-brain.env`；如果后续启用设备直连云端模式，设备侧 Key 只进 NVS。两种方式都不写入源码、不提交到 GitHub、不在日志中输出。

## 编译 XIAO 底盘固件

```bash
cd "firmware/chassis_xiao_esp32c3"
export IDF_PATH="$HOME/.espressif/esp-idf-v5.5.2"
. "$IDF_PATH/export.sh"
idf.py set-target esp32c3
idf.py build
```

接线锁定为 DualEye LCD1 Pin10/Pin9 交叉到 XIAO D7/D6，XIAO D2-D5 控制 DRV8833 AIN1/AIN2/BIN1/BIN2。底盘固件只接受带 `duration_ms` 的短时动作命令，普通 N20 + 前万向轮先按开环时间/PWM 标定。

## 最想请教的问题

如果你刚好擅长下面任一方向，非常欢迎开 issue 指点：

- ESP32-S3 DualEye 双屏 LVGL 初始化和双 240x240 圆屏刷新策略。
- ES8311/ES7210 音频链路、麦克风 RMS、TTS 播放与表情联动。
- 小车底盘开环短时差速、电机限速、超时停车、DRV8833 接法。
- 铜丝/黄铜车架的焊接结构强度和可维护性。
- 适合桌面巡游机器人的安全策略。
- 更好的双目表情状态机和动画参数设计。

我们会认真看，也会把采纳过程记在进展日志里。

## 进展记录

- [项目进展日志](docs/项目进展日志.md)
- [Atlas Brain 改造记录 2026-06-23](docs/Atlas_Brain改造记录_2026-06-23.md)
- [开发路线图](docs/开发路线图.md)
- [智能体架构设计 V0.11](docs/Atlas智能体架构设计_V0.11.md)
- [桌面智能硬件平台化执行记录 V0.12](docs/Atlas桌面智能硬件平台化执行记录_V0.12.md)
- [80 分加固包记录 2026-06-23](docs/Atlas_80分加固包_2026-06-23.md)
- [面包板临时联调接线说明](docs/面包板临时联调接线说明_Atlas_Rover_Mk1.md)
- [后桥、前桥、底板与 64T 齿轮轮方案](docs/后桥前桥与64T齿轮轮方案_Atlas_Rover_Mk1.md)

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
