# Atlas Device Status V0

版本：`atlas.device.status.v0`  
状态：草案，兼容当前 DualEye `/api/status` 与 `/api/status/lite`

## 1. 目标

统一描述一个 Atlas 设备的真实状态，让固件、Atlas Brain、Web 控制端、验收脚本看到同一套事实。

这份状态不是给用户直接看的 UI 文案，而是平台状态源。TOC 页面、管理后台、日志、自检都应从它派生。

## 2. 适用范围

- DualEye 桌面宠物。
- 后续 AI 音箱、AI 相框、智能摆件、机器人底盘等设备。
- HTTP 状态接口和 WebSocket `state` 事件。

## 3. 最小结构

```json
{
  "ok": true,
  "protocol": "atlas.device.status.v0",
  "device_id": "dualeye",
  "product_id": "atlas_dualeye_pet",
  "fingerprint": {
    "firmware_version": "0.14.x",
    "resource_version": "dualeye-assets-v0.x",
    "tool_schema_version": "atlas.tools.v0.desk_apps",
    "build_date": "2026-06-24"
  },
  "wifi": {
    "mode": "sta",
    "connected": true,
    "sta_ip": "192.168.3.60",
    "ap_ip": "192.168.4.1",
    "ssid": "鲁豫有约",
    "rssi": -55
  },
  "brain_ws": {
    "enabled": true,
    "connected": true,
    "stage": "ready",
    "url": "ws://192.168.3.53:8787/ws/brain",
    "last_error": null,
    "last_seen_ms": 1240
  },
  "scene": {
    "page": "chat",
    "expression": "listen",
    "severity": "ok",
    "label": "聆听中",
    "reason": null
  },
  "audio_service": {
    "mode": "listening",
    "playing": false,
    "recording": false,
    "muted": false,
    "last_failure": null
  },
  "ui": {
    "theme": "pet",
    "chat_mode": "pet_head",
    "page": "chat"
  },
  "apps": {
    "protocol": "atlas.desk_apps.v0",
    "clock": {"enabled": true, "synced": true},
    "pomodoro": {"enabled": true, "running": false},
    "calendar": {"enabled": true},
    "pet": {"enabled": true, "state": "idle"}
  },
  "capabilities": {
    "display": true,
    "dual_round_screen": true,
    "mic": true,
    "speaker": true,
    "touch": true,
    "opus": true,
    "brain_ws": true,
    "motion": false
  }
}
```

## 4. 字段要求

| 字段 | 要求 |
|---|---|
| `protocol` | 新接口必须返回；旧接口可缺省，Brain 侧应兼容 |
| `fingerprint` | 必须可定位固件、资源、工具表版本 |
| `scene` | 必须是用户感知状态源，不能只返回内部错误码 |
| `brain_ws` | Brain 离线时必须说明原因，但不能让本地页面黑屏 |
| `audio_service` | 必须暴露录音、播放、mute、失败原因 |
| `apps` | 只放真实可用或明确降级的应用，不放纯 mock |
| `capabilities` | 描述能力，不描述愿望；未完成能力必须 false 或 experimental |

## 5. 兼容策略

当前固件已经有：

- `/api/status`
- `/api/status/lite`
- `/api/capabilities`
- `/api/system/info`

V0 不要求立刻改动字段名，但新增功能必须优先补进上述结构。Web 端读取时按：

1. 优先读 `/api/status/lite` 做手机端快速状态。
2. 管理端读 `/api/status`。
3. 缺字段时回退旧字段，不直接报错。

## 6. 错误与降级

| 场景 | 状态要求 |
|---|---|
| Brain 离线 | `brain_ws.connected=false`，`scene.severity=warn`，本地页面继续可用 |
| 资源缺失 | `scene.severity=warn/error`，`selftest` 说明缺哪个资源 |
| 音频失败 | `audio_service.last_failure` 必须有 `stage/code/message` |
| Wi-Fi 断开 | 显示配网/离线场景，不进入通用异常页 |

## 6.1 配网与配置兼容

固件 common 层以 `atlas.config.v0` 和 `atlas.wifi.provisioning.v0` 固化 Wi-Fi/AP fallback、Brain URL、Pairing PIN、Provider 配置摘要的边界。现有 `/api/config/wifi`、`/api/wifi/scan`、`/api/config/llm`、`/api/status`、`/api/status/lite` 字段保持兼容；后续新增配置字段只能作为可选字段追加，不能删除或重命名既有字段。

无 Wi-Fi 配置、STA 连接失败或 Brain 离线时，设备必须继续保留 SoftAP/本地 UI 降级路径。双眼、时钟、番茄、日历、宠物头页面仍由本地 app state 驱动，不依赖 Brain 在线。

## 7. 验收方法

```bash
curl http://DUALEYE_IP/api/status/lite
curl http://DUALEYE_IP/api/status
curl http://DUALEYE_IP/api/capabilities
```

最小验收：

- `fingerprint` 存在。
- `scene` 存在。
- `apps` 存在。
- `audio_service` 存在。
- Brain 离线时本地时钟、番茄、日历、双眼仍可切换。
