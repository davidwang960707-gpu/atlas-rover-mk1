# Atlas TOC 连续语音实机验收 P4

目标：晚上用手机或电脑打开 `/app` 时，快速判断连续对话是否真的像用户态产品，而不是只能在后台看接口。

## 1. 启动 Mac Brain

```bash
python3 tools/atlas_brain_server.py --host 127.0.0.1 --port 8899 --dualeye-url http://设备IP
```

如果只是烧录前 dry-run：

```bash
python3 tools/atlas_brain_server.py --dry-run --host 127.0.0.1 --port 8899
```

## 2. 打开页面

浏览器打开：

```text
http://127.0.0.1:8899/app
```

手机同 Wi-Fi 打开页面上显示的 LAN URL。

## 3. 连续对话验收步骤

1. 点“连续对话”。
2. 允许浏览器麦克风权限。
3. 说一句短话，例如“打开番茄页面并鼓励我一下”。
4. 观察页面：
   - “识别”出现 ASR 文本。
   - “回复”出现 Brain 回复。
   - “播报”进入播报状态。
   - 播报结束后状态回到“继续听”。
5. 观察 DualEye：
   - 页面切到番茄或对应应用。
   - 表情/页面/文字与 `/app` 状态同步。
   - 如果没有声音，检查 TTS URL 是否同源 `/tts/latest.wav`。

## 4. 保存联调 trace

```bash
python3 tools/collect_atlas_realtime_trace.py \
  --brain-url http://127.0.0.1:8899 \
  --dualeye-url http://设备IP \
  --duration-sec 60 \
  --interval-sec 1 \
  --output-jsonl /tmp/atlas_trace.jsonl \
  --output-md /tmp/atlas_trace.md
```

复盘时优先看：

- `/tmp/atlas_trace.md` 的 `Brain runtime stage`
- `DualEye brain_ws`
- `DualEye turn/opus stage`
- OPUS frames、capture_failures、encode_failures、send_failures
- `last_page_change`

## 5. 无麦克风 Web 控制台检查

```bash
python3 tools/check_atlas_web_voice_console.py \
  --brain-url http://127.0.0.1:8899 \
  --output-md /tmp/atlas_web_voice_console.md
```

这个检查不依赖真实麦克风，只确认 `/app` 页面包含连续对话入口、自动切段、播报后续听、TTS 同源播放、Provider 缺项、runtime score 和工具调用 smoke。
