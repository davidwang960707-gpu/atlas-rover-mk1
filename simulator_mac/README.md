# Atlas Rover Mk.1 Mac 双眼模拟器

这个模拟器用于在 Mac 上快速预览 DualEye 的双目表情、时钟页、语音页、状态页和设置页。

运行：

```bash
cmake -S simulator_mac -B simulator_mac/build -G Ninja
cmake --build simulator_mac/build
./simulator_mac/build/atlas_dualeye_sim
```

快捷键：

| 按键 | 功能 |
|---|---|
| `1` | 双眼主页 |
| `2` | 时钟页 |
| `3` | 小车状态页 |
| `4` | 语音页 |
| `5` | 设置页 |
| `h` | 开心 |
| `l` | 聆听 |
| `t` | 思考 |
| `s` | 说话 |
| `m` | 移动中 |
| `e` | 错误 |
| `space` | 停止，终端打印 `AR1,STOP` |
| 方向键 | 模拟移动命令，终端打印 `AR1,MOVE/TURN` |
| `q` / `Esc` | 退出 |
