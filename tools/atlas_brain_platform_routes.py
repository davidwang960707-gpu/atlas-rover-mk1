"""Platform, runtime and provider route helpers for Atlas Brain."""

from __future__ import annotations

import time
from typing import Any, Callable

from atlas_brain_audio import compact_audio_payload, latest_tts_meta, latest_turn_meta
from atlas_brain_tool_routes import public_roles_payload


BRAIN_ENDPOINTS = [
    "/health",
    "/diagnostics",
    "/capabilities",
    "/api/capabilities",
    "/api/acceptance/report",
    "/api/runtime",
    "/api/diagnostics/simulate-turn",
    "/api/runtime/diagnostics",
    "/api/runtime/sessions",
    "/api/runtime/score",
    "/api/platform",
    "/api/providers",
    "/api/protocols",
    "/api/devices",
    "/api/device/live",
    "/api/device/scene",
    "/api/device/selftest",
    "/api/device/system-info",
    "/api/device/opus-probe",
    "/api/device/opus-stream/start",
    "/api/device/opus-turn/start",
    "/api/device/opus-stream/stop",
    "/api/device/opus-stream/status",
    "/devices",
    "/acceptance",
    "/skills",
    "/skill",
    "/api/tools",
    "/api/tools/list",
    "/api/tools/call",
    "/mcp/tools/list",
    "/mcp/tools/call",
    "/turn/text",
    "/turn/audio",
    "/api/brain/session",
    "/api/brain/events",
    "/ws/brain",
    "/ws/audio",
    "/api/audio/stream/status",
    "/api/audio/stream/simulate",
    "/api/sr/status",
    "/api/sr/simulate",
    "/role/switch",
    "/ota/manifest",
    "/api/ota/packages",
    "/ota/package/app_ota",
]


def build_health_payload(bridge: Any, *, rover_skills_enabled: bool) -> dict[str, Any]:
    platform = bridge.platform_snapshot()
    return {
        "ok": True,
        "service": "atlas-brain",
        "service_version": "0.2",
        "aliases": ["atlas-brain-mac"],
        "dualeye_url": bridge.dualeye_url,
        "pairing_code_known": bool(bridge.pin),
        "llm_enabled": bridge.llm_enabled(),
        "llm_model": bridge.llm_model if bridge.llm_enabled() else "",
        "asr_enabled": bridge.asr_enabled(),
        "asr_model": bridge.asr_model if bridge.asr_enabled() else "",
        "tts_enabled": bridge.tts_enabled(),
        "tts_model": bridge.tts_model if bridge.tts_enabled() else "",
        "tts_voice": bridge.tts_voice if bridge.tts_enabled() else "",
        "providers": bridge.provider_status(),
        "session": bridge.session.snapshot(),
        "skill_count": len(bridge.skills.list_public()),
        "rover_skills_enabled": rover_skills_enabled,
        "dry_run": bridge.dry_run,
        "latest_tts": latest_tts_meta(),
        "last_turn": latest_turn_meta(),
        "platform_summary": platform["summary"],
        "protocols": platform["protocols"],
    }


def build_diagnostics_payload(bridge: Any, *, rover_skills_enabled: bool) -> dict[str, Any]:
    payload = build_health_payload(bridge, rover_skills_enabled=rover_skills_enabled)
    try:
        payload["dualeye"] = bridge.status()
    except Exception as exc:
        payload["dualeye_error"] = str(exc)
    try:
        payload["dualeye_capabilities"] = bridge.capabilities()
    except Exception as exc:
        payload["dualeye_capabilities_error"] = str(exc)
    try:
        payload["dualeye_ota"] = bridge.ota_status()
    except Exception as exc:
        payload["dualeye_ota_error"] = str(exc)
    return payload


def build_capabilities_payload(bridge: Any, *, rover_skills_enabled: bool) -> dict[str, Any]:
    payload = build_health_payload(bridge, rover_skills_enabled=rover_skills_enabled)
    payload["brain"] = {
        "skills": bridge.skills.list_public(),
        "roles": public_roles_payload(),
        "endpoints": BRAIN_ENDPOINTS,
    }
    if bool(getattr(bridge, "dry_run", False)):
        payload["dualeye_capabilities_error"] = "dry_run"
        return payload
    try:
        payload["dualeye_capabilities"] = bridge.capabilities()
    except Exception as exc:
        payload["dualeye_capabilities_error"] = str(exc)
    return payload


def platform_payload(bridge: Any, recent_events: Callable[[], list[dict[str, Any]]]) -> dict[str, Any]:
    return {
        "ok": True,
        "platform": bridge.platform_snapshot(),
        "last_turn": latest_turn_meta(),
        "recent_events": recent_events(),
    }


def runtime_payload(bridge: Any) -> dict[str, Any]:
    return {"ok": True, "runtime": bridge.runtime.snapshot(), "score": bridge.runtime_score_payload()}


def runtime_diagnostics_payload(bridge: Any) -> dict[str, Any]:
    return {"ok": True, "diagnostics": bridge.runtime.diagnostics_snapshot(), "runtime": bridge.runtime.snapshot()}


def runtime_sessions_payload(bridge: Any) -> dict[str, Any]:
    snapshot = bridge.runtime.snapshot()
    return {
        "ok": True,
        "protocol": snapshot["protocol"],
        "sessions": snapshot["sessions"],
        "recent_events": snapshot["recent_events"],
    }


def provider_config_diagnostics(bridge: Any) -> dict[str, Any]:
    def line(required: list[tuple[str, str, bool]]) -> dict[str, Any]:
        missing = [field for field, _env, present in required if not present]
        missing_env = [env for _field, env, present in required if not present]
        return {
            "configured": not missing,
            "missing": missing,
            "missing_env": missing_env,
            "required_env": [env for _field, env, _present in required],
            "fields": {
                field: {"env": env, "present": bool(present)}
                for field, env, present in required
            },
        }

    api_key_present = bool(str(getattr(bridge, "llm_api_key", "") or "").strip())
    base_url_present = bool(str(getattr(bridge, "llm_base_url", "") or "").strip())
    return {
        "protocol": "atlas.provider.diagnostics.v0",
        "safe_for_ui": True,
        "notes": "Only presence/absence is reported; secret values are never returned.",
        "llm": line([
            ("api_key", "ATLAS_LLM_API_KEY", api_key_present),
            ("base_url", "ATLAS_LLM_BASE_URL", base_url_present),
            ("model", "ATLAS_LLM_MODEL", bool(str(getattr(bridge, "llm_model", "") or "").strip())),
        ]),
        "asr": line([
            ("api_key", "ATLAS_LLM_API_KEY", api_key_present),
            ("base_url", "ATLAS_LLM_BASE_URL", base_url_present),
            ("model", "ATLAS_ASR_MODEL", bool(str(getattr(bridge, "asr_model", "") or "").strip())),
        ]),
        "tts": line([
            ("api_key", "ATLAS_LLM_API_KEY", api_key_present),
            ("base_url", "ATLAS_LLM_BASE_URL", base_url_present),
            ("model", "ATLAS_TTS_MODEL", bool(str(getattr(bridge, "tts_model", "") or "").strip())),
            ("voice", "ATLAS_TTS_VOICE", bool(str(getattr(bridge, "tts_voice", "") or "").strip())),
        ]),
    }


def providers_payload(bridge: Any) -> dict[str, Any]:
    snapshot = bridge.platform_snapshot()
    return {
        "ok": True,
        "providers": snapshot["providers"],
        "summary": snapshot["summary"],
        "provider_diagnostics": provider_config_diagnostics(bridge),
    }


def protocols_payload(bridge: Any) -> dict[str, Any]:
    snapshot = bridge.platform_snapshot()
    return {"ok": True, "protocols": snapshot["protocols"], "summary": snapshot["summary"]}


def handle_health(handler: Any, bridge: Any, *, rover_skills_enabled: bool) -> None:
    handler.send_json(build_health_payload(bridge, rover_skills_enabled=rover_skills_enabled))


def handle_diagnostics(handler: Any, bridge: Any, *, rover_skills_enabled: bool) -> None:
    handler.send_json(build_diagnostics_payload(bridge, rover_skills_enabled=rover_skills_enabled))


def handle_capabilities(handler: Any, bridge: Any, *, rover_skills_enabled: bool) -> None:
    handler.send_json(build_capabilities_payload(bridge, rover_skills_enabled=rover_skills_enabled))


def handle_platform(handler: Any, bridge: Any, recent_events: Callable[[], list[dict[str, Any]]]) -> None:
    handler.send_json(platform_payload(bridge, recent_events))


def handle_runtime(handler: Any, bridge: Any) -> None:
    handler.send_json(runtime_payload(bridge))


def handle_runtime_diagnostics(handler: Any, bridge: Any) -> None:
    handler.send_json(runtime_diagnostics_payload(bridge))


def handle_runtime_sessions(handler: Any, bridge: Any) -> None:
    handler.send_json(runtime_sessions_payload(bridge))


def handle_runtime_score(handler: Any, bridge: Any) -> None:
    handler.send_json(bridge.runtime_score_payload())


def handle_providers(handler: Any, bridge: Any) -> None:
    handler.send_json(providers_payload(bridge))


def handle_protocols(handler: Any, bridge: Any) -> None:
    handler.send_json(protocols_payload(bridge))


def simulate_turn_payload(
    bridge: Any,
    payload: dict[str, Any],
    *,
    cache_tts_and_push: Callable[[dict[str, Any]], tuple[dict[str, Any], str, dict[str, Any]]],
) -> dict[str, Any]:
    text = str(payload.get("text") or "打开番茄页面并鼓励我一下").strip()
    speak = bool(payload.get("speak", True))
    started = time.time()
    text_result = bridge.send_text(text)

    tool_name = str(payload.get("tool") or "atlas.pomodoro.start")
    tool_args = payload.get("arguments")
    if not isinstance(tool_args, dict):
        tool_args = {"task_name": "保持专注", "focus_minutes": 25, "break_minutes": 5}
    tool_started = time.time()
    tool_result = bridge.execute_skill(tool_name, tool_args)
    tool_meta = bridge.skills.public_item(tool_name) or {}
    bridge.runtime.record_tool_call(
        name=tool_name,
        arguments=tool_args,
        risk=str(tool_result.get("risk", tool_meta.get("risk", ""))),
        target=str(tool_meta.get("target", "")),
        ok=bool(tool_result.get("ok")),
        error=str(tool_result.get("error", "")),
        error_code=str(tool_result.get("error_code", "")),
        elapsed_ms=int((time.time() - tool_started) * 1000),
        tts_requested=speak,
    )

    reply = str(tool_result.get("reply") or text_result.get("llm", {}).get("reply") or "番茄页面准备好了，慢慢来，你已经开始进入状态。").strip()
    result: dict[str, Any] = {
        "ok": bool(text_result.get("ok") or tool_result.get("ok")),
        "protocol": "atlas.diagnostics.simulated_turn.v0",
        "dry_run": bool(getattr(bridge, "dry_run", False)),
        "text": text,
        "text_result": text_result,
        "tool": tool_name,
        "arguments": bridge.runtime.tool_calls[-1]["arguments"] if bridge.runtime.tool_calls else {},
        "tool_result": tool_result,
        "reply": reply,
        "elapsed_ms": int((time.time() - started) * 1000),
    }
    if speak and reply:
        tts_started = time.time()
        tts = bridge.synthesize_speech(reply)
        result["tts"] = tts
        bridge.runtime.record_timeline(
            "tts",
            bool(tts.get("ok")),
            str(tts.get("provider", "") or tts.get("error", "")),
            {"provider": tts.get("provider", ""), "cloud_error": tts.get("cloud_error", "")},
            elapsed_ms=int((time.time() - tts_started) * 1000),
        )
        tts_store, tts_url, dualeye_play = cache_tts_and_push(tts)
        result["tts_cached"] = tts_store
        result["tts_url"] = tts_url
        result["dualeye_play"] = dualeye_play
        bridge.runtime.record_timeline(
            "playback",
            bool(tts_store.get("ready")) and bool(dualeye_play.get("ok")),
            "tts cached and pushed" if dualeye_play.get("ok") else str(dualeye_play.get("error", tts_store.get("error", ""))),
            {"tts_ready": bool(tts_store.get("ready")), "dualeye_play": dualeye_play},
        )
    bridge.runtime.record_turn_diagnosis(
        kind="simulated_turn",
        ok=bool(result.get("ok")),
        text=text,
        reply=reply,
        source=str(text_result.get("source", "simulation")),
        stages=[
            {"stage": "llm", "ok": True, "detail": str(text_result.get("source", "")) or "rules"},
            {"stage": "tool", "ok": bool(tool_result.get("ok")), "detail": tool_name},
            {"stage": "tts", "ok": (not speak) or bool(result.get("tts", {}).get("ok")), "detail": str(result.get("tts", {}).get("provider", "not_requested"))},
            {"stage": "playback", "ok": (not speak) or bool(result.get("dualeye_play", {}).get("ok")), "detail": str(result.get("dualeye_play", {}).get("error", ""))},
        ],
        error=str(tool_result.get("error", "") or result.get("tts", {}).get("error", "") or result.get("dualeye_play", {}).get("error", "")),
    )
    result["diagnostics"] = bridge.runtime.diagnostics_snapshot()
    return compact_audio_payload(result)


def handle_simulate_turn(
    handler: Any,
    bridge: Any,
    payload: dict[str, Any],
    *,
    cache_tts_and_push: Callable[[dict[str, Any]], tuple[dict[str, Any], str, dict[str, Any]]],
) -> None:
    handler.send_json(simulate_turn_payload(bridge, payload, cache_tts_and_push=cache_tts_and_push))
