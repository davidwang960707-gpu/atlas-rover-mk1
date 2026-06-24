# Atlas Rover DualEye 眼睛资源包 V0.5

这是一版用于固件内置 SPIFFS 和 SD 卡兼容使用的单眼资源包。V0.5 在 V0.4 双眼主题基础上，把 `atlas_pet_head` 正式纳入固件内置资源。

## 内容

- 7 套主题：
  - `raptor`：猛禽眼
  - `mecha`：机械电子眼
  - `goggle`：黄色护目镜眼
  - `pet`：电子宠物巡游
  - `blue_pupil`：蓝色瞳孔，含专用眨眼态和聆听脉冲
  - `no_smoking`：禁烟禁电子烟，静态展示
  - `tomoe_spin`：红色旋纹，固件运行时顺时针旋转并加速
- 3 个状态：
  - `idle`：待机
  - `blink`：眨眼
  - `listen`：聆听
- 每个状态包含：
  - `left.png`
  - `right.png`

所有 PNG 均为 `240 x 240`，用于 ESP32-S3-DualEye-Touch-LCD-1.28 的左右圆屏。

## pet_head V0.2

本资源包同时包含一套新的对话宠物头资源：

- 路径：`sdcard/atlas_pet_head/`
- 形态：左屏 2.5D 土拨鼠头，右屏短文本/应用信息。
- 关键帧：`idle/listen/speak/sing/happy/laugh/cry/sleepy/think/surprised`，全部 `240 x 240`。
- 基础动画：`blink` 6 帧、`speak` 8 帧、`sing` 10 帧、`laugh` 8 帧。
- 已压缩：42 张 PNG 保持 `240 x 240`，pet_head 目录约 1.2MB；完整 SPIFFS 资源目录约 3.6MB，可放入 4MB `storage` 分区。
- 规范文档：`docs/Atlas_pet_head资源规范_V0.2.md`。

这套资源不是双眼主题，而是对话界面的第二形态：更适合语音对话、讲故事、番茄专注、时钟宠物等场景。

V0.4 约束：

- 每块实体屏只显示一只眼，不再把一整张双眼/脸部图片塞进单块屏幕。
- 左右眼采用微妙朝内设计，面板方向按已实机验证的 Waveshare 90 度映射生成和验证。
- 固件会在 PNG 基础上叠加自动眨眼、轻微呼吸/漂移、聆听态脉冲或指定主题旋转，图片本身保持轻量静态。
- `raptor`、`mecha`、`goggle` 从已评审母版重新裁切，加入圆屏边缘暗角；`goggle` 额外做了圆形比例校正，避免高细节母版裁切后呈竖椭圆；`pet` 已替换为 `/Users/macbook/Desktop/marmot-pet` 的小电子宠物 IP，并提供 idle/blink/listen 三态。
- `goggle`、`no_smoking`、`tomoe_spin` 已重新放大并弱化圆屏边缘暗角，减少实机黑边；`no_smoking` 不再旋转，`tomoe_spin` 由固件从当前初始速度缓入加速到目标转速。
- `blue_pupil`、`no_smoking`、`tomoe_spin` 来自 `source/uploads/` 的用户素材，已清理棋盘背景并输出为 `240 x 240` RGB PNG。

## SD 卡放置方式

把 `sdcard` 目录里的 `atlas_eyes` 和 `atlas_pet_head` 整个文件夹复制到 TF/SD 卡根目录：

```text
/atlas_eyes/manifest.json
/atlas_eyes/raptor/idle/left.png
/atlas_eyes/raptor/idle/right.png
...
/atlas_pet_head/manifest.json
/atlas_pet_head/keyframes/idle.png
/atlas_pet_head/animations/speak/frame_00.png
```

## 固件接入路径

固件先按 `manifest.json` 或固定路径加载：

```text
/atlas_eyes/{theme}/{state}/left.png
/atlas_eyes/{theme}/{state}/right.png
/atlas_pet_head/keyframes/{state}.png
/atlas_pet_head/animations/{animation}/frame_00.png
```

当前固件已支持 `atlas_eyes` 的 `idle / blink / listen` 三个 PNG 状态，并把 `thinking / speaking / curious` 映射到 `listen`；`idle` 会定时自动切到 `blink` 做眨眼。V0.14.4 起，固件同时支持 `atlas_pet_head/keyframes/{state}.png` 和 `atlas_pet_head/animations/{animation}/frame_XX.png`，用于 `pet_head` 对话界面。

## 文件说明

- `sdcard/atlas_eyes/`：固件 SPIFFS 打包来源，也是可复制到 SD 卡的实际资源。
- `sdcard/atlas_pet_head/`：pet_head V0.2 关键帧、基础动画和 manifest。
- `source/pet_head/atlas_marmot_pet_head_2_5d_sheet_v0_2.png`：pet_head V0.2 设定源图。
- `source/pet_head/atlas_marmot_pet_head_2_5d_keyframes_preview_v0_2.png`：pet_head V0.2 圆屏预览图。
- `atlas_eyes_contact_sheet_v0_4.png`：V0.4 总览评审图。
- `atlas_eyes_contact_sheet_v0_3.png`：兼容旧引用，内容已同步为 V0.4。
- `atlas_eyes_contact_sheet_v0_2.png`：兼容旧引用，内容已同步为 V0.4。
- `atlas_eyes_contact_sheet_v0_1.png`：兼容旧引用，内容已同步为 V0.4。
- `tools/build_dualeye_sdcard_assets.py`：程序化生成 V0.4 单眼资源、manifest、contact sheet 和 zip 的脚本。
