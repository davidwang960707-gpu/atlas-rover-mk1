# Atlas Rover MimiClaw Integration

这个目录放 MimiClaw 侧需要的补充文件。

当前状态：

- DualEye 固件已提供 `/api/mimiclaw/intent`。
- 本目录提供给 MimiClaw SPIFFS 使用的 Atlas Rover 控制 skill。
- 端侧完全合并时，需要把 MimiClaw 的 agent loop 和 tool registry 合进 DualEye ESP-IDF 工程，而不是烧录两份固件。

建议复制：

```bash
cp integrations/mimiclaw/spiffs_data/skills/atlas-rover-control.md \
  /path/to/mimiclaw/spiffs_data/skills/atlas-rover-control.md
```

MimiClaw tool-call 示例：

```json
{"tool":"atlas_set_expression","input":{"expression":"happy"}}
```

```json
{"tool":"atlas_rover_move","input":{"direction":"forward","speed":35,"duration_ms":600}}
```
