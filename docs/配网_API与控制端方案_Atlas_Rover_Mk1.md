# Atlas Rover Mk.1 配网、API 与控制端方案 V0.2

本文档回答三个问题：

1. 现有 DualEye 程序和 MimiClaw/miniClaw 是一起烧录，还是分开烧录？
2. 烧录后如何配 Wi-Fi、配置大模型 API？
3. 用户手机端、电脑端是否需要控制和管理界面？

当前代码状态：

- DualEye 固件 V0.2 已落地 `atlas_config`、`atlas_wifi`、`atlas_admin_http`、`atlas_pairing`、`atlas_llm_client`、`atlas_mimiclaw_adapter` 骨架。
- 首次启动会在无 Wi-Fi 配置时开启 `AtlasRover-XXXX` SoftAP，管理地址为 `http://192.168.4.1`。
- Web 管理页已支持状态查看、Wi-Fi 配置、LLM/API 配置、安全配置、STOP、短时移动和文本意图测试。
- 真实 LLM 网络请求、真机双屏渲染、音频链路和底盘板固件尚未完成。

## 1. 烧录边界结论

Atlas Rover Mk.1 不按“表情程序一份、MimiClaw 再一份”拆烧录，而按硬件设备边界拆：

| 设备 | 烧录内容 | 说明 |
|---|---|---|
| DualEye 板 | 一份 DualEye 固件 | 包含双目 UI、表情、触摸、音频、Wi-Fi/BLE、配网、管理界面、语音事件入口、MimiClaw 适配层、UART 底盘协议 |
| 底盘板 | 一份底盘固件 | 包含电机控制、DRV8833/PWM、限速、加减速、超时停车、ACK 回传 |
| Mac/电脑宿主 | 不烧录到 DualEye | 如果选择 MiniClaw 宿主模式，MiniClaw 跑在 Mac/电脑上，DualEye 通过 Wi-Fi HTTP/WebSocket 与宿主通信 |

所以：

- **端侧 MimiClaw 模式**：MimiClaw 作为 DualEye 固件组件，一起编译、一起烧录。
- **电脑宿主 MiniClaw 模式**：MiniClaw 不烧进 DualEye，只在电脑上运行；DualEye 只烧 HMI/语音终端固件。
- **当前 Mk.1 推荐**：先做“DualEye 固件 + 可选电脑宿主”，等真机屏幕、音频、PSRAM、Flash 使用量验证后，再决定是否把 MimiClaw 完整端侧化。

## 2. 推荐三阶段方案

| 阶段 | 模式 | 为什么这样做 |
|---|---|---|
| V0.1 | DualEye 本地命令 + UART | 先保证双目、页面、STOP、前进/后退/转向等基础链路稳定 |
| V0.2 | DualEye 配网 + 手机/电脑 Web 管理界面 | 已完成基础骨架：SoftAP/STA、NVS、Web 管理页、配对码、安全配置 |
| V0.3 | MiniClaw 宿主或 MimiClaw 端侧 | 再接自然语言和大模型，避免一开始把硬件驱动、网络、语音、LLM 全绑在一起排错 |

## 3. 首次配网流程

烧录后，DualEye 应按以下逻辑启动：

```text
启动
  -> 从 NVS 读取 Wi-Fi 配置
  -> 有配置：尝试连接路由器
  -> 连接成功：显示 IP / 状态页
  -> 连接失败或无配置：进入配网模式
```

配网模式建议：

| 项目 | 方案 |
|---|---|
| SoftAP 名称 | `AtlasRover-XXXX`，XXXX 取 MAC 后 4 位 |
| 默认地址 | `http://192.168.4.1` |
| 入口提示 | DualEye 双屏显示 Wi-Fi 名称、IP、二维码或短提示 |
| 手机配网 | 手机连接 `AtlasRover-XXXX`，浏览器打开 `192.168.4.1` |
| 电脑配网 | 电脑连接同一 AP，浏览器打开 `192.168.4.1` |
| 可选增强 | 后续增加 mDNS / BLE Provisioning，手机体验更好 |
| 重置配网 | 当前 V0.2 通过 Web 管理页清除 Wi-Fi/API；长按触摸和串口命令后续再补 |

当前 V0.2 暂未硬编码 `atlas-rover.local`。原因是本地 ESP-IDF 工程里尚未确认可直接启用稳定的 mDNS 组件，先使用串口日志/页面显示的 IP 地址，避免文档写得比固件更超前。

Wi-Fi 配置保存：

- SSID 和密码保存在 ESP32 NVS。
- 不在日志里打印 Wi-Fi 密码。
- 不写入固件源码。
- 不提交到 GitHub。

## 4. 大模型 API 配置

API 配置不应该写死在固件里，也不应该进入仓库。正确方式是通过本地 Web 管理界面写入 NVS。

建议配置项：

| 配置项 | 说明 |
|---|---|
| LLM 模式 | `off` / `cloud` / `host` / `embedded` |
| Provider | OpenAI-compatible、DeepSeek、通义、硅基流动、本地 Ollama 等都可抽象成 provider |
| Base URL | API 网关或宿主地址，例如云端 HTTPS，或 `http://电脑IP:端口` |
| Model | 模型名称 |
| API Key | 只保存在设备 NVS 或电脑宿主，不写源码 |
| 语音语言 | 中文优先，后续可增加英文 |
| 最大回复长度 | 防止小设备长时间等待 |
| 工具调用开关 | 默认只开安全白名单 |

API Key 处理原则：

- Web 页面输入时只显示一次，保存后只显示“已配置”。
- 串口日志中永远不输出 API Key。
- 导出配置时默认不导出 API Key。
- 清除配置必须提供明确按钮。
- 若启用 NVS 加密，则把 API Key 放进加密 NVS；否则 README 明确标记为“原型阶段，不适合存放高价值密钥”。

## 5. 大模型不能直接控制电机

无论使用云端大模型、MiniClaw 宿主，还是端侧 MimiClaw，都必须遵守：

```text
用户语音/文本
  -> ASR/LLM/MimiClaw
  -> 结构化 RoverIntent
  -> 本地 Intent Router
  -> Safety Guard
  -> UART AR1 指令
  -> 底盘板
```

禁止：

- LLM 直接输出任意 UART 字符串。
- LLM 直接设置高速度、长时间运动。
- LLM 绕过 STOP、超时停车、低电、底盘离线检查。
- 云端返回内容直接作为电机指令执行。

运动类工具只开放白名单：

```text
rover.move(direction, speed_percent, duration_ms)
rover.turn(direction, speed_percent)
rover.stop()
eyes.set_expression(expression_id)
ui.set_page(page_id)
lights.set_mode(mode_id)
system.get_status()
```

安全裁剪建议：

| 项目 | 默认限制 |
|---|---|
| 最大速度 | 40% 起步，调试后再提高 |
| 单次运动时长 | 500-700 ms |
| 连续运动 | 必须由底盘板和 DualEye 双重超时保护 |
| STOP | 最高优先级，不需要确认 |
| 低置信度语义 | 只澄清，不动车 |
| 高风险指令 | 拒绝或要求二次确认 |

## 6. 手机端和电脑端控制/管理界面

需要。第一版不做原生 App，做响应式 Web 管理界面即可，手机和电脑都能用。

推荐由 DualEye 自己提供局域网 Web UI：

```text
手机/电脑浏览器
  -> DualEye SoftAP 地址或局域网 IP
  -> DualEye 内置 Web 管理界面
```

例如首次配网使用 `http://192.168.4.1`；连入路由器后使用串口日志中的局域网 IP，例如 `http://192.168.1.23`。mDNS 稳定后再补 `atlas-rover.local`。

### 页面规划

| 页面 | 手机端 | 电脑端 |
|---|---|---|
| 首次配网 | 输入 Wi-Fi、设备名 | 同手机 |
| 总览 Dashboard | 电量、Wi-Fi、底盘在线、当前表情、STOP | 更完整日志和状态 |
| 手动控制 | 大 STOP、方向按钮、速度滑条、短时移动 | 方向键/按钮、串口日志、ACK |
| 表情/页面 | 切表情、切时钟、切主题 | 表情调试、参数观察 |
| 语音/LLM 设置 | 开关语音、选择模式、填 API Key | 调试 prompt、工具调用、测试连接 |
| 安全设置 | 最大速度、运动时长、是否允许语音移动 | 更细的策略和日志 |
| 系统设置 | 重启、清配置、固件版本 | OTA、日志导出、诊断 |

手机端重点是“安全可控”：

- STOP 按钮常驻。
- 默认短按移动，不做长时间连续巡航。
- 运动按钮松开即停，或每条指令自带短超时。
- 显示底盘连接状态，离线时禁用移动按钮。

电脑端重点是“调试可见”：

- 显示 UART TX/RX。
- 显示最近 LLM 工具调用。
- 显示表情状态、页面状态、Wi-Fi 状态。
- 可导出日志，但不导出 API Key。

## 7. 本地鉴权与安全

管理界面不能默认裸奔。建议第一版使用轻量配对码：

1. 首次进入管理页面时，DualEye 双屏显示 6 位配对码。
2. 手机/电脑输入配对码。
3. 设备在 NVS 保存一个本地会话 token。
4. 重置配置时清除 token。

限制：

- 默认只允许局域网访问。
- 不做公网端口映射。
- 远程控制默认关闭。
- OTA 和清配置按钮必须二次确认。

## 8. 配置数据结构建议

```json
{
  "device": {
    "name": "Atlas Rover Mk.1",
    "pairing_enabled": true
  },
  "wifi": {
    "mode": "sta",
    "ssid": "stored-in-nvs",
    "password": "stored-in-nvs"
  },
  "llm": {
    "mode": "off",
    "provider": "openai_compatible",
    "base_url": "",
    "model": "",
    "api_key": "stored-in-nvs"
  },
  "safety": {
    "motion_enabled": false,
    "max_speed_percent": 40,
    "max_duration_ms": 700,
    "require_confirm_for_patrol": true
  },
  "ui": {
    "theme": "atlas_blue",
    "brightness": 70,
    "volume": 60
  }
}
```

## 9. 固件模块规划与当前落地

V0.1 已有：

- `atlas_expression.*`
- `atlas_display.*`
- `atlas_rover_uart.*`
- `atlas_voice.*`
- `atlas_ui.*`

V0.2 已新增：

| 模块 | 职责 |
|---|---|
| `atlas_config.*` | NVS 配置读写、默认值、清配置 |
| `atlas_wifi.*` | STA/AP 配网、连接状态、mDNS |
| `atlas_admin_http.*` | 手机/电脑 Web 管理界面和 REST API |
| `atlas_pairing.*` | 本地配对码、会话 token、权限检查 |
| `atlas_llm_client.*` | 云端/宿主 API 调用封装，不直接输出运动指令 |
| `atlas_mimiclaw_adapter.*` | 把 MimiClaw/MiniClaw 结果转为 `atlas_voice_intent_t` |

当前限制：

- `atlas_llm_client.*` 目前只做配置状态和就绪判断，还没有发起真实 HTTPS/HTTP LLM 请求。
- `atlas_mimiclaw_adapter.*` 当前先做本地关键词意图识别；遇到无法本地理解但 LLM 已配置时，会进入 `thinking` 安全占位，不会直接控制电机。
- 保存 Wi-Fi 后建议重启连接 STA；运行时热切 Wi-Fi 后续再补。
- API Key 存入 NVS，但原型阶段尚未启用 NVS 加密，建议只使用低风险测试 Key。

## 10. 对当前问题的直接回答

1. **现有程序和 MimiClaw 是否一起烧录？**  
   如果 MimiClaw 端侧运行，就和 DualEye 程序一起编译成一份 DualEye 固件烧录；如果 MiniClaw 跑在 Mac/电脑上，就不烧进 DualEye，DualEye 只通过 Wi-Fi 与宿主通信。底盘板永远是另一份独立固件。

2. **烧录后怎么配网和配大模型 API？**  
   第一版应进入 SoftAP 配网模式，手机/电脑连 `AtlasRover-XXXX`，打开 `192.168.4.1`，配置 Wi-Fi；入网后先使用串口日志中的设备 IP，配置 LLM 模式、Base URL、Model、API Key。API Key 只进 NVS，不进固件源码和 GitHub。`atlas-rover.local` 等 mDNS 入口后续确认稳定后再补。

3. **是否需要手机端/电脑端控制和管理界面？**  
   需要，而且应该优先做 Web 管理界面，不先做原生 App。手机端负责安全控制和配置；电脑端负责调试、日志、工具调用观察。STOP 按钮必须常驻，运动控制必须短时、限速、可随时打断。
