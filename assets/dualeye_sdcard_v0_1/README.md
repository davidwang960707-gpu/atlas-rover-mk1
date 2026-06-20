# Atlas Rover DualEye SD 眼睛资源包 V0.1

这是一版用于评审和固件接入的 SD 卡眼睛资源包。

## 内容

- 4 套主题：
  - `raptor`：猛禽眼
  - `mecha`：机械电子眼
  - `goggle`：黄色护目镜眼
  - `pet`：电子宠物巡游
- 3 个状态：
  - `idle`：待机
  - `blink`：眨眼
  - `listen`：聆听
- 每个状态包含：
  - `left.png`
  - `right.png`

所有 PNG 均为 `240 x 240`，用于 ESP32-S3-DualEye-Touch-LCD-1.28 的左右圆屏。

## SD 卡放置方式

把 `sdcard` 目录里的 `atlas_eyes` 整个文件夹复制到 TF/SD 卡根目录：

```text
/atlas_eyes/manifest.json
/atlas_eyes/raptor/idle/left.png
/atlas_eyes/raptor/idle/right.png
...
```

## 第一版接入建议

固件先按 `manifest.json` 或固定路径加载：

```text
/atlas_eyes/{theme}/{state}/left.png
/atlas_eyes/{theme}/{state}/right.png
```

先支持 `idle / blink / listen` 三个状态即可。等屏幕加载和主题切换稳定后，再扩展 `thinking / speaking / happy / angry / sleep` 等状态。

## 文件说明

- `source/atlas_dualeye_theme_master_v0_1.png`：AI 生成母版，保留用于追溯和重新裁切。
- `sdcard/atlas_eyes/`：可复制到 SD 卡的实际资源。
- `atlas_eyes_contact_sheet_v0_1.png`：总览评审图。
- `tools/build_dualeye_sdcard_assets.py`：从母版裁切生成资源的脚本。
