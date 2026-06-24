# Atlas Platform Specs

本目录用于沉淀 Atlas 智能硬件平台的稳定契约。这里的文档不是一次性设计稿，而是后续固件、Atlas Brain、Web 控制端、资源流水线共同遵守的接口说明。

当前原则：

1. 先稳定现有 DualEye 成果，不为了平台化打断实机可玩性。
2. 先写契约，再逐步抽象代码。
3. 新协议默认向后兼容现有 `/api/status`、`/api/selftest`、`/ws/brain`、`/ws/audio`、`/api/tools/list`。
4. HTTP 继续保留为配置、自检、兜底和调试入口；Brain WebSocket 逐步承担主交互链路。
5. 每个规格都必须包含版本号、兼容策略、最小验收方法。

## 规格清单

| 文件 | 说明 |
|---|---|
| [atlas_device_status_v0.md](atlas_device_status_v0.md) | 设备状态、场景、能力、版本指纹 |
| [atlas_selftest_v0.md](atlas_selftest_v0.md) | 烧录后自检输出和验收规则 |
| [atlas_brain_session_v1.md](atlas_brain_session_v1.md) | DualEye 与 Atlas Brain 的常驻 WebSocket 会话 |
| [atlas_tool_schema_v0.md](atlas_tool_schema_v0.md) | 页面、表情、宠物、应用、诊断等工具 Schema |
| [atlas_aop1_audio_frame_v0.md](atlas_aop1_audio_frame_v0.md) | OPUS/PCM 二进制音频帧格式 |
| [atlas_asset_manifest_v0.md](atlas_asset_manifest_v0.md) | 主题、字体、宠物头、开机动画等资源包 manifest |
| [atlas_board_profile_v0.md](atlas_board_profile_v0.md) | 板卡、外设、分区、能力声明 |

## 兼容口径

这些规格的第一版都以当前 Atlas DualEye 为基准。后续新增设备时，应新增 board profile 和 product profile，而不是把 DualEye 的特殊逻辑继续塞进通用协议里。

