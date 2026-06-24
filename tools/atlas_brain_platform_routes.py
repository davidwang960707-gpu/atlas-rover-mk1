"""Platform, runtime and provider route helpers for Atlas Brain."""

from __future__ import annotations

from typing import Any, Callable

from atlas_brain_audio import latest_tts_meta, latest_turn_meta
from atlas_brain_tool_routes import public_roles_payload


BRAIN_ENDPOINTS = [
    "/health",
    "/diagnostics",
    "/capabilities",
    "/api/capabilities",
    "/api/acceptance/report",
    "/api/runtime",
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


def runtime_sessions_payload(bridge: Any) -> dict[str, Any]:
    snapshot = bridge.runtime.snapshot()
    return {
        "ok": True,
        "protocol": snapshot["protocol"],
        "sessions": snapshot["sessions"],
        "recent_events": snapshot["recent_events"],
    }


def providers_payload(bridge: Any) -> dict[str, Any]:
    snapshot = bridge.platform_snapshot()
    return {"ok": True, "providers": snapshot["providers"], "summary": snapshot["summary"]}


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


def handle_runtime_sessions(handler: Any, bridge: Any) -> None:
    handler.send_json(runtime_sessions_payload(bridge))


def handle_runtime_score(handler: Any, bridge: Any) -> None:
    handler.send_json(bridge.runtime_score_payload())


def handle_providers(handler: Any, bridge: Any) -> None:
    handler.send_json(providers_payload(bridge))


def handle_protocols(handler: Any, bridge: Any) -> None:
    handler.send_json(protocols_payload(bridge))
