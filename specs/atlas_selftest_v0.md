# Atlas Selftest V0

版本：`atlas.selftest.v0`  
状态：草案，兼容当前 DualEye `/api/selftest`

## 1. 目标

让设备在烧录后能自己回答三个问题：

1. 我刷进去的是不是期望版本？
2. 我的资源、字体、音频、网络、Brain、工具表是否可用？
3. 如果不可用，具体缺什么，下一步怎么修？

## 2. 接口

```text
GET /api/selftest
```

## 3. 最小结构

```json
{
  "ok": true,
  "protocol": "atlas.selftest.v0",
  "fingerprint": {
    "firmware_version": "0.14.x",
    "resource_version": "dualeye-assets-v0.x",
    "tool_schema_version": "atlas.tools.v0.desk_apps"
  },
  "summary": {
    "pass": 12,
    "warn": 1,
    "fail": 0
  },
  "checks": [
    {
      "id": "firmware_fingerprint",
      "status": "pass",
      "detail": "version 0.14.x"
    },
    {
      "id": "brain_ws",
      "status": "warn",
      "detail": "Brain offline, local apps available"
    }
  ],
  "manual_tests": [
    "/api/status/lite",
    "/api/tools/list",
    "/api/audio/opus-stream/status"
  ],
  "next_steps": [
    "确认 fail=0",
    "打开 Mac Brain /acceptance",
    "执行 tools/check_atlas_preflash.py"
  ]
}
```

## 4. 检查项

| id | 类型 | 必需 | 说明 |
|---|---|---|---|
| `firmware_fingerprint` | pass/fail | 是 | 固件版本、构建信息、资源版本 |
| `partition` | pass/warn/fail | 是 | app/storage/model 分区状态 |
| `spiffs` | pass/fail | 是 | 资源分区挂载 |
| `eye_assets` | pass/warn/fail | 是 | 眼睛主题关键资源 |
| `pet_head_assets` | pass/warn/fail | 是 | 宠物头关键资源 |
| `font_zh` | pass/warn/fail | 是 | 中文字体资源 |
| `display` | pass/warn/fail | 是 | 左右屏驱动已初始化 |
| `audio_codec` | pass/warn/fail | 是 | 麦克风/喇叭初始化 |
| `wifi` | pass/warn | 是 | AP/STA 状态 |
| `brain_ws` | pass/warn | 是 | Brain 离线是 warn，不是 fail |
| `tool_schema` | pass/fail | 是 | 工具表存在 |
| `opus_stream` | pass/warn | 是 | OPUS 支持与推流状态 |
| `sr_probe` | pass/warn | 是 | WakeNet/AEC 探针 |
| `experience_voice` | pass/warn/fail | 是 | 连续语音、播放恢复、失败原因是否可观测 |
| `experience_ui_modes` | pass/warn/fail | 是 | `pet_head`、`text`、`eyes_only` 与本地应用状态 |
| `display_surfaces` | pass/warn/fail | 是 | 当前页面的双屏预期和最近页面切换原因 |
| `offline_fallback` | pass/warn | 是 | Brain/Wi-Fi 离线时本地 UI 是否仍可用 |
| `experience_tools` | pass/fail | 是 | 工具调用面是否覆盖页面/表情/主题/番茄/日历/pet_head |
| `motion_boundary` | pass/warn | 否 | 桌面宠物版 motion 应明确关闭 |

## 5. 状态语义

| 状态 | 说明 |
|---|---|
| `pass` | 该项可用 |
| `warn` | 该项降级但不影响核心体验 |
| `fail` | 该项会导致核心体验不可用 |

Brain 离线、未接底盘、未启用 WakeNet/AEC 都不能轻易算 fail。fail 应留给资源缺失、固件不匹配、音频硬件不可用、关键 API 不存在。

## 6. 验收方法

```bash
curl http://DUALEYE_IP/api/selftest
python3 tools/check_atlas_preflash.py --brain-url http://127.0.0.1:8787 --dualeye-url http://DUALEYE_IP
```

验收通过条件：

- `summary.fail=0`
- `fingerprint.firmware_version` 符合本轮目标
- `tool_schema_version` 符合本轮目标
- 核心资源检查 `pass`
- Brain 离线时是 `warn` 且本地页面可用

体验线 P1/P2 要求 `/api/selftest` 同时暴露实机排障视角。`experience_voice` 不能只说明音频硬件是否存在，还要能看出 continuous 开关、最近 reason、播放后恢复动作和 runtime reason；`display_surfaces` 用于比对当前页面左右屏预期；`offline_fallback` 必须把 Brain 离线视为可降级状态，不应导致黑屏或通用异常文字页。
