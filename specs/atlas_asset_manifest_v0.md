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

## 6. 校验规则

| 规则 | 说明 |
|---|---|
| PNG 尺寸 | 圆屏资源默认 240x240 |
| 透明底 | pet head 推荐透明底 |
| 文件缺失 | selftest 必须报具体路径 |
| 字体缺字 | App 文字必须可检测缺字 |
| 资源版本 | `/api/status.fingerprint.resource_version` 必须可见 |
| 包大小 | SPIFFS 包不得超过分区容量 |

## 7. 兼容策略

- 旧版 `atlas_eyes/manifest.json` 和 `atlas_pet_head/manifest.json` 继续可用。
- 顶层 manifest 缺失时，固件按旧路径检查关键资源。
- 新资源包必须向 `/api/selftest` 暴露关键检查项。

## 8. 验收方法

```bash
python3 tools/build_dualeye_sdcard_assets.py
python3 tools/build_pet_head_v03_assets.py
curl http://DUALEYE_IP/api/selftest
```

通过条件：

- `eye_assets=pass`
- `pet_head_assets=pass`
- `font_zh=pass/warn`，不能导致页面文字变方框
- 状态页面能看到资源版本

