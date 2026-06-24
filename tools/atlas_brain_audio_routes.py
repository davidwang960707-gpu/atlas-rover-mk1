"""HTTP audio routes for Atlas Brain.

This module keeps request/response details for browser audio, device WAV
compatibility, TTS cache push, ASR/TTS probes and OPUS stream control out of
the main server handler. WebSocket audio streaming still lives in
atlas_brain_server.py for now and is the next split target.
"""

from __future__ import annotations

import base64
import sys
import time
import urllib.parse
from http import HTTPStatus
from typing import Any

from atlas_brain_audio import (
    compact_audio_payload,
    latest_audio_stream_meta,
    recent_audio_streams,
    remember_audio_stream,
    remember_turn,
    simulate_audio_stream,
    store_latest_tts,
)


def _clamp_int(value: int, minimum: int, maximum: int) -> int:
    return max(minimum, min(maximum, value))


def _log_snippet(value: object, limit: int = 80) -> str:
    text = str(value or "").replace("\n", " ").replace("\r", " ").strip()
    if len(text) > limit:
        text = text[:limit - 3] + "..."
    return ascii(text)


def _reply_from_text_result(text_result: dict[str, Any], fallback: str = "") -> str:
    reply = str(text_result.get("llm", {}).get("reply", "")).strip()
    if reply:
        return reply
    for intent in text_result.get("intents", []):
        if not isinstance(intent, dict) or intent.get("tool") != "atlas_chat":
            continue
        input_obj = intent.get("input", {})
        if not isinstance(input_obj, dict):
            continue
        for key in ("speech", "chat_text"):
            value = str(input_obj.get(key, "")).strip()
            if value:
                return value
    return fallback.strip()


def _cache_tts_and_push(bridge: Any, tts_payload: dict[str, Any], latest_tts_url: str) -> tuple[dict[str, Any], str, dict[str, Any]]:
    tts_store = store_latest_tts(tts_payload)
    if not tts_store.get("ready"):
        return tts_store, "", {"ok": False, "error": str(tts_store.get("error", "tts not ready"))}
    return tts_store, latest_tts_url, bridge.play_latest_tts_on_dualeye(latest_tts_url)


def audio_stream_status_payload(bridge: Any, audio_ws_url: str) -> dict[str, Any]:
    try:
        dualeye_stream = bridge.dualeye_opus_stream_status()
    except Exception as exc:
        dualeye_stream = {"ok": False, "error": str(exc), "dualeye_url": bridge.dualeye_url}
    return {
        "ok": True,
        "protocol": "atlas.audio.stream.v0",
        "stage": "P2_dualeye_ws_opus_stream",
        "ws_endpoint": "/ws/audio",
        "ws_url_for_dualeye": audio_ws_url,
        "dualeye_stream_start_endpoint": "/api/device/opus-stream/start",
        "dualeye_stream_stop_endpoint": "/api/device/opus-stream/stop",
        "dualeye_opus_probe_endpoint": "/api/device/opus-probe",
        "dualeye_opus_probe_notes": "探针仍保留；/api/device/opus-stream/start 触发 OPUS 真流，/api/device/opus-turn/start 在流结束后自动进入 ASR/LLM/TTS。",
        "dualeye_stream": dualeye_stream,
        "runtime": bridge.runtime.snapshot(),
        "last_stream": latest_audio_stream_meta(),
        "recent_streams": recent_audio_streams(),
    }


def handle_device_wav_turn(handler: Any, bridge: Any, raw: bytes, query: dict[str, list[str]], latest_tts_url: str) -> None:
    if len(raw) < 44 or not raw.startswith(b"RIFF"):
        handler.send_json({"ok": False, "error": "WAV body required"}, HTTPStatus.BAD_REQUEST)
        return
    language = (query.get("language") or ["auto"])[-1] or "auto"
    speak_raw = str((query.get("speak") or ["1"])[-1]).lower()
    speak = speak_raw not in {"0", "false", "no"}
    audio_data_url = "data:audio/wav;base64," + base64.b64encode(raw).decode("ascii")
    result = bridge.send_audio(audio_data_url, language=language, speak=speak)
    tts_store: dict[str, Any] = {"ready": False}
    if speak and isinstance(result.get("tts"), dict):
        tts_store = store_latest_tts(result["tts"])
    reply = _reply_from_text_result(result.get("text_result", {}))
    asr_text = str(result.get("asr", {}).get("text", ""))
    device_asr_text = "" if result.get("ignored") else asr_text
    sys.stderr.write(
        "[voice-turn] "
        f"ok={bool(result.get('ok'))} "
        f"asr={_log_snippet(asr_text)} "
        f"reply={_log_snippet(reply)} "
        f"source={str(result.get('text_result', {}).get('source', ''))} "
        f"tts_ready={bool(tts_store.get('ready'))} "
        f"error={_log_snippet(result.get('asr', {}).get('error', '') or result.get('error', '') or tts_store.get('error', ''), 120)}\n"
    )
    turn_error = str(result.get("asr", {}).get("error", "") or result.get("error", "") or tts_store.get("error", ""))
    device_intent_ok = bool(result.get("ok"))
    turn_ok = bool(device_intent_ok or tts_store.get("ready") or reply)
    remember_turn({
        "kind": "device_audio",
        "ok": turn_ok,
        "device_intent_ok": device_intent_ok,
        "asr_text": asr_text,
        "reply": reply,
        "source": str(result.get("text_result", {}).get("source", "")),
        "intent_count": len(result.get("text_result", {}).get("intents", [])),
        "tts_ready": bool(tts_store.get("ready")),
        "tts_bytes": int(tts_store.get("bytes", 0) or 0),
        "error": turn_error,
    })
    handler.send_json({
        "ok": turn_ok,
        "device_intent_ok": device_intent_ok,
        "asr_text": device_asr_text,
        "reply": reply,
        "source": str(result.get("text_result", {}).get("source", "")),
        "intent_count": len(result.get("text_result", {}).get("intents", [])),
        "tts_ready": bool(tts_store.get("ready")),
        "tts_url": latest_tts_url if tts_store.get("ready") else "",
        "tts": tts_store,
        "error": turn_error,
    }, HTTPStatus.OK if turn_ok else HTTPStatus.BAD_GATEWAY)


def handle_audio_stream_simulate(handler: Any, payload: dict[str, Any]) -> None:
    result = simulate_audio_stream(payload)
    handler.send_json({"ok": True, "protocol": "atlas.audio.stream.v0", "result": result})


def handle_dualeye_opus_probe(handler: Any, bridge: Any, payload: dict[str, Any]) -> None:
    duration_ms = _clamp_int(int(payload.get("duration_ms", payload.get("duration", 1200)) or 1200), 60, 3000)
    result = bridge.run_dualeye_opus_probe(duration_ms)
    result.setdefault("protocol", "atlas.audio.stream.v0")
    result.setdefault("stage", "P2_dualeye_real_opus_probe")
    result.setdefault("notes", "DualEye 真机麦克风 PCM -> 60ms OPUS 编码探针；不是正式连续推流。")
    probe = result.get("probe") if isinstance(result.get("probe"), dict) else {}
    remember_audio_stream({
        "ok": bool(result.get("ok")),
        "stage": result.get("stage", "P2_dualeye_real_opus_probe"),
        "codec": "opus",
        "sample_rate": 16000,
        "channels": 1,
        "frame_ms": 60,
        "frames": int(probe.get("frames_encoded", 0) or 0),
        "bytes": int(probe.get("encoded_bytes", 0) or 0),
        "source": "dualeye_opus_probe",
        "error": str(result.get("error", "")),
    })
    handler.send_json(result, HTTPStatus.OK if result.get("ok") else HTTPStatus.BAD_GATEWAY)


def handle_dualeye_opus_stream_start(handler: Any,
                                     bridge: Any,
                                     path: str,
                                     payload: dict[str, Any],
                                     audio_ws_url: str) -> None:
    duration_ms = _clamp_int(int(payload.get("duration_ms", payload.get("duration", 5000)) or 5000), 0, 300000)
    ws_url = str(payload.get("url", "") or "").strip() or audio_ws_url
    force_turn = path == "/api/device/opus-turn/start"
    if force_turn or bool(payload.get("turn", payload.get("asr", False))):
        parsed_ws = urllib.parse.urlparse(ws_url)
        query = urllib.parse.parse_qs(parsed_ws.query)
        query["turn"] = ["1"]
        query["speak"] = ["1" if bool(payload.get("speak", True)) else "0"]
        query["language"] = [str(payload.get("language", "zh") or "zh")]
        query["tts_style"] = [str(payload.get("tts_style", bridge.session.default_tts_style) or bridge.session.default_tts_style)]
        if str(payload.get("tts_voice", "") or "").strip():
            query["tts_voice"] = [str(payload.get("tts_voice", "")).strip()]
        if bool(payload.get("tts_singing", False)):
            query["tts_singing"] = ["1"]
        ws_url = urllib.parse.urlunparse(parsed_ws._replace(query=urllib.parse.urlencode(query, doseq=True)))
    result = bridge.start_dualeye_opus_stream(ws_url, duration_ms)
    result.setdefault("protocol", "atlas.audio.stream.v0")
    result.setdefault("stage", "P2_dualeye_ws_opus_stream")
    result.setdefault("ws_url", ws_url)
    handler.send_json(result, HTTPStatus.OK if result.get("ok") else HTTPStatus.BAD_GATEWAY)


def handle_dualeye_opus_stream_stop(handler: Any, bridge: Any) -> None:
    result = bridge.stop_dualeye_opus_stream()
    result.setdefault("protocol", "atlas.audio.stream.v0")
    result.setdefault("stage", "P2_dualeye_ws_opus_stream")
    handler.send_json(result, HTTPStatus.OK if result.get("ok") else HTTPStatus.BAD_GATEWAY)


def handle_browser_audio_turn(handler: Any, bridge: Any, payload: dict[str, Any], latest_tts_url: str) -> None:
    audio_data = str(payload.get("audio_data", "")).strip()
    language = str(payload.get("language", "auto") or "auto")
    speak = bool(payload.get("speak", False))
    tts_voice = str(payload.get("tts_voice", "")).strip()
    tts_style = str(payload.get("tts_style", bridge.session.default_tts_style) or bridge.session.default_tts_style)
    tts_singing = bool(payload.get("tts_singing", False))
    if not audio_data:
        handler.send_json({"ok": False, "error": "audio_data required"}, HTTPStatus.BAD_REQUEST)
        return
    result = bridge.send_audio(
        audio_data,
        language=language,
        speak=speak,
        tts_voice=tts_voice,
        tts_style=tts_style,
        tts_singing=tts_singing,
    )
    if speak and isinstance(result.get("tts"), dict):
        tts_store, tts_url, dualeye_play = _cache_tts_and_push(bridge, result["tts"], latest_tts_url)
        result["tts_cached"] = tts_store
        result["tts_url"] = tts_url
        result["dualeye_play"] = dualeye_play
    remember_turn({
        "kind": "browser_audio",
        "ok": bool(result.get("ok")),
        "asr_text": str(result.get("asr", {}).get("text", "")),
        "reply": _reply_from_text_result(result.get("text_result", {}), fallback=""),
        "source": str(result.get("text_result", {}).get("source", "")),
        "intent_count": len(result.get("text_result", {}).get("intents", [])),
        "tts_ready": bool(result.get("tts_cached", {}).get("ready")),
        "dualeye_play": result.get("dualeye_play", {}),
        "error": str(result.get("asr", {}).get("error", "") or result.get("error", "")),
    })
    handler.send_json(compact_audio_payload(result))


def handle_asr(handler: Any, bridge: Any, payload: dict[str, Any]) -> None:
    audio_data = str(payload.get("audio_data", "")).strip()
    language = str(payload.get("language", "auto") or "auto")
    if not audio_data:
        handler.send_json({"ok": False, "error": "audio_data required"}, HTTPStatus.BAD_REQUEST)
        return
    asr = bridge.transcribe_audio(audio_data, language=language)
    handler.send_json({"ok": bool(asr.get("ok")), "asr": asr}, HTTPStatus.OK if asr.get("ok") else HTTPStatus.BAD_GATEWAY)


def handle_speak(handler: Any, bridge: Any, payload: dict[str, Any], latest_tts_url: str) -> None:
    text = str(payload.get("text", "")).strip()
    voice = str(payload.get("voice", "")).strip()
    if not voice:
        voice = str(payload.get("tts_voice", "")).strip()
    tts_style = str(payload.get("tts_style", "default") or "default")
    if tts_style == "default" and bridge.session.default_tts_style != "default":
        tts_style = bridge.session.default_tts_style
    tts_singing = bool(payload.get("tts_singing", False))
    audio_format = str(payload.get("format", "wav") or "wav")
    result = bridge.synthesize_speech(
        text,
        voice=voice,
        audio_format=audio_format,
        style=tts_style,
        singing=tts_singing,
    )
    if result.get("ok"):
        tts_store, tts_url, dualeye_play = _cache_tts_and_push(bridge, result, latest_tts_url)
        result["tts_cached"] = tts_store
        result["tts_url"] = tts_url
        result["dualeye_play"] = dualeye_play
    remember_turn({
        "kind": "speak",
        "ok": bool(result.get("ok")),
        "text": text,
        "tts_ready": bool(result.get("tts_cached", {}).get("ready")),
        "dualeye_play": result.get("dualeye_play", {}),
        "error": str(result.get("error", "")),
    })
    handler.send_json(compact_audio_payload(result))
