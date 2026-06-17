# Atlas Rover Mk.1 XIAO ESP32C3 底盘固件

这份固件烧录到 `Seeed Studio XIAO ESP32C3`，只负责底盘实时动作：

- 接收 DualEye 通过 UART 发来的 `AR1,` 指令。
- 控制两块 DRV8833 双路 H 桥驱动四个普通 N20 减速电机；固件逻辑仍按左/右两侧差速控制。
- 实现限速、动作时长截止、无效命令拒绝和 STOP 急停。
- 忽略所有不带 `AR1,` 前缀的串口内容，避免 DualEye 启动日志或乱码误触发电机。

## 接线

DualEye 到 XIAO：

```text
DualEye LCD1 Pin10 UART_TXD  -> XIAO D7 / GPIO20 / RX
DualEye LCD1 Pin9  UART_RXD  <- XIAO D6 / GPIO21 / TX
DualEye LCD1 Pin2/Pin6 GND   <-> XIAO GND
```

XIAO 到两块 DRV8833：

```text
XIAO D2 / GPIO4  -> 前 DRV8833 AIN1 + 后 DRV8833 AIN1
XIAO D3 / GPIO5  -> 前 DRV8833 AIN2 + 后 DRV8833 AIN2
XIAO D4 / GPIO6  -> 前 DRV8833 BIN1 + 后 DRV8833 BIN1
XIAO D5 / GPIO7  -> 前 DRV8833 BIN2 + 后 DRV8833 BIN2
```

电机输出：

```text
前 DRV8833 AOUT1/AOUT2 -> 左前 N20
前 DRV8833 BOUT1/BOUT2 -> 右前 N20
后 DRV8833 AOUT1/AOUT2 -> 左后 N20
后 DRV8833 BOUT1/BOUT2 -> 右后 N20
```

供电规则：

- DualEye 用自己的 3.7 V 电池接口或合规 5 V 输入供电。
- XIAO 可由 5 V 升压支路供电。
- 两块 DRV8833 VM 都接电机电源支路，电机电流不要经过 DualEye。
- DualEye、XIAO、两块 DRV8833 必须共地。
- 四个 N20 同时启动需要更大余量，5 V 升压建议 4-5 A 峰值；每块 DRV8833 的 VM/GND 附近并 470-1000 uF 电容。

## UART 协议

波特率 `115200`，8N1，一行一条命令：

```text
AR1,STOP
AR1,MOVE,F,40,500
AR1,MOVE,B,35,400
AR1,TURN,L,30,350
AR1,TURN,R,30,350
```

底盘板 ACK：

```text
AR1,ACK,OK
AR1,ACK,ERR
```

`MOVE` 和 `TURN` 必须带速度百分比和持续时间。Mk.1 四轮版使用普通 N20 + 两块 DRV8833，左前/左后视为左侧逻辑轮，右前/右后视为右侧逻辑轮，是开环短时差速方案，不承诺精确距离或精确角度。

## 构建与烧录

```bash
cd firmware/chassis_xiao_esp32c3
export IDF_PATH="$HOME/.espressif/esp-idf-v5.5.2"
. "$IDF_PATH/export.sh"
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

烧录后建议先让车轮悬空测试：

```text
AR1,MOVE,F,25,300
AR1,TURN,L,25,250
AR1,STOP
```
