# 小鲅 X1 DualEye 开机动画方案 V0.1

状态：设计定稿草案  
目标硬件：Waveshare ESP32-S3-DualEye-Touch-LCD-1.28  
资源策略：无 SD 卡，随 SPIFFS 资源包烧录  

## 1. 产品命名边界

对用户可见的设备标识统一使用 **小鲅 X1**。

`Atlas One / Atlas Brain / Atlas` 只作为内部项目、服务、协议和代码命名，不进入开机动画、用户 App 首页、设备屏幕标题和对外说明。开机画面右屏只能出现“小鲅 X1”或更短的“小鲅”，不能出现内部工程名。

## 2. 动画定位

开机动画不是 MP4，也不做实时 3D。ESP32-S3 上最稳的方案是左右屏同步播放 240x240 PNG 帧序列：

- 播放稳定，LVGL 解码路径可控。
- 无需视频解码器，避免占用音频/网络初始化资源。
- 可用透明底宠物头和像素风状态面板复用现有资源体系。
- 失败时能快速降级到静态帧，不进入黑屏或异常文字页。

第一版目标是“可爱、有品牌感、不卡主链路”，不是追求长片头。

## 3. 左右屏分工

| 屏幕 | 风格 | 内容 | 用户感受 |
|---|---|---|---|
| 左屏 | 2.5D 土拨鼠头 | 睡醒、发呆、转头、开心 | 设备像一个刚醒的小伙伴 |
| 右屏 | 8-bit 像素风状态面板 | 小鲅 X1、启动进度、Wi-Fi、Brain、音频状态 | 像复古掌机启动界面 |

左屏继续沿用 `pet_head` 的“只有头、透明底、蠢萌、反应慢半拍”方向。右屏不要做现代扁平调试文字，而是做像素风 UI：粗像素边框、低色数进度条、像素小图标、短状态词。

## 4. 分镜设计

总时长建议 2.4-2.8 秒，12 fps，约 30-34 帧。若冷启动资源紧张，可降到 18-24 帧。

实装 V0.1.1 因 4MB SPIFFS 分区已有眼睛主题、pet_head 和 3500 字库，24 帧和 12 帧版都会触发 `spiffsgen.py` 空间不足，因此当前烧录安全版采用 **6 帧、6fps、约 1 秒**。初版误用 `animations/blink` 脏帧后出现“四个眼睛”，V0.1.1 改为只使用圆屏安全区内的干净 keyframe/view 帧。

| 时间 | 左屏 | 右屏 |
|---|---|---|
| 0.0-0.4s | 黑场中土拨鼠头轮廓微亮，眼睛闭着 | 像素边框扫入，显示 `小鲅 X1` |
| 0.4-0.9s | 土拨鼠慢慢睁眼，眼神有点呆 | 像素进度条 25%，小图标 `LCD OK` |
| 0.9-1.4s | 小幅左右看，像刚开机找人 | 进度条 50%，小图标 `Wi-Fi` 闪烁 |
| 1.4-2.0s | 打一个小哈欠或张嘴“啵”一下 | 进度条 75%，小图标 `Brain` 连接中 |
| 2.0-2.6s | 表情变开心，回到待机朝前 | 进度条 100%，显示 `READY` 或 `LOCAL` |

状态文案要短，推荐只用：

- `小鲅 X1`
- `LCD`
- `Wi-Fi`
- `Brain`
- `Audio`
- `READY`
- `LOCAL`
- `PAIR`

中文只保留产品名，其他状态尽量用英文/图标，降低中文字体渲染风险。

## 5. 资源目录规范

固件资源路径：

```text
/boot/xiaoba_x1/
├── manifest.json
├── fallback_left.png
├── fallback_right.png
├── left/
│   ├── frame_00.png
│   ├── frame_01.png
│   └── ...
└── right/
    ├── frame_00.png
    ├── frame_01.png
    └── ...
```

每帧要求：

| 项 | 要求 |
|---|---|
| 尺寸 | 240x240 |
| 格式 | PNG |
| 背景 | 左屏透明底优先，右屏可黑底/深色底 |
| 安全区 | 关键文字和图标放在圆形屏中心 190px 内 |
| 命名 | 左右屏帧号必须一一对应 |
| 文件体积 | V0.1.1 当前约 103KB，最高不超过 1MB |

## 6. manifest 草案

```json
{
  "protocol": "atlas.boot_intro.v0",
  "id": "xiaoba_x1_boot_intro",
  "product_name": "小鲅 X1",
  "internal_codename": "dualeye_pet_device",
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

## 7. 固件播放策略

开机动画应作为独立的 `boot_intro` 场景，不挤占语音 turn、Wi-Fi 初始化和 Brain WebSocket 主链路。

建议策略：

- LVGL 与双屏初始化完成后立即显示 `fallback_left/right`，避免黑屏。
- 资源 manifest 校验通过后播放帧动画；校验失败只显示 fallback，不报异常文字页。
- 最短播放 900ms，最长播放 3000ms。
- Wi-Fi/Brain 状态可以异步更新右屏最后几帧的 outcome：`READY / LOCAL / PAIR`。
- 重启、OTA 后可播放 800ms 快启动版；冷启动播放完整版。
- 任何异常都回到正常应用页：双眼、时钟、配网或本地模式，不停留在错误屏。

## 8. 右屏 8-bit 视觉规范

| 元素 | 规范 |
|---|---|
| 主标题 | `小鲅 X1`，像素描边或预渲染 PNG，避免字体缺字 |
| 色彩 | 黑/深蓝底，青绿、奶白、橙色作为 4-6 色像素调色盘 |
| 图标 | LCD、Wi-Fi、Brain、Audio 四个 12-18px 像素图标 |
| 进度条 | 块状像素条，不用现代圆角进度条 |
| 动效 | 图标闪烁、进度块跳动、像素星点，不做大面积渐变 |
| 文案 | 极短；不显示 IP、端口、错误码这类调试信息 |

建议右屏状态面板采用预渲染 PNG 帧，尤其是“小鲅 X1”四个中文字符，避免再出现中文方框或排版错位。

## 9. 验收标准

| 项 | 通过条件 |
|---|---|
| 品牌 | 开机动画只显示 `小鲅 X1`，不显示内部项目名 |
| 双屏 | 左右屏都亮，帧号同步，无单屏黑屏 |
| 画面 | 圆屏安全区内无裁切，右屏像素风明确 |
| 体验 | 冷启动动画不超过 3 秒，结束后自动进入应用页 |
| 降级 | 缺资源、Brain 离线、Wi-Fi 未连接时不进入异常文字页 |
| 性能 | 播放期间不阻塞 Wi-Fi 初始化、音频服务和 Brain WS 连接 |
| 自检 | `/api/selftest` 能报告 `boot_intro_assets=pass/warn/fail` 和缺失路径 |

## 10. 下一步

1. 先生成一张左右屏关键帧设定图，确认“小鲅 X1 + 8-bit 状态面板”的视觉调性。
2. 生成 18-24 帧 V0.1 资源，优先保证流畅和体积。
3. 固件新增 `boot_intro` 资源检查和播放入口。
4. Web App 增加“开机动画预览/开关/快启动”配置项。
5. 真机验证冷启动、软重启、Brain 离线、未配网四种 outcome。
