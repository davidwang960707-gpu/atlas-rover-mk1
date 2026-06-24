"""Runtime scoring and acceptance checks for Atlas Brain."""

from __future__ import annotations

import json
import os
import time
import urllib.request
from typing import Any, Optional

from atlas_brain_audio import latest_audio_stream_meta
from atlas_brain_ota import build_ota_manifest


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
NO_PROXY_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
EXPECTED_DESK_APP_TOOLS = {
    "atlas.clock.show",
    "atlas.clock.sync",
    "atlas.clock.status",
    "atlas.calendar.show",
    "atlas.calendar.today",
    "atlas.calendar.set_note",
    "atlas.pomodoro.show",
    "atlas.pomodoro.start",
    "atlas.pomodoro.stop",
    "atlas.pomodoro.status",
    "atlas.weather.query",
    "atlas.web_search",
    "atlas.ui.set_chat_mode",
    "atlas.pet.set_state",
    "atlas.pet.play_animation",
    "atlas.ota.check",
}


def http_json(url: str, timeout: float = 5.0) -> dict[str, Any]:
    with NO_PROXY_OPENER.open(url, timeout=timeout) as resp:
        raw = resp.read().decode("utf-8", errors="replace")
    return json.loads(raw)


def rover_skills_enabled() -> bool:
    return os.getenv("ATLAS_ENABLE_ROVER_SKILLS", "0").strip().lower() in {"1", "true", "yes", "on"}


def build_runtime_score_payload(bridge: Any) -> dict[str, Any]:
    tools_payload = bridge.skills.tool_schema_payload()
    tool_names = {str(tool.get("name", "")) for tool in tools_payload.get("tools", []) if isinstance(tool, dict)}
    missing_tools = sorted(EXPECTED_DESK_APP_TOOLS - tool_names)
    latest_stream = bridge.runtime.latest_stream() or latest_audio_stream_meta()
    stream_ok = (
        str(latest_stream.get("stage", "")) == "P2_dualeye_ws_opus_stream"
        and int(latest_stream.get("atlas_frames", 0) or 0) > 0
        and int(latest_stream.get("sequence_gaps", 0) or 0) == 0
        and int(latest_stream.get("payload_len_mismatches", 0) or 0) == 0
    )
    ota_manifest = build_ota_manifest()
    status = bridge.last_status if isinstance(bridge.last_status, dict) else {}
    audio_service_status = status.get("audio_service") if isinstance(status.get("audio_service"), dict) else {}
    firmware_fingerprint = status.get("fingerprint") if isinstance(status.get("fingerprint"), dict) else {}
    source_audio_service_ready = os.path.exists(os.path.join(REPO_ROOT, "firmware", "dualeye", "main", "atlas_audio_service.c"))
    source_opus_ready = os.path.exists(os.path.join(REPO_ROOT, "firmware", "dualeye", "main", "atlas_opus_stream.c"))
    docs_ready = all(os.path.exists(path) for path in [
        os.path.join(REPO_ROOT, "README.md"),
        os.path.join(REPO_ROOT, "firmware", "dualeye", "README.md"),
        os.path.join(REPO_ROOT, "docs", "端到端能力对标_xiaozhi_Atlas_V0.12.md"),
    ])
    platform = bridge.platform_snapshot()
    capabilities = {
        "device_firmware": bool(firmware_fingerprint) or ota_manifest.get("status") == "package_ready",
        "audio_service": bool(audio_service_status) or source_audio_service_ready,
        "opus_uplink": stream_ok,
        "session_runtime": bool(bridge.runtime.snapshot().get("protocol") == "atlas.runtime.v0"),
        "tools": tools_payload.get("protocol") == "atlas.tools.v0.desk_apps" and not missing_tools,
        "provider_config": bridge.llm_enabled() and bridge.asr_enabled() and bridge.tts_enabled(),
        "web_console": True,
        "acceptance": True,
        "ota_manifest": ota_manifest.get("status") == "package_ready",
        "docs": docs_ready,
    }
    current_score = bridge.runtime.score(capabilities)
    ready_capabilities = dict(capabilities)
    ready_capabilities["opus_uplink"] = capabilities["opus_uplink"] or source_opus_ready
    ready_capabilities["provider_config"] = True
    ready_score = bridge.runtime.score(ready_capabilities)
    return {
        "ok": bool(current_score.get("ok")),
        "protocol": "atlas.runtime.score.v0",
        "score": current_score,
        "ready_score": ready_score,
        "capabilities": capabilities,
        "ready_capabilities": ready_capabilities,
        "missing_tools": missing_tools,
        "latest_stream": latest_stream,
        "provider_status": bridge.provider_status(),
        "platform_summary": platform.get("summary", {}),
        "ota_status": ota_manifest.get("status"),
        "notes": [
            "score 是当前实测/已配置状态。",
            "ready_score 把已实现但需要真机流或 API Key 的能力作为可达能力，用于烧录前判断代码成熟度。",
        ],
    }


def acceptance_check(name: str,
                     ok: bool,
                     required: bool = True,
                     detail: str = "",
                     data: Optional[dict[str, Any]] = None,
                     next_step: str = "") -> dict[str, Any]:
    if ok:
        status = "pass"
    else:
        status = "fail" if required else "warn"
    return {
        "name": name,
        "status": status,
        "ok": ok,
        "required": required,
        "detail": detail,
        "data": data or {},
        "next_step": next_step,
    }


def summarize_acceptance(checks: list[dict[str, Any]]) -> dict[str, int]:
    summary = {"pass": 0, "warn": 0, "fail": 0}
    for item in checks:
        status = str(item.get("status", "fail"))
        if status not in summary:
            status = "fail"
        summary[status] += 1
    return summary


def build_experience_status(bridge: Any, *, skip_device: bool = False) -> dict[str, Any]:
    runtime = bridge.runtime.snapshot()
    diagnostics = runtime.get("diagnostics") if isinstance(runtime.get("diagnostics"), dict) else {}
    platform = bridge.platform_snapshot()
    tools_payload = bridge.skills.tool_schema_payload()
    tool_count = int(tools_payload.get("tool_count", 0) or 0)
    app_count = int(platform.get("summary", {}).get("app_count", 0) or 0)
    latest_stream = runtime.get("latest_stream") if isinstance(runtime.get("latest_stream"), dict) else {}
    recent_turns = diagnostics.get("recent_turns") if isinstance(diagnostics.get("recent_turns"), list) else []
    recent_tools = diagnostics.get("recent_tool_calls") if isinstance(diagnostics.get("recent_tool_calls"), list) else []

    voice_ready = bool(bridge.asr_enabled() and bridge.llm_enabled() and bridge.tts_enabled())
    voice_state = "ready" if voice_ready else "needs_config"
    voice_score = 70 if voice_ready else 45
    if skip_device:
        voice_state = "needs_device" if voice_ready else "needs_config"
    if latest_stream.get("atlas_frames"):
        voice_score += 15
    if recent_turns:
        voice_score += 10
    voice_score = min(100, voice_score)

    pet_state = "ready" if app_count >= 3 else "needs_config"
    pet_score = min(100, 50 + app_count * 10)
    if skip_device:
        pet_state = "needs_device"
        pet_score = min(pet_score, 70)

    tools_ready = tools_payload.get("protocol") == "atlas.tools.v0.desk_apps" and tool_count >= 10
    tool_state = "ready" if tools_ready else "needs_config"
    tool_score = 80 if tools_ready else 45
    if recent_tools:
        tool_score += 10
    if diagnostics.get("latest_failure"):
        tool_score = min(tool_score, 75)
    tool_score = min(100, tool_score)

    return {
        "protocol": "atlas.experience.status.v0",
        "summary": {
            "voice_continuous_conversation": voice_score,
            "dualeye_pet_apps": pet_score,
            "brain_tools_diagnostics": tool_score,
        },
        "lines": {
            "voice_continuous_conversation": {
                "state": voice_state,
                "score": voice_score,
                "ready": voice_state == "ready",
                "needs_device": bool(skip_device),
                "detail": "ASR/LLM/TTS 均配置后可完整语音对话；真机连续语音仍需 OPUS 真流验证。" if not voice_ready else "Provider 已配置，等待真机连续流验收。" if skip_device else "语音链路可验收。",
                "latest_turn": diagnostics.get("latest_turn", {}),
                "latest_stream": latest_stream,
                "next_step": "配置 Provider 后重启 Mac Brain；连接真机后跑 OPUS turn。" if not voice_ready else "连接真机后去掉 skip_device 跑完整验收。" if skip_device else "",
            },
            "dualeye_pet_apps": {
                "state": pet_state,
                "score": pet_score,
                "ready": pet_state == "ready",
                "needs_device": bool(skip_device),
                "detail": f"已注册 app_count={app_count}，宠物/应用工具存在；真机显示效果需设备在线验证。",
                "next_step": "连接 DualEye 后 smoke /app、/devices/<id>/app 和宠物状态工具。" if skip_device else "",
            },
            "brain_tools_diagnostics": {
                "state": tool_state,
                "score": tool_score,
                "ready": tool_state == "ready",
                "needs_device": False,
                "detail": f"tool_count={tool_count}，runtime diagnostics 可定位 asr/llm/tool/tts/device_push/playback。",
                "latest_tool": diagnostics.get("latest_tool", {}),
                "latest_failure": diagnostics.get("latest_failure", {}),
                "next_step": diagnostics.get("next_step", "") or "运行 /api/diagnostics/simulate-turn 或实际工具调用，查看 /api/runtime diagnostics。",
            },
        },
    }


def build_acceptance_report(bridge: Any, audio_ws_url: str = "", skip_device: bool = False) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []
    started = time.time()

    health = {
        "service": "atlas-brain",
        "llm_enabled": bridge.llm_enabled(),
        "asr_enabled": bridge.asr_enabled(),
        "tts_enabled": bridge.tts_enabled(),
        "rover_skills_enabled": rover_skills_enabled(),
    }
    checks.append(acceptance_check(
        "Mac Brain 服务",
        True,
        detail="HTTP 服务已运行，平台化后端可响应。",
        data=health,
    ))

    providers = bridge.provider_status()
    checks.append(acceptance_check(
        "LLM Provider",
        bool(providers.get("llm", {}).get("enabled")),
        required=False,
        detail="未配置时页面/工具仍可用，但完整对话会退化。",
        data=providers.get("llm", {}),
        next_step="设置 ATLAS_LLM_API_KEY、ATLAS_LLM_BASE_URL、ATLAS_LLM_MODEL 后重启 Mac Brain。",
    ))
    checks.append(acceptance_check(
        "ASR/TTS Provider",
        bool(providers.get("asr", {}).get("enabled")) and bool(providers.get("tts", {}).get("enabled")),
        required=False,
        detail="ASR/TTS 未配置会影响语音对话和自动播报。",
        data={"asr": providers.get("asr", {}), "tts": providers.get("tts", {})},
        next_step="设置 ATLAS_ASR_MODEL、ATLAS_TTS_MODEL、ATLAS_TTS_VOICE 或使用本地回退语音。",
    ))

    tools_payload = bridge.skills.tool_schema_payload()
    tool_names = {str(tool.get("name", "")) for tool in tools_payload.get("tools", []) if isinstance(tool, dict)}
    missing_tools = sorted(EXPECTED_DESK_APP_TOOLS - tool_names)
    checks.append(acceptance_check(
        "Tool Schema V0",
        tools_payload.get("protocol") == "atlas.tools.v0.desk_apps" and not missing_tools,
        detail=f"tool_count={tools_payload.get('tool_count')} missing={missing_tools}",
        data={"protocol": tools_payload.get("protocol"), "tool_count": tools_payload.get("tool_count"), "missing": missing_tools},
        next_step="补齐缺失工具或重启 Mac Brain，确认加载的是最新 tools/atlas_brain_server.py。",
    ))

    platform = bridge.platform_snapshot()
    checks.append(acceptance_check(
        "平台化抽象",
        platform.get("summary", {}).get("device_count", 0) >= 1 and platform.get("summary", {}).get("protocol_count", 0) >= 4,
        detail=f"devices={platform.get('summary', {}).get('device_count')} protocols={platform.get('summary', {}).get('protocol_count')} apps={platform.get('summary', {}).get('app_count')}",
        data=platform.get("summary", {}),
        next_step="检查 PlatformBackend 的 device/provider/protocol/app 注册。",
    ))

    if skip_device:
        checks.append(acceptance_check(
            "DualEye 在线",
            False,
            required=False,
            detail="skip_device=1，服务侧自检不访问设备。",
            data={"dualeye_url": bridge.dualeye_url},
            next_step="连接真机后去掉 skip_device 再跑完整验收。",
        ))
        checks.append(acceptance_check(
            "DualEye Brain/OPUS 能力声明",
            False,
            required=False,
            detail="skip_device=1，未检查 /api/capabilities。",
            next_step="刷入后检查 /api/capabilities 的 opus_streaming=true。",
        ))
        checks.append(acceptance_check(
            "DualEye 自检",
            False,
            required=False,
            detail="skip_device=1，未检查 /api/selftest。",
            next_step="刷入后检查 selftest fail=0。",
        ))
    else:
        try:
            status = bridge.status()
            device_online = bool(status.get("ok", True))
            checks.append(acceptance_check(
                "DualEye 在线",
                device_online,
                detail=f"url={bridge.dualeye_url} page={status.get('ui', {}).get('page', '') if isinstance(status.get('ui'), dict) else ''}",
                data={
                    "firmware": status.get("firmware"),
                    "fingerprint": status.get("fingerprint", {}),
                    "wifi": status.get("wifi", {}),
                    "audio_service": status.get("audio_service", {}),
                    "voice_wake": status.get("voice_wake", {}),
                },
                next_step="确认 Mac 与 DualEye 在同一 Wi-Fi，或改用 AP 热点地址。",
            ))
        except Exception as exc:
            checks.append(acceptance_check(
                "DualEye 在线",
                False,
                detail=str(exc),
                data={"dualeye_url": bridge.dualeye_url},
                next_step="先恢复设备网络，再运行验收。",
            ))

        try:
            capabilities = bridge.capabilities()
            brain_channel = capabilities.get("brain_channel", {}) if isinstance(capabilities.get("brain_channel"), dict) else {}
            checks.append(acceptance_check(
                "DualEye Brain/OPUS 能力声明",
                brain_channel.get("protocol") == "atlas.brain.session.v1" and bool(brain_channel.get("opus_streaming")),
                detail=f"protocol={brain_channel.get('protocol')} opus_streaming={brain_channel.get('opus_streaming')}",
                data=brain_channel,
                next_step="重新编译刷入带 OPUS stream endpoint 的固件。",
            ))
        except Exception as exc:
            checks.append(acceptance_check(
                "DualEye Brain/OPUS 能力声明",
                False,
                detail=str(exc),
                next_step="检查 /api/capabilities 是否可访问。",
            ))

        try:
            selftest = http_json(f"{bridge.dualeye_url}/api/selftest", timeout=3.0)
            summary = selftest.get("summary", {}) if isinstance(selftest.get("summary"), dict) else {}
            fail = int(summary.get("fail", 1))
            checks.append(acceptance_check(
                "DualEye 自检",
                fail == 0,
                detail=f"pass={summary.get('pass')} warn={summary.get('warn')} fail={summary.get('fail')}",
                data={"summary": summary, "ready_to_flash": selftest.get("ready_to_flash"), "fingerprint": selftest.get("fingerprint", {})},
                next_step="先处理自检 fail 项；warn 中 WakeNet/AEC 资源探针可暂缓。",
            ))
        except Exception as exc:
            checks.append(acceptance_check(
                "DualEye 自检",
                False,
                detail=str(exc),
                next_step="检查 /api/selftest。",
            ))

    stream = bridge.runtime.latest_stream() or latest_audio_stream_meta()
    if skip_device:
        dualeye_stream: dict[str, Any] = {"ok": False, "skipped": True, "reason": "skip_device"}
    else:
        try:
            dualeye_stream = bridge.dualeye_opus_stream_status()
        except Exception as exc:
            dualeye_stream = {"ok": False, "error": str(exc)}
    stream_stage = str(stream.get("stage", ""))
    stream_frames = int(stream.get("atlas_frames", 0) or 0)
    stream_gaps = int(stream.get("sequence_gaps", 0) or 0)
    opus_stream_ok = stream_stage == "P2_dualeye_ws_opus_stream" and stream_frames > 0 and stream_gaps == 0
    checks.append(acceptance_check(
        "OPUS WebSocket 真流",
        opus_stream_ok,
        required=False,
        detail=f"last_stage={stream_stage or 'none'} atlas_frames={stream_frames} gaps={stream_gaps} device_stage={dualeye_stream.get('stream', {}).get('stage', '') if isinstance(dualeye_stream.get('stream'), dict) else ''}",
        data={"last_stream": stream, "dualeye_stream": dualeye_stream, "ws_url_for_dualeye": audio_ws_url},
        next_step="点验收页的 OPUS 真流 1.8s；若 frames=0，看 DualEye stream.stage 和 heap 字段。",
    ))

    if skip_device:
        sr_status = {"ok": False, "skipped": True, "reason": "skip_device"}
    else:
        try:
            sr_status = http_json(f"{bridge.dualeye_url}/api/sr/status", timeout=2.0)
        except Exception as exc:
            sr_status = {"ok": False, "error": str(exc)}
    checks.append(acceptance_check(
        "Wake/VAD/AEC 探针",
        bool(sr_status.get("ok")),
        required=False,
        detail="当前验收 energy gate VAD 与资源探针；WakeNet/AEC 不硬上。",
        data=sr_status,
        next_step="晚上实机看堆内存与模型分区，再决定是否启用 WakeNet/AEC。",
    ))

    ota_manifest = build_ota_manifest()
    checks.append(acceptance_check(
        "烧录包 Manifest",
        ota_manifest.get("status") == "package_ready" and len(ota_manifest.get("packages", [])) >= 4,
        detail=f"status={ota_manifest.get('status')} packages={len(ota_manifest.get('packages', []))} missing={ota_manifest.get('missing')}",
        data={"protocol": ota_manifest.get("protocol"), "status": ota_manifest.get("status"), "packages": ota_manifest.get("packages", []), "missing": ota_manifest.get("missing", [])},
        next_step="先执行 idf.py build，生成 bootloader/partition/app/storage 包。",
    ))

    summary = summarize_acceptance(checks)
    required_failed = [item for item in checks if item.get("status") == "fail" and item.get("required")]
    warnings = [item for item in checks if item.get("status") == "warn"]
    next_steps = [item["next_step"] for item in required_failed + warnings if item.get("next_step")]
    runtime_score = build_runtime_score_payload(bridge)
    experience = build_experience_status(bridge, skip_device=skip_device)
    return {
        "ok": len(required_failed) == 0,
        "ready_to_flash_test": len(required_failed) == 0 and not skip_device,
        "device_checks_skipped": skip_device,
        "protocol": "atlas.acceptance.v0",
        "generated_at": int(time.time()),
        "elapsed_ms": int((time.time() - started) * 1000),
        "summary": summary,
        "runtime_score": runtime_score,
        "experience_status": experience,
        "xiaozhi_gap": {
            "objective_maturity_estimate": "Atlas 本轮按 80 分可验收口径补强：会话运行时、OPUS 真流入口、工具化应用、平台后端和验收页已具备；流式 ASR/TTS、AEC/WakeNet、生产级 OTA 仍是后续超过 xiaozhi 的核心差距。",
            "atlas_advantage": "双屏表情、桌面宠物应用、可视化主题与本地 Mac Brain 可调试性。",
            "remaining_gaps": ["流式 ASR", "流式 TTS 播放", "真实 WakeNet/AEC", "多设备账号体系", "生产级 OTA/回滚", "长期压测"],
        },
        "checks": checks,
        "next_steps": next_steps[:6],
    }
