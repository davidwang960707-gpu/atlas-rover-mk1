# Atlas DualEye 桌面应用页面实测清单

日期：2026-06-24  
范围：DualEye 固件体验线 P5，配合服务端 field acceptance / trace 脚本做真机页面验收。

## 1. 先看字段

每次切页后先抓：

```bash
curl http://DUALEYE_IP/api/status/lite
curl http://DUALEYE_IP/api/selftest
```

重点字段：

| 目的 | 字段 |
|---|---|
| 请求是否到设备 | `experience.ui.current_page`、`experience.ui.last_page_change_ms`、`experience.ui.last_page_change_reason` |
| 最终是否被覆盖 | `experience.ui.rendered_page`、`scene.page`、`experience.ui.scene_severity` |
| 应该像哪个应用 | `experience.ui.expected_app` |
| 双屏应该怎么分工 | `experience.ui.expected_surfaces`、`experience.ui.left_screen`、`experience.ui.right_screen` |
| 本地应用状态 | `apps.clock`、`apps.pomodoro`、`apps.calendar`、`pet`、`chat.text` |
| 资源/字体/兜底 | `/api/selftest` 的 `eye_assets`、`desk_app_pages`、`display_surfaces`、`offline_fallback` |

## 2. 页面实测表

| 页面/模式 | 左屏预期 | 右屏预期 | 关键字段 | 常见失败归因 | 拍照记录点 |
|---|---|---|---|---|---|
| `eyes` | `left_eye` | `right_eye` | `expected_app=eyes`、`expected_surfaces=dual_eyes` | 页面请求没到；主题资源缺失；背光/单屏硬件问题 | 双眼表情、主题、左右亮度 |
| `chat` + `pet_head` | `pet_head` | `short_text` | `ui.chat_mode=pet_head`、`expected_surfaces=pet_head_left_text_right`、`pet.asset_id` | chat mode 没切；宠物头资源缺失；当前页被 scene 覆盖 | 左屏宠物透明底、右屏短文本 |
| `chat` + `text` | `text_left` | `text_right` | `ui.chat_mode=text`、`expected_surfaces=dual_text`、`chat.text` | 模式未保存；文本为空；字体 fallback 问题 | 双屏文字是否完整、中文是否可读 |
| `chat` + `eyes_only` | `eyes_only_left` | `eyes_only_right` | `ui.chat_mode=eyes_only`、`expected_surfaces=dual_eyes` | 模式未保存；仍显示 pet/text；scene 覆盖 | 双眼是否不夹杂异常文字页 |
| `clock` | `analog_clock` | `digital_clock` | `expected_app=clock`、`expected_surfaces=analog_left_digital_right`、`apps.clock.synced/time/date` | 时间未同步；请求没到；单屏驱动/背光异常 | 左模拟钟、右数字时间日期 |
| `pomodoro` | `pomodoro_timer` | `pomodoro_task` | `expected_app=pomodoro`、`expected_surfaces=timer_left_task_right`、`apps.pomodoro.running/progress_percent/remaining_ms` | 计时状态未更新；工具调用失败；scene 覆盖 | 倒计时、任务名、开始/停止后变化 |
| `calendar` | `calendar_title` | `calendar_note` | `expected_app=calendar`、`expected_surfaces=title_left_note_right`、`apps.calendar.title/note` | 日历文本为空；中文字体 fallback；页面未切到 | 标题、备注、中文显示 |
| `music` | 按 chat mode 分配 | 按 chat mode 分配 | `expected_app=music`、`expected_surfaces`、`pet.phase`、`audio_service.mode` | 音乐场景覆盖；播放状态未恢复；chat mode 不符合预期 | 宠物表情/文字与音乐状态是否一致 |
| `story` | 按 chat mode 分配 | 按 chat mode 分配 | `expected_app=story`、`expected_surfaces`、`chat.text`、`pet.phase` | 故事文本没更新；Brain 离线；scene 覆盖 | 文本/宠物状态是否像故事模式 |
| `photo` | `photo_left` | `photo_right` | `expected_app=photo`、`expected_surfaces=dual_photo`、`eye_assets` | 图片资源缺失；占位页未渲染；单屏异常 | 双屏图片/占位是否稳定 |
| `status` | `status_summary` | `status_detail` | `expected_app=status`、`display_surfaces` | 诊断页被其他 scene 覆盖；异常文字页 | 状态摘要与详情是否双屏分区 |

## 3. 失败归因顺序

1. 先看 `last_page_change_ms` 是否变化。没变化说明手机按钮或工具请求没有到设备。
2. 再看 `current_page` 与 `rendered_page`。不一致说明 scene、语音、Brain 离线或运行态覆盖了本地页面。
3. 看 `expected_app/expected_surfaces` 与实物是否一致。一致但屏幕不对，优先查资源、字体、背光和屏幕驱动。
4. 看本地 app 状态。时钟查 `apps.clock`，番茄查 `apps.pomodoro`，日历查 `apps.calendar`，宠物头查 `pet.asset_id` 和 `ui.chat_mode`。
5. 最后跑 `/api/selftest`。`desk_app_pages` 说明本轮页面契约，`display_surfaces` 给当前左右屏预期，`offline_fallback` 确认 Brain/Wi-Fi 离线不应导致黑屏。

## 4. 与服务端脚本配合的顺序

1. 服务端启动 field acceptance / trace，确认 DualEye IP 可访问。
2. DualEye 先抓 `/api/status/lite`，记录开机页面、`expected_app`、`expected_surfaces`。
3. 依次切 `eyes -> clock -> pomodoro -> calendar -> chat`，每一步抓 status/lite 并拍照。
4. 在 chat 页切 `pet_head -> text -> eyes_only`，每一步抓 status/lite 并拍照。
5. 切 `story/music/photo`，确认 `expected_app` 与左右屏分工可解释。
6. 断开 Brain 或 Wi-Fi，重复切 `eyes/clock/pomodoro/calendar/chat`，确认 `offline_fallback` 为可降级状态且不黑屏。
7. 跑 `/api/selftest` 收尾，要求 `summary.fail=0`；warn 必须能由 `brain_ws`、`sr_probe` 或资源降级解释。

## 5. 记录模板

| 时间 | 操作 | status/lite 关键字段 | 实机照片 | 结论 |
|---|---|---|---|---|
|  | 切到 clock | `current_page=` `rendered_page=` `expected_surfaces=` |  |  |
|  | 切到 pomodoro | `current_page=` `rendered_page=` `expected_surfaces=` |  |  |
|  | 切到 calendar | `current_page=` `rendered_page=` `expected_surfaces=` |  |  |
|  | 切 chat mode | `chat_mode=` `expected_surfaces=` |  |  |
|  | Brain 离线切页 | `brain_online=` `offline_fallback=` `expected_app=` |  |  |
