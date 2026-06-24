# Atlas Board Profile V0

版本：`atlas.board.profile.v0`  
状态：草案，当前基准板为 Waveshare ESP32-S3-DualEye-Touch-LCD-1.28

## 1. 目标

把“具体硬件是什么”从“产品应用是什么”里拆出来。以后同一套桌面宠物 App 可以跑在不同板子上，不同产品也能复用同一套 Brain 和工具协议。

## 2. 最小结构

```json
{
  "protocol": "atlas.board.profile.v0",
  "board_id": "waveshare_dualeye_s3",
  "chip": "esp32s3",
  "flash_mb": 16,
  "psram_mb": 8,
  "display": {
    "type": "dual_round_lcd",
    "count": 2,
    "width": 240,
    "height": 240,
    "safe_area": "round_240",
    "left_rotation": 0,
    "right_rotation": 0
  },
  "audio": {
    "mic": {"enabled": true, "codec": "ES7210"},
    "speaker": {"enabled": true, "codec": "ES8311"},
    "sample_rate": 16000
  },
  "input": {
    "touch": true,
    "buttons": []
  },
  "network": {
    "wifi": true,
    "ble": true
  },
  "storage": {
    "spiffs": true,
    "sdcard": false
  },
  "motion": {
    "supported": false
  }
}
```

## 3. 分层规则

| 层 | 可以知道 | 不应该知道 |
|---|---|---|
| Board Profile | pin、屏幕、音频 codec、分区、外设 | 番茄、宠物性格、LLM 角色 |
| Firmware Runtime | 音频服务、Brain WS、资源加载、状态机 | 某个具体产品的 UI 风格 |
| Device App | 时钟、番茄、宠物、日历、对话 | 具体 I2S pin、LCD 初始化细节 |
| Atlas Brain | 设备能力、工具、Provider、会话 | 设备底层 pin 和驱动 |

## 4. DualEye 当前 profile

当前 DualEye 的事实：

| 项 | 值 |
|---|---|
| 芯片 | ESP32-S3 |
| 屏幕 | 双 1.28 寸 240x240 圆屏 |
| 音频 | 板载麦克风 + ES8311/外放链路 |
| 网络 | Wi-Fi/BLE |
| 存储 | 当前优先 SPIFFS，无 SD 卡 |
| 交互 | 触摸可用，Web 控制为主 |
| 运动 | 当前桌面宠物版关闭 |

## 5. App 能力依赖

| App | 必需能力 | 可选能力 |
|---|---|---|
| 双眼/宠物 | display、asset_loader | touch |
| 时钟 | display、time | ntp |
| 番茄 | display、timer | speaker |
| 日历 | display | provider/calendar |
| 对话 | display、mic、speaker、brain_ws | opus_stream |
| 天气 | display、brain_provider | speaker |
| 音乐/故事 | speaker、brain_provider | pet animation |

## 6. 兼容策略

短期不搬动 `firmware/dualeye/main`。后续拆分时遵守：

1. 先新增 board profile 文档和配置。
2. 再把驱动相关代码标记为 board 层。
3. 再把 runtime 代码迁出 common。
4. 每迁一个模块，保留旧 include wrapper 或兼容头文件。
5. 每一步都要求 `idf.py build` 通过，且 `/api/selftest` 通过。

## 7. 验收方法

新板子接入前必须回答：

- 屏幕是什么？安全区域是什么？
- 麦克风和喇叭是否真实可用？
- 资源包放在哪里？
- Brain WS 是否可用？
- 哪些工具可执行？
- Brain 离线时本地能保留哪些能力？

