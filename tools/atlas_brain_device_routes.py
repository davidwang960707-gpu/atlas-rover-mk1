"""Device route helpers for Atlas Brain HTTP handlers."""

from __future__ import annotations

import time
from http import HTTPStatus
from typing import Any

from atlas_brain_audio import latest_tts_meta, latest_turn_meta
from atlas_brain_devices import http_json
from atlas_web_ui import render_device_app_page, render_devices_page


def live_device_payload(bridge: Any) -> dict[str, Any]:
    device = bridge.device_summary()
    online = bool(device.get("online"))
    status = bridge.last_status if online and isinstance(bridge.last_status, dict) else {}
    scene = status.get("scene") if isinstance(status.get("scene"), dict) else {}
    ui = status.get("ui") if isinstance(status.get("ui"), dict) else {}
    runtime = status.get("runtime") if isinstance(status.get("runtime"), dict) else {}
    audio_service = status.get("audio_service") if isinstance(status.get("audio_service"), dict) else {}
    voice_wake = status.get("voice_wake") if isinstance(status.get("voice_wake"), dict) else {}
    if not ui:
        ui = {
            "page": device.get("page", ""),
            "theme": device.get("theme", ""),
            "chat_mode": device.get("chat_mode", "pet_head"),
            "expression": device.get("expression", ""),
        }
    if not scene:
        if online:
            scene = {
                "state": device.get("scene_state") or "idle",
                "label": device.get("scene") or "待机",
                "title": device.get("scene_title") or "DualEye 已连接",
                "severity": device.get("scene_severity") or "info",
                "needs_attention": bool(device.get("needs_attention", False)),
            }
        else:
            scene = {
                "state": "offline",
                "label": "设备离线",
                "title": "DualEye 不可达",
                "subtitle": str(device.get("error", "")),
                "hint": "检查 Wi-Fi、设备 IP 或 USB 供电",
                "severity": "warn",
                "needs_attention": True,
            }
    if not audio_service:
        audio_service = {
            "mode": device.get("audio_mode") or "idle",
            "active": False,
        }
    if not voice_wake:
        voice_wake = {
            "enabled": bool(device.get("continuous_voice", False)),
        }
    return {
        "ok": True,
        "online": online,
        "service": "atlas-brain",
        "dualeye_url": bridge.dualeye_url,
        "pairing_code_known": bool(bridge.pin),
        "device_id": device.get("id", "dualeye"),
        "device": device,
        "scene": scene,
        "ui": ui,
        "pet_visual": bridge.session.pet_visual_snapshot(),
        "runtime": runtime,
        "audio_service": audio_service,
        "voice_wake": voice_wake,
        "providers": {
            "llm_enabled": bridge.llm_enabled(),
            "asr_enabled": bridge.asr_enabled(),
            "tts_enabled": bridge.tts_enabled(),
        },
        "latest_tts": latest_tts_meta(),
        "last_turn": latest_turn_meta(),
        "updated_at": int(time.time()),
    }


def devices_payload(bridge: Any, *, rover_skills_enabled: bool) -> dict[str, Any]:
    snapshot = bridge.platform_snapshot()
    return {
        "ok": True,
        "devices": snapshot["devices"],
        "admin_path": "/admin",
        "platform": {
            "service": "atlas-brain",
            "device_count": snapshot["summary"]["device_count"],
            "rover_skills_enabled": rover_skills_enabled,
        },
    }


def handle_status(handler: Any, bridge: Any) -> None:
    try:
        handler.send_json({"ok": True, "dualeye": bridge.status()})
    except Exception as exc:
        handler.send_json({"ok": False, "error": str(exc)}, HTTPStatus.BAD_GATEWAY)


def handle_device_scene(handler: Any, bridge: Any) -> None:
    try:
        status = bridge.status()
        handler.send_json({
            "ok": True,
            "device_id": bridge.session.device_id or "dualeye",
            "device": bridge.device_summary(),
            "scene": status.get("scene", {}),
            "ui": status.get("ui", {}),
            "runtime": status.get("runtime", {}),
            "audio_service": status.get("audio_service", {}),
            "voice_wake": status.get("voice_wake", {}),
            "last_turn": latest_turn_meta(),
        })
    except Exception as exc:
        device = bridge.device_summary()
        handler.send_json({
            "ok": False,
            "offline": True,
            "error": str(exc),
            "device_id": device.get("id", "dualeye"),
            "scene": {
                "state": "offline",
                "label": "设备离线",
                "title": "DualEye 不可达",
                "subtitle": str(device.get("error", "") or exc),
                "hint": "检查 Wi-Fi、设备 IP 或 USB 供电",
                "severity": "error",
                "needs_attention": True,
            },
            "ui": {
                "page": device.get("page", ""),
                "theme": device.get("theme", ""),
                "expression": device.get("expression", ""),
            },
            "runtime": {},
            "audio_service": {},
            "voice_wake": {},
        })


def handle_device_selftest(handler: Any, bridge: Any) -> None:
    try:
        handler.send_json(http_json(f"{bridge.dualeye_url}/api/selftest", timeout=3.0))
    except Exception as exc:
        handler.send_json({"ok": False, "error": str(exc), "dualeye_url": bridge.dualeye_url}, HTTPStatus.BAD_GATEWAY)


def handle_device_system_info(handler: Any, bridge: Any) -> None:
    try:
        handler.send_json(http_json(f"{bridge.dualeye_url}/api/system/info", timeout=3.0))
    except Exception as exc:
        handler.send_json({"ok": False, "error": str(exc), "dualeye_url": bridge.dualeye_url}, HTTPStatus.BAD_GATEWAY)


def handle_device_opus_stream_status(handler: Any, bridge: Any) -> None:
    try:
        handler.send_json(bridge.dualeye_opus_stream_status())
    except Exception as exc:
        handler.send_json({"ok": False, "error": str(exc), "dualeye_url": bridge.dualeye_url}, HTTPStatus.BAD_GATEWAY)


def handle_device_detail(handler: Any, bridge: Any, device_id: str) -> None:
    snapshot = bridge.platform_snapshot()
    for device in snapshot["devices"]:
        if str(device.get("id")) == device_id:
            handler.send_json({"ok": True, "device": device})
            return
    handler.send_json({"ok": False, "error": f"device not found: {device_id}"}, HTTPStatus.NOT_FOUND)


def handle_devices_page(handler: Any, bridge: Any) -> None:
    handler.send_html(render_devices_page(bridge.devices()))


def handle_device_app_page(handler: Any, bridge: Any, *, lan_url: str, rover_skills_enabled: bool) -> None:
    handler.send_html(render_device_app_page(
        bridge.device_summary(),
        dualeye_url=bridge.dualeye_url,
        lan_url=lan_url,
        rover_enabled=rover_skills_enabled,
    ))
