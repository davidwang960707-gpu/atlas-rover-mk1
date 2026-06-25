# Atlas Rover Mk.1 DualEye 固件 V0.15.0-xiaoba-boot

这个目录是 `ESP32-S3-DualEye-Touch-LCD-1.28` 的 ESP-IDF 固件工程，目标芯片为 `esp32s3`。

V0.15.0 已经从单文件脚手架升级为可配网、可管理、可日常控制、可驱动双屏、可接收 Atlas Brain 结构化意图，并带有电子宠物状态机、2.5D 土拨鼠头对话界面、拟 3D 转头资源、小鲅 X1 开机动画、板载音频自检、板载语音对话闭环、OPUS WebSocket 真流入口、Tool Schema、app OTA、ESP-SR 模型资源探针和运行时诊断的模块化程序。本版实体优先做“桌面智能硬件/电子宠物”，动态底盘先暂停，N20/DRV8833/UART 方案保留为后续可选模块。

- 双目表情参数模型：每块实体屏幕对应一只眼睛。
- 表情状态机：待机、开心、聆听、思考、说话、移动、好奇、困倦、惊讶、眨眼、爱心、爱钱、生气、充电、错误、大哭。
- UART 底盘协议：保留 `AR1,` 开头的运动/停止指令格式，但 V0.12 默认构建关闭动态底盘能力。
- 双板职责划分：当前 DualEye 负责 HMI/语音/意图和桌面应用；底盘板、普通 N20 + DRV8833 的开环短时差速控制、限速和超时停车先作为后续可选模块保留。
- 安全边界：V0.12 固件默认 `motion_supported=false`，Web/语音运动入口会置灰或返回暂停说明，避免桌面联调时误触发底盘。
- 语音事件接口：Atlas Brain 可输出标准事件、tool-call 或结构化 intent 来驱动表情、页面、应用和底盘指令；结构化协议统一为 `atlas.brain.v1`。
- 电子宠物状态机：维护心情值、能量值、好奇心；长时间不互动会困/睡觉，触摸会开心，移动后进入巡游状态，音乐/讲故事/对话有专属状态。
- 功能实装现状：
  - 双眼表情、页面切换、配对与安全控制：**已可用**。
  - 对话、日历、番茄、桌面时钟：**已可用**（可在 `/api/intent` 下发或通过 `/api/app/action`/配置接口触发，DualEye 上显示真实内容/状态）。
  - 音乐/故事：**显示与状态链路已接入**（显示/表情/页面切换有效，播放内容/外放由 Atlas Brain 服务层承担）。
  - 板载音频：**硬件链路已接入**，ES7210 板载麦克风、ES8311/功放/Speaker header 外放可通过 Web 自检接口测试。
  - 板载语音对话：**已接入 Atlas Brain / Mac 桥接闭环**，DualEye 可录制 16k/16bit WAV，POST 到 Mac `tools/atlas_brain_server.py`，由 MiMo ASR/LLM/TTS 处理后拉回 WAV 并通过 ES8311 播放。
  - 桥接台自动播报：**已接入 DualEye 拉流播放**，Mac/手机浏览器在 `http://Mac局域网IP:8787/` 完成语音转文字与 TTS 后，会请求 DualEye 拉取 `/tts/latest.wav` 并通过外接喇叭播放。
  - 板端语音触发：**已接入实验版 VAD/音量门限唤醒**，开启后 DualEye 会周期测麦克风电平，听到明显说话声后自动录音、发 Mac 桥接 ASR/LLM/TTS，并从外接喇叭播报。
  - 主题切换与配置：**固件持久化 + Web 实时生效**；`raptor/mecha/goggle/pet/blue_pupil/no_smoking/tomoe_spin` 会优先加载内置 PNG 资源。
  - 音频服务化：**已接入 `atlas_audio_service` worker**，语音 turn、OPUS 探针和播放任务统一记录 busy/job/last_failure，便于排查连续对话卡死。
  - Brain JSON/Binary 长连接：**已接入主动 `/ws/brain` 常驻会话客户端**，host 模式配置 Base URL 后，DualEye 会在 STA 联网时连接 Mac Brain，发送 `hello/ping/state`，并用同一条 WS 完成语音 turn：`turn.audio.begin` + binary WAV 上行 + binary WAV TTS 下行；设备侧 `/api/brain/ws` 保留为调试入口。
  - OPUS 60ms 探针与真流入口：**已接入 `/api/audio/opus-probe` 和 `/api/audio/opus-stream/start|stop|status`**，从 ES7210 采集真实 PCM，按 16k/mono/60ms 编码 OPUS；真流会推送 AOP1 二进制帧到 Mac Brain `/ws/audio`，当前先做帧统计和 VAD 语音段，不做流式 ASR。
  - WakeNet/AEC：**当前完成资源验证链路**，`espressif/esp-sr 2.3.1` 已进入构建，`build/srmodels/srmodels.bin` 打包 `wn9_nihaoxiaozhi_tts`，`/api/sr/status` 会尝试真实加载模型并创建 WakeNet 实例；尚未启用常驻关键词唤醒任务和真 AEC。
- Web 控制端能力声明：`/api/status` 返回 `features` 字段，当前 `eyes/clock/status/audio_hw/pet/chat/calendar/pomodoro/music/story` 为已打通状态显示链路；语音能力会跟随板载音频初始化状态，照片/闹钟为入口展示位（按钮会置灰）。
- NVS 配置：保存 Wi-Fi、LLM 模式、Base URL、Model、API Key 和安全限制。
- Wi-Fi 配网：无配置时开启 `AtlasRover-XXXX` SoftAP；有配置时优先连接路由器，失败后回落 APSTA。
- Web 管理页：手机/电脑访问设备 IP，可查看状态、保存配置、STOP、短时移动和测试文本意图。
- 配对码：启动时生成 6 位配对码；STOP 不需要配对码，移动和配置修改需要配对码。
- Web 入口拆分：`/app` 是日常应用页，`/admin` 是管理后台，根路径 `/` 默认进入应用页。
- Web 宠物控制：`/app` 已补电子宠物区块，可查看心情/能量/好奇心，并触发摸摸、玩耍、补能、休息、巡游、音乐、故事、对话状态。
- 对话界面模式：新增 `pet_head/text/eyes_only` 三种模式；`pet_head` 为左屏 2.5D 土拨鼠头、右屏短文本，`text` 为双屏文字，`eyes_only` 为纯表情眼睛。
- pet_head 资源：内置 `atlas_pet_head` V0.3，透明底、无 SD 卡、只有头；包含 11 张关键帧、`idle/listen/think/speak` 的 5 个 yaw 角度、4 条 6 帧转头过渡，以及 `blink/speak/sing/laugh` 4 组动画；`/api/selftest` 会检查 manifest、idle keyframe、speak animation、view 和 turn 资源。
- 小鲅 X1 开机动画：内置 `boot/xiaoba_x1` V0.1.1，左屏土拨鼠头睡醒/转头/开心，右屏 8-bit 像素状态面板显示 `小鲅 X1`；6 帧、6fps、约 1 秒，显示层在启动前约 1 秒优先播放，资源缺失时降级到 fallback/正常页面。
- Atlas Brain 适配层：当前支持本地关键词意图、LLM 配置状态、Mac 桥接服务，以及 `/api/intent` 结构化意图执行入口；`/api/brain/intent` 只保留旧客户端兼容。
- 主题同步：`raptor`（猛禽眼）、`mecha`（机械电子眼）、`goggle`（护目镜眼）、`pet`（电子宠物巡游）、`blue_pupil`（蓝色瞳孔）、`no_smoking`（禁烟禁电子烟）、`tomoe_spin`（红色旋纹）已升级为 V0.4 单眼资源；每套包含 `idle/blink/listen` 三态、左右眼各一张 240x240 PNG，共 42 张，打包进 `storage` SPIFFS 分区。`pet` 使用 `/Users/macbook/Desktop/marmot-pet` 里的电子宠物 IP，`classic`、`amber`、`mint`、`alert`、`night` 保留为参数化绘制回退风格。
- 眼睛活性：PNG 不再只是静态贴图，固件会按时间自动眨眼，并对聆听/思考/说话/移动状态加入轻微呼吸、漂移和脉冲。
- Waveshare 双屏后端：`atlas_display.c` 默认接入官方同款 GC9A01/LVGL 初始化，面板方向沿用实机验证过的 Waveshare 90 度映射；如果硬件初始化失败，会回退为串口日志渲染。
- 中文字库：`atlas_font_zh_16` 已升级为 16px、2bpp 的常用 3500 汉字字库，并保留 `lv_font_simsun_16_cjk` 兜底，重点修复番茄任务名、日历、对话和 Atlas Brain 文案中的中文方框。
- 功能页宠物层：桌面时钟、番茄专注、日历页会默认复用电子宠物 PNG 资源，不再要求先切到 `pet` 主题；日历页双屏都有宠物底图、短文案和状态条。
- V0.8.4 显示优化：`goggle` 护目镜眼资源改为 240x240 圆屏贴合的正圆素材；桌面时钟右屏改为 LVGL 石英表；番茄右屏补充常见任务中文字库并关闭缺字方框占位。
- V0.8.5 资源更新：烧录 `atlas_eyes_sdcard_v0_4.zip`，补齐 Web 管理后台里的 `blue_pupil`、`no_smoking`、`tomoe_spin` 选项；`no_smoking` 与 `tomoe_spin` 在固件运行时顺时针旋转。
- V0.8.6 视觉调校：`goggle`、`no_smoking`、`tomoe_spin` 资源放大并弱化黑边；`no_smoking` 改为静态展示；`tomoe_spin` 从 1.8 秒/圈初始速度开始，约 4 秒加速到 0.9 秒/圈并循环；UI 刷新节奏提升到约 30fps。
- V0.8.7 中文显示修复：番茄右屏任务名缩短到 6 个中文字符、状态行改为 `已用0/25分  0%`，避开圆屏边缘裁剪；`atlas_font_zh_16` 扩展 Atlas Brain 联调常用中文字形，并继续 fallback 到 `lv_font_simsun_16_cjk`。
- V0.8.8 常用汉字字库：`atlas_font_zh_16` 由小字库升级为通用规范汉字一级字表 3500 常用汉字 + 英文数字标点/UI 符号，减少后续配置任务名、对话、故事、日历文案出现方框的概率。
- V0.8.9 大字库渲染修复：打开 `CONFIG_LV_FONT_FMT_TXT_LARGE`，避免 3500 字库超过 1 MB 后 `bitmap_index` 被小字库格式截断；同时加固番茄右屏任务名文本层级。
- V0.9.0 压缩字库与校时修复：打开 `CONFIG_LV_USE_FONT_COMPRESSED`，修复 3500 汉字压缩字库“找到字但不显示”的问题；STA 连上路由器后自动 SNTP 校时，Web 应用页/管理页也可用浏览器时间一键校准。
- V0.9.1 手机配网优化：新增 `/api/wifi/scan`，管理页可扫描附近 Wi-Fi、点选 SSID 后输入密码保存；无 Wi-Fi 配置时默认 APSTA，方便手机连热点时同时扫描路由器。
- V0.9.2 Atlas Brain/MiMo 对话优化：`/app` 的 Mac 桥接区支持手动输入自然语言并自动读取 host Base URL；`tools/atlas_brain_server.py` 支持 MiMo 文本对话、浏览器 WAV 录音 ASR、TTS 音频 data URL 播放，并对 `mimo-v2.5-pro` 显式关闭思考模式。
- V0.9.3 板载音频自检：新增 `atlas_audio.*`，按 Waveshare 示例脚位接入 ES7210 麦克风、ES8311、GPIO9 功放和 Speaker header；新增 `/api/audio/status`、`/api/audio/beep`、`/api/audio/mic-level`，应用页/管理页可直接点“喇叭测试 / 麦克风测试”。实机串口已确认 ES8311 与 ES7210 初始化成功。
- V0.9.3 网络热修：有已保存 Wi-Fi 时也立即以 APSTA 启动，`AtlasRover-XXXX` 调试热点不再等 STA 失败才出现；HTTP 服务栈扩到 24KB，`/api/status` 大状态包改为堆内存，修复手机打开页面触发 `httpd` 栈溢出重启的问题。
- V0.9.4 板载语音闭环：新增 `/api/voice/turn`，DualEye 直接用板载 ES7210 录音 2.8 秒，封装为 16k/16bit 单声道 WAV 发给 Mac 桥接；早期链路使用 `/device/audio/wav` 和 `/tts/latest.wav`。
- V0.14.3 WS turn 收口：`/api/voice/turn` 主链路已切到常驻 `/ws/brain`，使用 JSON `turn.audio.begin` + binary WAV 上行，Mac Brain 通过同一条 WS 回传 `turn.result` 与 binary WAV TTS；Brain WS 未连接时只返回 `brain_offline` 并保持本地页面，不进入异常页或黑屏。
- V0.9.4 交互收敛：开机开发演示任务默认关闭，避免真实语音联调时被自动聆听/思考/说话演示状态干扰。
- V0.9.5 桥接台播报：新增 `/api/audio/play-url`，Mac 桥接页 `/text`、`/audio`、`/speak` 生成 TTS 后会缓存最近 WAV，并主动让 DualEye 从 Mac 局域网地址拉取播放；`/app` 和 `/admin` 增加“播放最近回复”测试按钮。MiMo TTS 可用时优先使用云端音色；云端 TTS 报错时自动用 Mac `say + afconvert` 生成 16k WAV 兜底，先保证机器人能开口。
- V0.9.6 音量与 ASR 链路：默认 UI 音量由 60 提升到 90，喇叭测试改为高音量测试，应用页保存主题时不再把音量重置为 60；Mac 桥接页新增链路状态卡片和 `/asr` 仅识别接口；TTS WAV 推给 DualEye 前做峰值归一化，改善小喇叭听感。
- V0.9.6 板端语音触发：新增 `/api/voice/wake`，可从 `/app` 或 `/admin` 开启/关闭实验版音量门限唤醒；状态包新增 `voice_wake` 字段。当前版本是 VAD 触发，不是关键词 WakeNet；关键词唤醒后续再接 ESP-SR。
- V0.9.7 连续对话与 TTS 风格：连续对话回答完会自动回到聆听状态，不需要每轮重新点击开启；Mac 桥接页新增小米 TTS 音色/风格选择，支持夹子音、甜美、俏皮、兴奋和唱歌模式，唱歌模式会按 MiMo TTS 要求给文本添加 `(唱歌)` 标签。
- V0.9.8 连续对话防误触发热修：不开启 4-6 秒冷却；把音量门限默认提高到 36，并要求连续 3 次检测超过阈值才触发；回答完不再强制把显示页切回语音页，避免用户手动切时钟/日历时被后台监听状态抢走。
- V0.9.9 连续监听稳定性热修：监听任务不再固定绑到 UI 所在核心，优先级下调；开启后先延迟 1.2 秒再采样，麦克风检测从 220ms 降为 90ms、空闲间隔拉长到 450ms，避免一开启连续监听就拖住 HTTP/Web 控制端。
- V0.12.0 平台化收口：新增 `atlas_runtime` 语音 turn 诊断，补 `/api/system/info` 与 `/api/diagnostics/turn`；动态底盘默认暂停；Mac Brain 增加 `/api/devices`、`/devices`、`/devices/<device_id>/app`，管理后台转向平台化智能硬件后端。
- V0.12 P3/P4 推进：新增 `atlas_sr_probe` 与 `/api/sr/status`，用于确认 ESP-SR/WakeNet/AEC 仍处于探针状态；`/api/capabilities` 增加语音识别能力声明。Mac Brain 侧已能通过 `/ws/audio` 模拟 60ms OPUS/PCM 帧流，DualEye 端已接入真实 OPUS 编码探针，但还没有把 OPUS chunk 正式推到 Brain WebSocket。
- V0.12 P0-P5 收口：`atlas_audio_service` 改为 worker 服务化；`/api/brain/ws` 升级为 JSON 长连接；新增 `atlas_opus_stream` 与 `/api/audio/opus-probe`，完成板载麦克风真实 PCM -> 60ms OPUS 编码探针；`/api/sr/status` 改为 WakeNet/AEC 资源验证；Mac Brain 增加 Tool Schema V0 和 OTA 包 manifest。
- V0.12.1 烧录前加固包：新增稳定版本指纹 `0.12.1-preflash-hardening`，`/api/status`、`/api/capabilities`、`/api/system/info` 都返回 `fingerprint`；新增 `/api/selftest` 轻量自检，覆盖 SPIFFS、资源包、Wi-Fi、Mac Brain 配置、板载音频、audio service worker、Brain WS、OPUS probe、SR probe、Tool Schema、内存和动态底盘边界；Mac Brain 新增 `/acceptance` 验收页与 `tools/check_atlas_preflash.py` 检查脚本。
- V0.13.0 80 分加固包：版本指纹升级为 `0.13.0-runtime-80`；Mac Brain 新增 `atlas_brain_runtime`、`/api/runtime`、`/api/runtime/score`，按会话统计 OPUS AOP1 帧、缺帧、payload mismatch 和 VAD 语音段；`atlas_opus_stream` 增加播放 mute 期间跳过采集，并记录 `muted_frames/capture_failures/encode_failures`；本地模拟 OPUS 1.8 秒通过 30 帧、0 缺帧、0 mismatch，固件编译通过，app 分区剩余约 66%。
- 发布候选注意：烧录前需要重启 Mac Brain 到当前代码；如果旧服务仍占用 `127.0.0.1:8787`，检查脚本会提示 `/api/tools/list` 或 `/api/ota/packages` 缺失，这不是固件问题，而是 Mac Brain 进程未更新。
- V0.14.0 P0-P5 加固：连续语音 turn 失败后会回到 monitoring，不再把音频服务卡死在 error；`/api/status/lite` 为手机端提供轻量状态包；固件侧新增 `/api/tools/list`、`/api/tools/call`、`/api/ota/manifest`、`/api/ota/packages`；`atlas.clock.sync` 支持毫秒级大时间戳，避免 32 位溢出；`tools/check_atlas_preflash.py` 同步检查 full status 与 lite status。
- V0.14.2 SR 模型与 OTA 加固：引入 `espressif/esp-sr 2.3.1`，默认打包 `wn9_nihaoxiaozhi_tts` 到 `build/srmodels/srmodels.bin`，并移除旧的 `model.bin` 占位冲突；`/api/sr/status` 可真实加载模型、创建 WakeNet 实例并返回采样率/chunk；分区表切到双 OTA app slot，新增 `/api/ota/apply`。首次刷入本版必须全量烧录。
- V0.14.3 Atlas Brain 主线改造：Mac 主服务入口切到 `tools/atlas_brain_server.py`；DualEye 新增 `/api/intent`，旧路径作为兼容别名；管理页、README、预览工具和启动脚本统一 Atlas Brain / Mac 桥口径。
- V0.14.4 pet_head 对话界面：固件新增 `chat_mode` 持久化配置和状态上报，`/app` 与 `/admin` 可切换 `pet_head/text/eyes_only`；Atlas Brain 工具表新增 `atlas.ui.set_chat_mode`、`atlas.pet.set_state`、`atlas.pet.play_animation`；SPIFFS 内置 42 张 pet_head PNG，压缩后总资源目录约 3.6MB，`idf.py build` 通过，app 大小 `0x23b070`。
- V0.14.8 pet_head 拟 3D 转头：资源升级为 `atlas.pet_head.v0.3`，透明 PNG 内置 SPIFFS，无需 SD 卡；待机会左右慢转头，聆听/思考轻微摆头，说话保留 10fps 嘴型动画，资源目录约 1.1MB，整个 `storage` 源目录约 3.8MB。
- V0.15.1 小鲅 X1 开机动画验收修复：资源升级为 `dualeye-assets-v0.8-xiaoba-boot-verified`，修复初版误用 blink 脏帧导致的“四眼”问题；新增 Brain/Firmware 资源哈希比对、四眼检测和圆屏烧录等效预览，冷启动仍播放约 1 秒；`/api/selftest` 会检查 boot manifest、左右首帧和 fallback。
- V0.12 场景总线：新增 `atlas_scene`，把页面、表情、runtime、audio service、Wi-Fi、Mac Brain 配置合成为统一 `scene`；`/api/status`、`/api/diagnostics/turn`、屏幕状态页和 Mac Brain 设备页使用同一套场景判断。
- V0.12 状态体验：`status` 与 `voice` 不再是普通眼睛页，已补成双屏信息页；配网、监听、录音、识别、思考、播放、收尾、大脑离线、音频异常都会显示明确标题、原因和下一步提示。
- Flash/PSRAM 配置：已按 DualEye 官方规格设置为 16MB Flash、8MB PSRAM 方向；当前使用 960KB `model` 分区、双 5MB OTA app slot 和 4MB `storage` SPIFFS 资源分区。
- 开发演示：`main.c` 中 `ATLAS_ENABLE_DEV_EVENT_DEMO` 默认关闭；需要单独演示聆听/思考/说话/成功表情时可临时设为 `1`，不会发送移动指令。

## 目录结构

```text
main/
  main.c                 程序入口和 FreeRTOS 任务
  atlas_expression.*     双眼表情参数帧
  atlas_display.*        双屏显示适配层，Waveshare GC9A01/LVGL + 日志回退
  atlas_rover_uart.*     AR1 UART 底盘协议
  atlas_voice.*          语音/Atlas Brain 事件入口
  atlas_brain_intent.* 结构化意图解析
  atlas_ui.*             页面、表情、运动、安全状态机
  atlas_scene.*          设备场景总线，统一页面/运行时/音频/网络状态
  atlas_pet.*            电子宠物心情/能量/好奇心状态机
  atlas_runtime.*        语音 turn runtime 诊断
  atlas_audio_service.*  录音/播放/连续监听/mute 服务层
  atlas_brain_ws_client.* 主动连接 Mac Brain /ws/brain 的常驻会话客户端
  atlas_opus_stream.*    60ms OPUS 编码探针
  atlas_sr_probe.*       ESP-SR/WakeNet/AEC 探针
  atlas_config.*         NVS 配置读写
  atlas_wifi.*           SoftAP/STA/APSTA 网络
  atlas_admin_http.*     Web 管理页和 REST API
  atlas_pairing.*        本地 6 位配对码
  atlas_llm_client.*     LLM 配置状态封装
  atlas_brain_adapter.* 文本/语音意图适配层
  atlas_audio.*          板载麦克风/外放初始化与自检接口
```

## 首次启动和管理页

1. 烧录后打开串口监视器，查看启动日志中的 6 位配对码。
2. 如果没有保存过 Wi-Fi，DualEye 会开启 `AtlasRover-XXXX` 热点。
3. 手机或电脑连接该热点，浏览器打开 `http://192.168.4.1`。
4. 默认入口 `/` 是用户应用页；后台设置入口是 `/admin`。
5. 在管理页保存 Wi-Fi、LLM/API 和安全设置。
6. 保存 Wi-Fi 后建议重启；设备会同时开启调试热点并尝试连接路由器。路由器连上后，可使用 STA IP；调试热点入口仍为 `http://192.168.4.1`。

当前没有硬编码 `atlas-rover.local`，请优先使用页面或串口日志显示的 IP 地址。后续如果确认 ESP-IDF 工程可稳定接入 mDNS，再补本地域名。

## 本地预览

1. 先启动本地代理脚本：

```bash
python3 tools/preview_dualeye_admin.py --host 127.0.0.1 --port 8767
```

2. 浏览器打开：
- 应用页 `http://127.0.0.1:8767/app`
- 管理页 `http://127.0.0.1:8767/admin`

预览环境只做网页交互链路模拟，不会下发真实 UART 或驱动底盘。

## Atlas Brain / Mac 桥接

当前采用“方案 2”：Atlas Brain 先跑在 Mac 侧，DualEye 固件负责显示/UI/Safety Guard/音频采集播放，并通过常驻 `/ws/brain` 会话、`/api/intent` 和 OPUS `/ws/audio` 与 Mac Brain 协作。语音 turn 主链路已经改为 `/ws/brain` JSON + binary WAV；HTTP WAV 只作为 Mac 侧兼容调试入口，不再作为 DualEye 主链路兜底。

### 推荐网络拓扑

最稳定的方式：

```text
家用 Wi-Fi / 路由器
  ├─ DualEye：STA 模式，IP 来自 /api/status 的 wifi.sta_ip
  ├─ Mac：运行 Atlas Brain，可访问互联网和 DualEye
  └─ 手机/电脑浏览器：访问 DualEye Web，也能访问 Atlas Brain
```

操作步骤：

1. 首次烧录后，手机或 Mac 连接 `AtlasRover-XXXX` 热点，打开 `http://192.168.4.1/admin`。
2. 在「Wi-Fi 配网」里保存家用 Wi-Fi SSID/密码，然后重启 DualEye。
3. 重新打开 `/api/status` 或串口日志，记下 `wifi.sta_ip`，例如 `http://192.168.31.123`。
4. Mac 保持在同一个家用 Wi-Fi，运行桥接：

```bash
python3 tools/atlas_brain_server.py --dualeye-url http://192.168.31.123
```

5. 脚本会打印 Mac 的 LAN URL，例如 `http://192.168.31.20:8787`。在 `/app` 的「Atlas Brain / Mac 桥接」和 `/admin` 的 Base URL 里填这个地址。

桥接默认监听：

```text
http://127.0.0.1:8787
http://<Mac局域网IP>:8787
```

`127.0.0.1` 只适合 Mac 本机浏览器测试；手机上必须填 Mac 的局域网 IP。

### 救援/离线拓扑

如果暂时没有家用 Wi-Fi，可以这样临时测试：

```text
DualEye SoftAP：192.168.4.1
  ├─ Mac：连接 AtlasRover-XXXX，拿到 192.168.4.x，运行桥接
  └─ 手机：也连接 AtlasRover-XXXX，访问 DualEye 和 Atlas Brain
```

这种模式下运行：

```bash
python3 tools/atlas_brain_server.py --dualeye-url http://192.168.4.1
```

然后在 Web 里填 `http://Mac的192.168.4.x地址:8787`。缺点是 Mac 可能没有互联网，云端 LLM/ASR/TTS 可能不可用；除非 Mac 还有以太网、USB 共享网络或其他联网方式。

### 不推荐/会不通的组合

- 手机连 DualEye 热点，Mac 连家用 Wi-Fi：手机一般访问不到 Atlas Brain。
- Mac 连 DualEye 热点，DualEye 已经切到家用 Wi-Fi STA：Mac 可能访问不到 DualEye 的 STA IP。
- Web 里填 `127.0.0.1:8787` 后用手机打开：手机会访问手机自己，不会访问 Mac。
- 开了 VPN、防火墙阻止 Python 入站、公司/酒店 Wi-Fi 开启客户端隔离：手机可能访问不到 Mac bridge。

### Mac 网络自检

```bash
python3 tools/check_atlas_brain_network_macos.py \
  --dualeye-url http://192.168.31.123 \
  --bridge-url http://127.0.0.1:8787
```

它会打印 Mac 可用 IP，并检查 DualEye `/api/status` 和 Atlas Brain `/health` 是否可达。

测试文本转意图：

```bash
curl -X POST http://127.0.0.1:8787/text \
  -H 'Content-Type: application/json' \
  -d '{"text":"开始 25 分钟番茄，任务是固件测试"}'
```

Atlas Brain 会自动读取 DualEye `/api/status` 的配对码，再优先转发到 `/api/intent`。运动类指令仍受 DualEye 安全配置限制：Web 手动模式用 `manual`，AI 运动用 `ai`。

Web 控制端配置：

- `/app`：在「Atlas Brain / Mac 桥接」里填 `http://Mac局域网IP:8787`，可直接测试桥接和发送文本。
- `/admin`：在「大模型/API」里把模式选为 `host`，Provider 可填 `atlas_brain_mac`，Base URL 填同一个桥接地址。
- 如果手机连接的是 DualEye 热点，桥接地址必须是 Mac 在该网络里的 IP；`127.0.0.1` 只适合在 Mac 本机浏览器测试。

## Web API 骨架

| 接口 | 方法 | 配对码 | 说明 |
|---|---|---|---|
| `/` / `/app` | GET | 否 | 用户应用页：表情、显示、桌面应用和 Atlas Brain 对话入口 |
| `/admin` | GET | 否 | 管理后台：配网、API、安全和调试 |
| `/api/status` | GET | 否 | 查看 Wi-Fi、UI、电子宠物、LLM、安全状态，不返回 API Key 明文；含 `features` 能力位（真实控制链路 vs 页面占位） |
| `/api/selftest` | GET | 否 | 烧录前/烧录后轻量自检，返回版本指纹、pass/warn/fail、资源包、音频服务、Wi-Fi、Brain 配置、OPUS/SR 探针和内存检查 |
| `/api/system/info` | GET | 否 | 返回构建、芯片、内存、资源、音频服务、语音识别探针和 runtime 诊断 |
| `/api/capabilities` | GET | 否 | 返回显示、音频、控制、资源、OTA 和安全能力声明 |
| `/api/diagnostics/turn` | GET | 否 | 最近语音 turn 诊断、audio service、voice wake 和 scene 快照 |
| `/api/wifi/scan` | GET | 否 | 扫描附近 Wi-Fi，返回 SSID/RSSI/信道/加密状态，供手机端配网点选 |
| `/api/audio/status` | GET | 否 | 查看 ES7210/ES8311/I2S/I2C 初始化状态、最近麦克风电平和测试计数 |
| `/api/audio/beep` | POST | 是 | 通过 ES8311 + 功放 + Speaker header 播放短促测试音 |
| `/api/audio/mic-level` | POST | 是 | 通过 ES7210 板载麦克风采样并返回 `level/rms/peak` |
| `/api/audio/play-url` | POST | 是 | 手动播放 HTTP WAV 调试入口；语音 turn 主链路不再依赖它 |
| `/api/audio/opus-probe` | POST | 是 | 采集真实麦克风 PCM 并按 16k/mono/60ms 编码 OPUS，返回帧大小、字节数和麦克风电平统计 |
| `/api/audio/opus-stream/start` | POST | 是 | 启动 DualEye -> Mac Brain 的 AOP1/OPUS WebSocket 真流，参数含 `url`、`duration_ms` |
| `/api/audio/opus-stream/stop` | POST | 是 | 请求停止 OPUS 真流 |
| `/api/audio/opus-stream/status` | GET | 否 | 查看 OPUS 真流状态、帧数、字节数、mute 跳过、采集/编码/发送失败和 heap |
| `/api/voice/turn` | POST | 是 | 板载麦克风录音一轮，经 Mac 桥接调用 MiMo ASR/LLM/TTS，拉回 WAV 并通过 ES8311 播放 |
| `/api/voice/wake` | POST | 是 | 开启/关闭实验版语音触发：`enabled=1/0&threshold=36&hits=3&duration=2800` |
| `/api/brain/ws` | GET | 否 | 设备侧调试用 Brain JSON WebSocket，支持 `hello/status/recent_events/ping`；主动连接 Mac Brain 的客户端在 `atlas_brain_ws_client.*` |
| `/api/sr/status` | GET | 否 | WakeNet/AEC 资源验证探针，返回 ESP-SR、模型分区、WakeNet 模型加载/创建结果、OPUS、堆内存和当前 fallback 状态 |
| `/api/ota/status` | GET | 否 | 返回当前 OTA 支持边界；本版支持 app OTA slot，非 app 分区仍需 USB 全量刷 |
| `/api/ota/apply` | POST | 是 | 从 `url` 下载 app bin 写入下一个 OTA slot；可传 `reboot=1` 写入成功后重启 |
| `/api/rover/stop` | POST | 否 | 立即发送 `AR1,STOP` |
| `/api/rover/move` | POST | 是 | 短时移动，受最大速度/最大时长限制 |
| `/api/app/expression` | POST | 是 | 切换双眼表情 |
| `/api/app/page` | POST | 是 | 切换显示页：双眼、时钟、闹钟、照片、状态等 |
| `/api/app/action` | POST | 是 | 触发应用动作：时钟、音乐、故事、陪聊、日历、番茄、闹钟；`clock/chat/calendar/pomodoro` 已接入状态写入与显示，`music/story` 进入播放/讲述状态位。 |
| `/api/pet/event` | POST | 是 | 触发电子宠物事件：`touch`/`wake`、`play`、`feed`、`rest`、`patrol`、`music`、`story`、`chat` |
| `/api/pet/wake` | POST | 否 | 唤醒保底通道，不依赖配对码，用于 Sleepy/Sleeping 状态恢复 |
| `/api/config/wifi` | POST | 是 | 保存 Wi-Fi |
| `/api/config/llm` | POST | 是 | 保存 LLM 模式、Base URL、Model、API Key |
| `/api/config/safety` | POST | 是 | 保存是否允许移动、最大速度、最大时长 |
| `/api/config/ui` | POST | 是 | 保存主题、对话界面模式、屏幕亮度、音量 |
| `/api/voice/text` | POST | 是 | 文本意图测试，进入 Atlas Brain/本地关键词适配层 |
| `/api/intent` | POST | 是 | 接收 Atlas Brain tool-call 或结构化意图，驱动表情、页面、应用和安全运动 |
| `/api/brain/intent` | POST | 是 | 旧客户端兼容别名，内部走同一套 `/api/intent` 处理逻辑 |
| `/api/config/reset` | POST | 是 | 清除 Wi-Fi 和 LLM 配置 |
| `/api/system/reboot` | POST | 是 | 重启设备 |

安全默认值：

- V0.12 默认 `motion_supported=false`，实体先不做动态小车；Web/语音移动入口应置灰或返回“本版暂停”。
- 底盘 UART、N20 + DRV8833 和安全限速代码保留为后续可选模块；恢复运动前需要重新启用构建开关并做桌面外安全测试。
- 历史默认值曾是 `motion_enabled=true`、`control_mode=manual`、最大速度 `40%`、最大时长 `700 ms`；这不再是当前桌面电子宠物版本的默认策略。
- STOP 最高优先级，不需要配对码。
- Web 移动和配置修改仍需要 6 位配对码，避免同一局域网里的误触。
- API Key 只存 NVS，不打印、不进仓库；当前原型阶段尚未启用 NVS 加密，建议只放低风险测试 Key。

控制模式：

| 模式 | 允许的运动来源 | 说明 |
|---|---|---|
| `manual` | Web 手动控制 | 用户在应用页点前进/后退/转向；DualEye 裁剪速度和时长后通过 UART 下发 |
| `ai` | 语音 / Atlas Brain / LLM | AI 产生结构化运动意图；DualEye 做 Safety Guard 后通过 UART 下发 |

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

显示渲染有两条路径：`raptor/mecha/goggle/pet` 的 `idle/blink/listen` 三态优先从内置 SPIFFS 资源分区加载 240x240 PNG；其余主题或未覆盖状态继续走 `atlas_eye_pose_t` 参数化绘制回退。`atlas_display_render()` 负责把页面、表情、运动、音量和应用 payload 映射到两块实体屏，并对 PNG 主题追加自动眨眼、呼吸和轻微漂移。

面板方向沿用 Waveshare ESP-IDF 示例中已实机验证过的 90 度映射：左屏 `mirror(false,false)+swap_xy(true)`，右屏 `mirror(true,true)+swap_xy(true)`。如果后续更换安装姿态，再单独做旋转配置，不把缺字问题误判成方向问题。

主题 ID：

| ID | 中文名 | 说明 |
|---|---|---|
| `classic` | 经典蓝眼 | 默认开箱主题，黑底青蓝眼。 |
| `amber` | 琥珀巡航 | 更接近黄铜车架和复古巡航感。 |
| `mint` | 薄荷友好 | 更温和，适合陪伴/讲故事。 |
| `alert` | 红色警戒 | 错误、拒绝、急停更醒目。 |
| `night` | 低亮夜航 | 夜间低亮、柔蓝显示。 |
| `raptor` | 猛禽眼 | 黑金边框、冷色扫描，偏“猎鹰机瞳”审美。 |
| `mecha` | 机械电子眼 | 机械感蓝紫高对比主题。 |
| `goggle` | 护目镜眼 | 明黄/黄绿护目镜色调，偏“护目型电子宠物”。 |
| `pet` | 电子宠物巡游 | 暖色友好，强调情绪与状态变化。 |

> 4 套评审主题的 PNG 已随固件烧录到 `storage` SPIFFS 分区，**无 SD 卡也能直接显示和切换**。
> SD 卡可后续用于放置额外素材（照片、贴纸、铃声包），不影响 24 张基础眼睛资源。

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

底盘固件在 `firmware/chassis_xiao_esp32c3`，目标芯片 `esp32c3`。DualEye 与底盘板分别烧录，Atlas Brain 结构化意图入口随 DualEye 固件构建。

如果本机设置了 SOCKS 代理，ESP-IDF component manager 可能需要 Python 依赖：

```bash
env -u http_proxy -u https_proxy -u HTTP_PROXY -u HTTPS_PROXY \
  "$IDF_PYTHON_ENV_PATH/bin/python" -m pip install PySocks
```

本地已验证：`idf.py build` 可以生成 `build/atlas_rover_dualeye.bin`、`build/srmodels/srmodels.bin` 和 `build/storage.bin`。本版分区表已变化，首次升级必须全量烧录；`srmodels.bin` 会写入 `0x10000` 的 `model` 分区，`storage.bin` 会写入 `0xB00000`，包含内置双眼 PNG、pet_head 土拨鼠头资源、小鲅 X1 开机动画、电子宠物和页面资源。

## 主题控制方式（建议4套主题）

- 4 套推荐主题已内置：`raptor` / `mecha` / `goggle` / `pet`。
- 切换方式：
  - Web 入口：应用页「双眼主题」选择相应名称，点击「保存主题」；
  - API 入口：`POST /api/config/ui`，Body 示例：`pin=xxxxxx&theme=raptor&chat_mode=eyes_only&brightness=70&volume=90`。
- 这 4 套主题优先使用 SPIFFS 里的 PNG：`/atlas_eyes/{theme}/{idle|blink|listen}/{left|right}.png`；未覆盖状态会映射到最接近的 PNG，其他主题走参数化绘制回退。
- `idle` 会自动眨眼；`listen/thinking/speaking/curious` 会使用 `listen` 图片并增加微动效。

## 后续接入点

1. 真机屏幕：上电实测 GC9A01/LVGL 后端的旋转、左右眼方向、背光曲线和刷新稳定性。
2. 触摸：接入 CST816S 双路触摸事件，进入 `atlas_ui_handle_voice_intent()` 或新增 `atlas_ui_handle_touch_event()`。
3. Atlas Brain：当前可通过 `/api/intent` 提交 tool-call，Mac 服务可代理 MiMo ASR/LLM/TTS；下一步把长期 WebSocket session、技能、状态和任务编排继续沉到 Atlas Brain 服务侧。
4. 音频：板载录音和外放闭环已经接入；下一步优化唤醒词/VAD、录音时长自适应、TTS 播放时口型/音量同步，以及失败时的屏幕提示。
5. 底盘 ACK：底盘板回传 `AR1,ACK,*`，DualEye 会更新状态或进入错误表情。
6. 真实 LLM：`atlas_llm_client.*` 目前只做配置状态；云端/宿主调用要先输出结构化意图，再进入本地 Safety Guard。
