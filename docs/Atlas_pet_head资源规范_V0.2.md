# Atlas pet_head 资源规范 V0.3

## 1. 目标

`pet_head` 是 Atlas DualEye 对话界面的第二形态：左屏显示 2.5D/伪 3D 土拨鼠头，右屏显示短文本、任务名、倒计时或会话摘要。它不替代双眼主题，而是用于“机器人像一个有性格的小桌面伙伴”的场景。

本版视觉评审关键词：

- 只有头，不出现身体、爪子、肩膀。
- 呆、蠢萌、反应慢半拍，不要机灵、帅气或攻击性。
- 软胶玩具质感，保留 2.5D 体积，不做真 3D 实时渲染。
- 适配 240x240 圆形屏，主体填充 86%-94%。
- 背景使用透明 PNG。黑底在实机圆屏上会有色差，只作为预览垫底。
- 继续无 SD 卡，资源打入 `storage` SPIFFS。

## 2. 屏幕分工

| 模式 | 左屏 | 右屏 | 使用场景 |
|---|---|---|---|
| `text` | 会话文字 | 会话文字 | 长文本、调试、可读性优先 |
| `pet_head` | 土拨鼠头状态/动画 | 短文本/应用信息 | 日常对话、播报、番茄、时钟、故事 |
| `eyes_only` | 简单双眼/表情 | 简单双眼/表情 | 无文字陪伴、情绪反馈 |

`pet_head` 模式下，右屏文字必须短，建议每行 6-9 个汉字，最多 3-4 行。长回复由 Atlas Brain 先压缩成一句话，再送给 DualEye 展示。

## 3. 文件结构

固件资源根目录：

```text
/atlas_pet_head
├── manifest.json
├── keyframes
│   ├── idle.png
│   ├── listen.png
│   ├── speak.png
│   ├── sing.png
│   ├── happy.png
│   ├── laugh.png
│   ├── cry.png
│   ├── sleepy.png
│   ├── think.png
│   └── surprised.png
├── views
│   ├── idle/yaw_l30.png ... yaw_r30.png
│   ├── listen/yaw_l30.png ... yaw_r30.png
│   ├── think/yaw_l30.png ... yaw_r30.png
│   └── speak/yaw_l30.png ... yaw_r30.png
├── animations
│   ├── blink/frame_00.png ... frame_05.png
│   ├── speak/frame_00.png ... frame_07.png
│   ├── sing/frame_00.png ... frame_09.png
│   └── laugh/frame_00.png ... frame_07.png
└── transitions
    ├── turn_yaw_c_to_yaw_l30/frame_00.png ... frame_05.png
    ├── turn_yaw_l30_to_yaw_c/frame_00.png ... frame_05.png
    ├── turn_yaw_c_to_yaw_r30/frame_00.png ... frame_05.png
    └── turn_yaw_r30_to_yaw_c/frame_00.png ... frame_05.png
```

源图和评审图：

- `assets/dualeye_sdcard_v0_1/source/pet_head/atlas_marmot_pet_head_2_5d_sheet_v0_2.png`
- `assets/dualeye_sdcard_v0_1/source/pet_head/atlas_marmot_pet_head_2_5d_keyframes_preview_v0_2.png`
- `assets/dualeye_sdcard_v0_1/source/pet_head/atlas_marmot_pet_head_2_5d_blink_preview_v0_2.gif`
- `assets/dualeye_sdcard_v0_1/source/pet_head/atlas_marmot_pet_head_2_5d_speak_preview_v0_2.gif`
- `assets/dualeye_sdcard_v0_1/source/pet_head/atlas_marmot_pet_head_2_5d_sing_preview_v0_2.gif`
- `assets/dualeye_sdcard_v0_1/source/pet_head/atlas_marmot_pet_head_2_5d_laugh_preview_v0_2.gif`
- `assets/dualeye_sdcard_v0_1/source/pet_head/atlas_marmot_pet_head_2_5d_contact_sheet_v0_3.png`
- `assets/dualeye_sdcard_v0_1/source/pet_head/atlas_marmot_pet_head_2_5d_yaw_sheet_v0_3_chromakey.png`

独立资源包：

- `assets/dualeye_sdcard_v0_1/atlas_pet_head_sdcard_v0_3.zip`

## 4. 状态定义

| 状态 | 中文 | 气质 | 推荐触发 |
|---|---|---|---|
| `idle` | 待机 | 呆呆加载中 | 默认待机、时钟宠物 |
| `listen` | 聆听 | 没听懂但努力听 | ASR 录音、唤醒等待 |
| `speak` | 说话 | 小嘴输出 | TTS 播放、回答 |
| `sing` | 唱歌 | 张嘴乱唱但很快乐 | 音乐、唱歌 TTS |
| `happy` | 开心 | 慢慢开心 | 触摸、任务完成 |
| `laugh` | 大笑 | 蠢萌大笑 | 笑话、轻松回复 |
| `cry` | 大哭 | 委屈到冒水 | 安慰、失败反馈 |
| `sleepy` | 困困 | 电量不足式发呆 | 长时间不互动、夜间 |
| `think` | 思考 | 努力加载但看起来不太会 | LLM 思考、工具调用、Brain 离线软降级 |
| `surprised` | 惊讶 | 突然没反应过来 | 唤醒、通知、意外事件 |

## 5. 动画策略

当前 V0.2 已生成基础可用帧，属于“固件验证版动画”，后续还可以做逐帧美术增强。

| 动画 | 帧数 | fps | 循环 | 用法 |
|---|---:|---:|---|---|
| `blink` | 6 | 12 | 否 | 待机随机眨眼 |
| `speak` | 8 | 10 | 是 | TTS 播放期间循环 |
| `sing` | 10 | 10 | 是 | 音乐/唱歌期间循环 |
| `laugh` | 8 | 12 | 是 | 笑话或开心反馈 |

拟 3D 转头 V0.3 采用少帧策略，避免 UI 解码抢占音频链路：

| 类型 | 帧数/FPS | 用法 |
|---|---:|---|
| `views/*/yaw_*` | 单帧 | 状态稳定时显示 |
| `turn_yaw_c_to_yaw_l30` | 6 / 12fps | 待机向左看 |
| `turn_yaw_l30_to_yaw_c` | 6 / 12fps | 左侧回正 |
| `turn_yaw_c_to_yaw_r30` | 6 / 12fps | 待机向右看 |
| `turn_yaw_r30_to_yaw_c` | 6 / 12fps | 右侧回正 |

推荐运行时规则：

- `idle` 每 3.5-9 秒随机触发一次 `blink`。
- ASR 录音时显示 `listen`，超过 12 秒无结果回到 `idle`。
- LLM 生成中显示 `think`，TTS 播放时切 `speak` 动画。
- 唱歌工具或音乐播放时切 `sing` 动画。
- Brain 离线不要进入异常文字页，左屏显示 `think`，右屏显示“Brain 离线，我先发会儿呆”。

## 6. Atlas Brain 控制建议

后续 Tool Schema V0 建议补充以下工具：

```json
{
  "name": "atlas.ui.set_chat_mode",
  "arguments": {
    "mode": "pet_head"
  }
}
```

```json
{
  "name": "atlas.pet.set_state",
  "arguments": {
    "state": "listen",
    "right_text": "我在听"
  }
}
```

```json
{
  "name": "atlas.pet.play_animation",
  "arguments": {
    "animation": "speak",
    "loop": true,
    "right_text": "正在回答"
  }
}
```

这些工具应走 Atlas Brain WebSocket session，不再走旧的零散 HTTP 流程。

## 7. 接入顺序

1. 固件先读取 `/atlas_pet_head/manifest.json`，校验 `schema` 和 `version`。
2. 新增 `pet_head` 页面渲染器，支持 keyframe 和 animation 两种资源。
3. Web 控制端新增“对话界面模式”：文字、宠物头、纯眼睛。
4. Atlas Brain 对话流程根据 ASR/LLM/TTS 状态自动下发表情状态。
5. 语音播报期间同步 `speak` 或 `sing` 动画，播报结束回到 `idle` 或上一个应用页。

## 8. V0.3 实装记录

- 生成脚本：[build_pet_head_v03_assets.py](</Users/macbook/Documents/Atlas One/tools/build_pet_head_v03_assets.py>)
- 资源目录：[atlas_pet_head](</Users/macbook/Documents/Atlas One/assets/dualeye_sdcard_v0_1/sdcard/atlas_pet_head>)
- 预览图：[atlas_marmot_pet_head_2_5d_contact_sheet_v0_3.png](</Users/macbook/Documents/Atlas One/assets/dualeye_sdcard_v0_1/source/pet_head/atlas_marmot_pet_head_2_5d_contact_sheet_v0_3.png>)
- 资源包：[atlas_pet_head_sdcard_v0_3.zip](</Users/macbook/Documents/Atlas One/assets/dualeye_sdcard_v0_1/atlas_pet_head_sdcard_v0_3.zip>)
- 透明检查：87 张 PNG 均带 alpha。
- 体积检查：`atlas_pet_head` 约 1.1MB，完整 `sdcard` 源目录约 3.8MB，继续适配 4MB `storage` SPIFFS。
