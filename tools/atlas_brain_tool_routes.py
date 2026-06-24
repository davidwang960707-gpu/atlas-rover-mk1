"""Tool and legacy skill route helpers for Atlas Brain HTTP handlers."""

from __future__ import annotations

from http import HTTPStatus
from typing import Any, Callable

from atlas_brain_audio import compact_audio_payload, remember_turn
from atlas_brain_tools import ROLE_PROFILES


TtsPushCallback = Callable[[dict[str, Any]], tuple[dict[str, Any], str, dict[str, Any]]]


def public_roles_payload() -> dict[str, dict[str, Any]]:
    return {key: {k: v for k, v in value.items() if k != "prompt"} for key, value in ROLE_PROFILES.items()}


def legacy_tools_list(*, rover_skills_enabled: bool) -> list[str]:
    tools = [
        "atlas_show_page",
        "atlas_set_expression",
        "atlas_pomodoro",
        "atlas_calendar",
        "atlas_chat",
        "atlas_app_action",
    ]
    if rover_skills_enabled:
        return ["atlas_rover_move", "atlas_rover_stop"] + tools
    return tools


def handle_legacy_skills(handler: Any, bridge: Any) -> None:
    handler.send_json({
        "ok": True,
        "protocol": "atlas.skills.legacy.v1",
        "skills": bridge.skills.list_public(),
        "roles": public_roles_payload(),
    })


def handle_tools_list(handler: Any, bridge: Any, path: str) -> None:
    payload = bridge.skills.tool_schema_payload()
    if path == "/mcp/tools/list":
        handler.send_json({
            "ok": True,
            "protocol": "atlas.tools.v0.desk_apps",
            "tools": payload["tools"],
        })
    else:
        handler.send_json(payload)


def handle_legacy_tools(handler: Any, bridge: Any, *, rover_skills_enabled: bool) -> None:
    handler.send_json({
        "ok": True,
        "legacy_tools": legacy_tools_list(rover_skills_enabled=rover_skills_enabled),
        "skills": bridge.skills.list_public(),
        "tool_schema": bridge.skills.tool_schema_payload(),
    })


def _maybe_speak_tool_reply(
    bridge: Any,
    payload: dict[str, Any],
    result: dict[str, Any],
    reply: str,
    cache_tts_and_push: TtsPushCallback,
) -> None:
    if not bool(payload.get("speak", False)) or not reply:
        return
    result["tts"] = bridge.synthesize_speech(
        reply,
        voice=str(payload.get("tts_voice", "")).strip(),
        style=str(payload.get("tts_style", bridge.session.default_tts_style) or bridge.session.default_tts_style),
        singing=bool(payload.get("tts_singing", False)),
    )
    tts_store, tts_url, dualeye_play = cache_tts_and_push(result["tts"])
    result["tts_cached"] = tts_store
    result["tts_url"] = tts_url
    result["dualeye_play"] = dualeye_play


def handle_tool_call(
    handler: Any,
    bridge: Any,
    payload: dict[str, Any],
    *,
    cache_tts_and_push: TtsPushCallback,
) -> None:
    tool_name = str(payload.get("name") or payload.get("tool") or "").strip()
    args = payload.get("arguments", payload.get("args", payload.get("input", {})))
    if not isinstance(args, dict):
        args = {}
    if not tool_name:
        handler.send_json({"ok": False, "error": "tool name required"}, HTTPStatus.BAD_REQUEST)
        return
    tool_meta = bridge.skills.public_item(tool_name)
    if tool_meta is None:
        handler.send_json({"ok": False, "error": f"unknown tool: {tool_name}", "tool": tool_name}, HTTPStatus.NOT_FOUND)
        return
    if bool(tool_meta.get("confirm_required")) and not bool(payload.get("confirmed", False)):
        handler.send_json({
            "ok": False,
            "error": "confirmation required",
            "tool": tool_name,
            "risk": tool_meta.get("risk", "unknown"),
            "confirm_required": True,
        }, HTTPStatus.CONFLICT)
        return

    result = bridge.execute_skill(tool_name, args)
    reply = str(result.get("reply") or result.get("speech") or "").strip()
    _maybe_speak_tool_reply(bridge, payload, result, reply, cache_tts_and_push)
    remember_turn({
        "kind": "tool_call",
        "ok": bool(result.get("ok")),
        "tool": tool_name,
        "args": args,
        "reply": reply,
        "risk": tool_meta.get("risk", ""),
        "target": tool_meta.get("target", ""),
        "tts_ready": bool(result.get("tts_cached", {}).get("ready")),
        "dualeye_play": result.get("dualeye_play", {}),
        "error": str(result.get("error", "")),
    })
    handler.send_json(compact_audio_payload({
        "ok": bool(result.get("ok")),
        "protocol": "atlas.tools.v0.desk_apps",
        "tool": tool_name,
        "result": result,
    }), HTTPStatus.OK if result.get("ok") else HTTPStatus.BAD_GATEWAY)


def handle_legacy_skill_call(
    handler: Any,
    bridge: Any,
    payload: dict[str, Any],
    *,
    cache_tts_and_push: TtsPushCallback,
) -> None:
    skill_name = str(payload.get("skill") or payload.get("name") or "").strip()
    args = payload.get("args", {})
    if not isinstance(args, dict):
        args = {}
    if not skill_name:
        handler.send_json({"ok": False, "error": "skill required"}, HTTPStatus.BAD_REQUEST)
        return

    result = bridge.execute_skill(skill_name, args)
    reply = str(result.get("reply") or result.get("speech") or "").strip()
    _maybe_speak_tool_reply(bridge, payload, result, reply, cache_tts_and_push)
    remember_turn({
        "kind": "skill",
        "ok": bool(result.get("ok")),
        "skill": skill_name,
        "args": args,
        "reply": reply,
        "tts_ready": bool(result.get("tts_cached", {}).get("ready")),
        "dualeye_play": result.get("dualeye_play", {}),
        "error": str(result.get("error", "")),
    })
    handler.send_json(compact_audio_payload(result), HTTPStatus.OK if result.get("ok") else HTTPStatus.BAD_GATEWAY)
