# Atlas Rover Mk.1 V1.0 语音与双板 UART 控制方案

## 结论

若采用两块开发板或成品底盘，建议把 ESP32-S3-DualEye-Touch-LCD-1.28 定位为语音/HMI 主控：
它负责板载麦克风收音、命令词识别、双眼表情和 Wi-Fi/BLE 交互；底盘控制板负责电机驱动、限速、转向和安全停车。
两块板之间使用 3.3 V TTL UART 串口通信，指令用一行一条文本协议即可。
官方资料确认 DualEye 外露 UART：LCD1-Board SH1.0 14PIN 的 Pin 9 为 UART_RXD，Pin 10 为 UART_TXD。

## 系统链路

`语音命令 -> DualEye 解析 -> UART 文本指令 -> 底盘控制板 -> 电机驱动 -> 车轮/履带动作`

| 类型 | 项目 | 说明 |
| --- | --- | --- |
| 角色分工 | DualEye 板 | 语音入口、唤醒/命令词识别、双眼表情、Wi-Fi/BLE 交互、串口下发动作命令 |
| 角色分工 | 底盘控制板 | 接收 UART 指令，做限速/超时保护，控制普通 N20 + DRV8833 开环短时差速底盘 |
| 官方接口确认 | LCD1-Board SH1.0 14PIN | Pin 9 = UART_RXD，Pin 10 = UART_TXD，Pin 2/Pin 6 = GND，Pin 5 = 3V3；施工建议用 SH1.0 14P 转杜邦/排针转接线。 |
| 信号连接 | DualEye Pin10 UART_TXD -> 底盘板 RX | 3.3 V TTL 串口，短线即可；不要接 RS232 电平 |
| 信号连接 | DualEye Pin9 UART_RXD <- 底盘板 TX | 若底盘板 TX 为 5 V TTL，需要分压或电平转换后再进 DualEye RX |
| 信号连接 | DualEye Pin2/Pin6 GND <-> 底盘板 GND | 必须共地，否则串口和电机控制会不稳定 |
| 逻辑供电 | 5 V 支路 -> 底盘板 5V/VIN | 只在底盘板需要外部逻辑供电且接口允许 5 V 时使用；不要反灌 DualEye |
| 电机供电 | 电池/升压/底盘板 VM -> 电机驱动 | 电机电流不经过 DualEye；DualEye 只发命令 |
| 协议防误触发 | 命令必须以 AR1, 开头 | 底盘板丢弃所有不带 AR1 前缀的串口内容，避免 DualEye 启动日志、调试打印或乱码误触发电机。 |
| 安全策略 | 底盘板 300-500 ms 指令超时停车 | 语音识别卡住、串口断开或 DualEye 重启时，小车自动停下 |

## DualEye UART 接口针位

| DualEye 接口针位 | 信号 | 接法/用途 |
| --- | --- | --- |
| LCD1-Board SH1.0 14PIN Pin 10 | UART_TXD | 接底盘控制板 RX，DualEye 下发 AR1 运动/灯光指令 |
| LCD1-Board SH1.0 14PIN Pin 9 | UART_RXD | 接底盘控制板 TX；若底盘板 TX 是 5 V TTL，先分压或电平转换 |
| LCD1-Board SH1.0 14PIN Pin 2 或 Pin 6 | GND | 接底盘控制板 GND，必须共地 |
| LCD1-Board SH1.0 14PIN Pin 5 | 3V3 | 仅作 3.3 V 逻辑参考或低功耗外设供电；不要给底盘板/电机供电 |

## 推荐接线

```text
DualEye LCD1 Pin10 UART_TXD  -> 底盘板 RX
DualEye LCD1 Pin9  UART_RXD  <- 底盘板 TX
DualEye LCD1 Pin2/Pin6 GND   <-> 底盘板 GND
5 V 升压 OUT+                -> 底盘板 5V/VIN（仅当底盘板需要且允许 5 V）
电池/升压 VM                 -> 电机驱动 VM 或成品底盘电机电源端
```

注意：ESP32-S3 的 UART 是 3.3 V TTL。不要接 RS232 电平。若底盘板 TX 是 5 V TTL，进入 DualEye RX 前必须加分压或电平转换。
底盘板可以另接 5 V 逻辑电源，但电机供电不要从 DualEye 板上取。

## 推荐串口命令

| 命令示例 | 含义 |
| --- | --- |
| AR1,MOVE,F,60,800 | 前进，速度 60%，持续 800 ms |
| AR1,MOVE,B,50,500 | 后退，速度 50%，持续 500 ms |
| AR1,TURN,L,30,350 | 左轮后退/右轮前进或差速左转，速度 30%，持续 350 ms |
| AR1,TURN,R,30,350 | 左轮前进/右轮后退或差速右转，速度 30%，持续 350 ms |
| AR1,STOP | 立即停车 |
| AR1,LIGHT,BREATH | 切换前灯/状态灯呼吸效果 |

## 底盘板必须实现的安全逻辑

- 只解析以 `AR1,` 开头的命令；不带前缀的串口日志、调试打印、乱码全部丢弃。
- 收到 `AR1,STOP` 立即停车。
- 300-500 ms 内没有收到新的运动指令，自动停车。
- 上电默认停车，不执行上一次残留命令。
- 限制最大 PWM，首次调试建议不超过 40%。
- 若语音识别误触发，DualEye 可以先要求二次确认，例如“开始巡游”后才允许连续运动。

## 与当前 Mk.1 单板方案的关系

- Mk.1 先采用双板方案：DualEye 通过 UART 控制底盘板，底盘板再控制普通 N20 + DRV8833 + 前万向轮。
- 不上编码器/IMU 时，转向只做时间/PWM 标定，不承诺精确角度。
- 两种方案不要混接电机供电。无论哪种方案，电机电流都不从 DualEye 板取。
