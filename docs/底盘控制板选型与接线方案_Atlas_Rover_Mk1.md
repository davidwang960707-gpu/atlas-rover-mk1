# 底盘控制板选型与接线方案 - Atlas Rover Mk.1

## 结论

Mk.1 底盘板推荐使用 `Seeed Studio XIAO ESP32C3`。

原因很简单：它足够小，能放进 120 x 90 mm 车架；逻辑电平是 3.3 V，能直接和 DualEye UART 通信；有 USB-C 便于单独烧录；有足够 GPIO/PWM 控制两块 DRV8833。四电机版里，XIAO 仍然只输出左/右两侧逻辑控制信号，前后同侧驱动板输入并联。

便宜替代是 `ESP32-C3 SuperMini`，但不同商家的丝印、GPIO 排布和 USB 下载方式不完全一致。第一版为了少踩坑，先按 XIAO ESP32C3 固化接线。

## 板卡职责

| 板卡 | 职责 | 不做什么 |
| --- | --- | --- |
| DualEye | 双眼显示、触摸、语音、MimiClaw、Web/API、运动意图裁剪 | 不直接驱动电机，不从自身给电机供电 |
| XIAO ESP32C3 底盘板 | 接收 `AR1,` UART 指令，执行限速、短时开环差速、超时停车，控制两块 DRV8833 | 不做语音理解，不跑大模型，不承诺精确距离/角度 |
| DRV8833 x 2 | 前后各一块，给四个 N20 电机输出电流和换向 | 不理解串口，不做安全逻辑，不直接并联四电机到一块板 |

## 电源连接

推荐单节 18650 分两条支路：

```text
18650 BAT+ / BAT-
  ├─ 主控支路 -> DualEye 电池接口
  └─ 电机/底盘支路 -> 电机支路开关 -> 5 V 升压
                                      ├─ XIAO ESP32C3 5V / GND
                                      ├─ 前 DRV8833 VM / GND
                                      ├─ 后 DRV8833 VM / GND
                                      └─ WS2812B 5V / GND
```

关键规则：

- DualEye 和底盘板必须共地。
- 电机电流只走 5 V 升压支路和 DRV8833，不经过 DualEye。
- 不要用 DualEye 的 3V3 给底盘板或电机供电。
- XIAO ESP32C3 可从 5 V 升压的 5V/VBUS 引脚供电；若电机干扰导致重启，再给底盘板单独加一个小 5 V/3.3 V 稳压支路。
- 两块 DRV8833 的 VM 附近都建议并 470-1000 uF 电解电容和 0.1 uF 陶瓷电容。
- 四个 N20 同时启动会明显增加电流，5 V 升压模块建议 4-5 A 峰值能力，连续电流至少 2-3 A；单节 18650 建议持续放电 >= 8 A。

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

## 底盘板到两块 DRV8833

四电机版推荐使用两块 DRV8833：前 DRV8833 管左前/右前，后 DRV8833 管左后/右后。两块驱动板的输入脚并联，固件仍按左右两侧差速控制。

| XIAO ESP32C3 | ESP32-C3 GPIO | 前 DRV8833 | 后 DRV8833 | 用途 |
| --- | --- | --- | --- | --- |
| D2 | GPIO4 | AIN1 | AIN1 | 左侧正转 PWM |
| D3 | GPIO5 | AIN2 | AIN2 | 左侧反转 PWM |
| D4 | GPIO6 | BIN1 | BIN1 | 右侧正转 PWM |
| D5 | GPIO7 | BIN2 | BIN2 | 右侧反转 PWM |
| 3V3 | 3.3 V | nSLEEP/STBY/EN | nSLEEP/STBY/EN | 若模块有睡眠/使能脚，用 10 kΩ 上拉到 3V3 |
| GND | GND | GND | GND | 共地 |

电机连接：

| 驱动板 | A 通道 | B 通道 | 电源 |
| --- | --- | --- | --- |
| 前 DRV8833 | 左前 N20 两根线 | 右前 N20 两根线 | VM/GND 接 5 V 电机支路 |
| 后 DRV8833 | 左后 N20 两根线 | 右后 N20 两根线 | VM/GND 接 5 V 电机支路 |

若前进时某一侧轮子反了，优先交换那一侧电机的两根线；也可以在底盘固件里配置左右轮反相。

不建议把左前/左后两个电机直接并联到同一个 DRV8833 A 通道、右前/右后并联到 B 通道作为正式方案。空载短测可以，但长期运行时启动电流、左右负载差和散热都不可控。

## 差速动作定义

| 指令 | 左轮 | 右轮 | 说明 |
| --- | --- | --- | --- |
| `MOVE,F,speed,duration` | 左前/左后前进 | 右前/右后前进 | 直行前进 |
| `MOVE,B,speed,duration` | 左前/左后后退 | 右前/右后后退 | 直行后退 |
| `TURN,L,speed,duration` | 左前/左后后退 | 右前/右后前进 | 原地或小半径左转 |
| `TURN,R,speed,duration` | 左前/左后前进 | 右前/右后后退 | 原地或小半径右转 |
| `STOP` | 两个左侧电机停止 | 两个右侧电机停止 | 立即刹停或滑停 |

Mk.1 是普通 N20 + 四轮差速，没有编码器/IMU，所以左转/右转只按时间和 PWM 标定，不保证角度。四轮转向需要轮胎轻微打滑，转向速度建议先限制在 20-35%。

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
| DRV8833 双路 H 桥模块 | 2 | 2 路直流电机，VM 2.7-10.8 V；前后一块 |
| N20 金属减速电机 | 4 | 6 V，100-150 RPM，3 mm D 轴 |
| 64T 3 mm D 孔齿轮轮 | 4 | 外径 30-36 mm，外圈套 O 型圈/热缩圈 |
| 5 V 升压模块 | 1 | 建议 4-5 A 峰值，连续至少 2-3 A |
| 470-1000 uF 电容 | 2-4 | 每块 DRV8833 的 VM/GND 附近至少 1 个 |

## 第一版引脚锁定

```c
#define CHASSIS_UART_TX_GPIO 21  // XIAO D6 -> DualEye RX
#define CHASSIS_UART_RX_GPIO 20  // XIAO D7 <- DualEye TX

#define MOTOR_LEFT_IN1_GPIO   4  // XIAO D2 -> 前/后 DRV8833 AIN1
#define MOTOR_LEFT_IN2_GPIO   5  // XIAO D3 -> 前/后 DRV8833 AIN2
#define MOTOR_RIGHT_IN1_GPIO  6  // XIAO D4 -> 前/后 DRV8833 BIN1
#define MOTOR_RIGHT_IN2_GPIO  7  // XIAO D5 -> 前/后 DRV8833 BIN2
```

后续如果换 ESP32-C3 SuperMini，只改底盘板固件里的这些宏和接线表，DualEye 固件不需要改。若 Mk.2 要做四轮独立速度补偿，才需要增加到 8 路电机控制输出并重写底盘固件。
