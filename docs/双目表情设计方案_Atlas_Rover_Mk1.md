# Atlas Rover Mk.1 双目表情设计方案 V0.2

本文档定义 Atlas Rover Mk.1 的双实体圆屏表情方案。核心约束是：左侧实体屏只显示左眼，右侧实体屏只显示右眼；两块 1.28 英寸圆屏组合成一个“脸”，不要在单块屏幕里再绘制一对小眼睛。

## 1. 开源方案调研结论

| 参考项目 | 可借鉴点 | 对 Atlas 的取舍 |
|---|---|---|
| [FluxGarage/RoboEyes](https://github.com/FluxGarage/RoboEyes) | 参数化眼睛、mood、自动眨眼、idle、小幅随机运动，适合低算力实时绘制。 | 优先借鉴。Atlas 用“参数模型 + 状态机”实现，不直接复制代码。 |
| [Adafruit Uncanny Eyes](https://github.com/adafruit/Uncanny_Eyes) | 真实虹膜、瞳孔跟踪、上下眼睑遮罩、眨眼同步，视觉生命感强。 | 借鉴眼睑遮罩和瞳孔跟随，但不走写实眼球路线，避免和铜丝复古车架风格冲突。 |
| [playfultechnology/esp32-eyes](https://github.com/playfultechnology/esp32-eyes) | Face/Emotion/Expression/Look/Blink 分层清晰，支持随机情绪和看向指定位置。 | 借鉴架构。该项目为 GPL-3.0，固件里不直接复用源码。 |
| [upiir/dual_lcd_robot_eyes](https://github.com/upiir/dual_lcd_robot_eyes) | 双圆屏机器人眼睛的素材/转换流程，适合观察实体双屏效果。 | 借鉴双屏构图和素材转数组流程；Atlas 推荐先用参数绘制，后续可补位图主题。 |
| [LVGL PC VSCode Simulator](https://github.com/lvgl/lv_port_pc_vscode) / [LVGL](https://github.com/lvgl/lvgl) | 桌面模拟 UI，再迁移到 MCU；LVGL 可运行在 PC 和嵌入式端。 | 后续正式固件建议用 LVGL 组件化页面，当前 Web Preview 用来快速预览表情语言。 |
| [Waveshare DualEye 官方资料](https://docs.waveshare.com/ESP32-S3-DualEye-Touch-LCD-1.28) | 双 1.28 英寸 LCD、每屏 240 x 240、支持 LVGL、I2C 触摸、音频和 UART/I2C/IO 引出。 | 每只眼按 240 x 240 圆形画布设计，边缘保留 12-18 px 安全区。 |

## 2. 视觉原则

1. 单屏单眼：每个实体屏幕只画一只眼睛。左屏 = 左眼，右屏 = 右眼。
2. 卡通机械感：黑底、青蓝主发光、薄荷绿积极反馈、琥珀色充电/提示、红橙色错误/拒绝。
3. 表情靠几何变化表达：眼睑开合、虹膜大小、瞳孔位置、眉线倾角、扫描线、外圈脉冲。
4. 不滥用文字：表情屏本身只显示眼睛；状态、设置、时钟等页面才显示图形信息。
5. 安全状态优先：停车、错误、低电、电机超时优先级高于卖萌表情。

## 3. 表情参数模型

建议固件内部使用统一参数结构，不为每个表情写死整屏位图。

```c
typedef struct {
    float look_x;        // -1.0 左, 0 中, 1.0 右
    float look_y;        // -1.0 上, 0 中, 1.0 下
    float iris_scale;    // 虹膜缩放
    float pupil_scale;   // 瞳孔缩放
    float top_lid;       // 上眼睑开合
    float bottom_lid;    // 下眼睑开合
    float brow_tilt;     // 眉线倾角
    uint32_t accent_rgb; // 当前强调色
    uint8_t effect;      // none / scan / pulse / charge / shake
} atlas_eye_pose_t;
```

每个表情是左右两只眼的 `atlas_eye_pose_t` 加一组动画参数。这样同一个渲染器可以同时服务双眼、时钟页、语音页和状态页。

## 4. 表情库 V0.1

| ID | 中文名 | 视觉设计 | 触发场景 | 优先级 |
|---|---|---|---|---|
| idle | 待机 | 两眼居中，轻微呼吸缩放，偶发眨眼，左右目光略有内聚。 | 默认状态、停止后 1-2 秒回落。 | 低 |
| happy | 开心 | 两眼变成上扬弧线，薄荷绿发光，短促弹性动画。 | 指令执行成功、触摸互动成功。 | 中 |
| listen | 聆听 | 虹膜放大，外圈脉冲，亮度随麦克风 RMS 微动。 | 唤醒词触发、正在收音。 | 中 |
| thinking | 思考 | 半睁眼，左眼看左上、右眼看右上，扫描线缓慢扫过。 | miniClaw/MimiClaw 正在理解指令。 | 中 |
| speaking | 说话 | 虹膜随 TTS 音量跳动，下眼睑轻微抖动。 | 语音回复播放时。 | 中 |
| moving | 移动 | 半专注眼，目光按底盘指令偏移：前进向上、后退向下、左转向左、右转向右。 | UART 指令已下发且底盘 ACK。 | 高 |
| surprised | 惊讶 | 双眼全开、虹膜放大、瞳孔缩小，瞬时亮一下。 | 避障急停、被拿起、用户突然触碰。 | 高 |
| curious | 好奇 | 左眼略大右眼略小，左右眼轻微不同步，形成偏头感。 | 听到不确定指令、等待用户确认。 | 中 |
| sleepy | 困倦 | 上眼睑压低，目光下垂，亮度降低。 | 长时间空闲、低功耗准备。 | 低 |
| wink | 眨眼 | 左眼闭合成弧线，右眼微微放大。 | 任务完成、小彩蛋、Wi-Fi 连接成功。 | 低 |
| angry | 拒绝 | 红橙色、斜眉、半眯眼。 | 危险/不支持指令、底盘保护触发。 | 高 |
| charging | 充电 | 琥珀色外圈从下向上填充，眼神放松。 | 插电、充电中。 | 中 |
| error | 错误 | 两眼变成红色 X 或断线，短闪 2 次后进入安全页。 | UART 断连、电池异常、驱动故障。 | 最高 |

## 5. 页面与表情状态映射

| 系统状态 | 页面 | 表情 | 底盘控制 |
|---|---|---|---|
| 空闲待机 | 双眼页 | idle | 不发移动指令 |
| 唤醒/收音 | 双眼页或语音页 | listen | 不发移动指令 |
| 指令理解 | 双眼页 | thinking | 不发移动指令 |
| 指令确认成功 | 双眼页 | happy -> moving | 通过 UART 发送 AR1 指令 |
| 移动中 | 双眼页 | moving | 等待 ACK/超时停车 |
| 停止 | 双眼页 | surprised 或 idle | 发送 `AR1,STOP` |
| 低电/异常 | 状态页 | error | 强制 STOP |
| 充电 | 状态页 | charging | 禁止移动 |
| 时钟模式 | 时钟页 | 无眼睛表情或低频眨眼角标 | 不发移动指令 |

## 6. 主题皮肤候选 V0.2

Web 评审阶段先保留 5 套主题，后续接入 Waveshare DualEye 官方 LCD/LVGL 初始化时，优先把这些颜色抽成 LVGL style token，而不是在每个控件里写死颜色。

| 主题 ID | 中文名 | 用途定位 | 主视觉 | 适合场景 |
|---|---|---|---|---|
| classic | 经典蓝眼 | 默认开箱主题，最贴近当前概念图。 | 黑底、青蓝眼睛、黄铜外圈、薄荷正反馈。 | 日常待机、移动、语音互动。 |
| amber | 琥珀巡航 | 强化铜丝车架和复古机械感。 | 琥珀提示色、铜色边框、保留少量青蓝作为科技感。 | 展示、巡航、充电提示。 |
| mint | 薄荷友好 | 更温和、更像陪伴机器人。 | 薄荷绿表情、浅青辅助、低对比黄铜边。 | 开心、讲故事、儿童/桌面陪伴场景。 |
| alert | 红色警戒 | 安全与拒绝状态更明确。 | 红橙眼睛、暖黄辅助、暗红背景。 | 错误、拒绝危险指令、急停、底盘保护。 |
| night | 低亮夜航 | 降低亮度和刺眼感。 | 深色低亮背景、柔蓝眼睛、低饱和边框。 | 夜间时钟、低功耗待机、安静陪伴。 |

每套主题至少包含这些 token：`bg`、`panel`、`eye_bg`、`line`、`cyan`、`mint`、`red`、`amber`、`text`、`muted`。表情 ID 不跟主题绑定，同一个 `happy/listen/moving/error` 可以套用任意主题。

## 7. 实现建议

1. 第一阶段：Web Preview 确认表情语言，所有表情先用 CSS/Canvas 参数实现。
2. 第二阶段：ESP-IDF/LVGL 中实现同样的 `atlas_eye_pose_t`，双屏分别调用 `render_left_eye()` 和 `render_right_eye()`。
3. 第三阶段：miniClaw/MimiClaw 输出语义事件，如 `VOICE_LISTENING`、`THINKING`、`MOVE_FORWARD`、`STOPPED`，UI 状态机统一映射到表情。
4. 第四阶段：增加音频驱动，让 listen/speaking 的虹膜脉冲跟随麦克风输入和 TTS 音量。
5. 第五阶段：固化 5 套主题 token，并在 LVGL 端实现主题切换、NVS 保存和 Web 管理页同步。

## 8. 当前已落地

已更新 `/Users/macbook/Documents/Atlas One/simulator_web/index.html`：

- 每块圆屏只显示一只眼睛。
- 支持 idle、happy、listen、thinking、speaking、moving、curious、sleepy、surprised、wink、angry、charging、error。
- 支持 classic、amber、mint、alert、night 5 套 Web 主题候选，切换结果会保存在浏览器本地。
- 底盘方向指令会改变移动表情的目光方向。
- 继续使用 VS Code Live Preview 即可快速查看效果。
