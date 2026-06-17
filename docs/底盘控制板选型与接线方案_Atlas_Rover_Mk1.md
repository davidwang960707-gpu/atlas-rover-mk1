# 底盘控制板选型与接线方案 - Atlas Rover Mk.1

## 结论

Mk.1 底盘板推荐使用 `Seeed Studio XIAO ESP32C3`。

原因很简单：它足够小，能放进 120 x 90 mm 车架；逻辑电平是 3.3 V，能直接和 DualEye UART 通信；有 USB-C 便于单独烧录；有足够 GPIO/PWM 控制 DRV8833 的两个 N20 电机。

便宜替代是 `ESP32-C3 SuperMini`，但不同商家的丝印、GPIO 排布和 USB 下载方式不完全一致。第一版为了少踩坑，先按 XIAO ESP32C3 固化接线。

## 板卡职责

| 板卡 | 职责 | 不做什么 |
| --- | --- | --- |
| DualEye | 双眼显示、触摸、语音、MimiClaw、Web/API、运动意图裁剪 | 不直接驱动电机，不从自身给电机供电 |
| XIAO ESP32C3 底盘板 | 接收 `AR1,` UART 指令，执行限速、短时开环差速、超时停车，控制 DRV8833 | 不做语音理解，不跑大模型，不承诺精确距离/角度 |
| DRV8833 | 给两个 N20 电机输出电流和换向 | 不理解串口，不做安全逻辑 |

## 电源连接

推荐单节 18650 分两条支路：

```text
18650 BAT+ / BAT-
  ├─ 主控支路 -> DualEye 电池接口
  └─ 电机/底盘支路 -> 电机支路开关 -> 5 V 升压
                                      ├─ XIAO ESP32C3 5V / GND
                                      ├─ DRV8833 VM / GND
                                      └─ WS2812B 5V / GND
```

关键规则：

- DualEye 和底盘板必须共地。
- 电机电流只走 5 V 升压支路和 DRV8833，不经过 DualEye。
- 不要用 DualEye 的 3V3 给底盘板或电机供电。
- XIAO ESP32C3 可从 5 V 升压的 5V/VBUS 引脚供电；若电机干扰导致重启，再给底盘板单独加一个小 5 V/3.3 V 稳压支路。
- DRV8833 的 VM 附近建议并 470-1000 uF 电解电容和 0.1 uF 陶瓷电容。

## DualEye 到底盘板 UART

DualEye 使用 LCD1-Board SH1.0 14PIN 的外露 UART。施工时用 SH1.0 14P 转杜邦/排针转接线引出。

| DualEye LCD1 接口 | 信号 | XIAO ESP32C3 | 说明 |
| --- | --- | --- | --- |
| Pin 10 | UART_TXD | D7 / GPIO20 / RX | DualEye 发命令到底盘板 |
| Pin 9 | UART_RXD | D6 / GPIO21 / TX | 底盘板回 ACK 给 DualEye |
| Pin 2 或 Pin 6 | GND | GND | 必须共地 |
| Pin 5 | 3V3 | 不接供电 | 只作为 3.3 V 逻辑参考，不给底盘板供电 |

串口参数：

```text
115200 baud, 8 data bits, no parity, 1 stop bit
```

注意：

- 两边都是 3.3 V TTL，可直接连接。
- TX/RX 必须交叉：DualEye TX 接底盘板 RX，DualEye RX 接底盘板 TX。
- 若替换成 5 V TTL 底盘板，底盘板 TX 进 DualEye RX 前必须分压或电平转换。
- 底盘板固件不要把普通调试日志输出到这一路 UART，避免 DualEye 把日志当 ACK。调试优先走 USB CDC 或单独日志 UART。

## 底盘板到 DRV8833

推荐把左电机接 DRV8833 A 通道，右电机接 B 通道。

| XIAO ESP32C3 | ESP32-C3 GPIO | DRV8833 | 用途 |
| --- | --- | --- | --- |
| D2 | GPIO4 | AIN1 | 左轮正转 PWM |
| D3 | GPIO5 | AIN2 | 左轮反转 PWM |
| D4 | GPIO6 | BIN1 | 右轮正转 PWM |
| D5 | GPIO7 | BIN2 | 右轮反转 PWM |
| 3V3 | 3.3 V | nSLEEP/STBY/EN | 若模块有睡眠/使能脚，用 10 kΩ 上拉到 3V3 |
| GND | GND | GND | 共地 |

电机连接：

| DRV8833 | 接到 |
| --- | --- |
| AOUT1/AOUT2 | 左 N20 电机两根线 |
| BOUT1/BOUT2 | 右 N20 电机两根线 |
| VM | 5 V 升压输出正极 |
| GND | 5 V 升压输出负极 / 公共地 |

若前进时某一侧轮子反了，优先交换那一侧电机的两根线；也可以在底盘固件里配置左右轮反相。

## 差速动作定义

| 指令 | 左轮 | 右轮 | 说明 |
| --- | --- | --- | --- |
| `MOVE,F,speed,duration` | 前进 | 前进 | 直行前进 |
| `MOVE,B,speed,duration` | 后退 | 后退 | 直行后退 |
| `TURN,L,speed,duration` | 后退 | 前进 | 原地或小半径左转 |
| `TURN,R,speed,duration` | 前进 | 后退 | 原地或小半径右转 |
| `STOP` | 停止 | 停止 | 立即刹停或滑停 |

Mk.1 是普通 N20 + 前万向轮，没有编码器/IMU，所以左转/右转只按时间和 PWM 标定，不保证角度。

## UART 协议

DualEye 只发送以 `AR1,` 开头的一行文本：

```text
AR1,MOVE,F,40,500
AR1,MOVE,B,35,400
AR1,TURN,L,30,350
AR1,TURN,R,30,350
AR1,STOP
```

底盘板 ACK：

```text
AR1,ACK,OK
AR1,ACK,BUSY
AR1,ACK,ERR
```

解析规则：

- 只接受 `AR1,` 前缀。
- `MOVE` 方向只接受 `F` / `B`。
- `TURN` 方向只接受 `L` / `R`。
- `speed` 限制在 0-60，首次调试建议再限制到 40。
- `duration_ms` 限制在 50-1500，常用 300-800。
- 命令错误时不动，回 `AR1,ACK,ERR`。
- 收到 `STOP` 时无条件停车，回 `AR1,ACK,OK`。

## 底盘板固件安全逻辑

底盘板必须自己保护自己，不能假设 DualEye 永远正常。

1. 上电默认 `STOP`。
2. 每条运动命令都必须带 `duration_ms`，底盘板按 `now + duration_ms` 建立动作截止时间。
3. 到截止时间立即停车。
4. 速度超过上限时自动裁剪。
5. 运动中收到新运动指令，先覆盖旧动作并重新计算截止时间。
6. 运动中收到 `STOP`，立即停车。
7. 串口出现乱码、非 `AR1,` 内容、字段缺失，全部忽略或回 `ERR`，不能误触发电机。
8. 主循环或电机任务异常时，独立 watchdog 停车。

## 推荐采购项

| 名称 | 数量 | 推荐规格 |
| --- | --- | --- |
| Seeed Studio XIAO ESP32C3 | 1 | ESP32-C3，USB-C，3.3 V GPIO，外接天线版本 |
| SH1.0 14P 转杜邦/排针转接线 | 1-2 | 用于引出 DualEye LCD1 UART |
| DRV8833 双路 H 桥模块 | 1 | 2 路直流电机，VM 2.7-10.8 V |
| 5 V 升压模块 | 1 | 建议 1.5 A 以上余量 |
| 470-1000 uF 电容 | 1-2 | 并在 DRV8833 VM/GND 附近 |

## 第一版引脚锁定

```c
#define CHASSIS_UART_TX_GPIO 21  // XIAO D6 -> DualEye RX
#define CHASSIS_UART_RX_GPIO 20  // XIAO D7 <- DualEye TX

#define MOTOR_LEFT_IN1_GPIO   4  // XIAO D2 -> DRV8833 AIN1
#define MOTOR_LEFT_IN2_GPIO   5  // XIAO D3 -> DRV8833 AIN2
#define MOTOR_RIGHT_IN1_GPIO  6  // XIAO D4 -> DRV8833 BIN1
#define MOTOR_RIGHT_IN2_GPIO  7  // XIAO D5 -> DRV8833 BIN2
```

后续如果换 ESP32-C3 SuperMini，只改底盘板固件里的这些宏和接线表，DualEye 固件不需要改。
