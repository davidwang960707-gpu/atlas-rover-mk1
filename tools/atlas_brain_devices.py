"""DualEye device access layer for Atlas Brain.

The server keeps the public ``Bridge`` object for compatibility, while this
module owns the concrete device HTTP details: status reads, pairing retry,
intent delivery, UI config, OPUS controls, and device summaries.
"""

from __future__ import annotations

import json
import urllib.error
import urllib.parse
import urllib.request
from typing import Any, Callable, Optional

from atlas_brain_core import AtlasDevice, PlatformBackend


NO_PROXY_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def http_json(url: str, timeout: float = 5.0) -> dict[str, Any]:
    with NO_PROXY_OPENER.open(url, timeout=timeout) as resp:
        raw = resp.read().decode("utf-8", errors="replace")
    return json.loads(raw)


def post_form(url: str, values: dict[str, str], timeout: float = 8.0) -> dict[str, Any]:
    data = urllib.parse.urlencode(values).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=data,
        method="POST",
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    try:
        with NO_PROXY_OPENER.open(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
            payload = json.loads(raw) if raw else {"ok": True}
            payload.setdefault("status", resp.status)
            return payload
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8", errors="replace")
        try:
            payload = json.loads(raw) if raw else {}
        except json.JSONDecodeError:
            payload = {"body": raw}
        payload.setdefault("ok", False)
        payload["status"] = exc.code
        return payload


def is_pairing_error(payload: dict[str, Any]) -> bool:
    error = str(payload.get("error", "")).lower()
    return int(payload.get("status", 0) or 0) == 403 or "pairing" in error


def clamp_int(value: int, minimum: int, maximum: int) -> int:
    return max(minimum, min(maximum, value))


def int_or_default(value: Any, default: int) -> int:
    try:
        return int(value if value not in {None, ""} else default)
    except (TypeError, ValueError):
        return default


class DualEyeDeviceClient:
    def __init__(
        self,
        *,
        base_url: str,
        pin: str,
        dry_run: bool,
        session: Any,
        platform: PlatformBackend,
        latest_audio_stream_meta: Callable[[], dict[str, Any]],
    ) -> None:
        self.base_url = base_url.rstrip("/")
        self.pin = pin.strip()
        self.dry_run = dry_run
        self.session = session
        self.platform = platform
        self.latest_audio_stream_meta = latest_audio_stream_meta
        self.last_status: dict[str, Any] = {}

    def status(self, timeout: float = 1.2) -> dict[str, Any]:
        try:
            self.last_status = http_json(f"{self.base_url}/api/status/lite", timeout=timeout)
        except Exception:
            self.last_status = http_json(f"{self.base_url}/api/status", timeout=max(timeout, 2.5))
        code = str(self.last_status.get("pairing_code", "")).strip()
        if code:
            self.pin = code
        self.session.update_from_status(self.last_status)
        return self.last_status

    def capabilities(self) -> dict[str, Any]:
        return http_json(f"{self.base_url}/api/capabilities")

    def ota_status(self) -> dict[str, Any]:
        return http_json(f"{self.base_url}/api/ota/status")

    def device_summary(self) -> dict[str, Any]:
        status: dict[str, Any] = {}
        online = False
        error = ""
        if self.dry_run:
            error = "dry_run"
        else:
            try:
                status = self.status(timeout=1.2)
                online = bool(status.get("ok"))
            except Exception as exc:
                error = str(exc)
        ui = status.get("ui") if isinstance(status.get("ui"), dict) else {}
        scene = status.get("scene") if isinstance(status.get("scene"), dict) else {}
        wifi = status.get("wifi") if isinstance(status.get("wifi"), dict) else {}
        features = status.get("features") if isinstance(status.get("features"), dict) else {}
        runtime = status.get("runtime") if isinstance(status.get("runtime"), dict) else {}
        audio_service = status.get("audio_service") if isinstance(status.get("audio_service"), dict) else {}
        voice_wake = status.get("voice_wake") if isinstance(status.get("voice_wake"), dict) else {}
        summary = {
            "id": self.session.device_id or "dualeye",
            "name": "小鲅 X1",
            "model": "waveshare-dualeye-s3-1.28",
            "url": self.base_url,
            "online": online,
            "error": error,
            "app_path": f"/devices/{urllib.parse.quote(self.session.device_id or 'dualeye')}/app",
            "admin_path": "/admin",
            "page": str(ui.get("page", "")),
            "theme": str(ui.get("theme", "")),
            "chat_mode": str(ui.get("chat_mode", "pet_head") or "pet_head"),
            "expression": str(ui.get("expression", "")),
            "pet_visual": self.session.pet_visual_snapshot(),
            "scene": str(scene.get("label") or scene.get("state") or ""),
            "scene_state": str(scene.get("state", "")),
            "scene_title": str(scene.get("title", "")),
            "scene_severity": str(scene.get("severity", "")),
            "needs_attention": bool(scene.get("needs_attention", False)),
            "runtime_state": str(runtime.get("state", "")),
            "audio_mode": str(audio_service.get("mode", "")),
            "continuous_voice": bool(voice_wake.get("enabled", False)),
            "sta_ip": str(wifi.get("sta_ip", "")),
            "features": features,
        }
        self.platform.devices.upsert(AtlasDevice(
            device_id=str(summary["id"]),
            name=str(summary["name"]),
            model=str(summary["model"]),
            base_url=self.base_url,
            kind="dualeye",
            online=online,
            capabilities=features,
            status={
                "page": summary["page"],
                "theme": summary["theme"],
                "chat_mode": summary["chat_mode"],
                "expression": summary["expression"],
                "pet_visual": summary["pet_visual"],
                "scene": summary["scene"],
                "scene_state": summary["scene_state"],
                "scene_title": summary["scene_title"],
                "scene_severity": summary["scene_severity"],
                "needs_attention": summary["needs_attention"],
                "runtime_state": summary["runtime_state"],
                "audio_mode": summary["audio_mode"],
                "continuous_voice": summary["continuous_voice"],
                "sta_ip": summary["sta_ip"],
                "error": error,
            },
            app_path=str(summary["app_path"]),
            admin_path="/admin",
            tags=["dual-screen", "voice", "desktop-companion"],
        ))
        return summary

    def devices(self) -> list[dict[str, Any]]:
        self.device_summary()
        return self.platform.devices.list()

    def ensure_pin(self) -> str:
        if self.pin:
            return self.pin
        try:
            self.status()
        except Exception:
            pass
        return self.pin

    def post_form(self, path: str, values: dict[str, str], timeout: float = 8.0) -> dict[str, Any]:
        pin = self.ensure_pin()
        if self.dry_run:
            return {"ok": True, "dry_run": True, "path": path, "values": values}
        if not pin:
            return {"ok": False, "error": "pairing code unavailable"}

        body = {"pin": pin}
        body.update(values)
        result = post_form(f"{self.base_url}{path}", body, timeout=timeout)
        if not is_pairing_error(result):
            return result

        old_pin = self.pin
        try:
            self.status()
        except Exception as exc:
            result["pairing_refresh_error"] = str(exc)
            return result

        if not self.pin or self.pin == old_pin:
            result["pairing_refreshed"] = bool(self.pin)
            return result

        body["pin"] = self.pin
        retry = post_form(f"{self.base_url}{path}", body, timeout=timeout)
        retry["pairing_retried"] = True
        return retry

    def play_latest_tts(self, tts_url: str) -> dict[str, Any]:
        return self.post_form("/api/audio/play-url", {"url": tts_url}, timeout=60.0)

    def run_opus_probe(self, duration_ms: int = 1200) -> dict[str, Any]:
        duration_ms = clamp_int(duration_ms, 60, 3000)
        return self.post_form("/api/audio/opus-probe", {"duration": str(duration_ms)}, timeout=20.0)

    def start_opus_stream(self, ws_url: str, duration_ms: int = 5000) -> dict[str, Any]:
        duration_ms = clamp_int(duration_ms, 0, 300000)
        values = {"url": ws_url, "duration": str(duration_ms)}
        return self.post_form("/api/audio/opus-stream/start", values, timeout=10.0)

    def stop_opus_stream(self) -> dict[str, Any]:
        return self.post_form("/api/audio/opus-stream/stop", {}, timeout=8.0)

    def opus_stream_status(self) -> dict[str, Any]:
        if self.dry_run:
            return {"ok": True, "dry_run": True, "stream": self.latest_audio_stream_meta()}
        return http_json(f"{self.base_url}/api/audio/opus-stream/status", timeout=3.0)

    def send_intent(self, intent: dict[str, Any]) -> dict[str, Any]:
        values = {"intent": json.dumps(intent, ensure_ascii=False, separators=(",", ":"))}
        result = self.post_form("/api/intent", values)
        if result.get("ok") or int(result.get("status", 0) or 0) not in {404, 405}:
            return result
        fallback = self.post_form("/api/brain/intent", values)
        fallback["alias_endpoint_used"] = True
        fallback["preferred_endpoint"] = "/api/intent"
        return fallback

    def send_intents(self, intents: list[dict[str, Any]]) -> list[dict[str, Any]]:
        return [self.send_intent(intent) for intent in intents]

    def set_theme(
        self,
        theme: str,
        brightness: Optional[int] = None,
        volume: Optional[int] = None,
        chat_mode: Optional[str] = None,
    ) -> dict[str, Any]:
        theme = theme.strip()
        if not theme:
            return {"ok": False, "error": "theme required"}
        ui = {}
        try:
            status = self.status()
            ui_obj = status.get("ui", {})
            if isinstance(ui_obj, dict):
                ui = ui_obj
        except Exception:
            ui = {}
        use_brightness = clamp_int(int_or_default(brightness if brightness is not None else ui.get("brightness", 70), 70), 0, 100)
        use_volume = clamp_int(int_or_default(volume if volume is not None else ui.get("volume", 90), 90), 0, 100)
        use_chat_mode = str(chat_mode or ui.get("chat_mode") or "pet_head").strip() or "pet_head"
        result = self.post_form(
            "/api/config/ui",
            {
                "theme": theme,
                "chat_mode": use_chat_mode,
                "brightness": str(use_brightness),
                "volume": str(use_volume),
            },
        )
        if result.get("ok"):
            self.session.current_theme = theme
        return result

    def set_chat_mode(self, chat_mode: str) -> dict[str, Any]:
        chat_mode = chat_mode.strip()
        if chat_mode not in {"pet_head", "text", "eyes_only"}:
            return {"ok": False, "error": "chat_mode must be pet_head/text/eyes_only"}
        ui: dict[str, Any] = {}
        try:
            status = self.status()
            ui_obj = status.get("ui", {})
            if isinstance(ui_obj, dict):
                ui = ui_obj
        except Exception:
            ui = {}
        result = self.post_form(
            "/api/config/ui",
            {
                "theme": str(ui.get("theme") or self.session.current_theme or "pet"),
                "chat_mode": chat_mode,
                "brightness": str(clamp_int(int_or_default(ui.get("brightness", 70), 70), 0, 100)),
                "volume": str(clamp_int(int_or_default(ui.get("volume", 90), 90), 0, 100)),
            },
        )
        if result.get("ok"):
            self.session.current_chat_mode = chat_mode
        return result
