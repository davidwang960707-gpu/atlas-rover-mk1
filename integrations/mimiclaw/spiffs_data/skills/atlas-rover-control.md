# Atlas Rover Control

Control Atlas Rover Mk.1 through safe DualEye intent tools.

## When to use

Use this skill when the user asks Atlas Rover to:

- Change eye expression or mood
- Show clock, status, music, story, chat, calendar, pomodoro, photo, or eyes page
- Move a short distance
- Stop immediately
- Play music, tell a story, chat, show calendar, or start pomodoro
- Treat Atlas Rover as a desktop pet: touch it, play with it, let it rest, or switch pet mood/state

## Safety rules

- Never drive motors directly.
- Never output raw UART.
- Always use Atlas Rover tools.
- `atlas_rover_stop` is always allowed and has highest priority.
- Current Mk.1 chassis is ordinary N20 motors + DRV8833 + front caster, using open-loop timed differential drive.
- Do not request exact distance, exact angle, odometry, encoder control, or IMU correction.
- For movement, use short low-speed commands:
  - speed: 20-40 by default
  - duration_ms: 300-800 by default
- If the user asks for continuous movement, patrol, follow-me, or unclear long movement, ask for confirmation.
- If confidence is low, ask a clarifying question instead of moving.

## Tools

Use one of these tool calls:

```json
{"tool":"atlas_set_expression","input":{"expression":"happy"}}
```

Allowed expressions:

`idle`, `happy`, `listen`, `thinking`, `speaking`, `moving`, `curious`, `sleepy`, `surprised`, `wink`, `love`, `money`, `angry`, `charging`, `error`, `cry`

```json
{"tool":"atlas_show_page","input":{"page":"clock"}}
```

Allowed pages:

`eyes`, `clock`, `status`, `voice`, `alarm`, `photo`, `music`, `story`, `chat`, `calendar`, `pomodoro`

```json
{"tool":"atlas_app_action","input":{"action":"music.play"}}
```

Useful actions:

`music.play`, `story.tell`, `chat.reply`, `calendar.show`, `calendar.add_reminder`, `pomodoro.start`, `pomodoro.stop`, `status.report`

```json
{"tool":"atlas_pet_event","input":{"event":"touch"}}
```

Allowed pet events:

`interaction`, `touch`, `play`, `feed`, `voice_listen`, `think`, `speak`, `patrol`, `music`, `story`, `chat`, `stop`, `rest`, `charge`, `error`

Pet event meanings:

- `touch`: user touched the robot; make it happy.
- `play`: playful interaction; raise mood and curiosity.
- `feed`:补能/充电语义；提高能量值。
- `rest`:让它休息或睡一会。
- `patrol`:巡游状态，只表示宠物状态；真正移动仍要用 `atlas_rover_move`。
- `music` / `story` / `chat`:音乐、讲故事、对话专属状态。

```json
{"tool":"atlas_rover_move","input":{"direction":"forward","speed":35,"duration_ms":600}}
```

Allowed directions:

`forward`, `backward`, `left`, `right`

```json
{"tool":"atlas_rover_stop","input":{}}
```

## Examples

User: "开心一点"

```json
{"tool":"atlas_set_expression","input":{"expression":"happy"}}
```

User: "显示番茄钟"

```json
{"tool":"atlas_show_page","input":{"page":"pomodoro"}}
```

User: "往前走一点"

```json
{"tool":"atlas_rover_move","input":{"direction":"forward","speed":35,"duration_ms":600}}
```

User: "摸摸你"

```json
{"tool":"atlas_pet_event","input":{"event":"touch"}}
```

User: "你困了就休息一下"

```json
{"tool":"atlas_pet_event","input":{"event":"rest"}}
```

User: "停下"

```json
{"tool":"atlas_rover_stop","input":{}}
```

User: "一直往前走"

Ask for confirmation. Do not move until confirmed.
