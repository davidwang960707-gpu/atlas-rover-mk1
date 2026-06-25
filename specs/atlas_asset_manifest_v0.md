# Atlas Asset Manifest V0

版本：`atlas.asset.manifest.v0`  
状态：草案，兼容当前 SPIFFS 资源包

## 1. 目标

统一描述固件资源包，让主题、宠物头、字体、开机动画、应用图形可以独立于 C 代码升级和校验。

## 2. 资源包目录

建议结构：

```text
sdcard/
  manifest.json
  atlas_eyes/
    manifest.json
    ...
  atlas_pet_head/
    manifest.json
    ...
  fonts/
  boot/
    xiaoba_x1/
      manifest.json
      left/
      right/
  apps/
```

当前无 SD 卡时，`sdcard/` 内容烧入 SPIFFS。

## 3. 顶层 manifest

```json
{
  "protocol": "atlas.asset.manifest.v0",
  "asset_pack_id": "dualeye-assets",
  "version": "0.6.0",
  "target": {
    "board_profile": "waveshare_dualeye_s3",
    "screen": "dual_round_240"
  },
  "requires": {
    "firmware_min": "0.14.0",
    "tool_schema": "atlas.tools.v0.desk_apps"
  },
  "resources": [
    {
      "id": "atlas_eyes",
      "type": "eye_theme_pack",
      "manifest": "atlas_eyes/manifest.json"
    },
    {
      "id": "atlas_pet_head",
      "type": "pet_head_pack",
      "manifest": "atlas_pet_head/manifest.json"
    },
    {
      "id": "xiaoba_x1_boot_intro",
      "type": "boot_intro_pack",
      "manifest": "boot/xiaoba_x1/manifest.json"
    }
  ],
  "checksums": {
    "sha256": "optional-pack-hash"
  }
}
```

## 4. pet_head manifest

```json
{
  "protocol": "atlas.pet_head.v0",
  "version": "0.3.0",
  "canvas": {"width": 240, "height": 240, "background": "transparent"},
  "states": {
    "idle": {"keyframe": "idle.png"},
    "listen": {"keyframe": "listen.png"},
    "speak": {"animation": "speak", "fps": 10},
    "offline": {"keyframe": "offline.png"}
  },
  "views": {
    "idle": ["yaw_l30", "yaw_l15", "yaw_c", "yaw_r15", "yaw_r30"]
  },
  "transitions": {
    "yaw_c_to_yaw_l30": {"frames": 6, "fps": 12}
  }
}
```

## 5. 眼睛主题 manifest

```json
{
  "protocol": "atlas.eye_theme.v0",
  "version": "0.4.0",
  "canvas": {"width": 240, "height": 240},
  "themes": {
    "goggle": {
      "states": ["idle", "blink", "listen"],
      "left": "goggle/left_idle.png",
      "right": "goggle/right_idle.png",
      "fit": "cover_circle"
    },
    "tomoe_spin": {
      "states": ["idle"],
      "animation": "rotate_clockwise",
      "acceleration_ms": 3500
    }
  }
}
```

## 6. 开机动画 manifest

开机动画面向用户时只展示产品名 `小鲅 X1`。`Atlas` 仅用于内部协议、文件和服务命名，不作为设备开机品牌露出。

```json
{
  "protocol": "atlas.boot_intro.v0",
  "id": "xiaoba_x1_boot_intro",
  "product_name": "小鲅 X1",
  "version": "0.1.1",
  "canvas": {"width": 240, "height": 240},
  "fps": 6,
  "duration_ms": 1000,
  "frame_count": 6,
  "asset_policy": "embedded_spiffs_no_sdcard",
  "left": {
    "style": "pet_head_2_5d",
    "frames": "left/frame_%02d.png",
    "fallback": "fallback_left.png"
  },
  "right": {
    "style": "8bit_pixel_status",
    "frames": "right/frame_%02d.png",
    "fallback": "fallback_right.png"
  },
  "outcomes": {
    "ready": {"right_label": "READY", "next_page": "eyes"},
    "local_mode": {"right_label": "LOCAL", "next_page": "clock"},
    "pairing": {"right_label": "PAIR", "next_page": "provisioning"}
  }
}
```

资源结构：

```text
boot/xiaoba_x1/
  manifest.json
  fallback_left.png
  fallback_right.png
  left/frame_00.png
  right/frame_00.png
```

右屏推荐使用 8-bit 像素风状态面板，`小鲅 X1` 可预渲染到 PNG，避免中文字体缺字或排版错位。

## 7. 校验规则

| 规则 | 说明 |
|---|---|
| PNG 尺寸 | 圆屏资源默认 240x240 |
| 透明底 | pet head 推荐透明底 |
| 文件缺失 | selftest 必须报具体路径 |
| 字体缺字 | App 文字必须可检测缺字 |
| 资源版本 | `/api/status.fingerprint.resource_version` 必须可见 |
| 开机动画 | 缺失时只降级到 fallback，不进入异常文字页 |
| 包大小 | SPIFFS 包不得超过分区容量 |

## 8. 兼容策略

- 旧版 `atlas_eyes/manifest.json` 和 `atlas_pet_head/manifest.json` 继续可用。
- 顶层 manifest 缺失时，固件按旧路径检查关键资源。
- 新资源包必须向 `/api/selftest` 暴露关键检查项。

## 9. 验收方法

```bash
python3 tools/build_dualeye_sdcard_assets.py
python3 tools/build_pet_head_v03_assets.py
curl http://DUALEYE_IP/api/selftest
```

通过条件：

- `eye_assets=pass`
- `pet_head_assets=pass`
- `boot_intro_assets=pass/warn`，缺失路径必须可见
- `font_zh=pass/warn`，不能导致页面文字变方框
- 状态页面能看到资源版本
