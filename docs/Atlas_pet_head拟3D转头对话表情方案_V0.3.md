# Atlas pet_head 拟 3D 转头对话表情方案 V0.3

## 1. 结论

小智的 GIF/emoji 表情路线，本质上也是“二维动画资产 + 情绪状态机”，不是在 ESP32 上实时跑 3D。它的核心价值不在 3D 技术，而在会话协议里把 `emotion` 独立出来，让端侧根据情绪切表情、动画和 UI 状态。

Atlas 应该走同一条稳路线，但做成自己的角色系统：

- 左屏：2.5D 土拨鼠头，负责情绪、说话、转头、眨眼、点头。
- 右屏：短文本、工具结果、应用信息。
- 真机端：只播放预渲染 PNG 帧，不做实时 3D 渲染。
- Mac Brain：负责把 ASR/LLM/TTS/工具状态转换成宠物状态事件。

目标不是“看起来像 GIF”，而是“对话时像一个有反应的小东西”：听你说话、歪头思考、回答时张嘴、讲笑话会大笑、失败时不进入调试文字页。

## 2. 参考基线

参考对象：

- 小智固件项目：[78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)
- 小智情绪协议：[Emoji Emotion Display](https://xiaozhi.dev/en/docs/development/emotion/)
- Waveshare DualEye 官方仓库：[ESP32-S3-DualEye-Touch-LCD-1.28](https://github.com/waveshareteam/ESP32-S3-DualEye-Touch-LCD-1.28)

可借鉴点：

- 情绪是会话协议的一等字段，不混在正文里。
- 端侧只做轻量 UI 状态机，复杂理解放在服务端。
- 表情资产可以 GIF/帧动画化，但要保证音频链路不卡。

Atlas 的差异点：

- Atlas 是双圆屏，适合“左角色 + 右文字/工具”的分屏人格。
- Atlas 当前固件已经有 `/atlas_pet_head/keyframes` 和 `/animations` 读取能力，可以渐进增强。
- Atlas 需要避免异常文字页，Brain 离线时也要保持宠物表情可用。

## 3. 视觉目标

角色关键词：

- 只有头，不出现身体和爪子。
- 呆、蠢萌、慢半拍，不能太机灵。
- 2.5D 软胶玩具质感，头有体积，眼神有点空。
- 240x240 圆屏内填充 86%-94%，不要四周留大黑边。

拟 3D 转头不是用 LVGL 旋转一张图，而是预渲染多角度帧。转头时同时变化：

- 头部轮廓左右压缩和侧面厚度。
- 鼻子、嘴、眼睛位置产生视差。
- 高光和阴影跟着角度偏移。
- 耳朵前后层级变化。
- 眨眼、张嘴等局部帧跟随当前角度。

这样看起来像 3D，但真机只是换 PNG，稳定很多。

## 4. 资源规格

### 4.1 画布

| 项目 | 规格 |
|---|---|
| 画布 | 240x240 px |
| 安全圆 | 半径 116 px，中心 120,120 |
| 主体填充 | 头部最大宽高 206-224 px |
| 背景 | 透明优先；若固件/PNG 解码不稳，则使用纯黑背景 |
| 文件 | PNG，最终由构建脚本压缩/校验 |
| 帧率 | 8-12 fps，优先稳定 |

### 4.2 角度集合

P0 先做 5 个 yaw 角度，不做复杂 pitch：

| view id | 视觉角度 | 用途 |
|---|---|---|
| `yaw_l30` | 明显看左 | 对用户左侧、躲避、思考 |
| `yaw_l15` | 轻微看左 | 自然转头过渡 |
| `yaw_c` | 正脸 | 默认、回答 |
| `yaw_r15` | 轻微看右 | 自然转头过渡 |
| `yaw_r30` | 明显看右 | 对用户右侧、惊讶 |

P1 再增加 pitch：

- `pitch_up`：看上方，适合思考。
- `pitch_down`：低头，适合困、委屈、失败。

### 4.3 状态集合

| state | 中文 | 关键表现 |
|---|---|---|
| `idle` | 待机 | 慢眨眼，偶尔轻微转头 |
| `listen` | 聆听 | 眼睛睁大，头微歪 |
| `think` | 思考 | 眼神上飘，嘴巴呆住 |
| `speak` | 说话 | 小嘴开合，头轻点 |
| `sing` | 唱歌 | 嘴更夸张，头有节奏 |
| `happy` | 开心 | 嘴角上扬，眼睛变软 |
| `laugh` | 大笑 | 眼睛眯，嘴张大 |
| `cry` | 大哭 | 眼泪/委屈嘴，不进入错误页 |
| `sleepy` | 困 | 眼皮下垂，头低 |
| `surprised` | 惊讶 | 瞳孔变小，头后仰感 |
| `offline` | Brain 离线 | 呆滞加载，不显示异常堆栈文字 |

## 5. 文件结构

保持现有固件路径向后兼容，新增 `views` 和 `transitions`。

```text
/atlas_pet_head
├── manifest.json
├── keyframes
│   ├── idle.png                  # 兼容旧固件，等同 views/idle/yaw_c.png
│   ├── listen.png
│   ├── think.png
│   ├── speak.png
│   ├── sing.png
│   ├── happy.png
│   ├── laugh.png
│   ├── cry.png
│   ├── sleepy.png
│   ├── surprised.png
│   └── offline.png
├── views
│   ├── idle/yaw_l30.png ... yaw_r30.png
│   ├── listen/yaw_l30.png ... yaw_r30.png
│   └── speak/yaw_l30.png ... yaw_r30.png
├── animations
│   ├── blink/yaw_c/frame_00.png ... frame_05.png
│   ├── speak/yaw_c/frame_00.png ... frame_09.png
│   ├── sing/yaw_c/frame_00.png ... frame_11.png
│   └── laugh/yaw_c/frame_00.png ... frame_09.png
└── transitions
    ├── turn_yaw_c_to_yaw_l30/frame_00.png ... frame_05.png
    ├── turn_yaw_l30_to_yaw_c/frame_00.png ... frame_05.png
    ├── turn_yaw_c_to_yaw_r30/frame_00.png ... frame_05.png
    └── turn_yaw_r30_to_yaw_c/frame_00.png ... frame_05.png
```

`manifest.json` 建议字段：

```json
{
  "schema": "atlas.pet_head.v0.3",
  "version": "0.3.0",
  "canvas": [240, 240],
  "default_view": "yaw_c",
  "states": ["idle", "listen", "think", "speak", "sing", "happy", "laugh", "cry", "sleepy", "surprised", "offline"],
  "views": ["yaw_l30", "yaw_l15", "yaw_c", "yaw_r15", "yaw_r30"],
  "animations": {
    "blink": {"frames": 6, "fps": 12, "loop": false},
    "speak": {"frames": 10, "fps": 10, "loop": true},
    "sing": {"frames": 12, "fps": 10, "loop": true},
    "laugh": {"frames": 10, "fps": 12, "loop": true}
  }
}
```

## 6. 会话状态机

### 6.1 主链路

```mermaid
flowchart LR
  "待机 idle" -->|"唤醒/开始录音"| "聆听 listen"
  "聆听 listen" -->|"ASR 文本完成"| "思考 think"
  "思考 think" -->|"LLM 首包/工具执行"| "轻微转头 turn"
  "轻微转头 turn" -->|"TTS 播放"| "说话 speak"
  "说话 speak" -->|"播放结束"| "回待机 idle"
  "思考 think" -->|"失败/Brain 离线"| "离线 offline"
  "离线 offline" -->|"Brain 恢复"| "待机 idle"
```

### 6.2 转头规则

P0 规则：

- `idle`：每 6-12 秒随机一次 `yaw_c -> yaw_l15/yaw_r15 -> yaw_c`。
- `listen`：进入时执行一次轻微歪头，优先 `yaw_l15` 或 `yaw_r15`。
- `think`：在 `yaw_l15` 和 `yaw_r15` 之间慢切，制造“脑子在转”的感觉。
- `speak`：主要保持 `yaw_c`，每 1.2-2 秒轻点头或轻微左右摆。
- `laugh`：固定正脸，做大嘴循环，避免转头影响喜感。
- `cry/sleepy`：低动效，防止显得烦躁。

P1 规则：

- 根据用户点击或 Web 端方位按钮，指定看左/看右。
- 根据音频输入能量峰值做轻微随机看向，不做真实声源定位。

## 7. Brain 事件协议

新增或稳定以下事件，统一走 DualEye 常驻 WebSocket session；HTTP 只保留降级和调试。

```json
{
  "type": "ui.pet.state",
  "state": "listen",
  "view": "yaw_l15",
  "right_text": "我在听",
  "ttl_ms": 12000,
  "source": "voice"
}
```

```json
{
  "type": "ui.pet.animation",
  "animation": "speak",
  "view": "yaw_c",
  "loop": true,
  "right_text": "说重点...",
  "source": "tts"
}
```

```json
{
  "type": "ui.pet.turn",
  "from": "yaw_c",
  "to": "yaw_r30",
  "duration_ms": 420,
  "source": "idle_micro_motion"
}
```

### 7.1 LLM emotion 映射

| LLM emotion | Atlas pet state | 备注 |
|---|---|---|
| `neutral` | `idle` | 正常回答 |
| `happy` / `smile` | `happy` | 轻开心 |
| `laughing` | `laugh` | 笑话、玩梗 |
| `sad` / `crying` | `cry` | 软反馈，不显示错误文字 |
| `surprised` | `surprised` | 新消息、意外 |
| `thinking` | `think` | 工具调用/搜索中 |
| `sleepy` | `sleepy` | 长时间不互动 |
| `angry` | `think` | Atlas 不做攻击性，压成皱眉思考 |
| `loving` | `happy` | 可加爱心特效但不喧宾夺主 |

## 8. 固件改造点

### 8.1 `atlas_pet`

建议扩展运行时视觉状态：

```c
typedef enum {
    ATLAS_PET_VIEW_YAW_L30,
    ATLAS_PET_VIEW_YAW_L15,
    ATLAS_PET_VIEW_YAW_C,
    ATLAS_PET_VIEW_YAW_R15,
    ATLAS_PET_VIEW_YAW_R30,
} atlas_pet_view_t;

typedef struct {
    char state[16];
    char animation[16];
    atlas_pet_view_t view;
    atlas_pet_view_t target_view;
    uint32_t animation_until_ms;
    uint32_t next_micro_motion_ms;
    char right_text[96];
} atlas_pet_visual_t;
```

原则：

- 宠物生命值状态和视觉状态分开。
- 音频服务状态优先级最高：录音、思考、播放不能被 idle 微动效打断。
- Brain 离线是正常视觉状态，不是 `ATLAS_SCENE_ERROR`。

### 8.2 `atlas_display`

新增读取顺序：

1. `/atlas_pet_head/transitions/{turn}/frame_%02u.png`
2. `/atlas_pet_head/animations/{animation}/{view}/frame_%02u.png`
3. `/atlas_pet_head/views/{state}/{view}.png`
4. `/atlas_pet_head/keyframes/{state}.png`
5. 内置 LVGL 简笔 fallback

这样即使新资源缺失，真机也不会白屏或黑屏。

### 8.3 `atlas_brain_ws_client`

需要支持三类 UI 事件：

- `ui.pet.state`
- `ui.pet.animation`
- `ui.pet.turn`

事件只更新 UI 状态，不阻塞音频任务。

### 8.4 `/api/status`

补充用户可感知字段：

```json
{
  "ui": {
    "chat_mode": "pet_head",
    "pet_state": "speak",
    "pet_view": "yaw_c",
    "pet_animation": "speak",
    "pet_asset_version": "0.3.0",
    "right_text": "收到，我来想想"
  }
}
```

手机端和 Mac 控制台必须显示这些真实状态，不能只显示旧的 expression。

## 9. Web 控制端改造点

TOC 控制台增加“宠物表情实验台”：

- 对话模式：双屏文字 / 土拨鼠头 / 纯眼睛。
- 状态按钮：待机、聆听、思考、说话、唱歌、开心、大笑、大哭、困、惊讶。
- 转头按钮：看左、正脸、看右、随机微动。
- 预览区：左屏模拟宠物头，右屏模拟短文本。
- 资源健康：显示 pet asset version、缺失帧数量、fallback 次数。

用户主应用页不显示调试名词，只显示自然状态：

- “我在听”
- “想一想”
- “正在说”
- “Brain 离线，我先发会儿呆”

## 10. 资源生产流程

### 10.1 你需要提供什么

如果走快线，我可以先用现有土拨鼠风格继续生成，不强制等你补素材。

你提供越多，效果越稳：

- 1 张最终正脸风格图。
- 是否必须“只有头”。
- 呆萌程度：普通呆 / 很蠢萌 / 欠揍但可爱。
- 是否接受黑底，还是必须透明底。
- 是否继续无 SD 卡，把资源打进固件；还是后续改 SD 卡。

### 10.2 生成原则

- 先生成 11 个状态的 `yaw_c` 关键帧。
- 再补 `idle/listen/think/speak` 的 5 个 yaw 角度。
- 最后补 `speak/sing/laugh/blink/turn` 动画帧。
- 每次只扩一组，先真机看效果，避免一次塞太多资源导致固件包膨胀。

## 11. 分阶段实施

### P0：方案和资源契约

- 固化本文档。
- 更新 `Atlas_pet_head资源规范` 到 V0.3。
- 生成一张 5 角度 + 10 状态的评审图。

验收：

- 资源命名明确。
- Brain/固件/Web 三方字段一致。

### P1：中心状态可用

- 生成并烧录 `yaw_c` 关键帧。
- 对话链路自动切 `listen/think/speak/idle`。
- Brain 离线显示 `offline`，不进异常文字页。

验收：

- 语音一轮对话能看到聆听、思考、说话、回待机。
- 右屏短文本无方框、无错位。

### P2：拟 3D 转头

- 加入 `yaw_l30/yaw_l15/yaw_c/yaw_r15/yaw_r30`。
- 加入转头过渡帧。
- idle 微动效、think 慢摆头、listen 歪头。

验收：

- 肉眼能明显感到头在转。
- 10 分钟待机不白屏、不黑屏、不影响 WS 心跳。

### P3：说话/唱歌/大笑动画

- `speak` 根据 TTS 播放循环嘴型。
- `sing` 节奏更夸张。
- `laugh` 只在笑话或高兴回复触发。

验收：

- TTS 播放期间嘴型持续动。
- 播放结束 300ms 内回 idle 或当前应用状态。

### P4：产品化收口

- Web 控制端同步真实 pet 状态。
- `/api/selftest` 检查 pet 资源完整性。
- 生成真机测试报告。

验收：

- 手机用户不需要看调试页也能理解设备状态。
- 缺资源时可见 fallback，不出现白屏。

## 12. 风险和取舍

| 风险 | 原因 | 处理 |
|---|---|---|
| 资源包过大 | 多状态、多角度、多帧 | 先 P0/P1 小包，动画逐步加 |
| 刷新不丝滑 | PNG 解码和 LVGL 切图成本 | 控制 8-12 fps，帧数少但关键帧准 |
| 视觉不够 3D | 单图旋转会假 | 必须多角度预渲染 |
| 音频被 UI 卡住 | UI 切帧和音频同抢资源 | UI 事件不阻塞音频服务，必要时降帧 |
| Brain 离线吓人 | 旧异常页像调试设备 | offline 视为宠物状态 |

## 13. 下一步

建议下一步直接做 P0/P1：

1. 更新资源规范 V0.3。
2. 出一张“土拨鼠头 10 状态 + 5 角度”评审图。
3. 改固件资源读取逻辑，支持 `views/{state}/{view}.png` 并保留旧 keyframe fallback。
4. 改 Atlas Brain，使对话阶段自动下发 `ui.pet.state` 和 `ui.pet.animation`。
5. 改 Web 控制端，加入宠物表情实验台。

## 14. V0.3 已执行内容

已按“现有土拨鼠、无 SD 卡、只有头、蠢萌欠揍但可爱、透明底”的约束完成第一轮资源生成和固件接入：

- 新增生成脚本：[build_pet_head_v03_assets.py](</Users/macbook/Documents/Atlas One/tools/build_pet_head_v03_assets.py>)
- 新增透明资源包：[atlas_pet_head_sdcard_v0_3.zip](</Users/macbook/Documents/Atlas One/assets/dualeye_sdcard_v0_1/atlas_pet_head_sdcard_v0_3.zip>)
- 新增预览表：[atlas_marmot_pet_head_2_5d_contact_sheet_v0_3.png](</Users/macbook/Documents/Atlas One/assets/dualeye_sdcard_v0_1/source/pet_head/atlas_marmot_pet_head_2_5d_contact_sheet_v0_3.png>)
- 固件 `atlas_display.c` 已接入 `views` 和 `transitions`，旧 keyframe/animation 仍作为 fallback。
- `/api/capabilities` 和 `/api/selftest` 已升级到 `dualeye-assets-v0.6-pet-head-yaw`，会检查 view 与 turn 资源。
- 透明检查：87 张 PNG 均带 alpha。
- 体积检查：`atlas_pet_head` 约 1.1MB，完整 SPIFFS 源目录约 3.8MB，继续适配 4MB `storage` 分区。

帧率策略：

- 待机转头：6 帧 / 12fps，约 0.5 秒完成一次转向。
- 说话：8 帧 / 10fps，优先保障 TTS 播放稳定。
- 唱歌：10 帧 / 10fps，节奏更明显但不拉高 UI 负载。
- 大笑：8 帧 / 12fps，短促有冲击力。
