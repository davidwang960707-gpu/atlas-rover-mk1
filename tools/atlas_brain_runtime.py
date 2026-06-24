#!/usr/bin/env python3
"""Runtime primitives for Atlas Brain.

The goal is not to clone xiaozhi's server stack.  It is to keep the small Mac
bridge honest: every device connection has a session, every audio stream has
measurable frame health, and the product score is derived from observable
capabilities instead of mood.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Optional
import time
import uuid


SESSION_STATES = {
    "idle",
    "connecting",
    "listening",
    "recording",
    "thinking",
    "speaking",
    "cooldown",
    "error",
    "closed",
}

DIAGNOSTIC_STAGES = {
    "asr",
    "llm",
    "tool",
    "tts",
    "device_push",
    "playback",
    "brain_offline",
}

STAGE_NEXT_STEPS = {
    "asr": "检查 ATLAS_ASR_MODEL、音频 data URL、麦克风权限和输入音量。",
    "llm": "检查 ATLAS_LLM_API_KEY / ATLAS_LLM_BASE_URL / ATLAS_LLM_MODEL；未配置时会退回规则意图。",
    "tool": "检查工具名、参数 schema、confirm_required 和工具执行结果。",
    "tts": "检查 ATLAS_TTS_MODEL / ATLAS_TTS_VOICE；未配置时会尝试 macOS say 回退。",
    "device_push": "检查 DualEye 是否在线、配对码是否有效、/api/intent 或 /api/brain/intent 是否可达。",
    "playback": "检查 TTS 缓存是否 ready、/tts/latest.wav 是否可访问、DualEye /api/audio/play-url 是否成功。",
    "brain_offline": "检查 Mac Brain 进程和端口是否在线，确认手机/设备访问的是当前 LAN URL。",
}


def _summarize_value(value: Any, *, max_len: int = 180) -> Any:
    if isinstance(value, dict):
        return {str(k): _summarize_value(v, max_len=max_len) for k, v in list(value.items())[:12]}
    if isinstance(value, list):
        return [_summarize_value(item, max_len=max_len) for item in value[:6]]
    if isinstance(value, (int, float, bool)) or value is None:
        return value
    text = str(value)
    return text if len(text) <= max_len else text[: max_len - 3] + "..."


def summarize_arguments(args: Any) -> dict[str, Any]:
    if not isinstance(args, dict):
        return {}
    summary: dict[str, Any] = {}
    for key, value in args.items():
        lowered = str(key).lower()
        if any(secret in lowered for secret in ("key", "token", "secret", "password", "pin")):
            summary[str(key)] = "***"
        else:
            summary[str(key)] = _summarize_value(value, max_len=120)
    return summary


@dataclass
class RuntimeEvent:
    ts: float
    kind: str
    device_id: str = "dualeye"
    session_id: str = ""
    detail: str = ""
    payload: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {
            "ts": self.ts,
            "kind": self.kind,
            "device_id": self.device_id,
            "session_id": self.session_id,
            "detail": self.detail,
            "payload": self.payload,
        }


@dataclass
class SpeechSegment:
    start_seq: int
    end_seq: int
    start_ms: int
    end_ms: int
    frames: int = 0
    max_level: int = 0
    max_peak: int = 0
    avg_rms_acc: int = 0

    def note(self, seq: int, timestamp_ms: int, mic_level: int, mic_rms: int, mic_peak: int) -> None:
        self.end_seq = seq
        self.end_ms = timestamp_ms
        self.frames += 1
        self.max_level = max(self.max_level, mic_level)
        self.max_peak = max(self.max_peak, mic_peak)
        self.avg_rms_acc += mic_rms

    def to_dict(self) -> dict[str, Any]:
        return {
            "start_seq": self.start_seq,
            "end_seq": self.end_seq,
            "start_ms": self.start_ms,
            "end_ms": self.end_ms,
            "frames": self.frames,
            "duration_ms": max(0, self.end_ms - self.start_ms),
            "max_level": self.max_level,
            "max_peak": self.max_peak,
            "avg_rms": int(self.avg_rms_acc / self.frames) if self.frames else 0,
        }


@dataclass
class AudioStreamStats:
    session_id: str
    device_id: str = "dualeye"
    stage: str = "P2_dualeye_ws_opus_stream"
    codec: str = "opus"
    sample_rate: int = 16000
    channels: int = 1
    frame_ms: int = 60
    started_at: float = field(default_factory=time.time)
    ended_at: float = 0.0
    frames: int = 0
    atlas_frames: int = 0
    legacy_binary_frames: int = 0
    binary_messages: int = 0
    text_messages: int = 0
    bytes: int = 0
    wire_bytes: int = 0
    sequence_gaps: int = 0
    payload_len_mismatches: int = 0
    last_seq: int = 0
    last_packet_bytes: int = 0
    mic_level: int = 0
    mic_rms: int = 0
    mic_peak: int = 0
    turn_id: str = ""
    closed: bool = False
    speech_segments: list[SpeechSegment] = field(default_factory=list)
    _active_segment: Optional[SpeechSegment] = None
    _silence_frames: int = 0

    def note_text(self, event: dict[str, Any]) -> None:
        self.text_messages += 1
        event_type = str(event.get("type", event.get("event", "")) or "")
        if event_type == "hello":
            params = event.get("audio_params", {})
            if isinstance(params, dict):
                self.codec = str(params.get("format", self.codec) or self.codec).lower()
                self.sample_rate = int(params.get("sample_rate", self.sample_rate) or self.sample_rate)
                self.channels = int(params.get("channels", self.channels) or self.channels)
                self.frame_ms = int(params.get("frame_duration", params.get("frame_ms", self.frame_ms)) or self.frame_ms)
        elif event_type == "start":
            self.codec = str(event.get("codec", self.codec) or self.codec).lower()
            self.sample_rate = int(event.get("sample_rate", self.sample_rate) or self.sample_rate)
            self.channels = int(event.get("channels", self.channels) or self.channels)
            self.frame_ms = int(event.get("frame_ms", self.frame_ms) or self.frame_ms)
            self.turn_id = str(event.get("turn_id", self.turn_id) or self.turn_id)
        elif event_type == "listen":
            self.turn_id = str(event.get("turn_id", self.turn_id) or self.turn_id)

    def note_binary(self, payload_len: int, wire_len: int, frame: dict[str, Any]) -> None:
        self.binary_messages += 1
        self.wire_bytes += wire_len
        if frame.get("ok"):
            self.atlas_frames += 1
            self.frames += 1
            actual_payload_len = int(frame.get("actual_payload_len", payload_len) or 0)
            self.bytes += actual_payload_len
            self.last_packet_bytes = actual_payload_len
            self.sample_rate = int(frame.get("sample_rate", self.sample_rate) or self.sample_rate)
            self.channels = int(frame.get("channels", self.channels) or self.channels)
            self.frame_ms = int(frame.get("frame_ms", self.frame_ms) or self.frame_ms)
            self.mic_level = int(frame.get("mic_level", 0) or 0)
            self.mic_rms = int(frame.get("mic_rms", 0) or 0)
            self.mic_peak = int(frame.get("mic_peak", 0) or 0)
            seq = int(frame.get("seq", 0) or 0)
            if self.last_seq and seq > self.last_seq + 1:
                self.sequence_gaps += seq - self.last_seq - 1
            self.last_seq = seq
            if not bool(frame.get("payload_len_match", True)):
                self.payload_len_mismatches += 1
            self._note_vad(seq, int(frame.get("timestamp_ms", 0) or 0), self.mic_level, self.mic_rms, self.mic_peak)
        else:
            self.legacy_binary_frames += 1
            self.frames += 1
            self.bytes += payload_len
            self.last_packet_bytes = payload_len

    def _note_vad(self, seq: int, timestamp_ms: int, mic_level: int, mic_rms: int, mic_peak: int) -> None:
        voice = mic_level >= 24 or mic_rms >= 220 or mic_peak >= 1200
        if voice:
            self._silence_frames = 0
            if self._active_segment is None:
                self._active_segment = SpeechSegment(
                    start_seq=seq,
                    end_seq=seq,
                    start_ms=timestamp_ms,
                    end_ms=timestamp_ms,
                )
            self._active_segment.note(seq, timestamp_ms, mic_level, mic_rms, mic_peak)
            return

        if self._active_segment is not None:
            self._silence_frames += 1
            if self._silence_frames >= 5:
                self.speech_segments.append(self._active_segment)
                self._active_segment = None
                self._silence_frames = 0

    def close(self) -> None:
        self.closed = True
        self.ended_at = time.time()
        if self._active_segment is not None:
            self.speech_segments.append(self._active_segment)
            self._active_segment = None

    def to_dict(self) -> dict[str, Any]:
        segments = [segment.to_dict() for segment in self.speech_segments]
        if self._active_segment is not None:
            segments.append(self._active_segment.to_dict())
        return {
            "ok": self.atlas_frames > 0 and self.sequence_gaps == 0,
            "stage": self.stage,
            "session_id": self.session_id,
            "device_id": self.device_id,
            "codec": self.codec,
            "sample_rate": self.sample_rate,
            "channels": self.channels,
            "frame_ms": self.frame_ms,
            "frames": self.frames,
            "atlas_frames": self.atlas_frames,
            "legacy_binary_frames": self.legacy_binary_frames,
            "binary_messages": self.binary_messages,
            "text_messages": self.text_messages,
            "bytes": self.bytes,
            "wire_bytes": self.wire_bytes,
            "sequence_gaps": self.sequence_gaps,
            "payload_len_mismatches": self.payload_len_mismatches,
            "last_seq": self.last_seq,
            "last_packet_bytes": self.last_packet_bytes,
            "mic_level": self.mic_level,
            "mic_rms": self.mic_rms,
            "mic_peak": self.mic_peak,
            "estimated_audio_ms": self.frames * self.frame_ms,
            "started_at": self.started_at,
            "ended_at": self.ended_at,
            "closed": self.closed,
            "turn_id": self.turn_id,
            "speech_segments": segments,
            "speech_segment_count": len(segments),
        }


@dataclass
class DeviceSession:
    session_id: str
    device_id: str = "dualeye"
    transport: str = "unknown"
    state: str = "idle"
    created_at: float = field(default_factory=time.time)
    updated_at: float = field(default_factory=time.time)
    turn_count: int = 0
    error_count: int = 0
    last_error: str = ""
    last_detail: str = ""
    stream: Optional[AudioStreamStats] = None

    def transition(self, state: str, detail: str = "") -> None:
        if state not in SESSION_STATES:
            state = "error"
            self.error_count += 1
            self.last_error = f"invalid state: {state}"
        self.state = state
        self.last_detail = detail
        self.updated_at = time.time()

    def to_dict(self) -> dict[str, Any]:
        return {
            "session_id": self.session_id,
            "device_id": self.device_id,
            "transport": self.transport,
            "state": self.state,
            "created_at": self.created_at,
            "updated_at": self.updated_at,
            "turn_count": self.turn_count,
            "error_count": self.error_count,
            "last_error": self.last_error,
            "last_detail": self.last_detail,
            "stream": self.stream.to_dict() if self.stream is not None else {},
        }


class AtlasBrainRuntime:
    def __init__(self) -> None:
        self.sessions: dict[str, DeviceSession] = {}
        self.events: list[RuntimeEvent] = []
        self.closed_streams: list[dict[str, Any]] = []
        self.timeline: list[dict[str, Any]] = []
        self.tool_calls: list[dict[str, Any]] = []
        self.turns: list[dict[str, Any]] = []

    def emit(self, kind: str, device_id: str = "dualeye", session_id: str = "", detail: str = "", payload: Optional[dict[str, Any]] = None) -> RuntimeEvent:
        event = RuntimeEvent(time.time(), kind, device_id=device_id, session_id=session_id, detail=detail, payload=payload or {})
        self.events.append(event)
        del self.events[:-120]
        return event

    def open_session(self, device_id: str = "dualeye", transport: str = "websocket") -> DeviceSession:
        session = DeviceSession(session_id=f"atlas-{uuid.uuid4().hex[:12]}", device_id=device_id, transport=transport, state="connecting")
        self.sessions[session.session_id] = session
        self.emit("session.open", device_id=device_id, session_id=session.session_id, detail=transport)
        return session

    def close_session(self, session_id: str, reason: str = "closed") -> None:
        session = self.sessions.get(session_id)
        if session is None:
            return
        if session.stream is not None:
            session.stream.close()
            self.closed_streams.append(session.stream.to_dict())
            del self.closed_streams[:-30]
        session.transition("closed", reason)
        self.emit("session.close", device_id=session.device_id, session_id=session_id, detail=reason)

    def start_audio_stream(self, session_id: str, device_id: str = "dualeye") -> AudioStreamStats:
        session = self.sessions.get(session_id)
        if session is None:
            session = self.open_session(device_id=device_id, transport="websocket_audio")
        stream = AudioStreamStats(session_id=session.session_id, device_id=device_id)
        session.stream = stream
        session.transition("recording", "audio stream started")
        self.emit("audio.stream.start", device_id=device_id, session_id=session.session_id)
        self.record_timeline("asr", True, "audio stream started", {"session_id": session.session_id})
        return stream

    def record_timeline(self,
                        stage: str,
                        ok: bool,
                        detail: str = "",
                        payload: Optional[dict[str, Any]] = None,
                        *,
                        elapsed_ms: int = 0,
                        turn_id: str = "") -> dict[str, Any]:
        if stage not in DIAGNOSTIC_STAGES:
            stage = "brain_offline"
        item = {
            "ts": time.time(),
            "stage": stage,
            "ok": bool(ok),
            "detail": str(detail or ""),
            "elapsed_ms": int(elapsed_ms or 0),
            "turn_id": str(turn_id or ""),
            "next_step": "" if ok else STAGE_NEXT_STEPS.get(stage, ""),
            "payload": _summarize_value(payload or {}, max_len=180),
        }
        self.timeline.append(item)
        del self.timeline[:-80]
        self.emit(f"diagnostic.{stage}", detail=item["detail"], payload={k: v for k, v in item.items() if k != "payload"})
        return item

    def record_tool_call(self,
                         *,
                         name: str,
                         arguments: Optional[dict[str, Any]],
                         risk: str = "",
                         ok: bool = False,
                         error: str = "",
                         error_code: str = "",
                         elapsed_ms: int = 0,
                         tts_requested: bool = False,
                         tts_ready: bool = False,
                         confirm_required: bool = False,
                         confirmation_missing: bool = False,
                         target: str = "",
                         legacy: bool = False) -> dict[str, Any]:
        item = {
            "ts": time.time(),
            "name": str(name or ""),
            "arguments": summarize_arguments(arguments or {}),
            "risk": str(risk or ""),
            "target": str(target or ""),
            "ok": bool(ok),
            "error_code": str(error_code or ""),
            "error": str(error or ""),
            "elapsed_ms": int(elapsed_ms or 0),
            "tts_requested": bool(tts_requested),
            "tts_ready": bool(tts_ready),
            "confirm_required": bool(confirm_required),
            "confirmation_missing": bool(confirmation_missing),
            "legacy": bool(legacy),
        }
        self.tool_calls.append(item)
        del self.tool_calls[:-40]
        detail = f"{item['name']} ok={item['ok']}"
        if item["error"]:
            detail += f" error={item['error'][:120]}"
        self.record_timeline("tool", bool(ok), detail, {"tool": item["name"], "arguments": item["arguments"]}, elapsed_ms=elapsed_ms)
        return item

    def record_turn_diagnosis(self,
                              *,
                              kind: str,
                              ok: bool,
                              text: str = "",
                              reply: str = "",
                              turn_id: str = "",
                              source: str = "",
                              stages: Optional[list[dict[str, Any]]] = None,
                              error: str = "") -> dict[str, Any]:
        clean_stages = []
        failed_stage = ""
        for stage in stages or []:
            name = str(stage.get("stage", "") or "")
            if name not in DIAGNOSTIC_STAGES:
                name = "brain_offline"
            item = {
                "stage": name,
                "ok": bool(stage.get("ok")),
                "detail": str(stage.get("detail", "")),
                "elapsed_ms": int(stage.get("elapsed_ms", 0) or 0),
                "next_step": "" if stage.get("ok") else STAGE_NEXT_STEPS.get(name, ""),
            }
            if not item["ok"] and not failed_stage:
                failed_stage = name
            clean_stages.append(item)
            self.record_timeline(name, item["ok"], item["detail"], {"kind": kind}, elapsed_ms=item["elapsed_ms"], turn_id=turn_id)
        item = {
            "ts": time.time(),
            "kind": str(kind or "turn"),
            "ok": bool(ok),
            "turn_id": str(turn_id or ""),
            "text": str(text or "")[:180],
            "reply": str(reply or "")[:220],
            "source": str(source or ""),
            "failed_stage": "" if ok else (failed_stage or self.infer_failed_stage(error)),
            "error": str(error or ""),
            "stages": clean_stages,
            "next_step": "" if ok else STAGE_NEXT_STEPS.get(failed_stage or self.infer_failed_stage(error), ""),
        }
        self.turns.append(item)
        del self.turns[:-30]
        return item

    def infer_failed_stage(self, error: str = "") -> str:
        for item in reversed(self.timeline):
            if not item.get("ok"):
                return str(item.get("stage") or "brain_offline")
        lowered = str(error or "").lower()
        if "asr" in lowered:
            return "asr"
        if "tts" in lowered or "say" in lowered:
            return "tts"
        if "llm" in lowered or "provider" in lowered or "api_key" in lowered:
            return "llm"
        if "dualeye" in lowered or "intent" in lowered or "connection" in lowered:
            return "device_push"
        return "brain_offline"

    def diagnostics_snapshot(self) -> dict[str, Any]:
        latest_turn = dict(self.turns[-1]) if self.turns else {}
        latest_tool = dict(self.tool_calls[-1]) if self.tool_calls else {}
        latest_failure = {}
        for item in reversed(self.timeline):
            if not item.get("ok"):
                latest_failure = dict(item)
                break
        if latest_failure:
            failed_stage = str(latest_failure.get("stage") or "brain_offline")
        elif latest_turn:
            failed_stage = str(latest_turn.get("failed_stage") or "")
        else:
            failed_stage = ""
        return {
            "ok": not bool(latest_failure),
            "protocol": "atlas.runtime.diagnostics.v0",
            "answerable_stages": sorted(DIAGNOSTIC_STAGES),
            "stage": failed_stage or "ready",
            "latest_failure": latest_failure,
            "latest_turn": latest_turn,
            "latest_tool": latest_tool,
            "timeline": list(self.timeline[-30:]),
            "recent_tool_calls": list(reversed(self.tool_calls[-10:])),
            "recent_turns": list(reversed(self.turns[-10:])),
            "next_step": STAGE_NEXT_STEPS.get(failed_stage, ""),
        }

    def latest_stream(self) -> dict[str, Any]:
        active = [
            session.stream.to_dict()
            for session in self.sessions.values()
            if session.stream is not None and not session.stream.closed
        ]
        if active:
            return active[-1]
        if self.closed_streams:
            return dict(self.closed_streams[-1])
        return {}

    def snapshot(self) -> dict[str, Any]:
        sessions = [session.to_dict() for session in self.sessions.values()]
        return {
            "ok": True,
            "protocol": "atlas.runtime.v0",
            "session_count": len(sessions),
            "active_sessions": sum(1 for session in sessions if session.get("state") not in {"closed", "error"}),
            "sessions": sessions[-20:],
            "latest_stream": self.latest_stream(),
            "recent_events": [event.to_dict() for event in self.events[-40:]],
            "diagnostics": self.diagnostics_snapshot(),
        }

    def score(self, capabilities: dict[str, Any]) -> dict[str, Any]:
        criteria = [
            ("device_firmware", 10, bool(capabilities.get("device_firmware"))),
            ("audio_service", 12, bool(capabilities.get("audio_service"))),
            ("opus_uplink", 14, bool(capabilities.get("opus_uplink"))),
            ("session_runtime", 12, bool(capabilities.get("session_runtime"))),
            ("tools", 10, bool(capabilities.get("tools"))),
            ("provider_config", 8, bool(capabilities.get("provider_config"))),
            ("web_console", 8, bool(capabilities.get("web_console"))),
            ("acceptance", 10, bool(capabilities.get("acceptance"))),
            ("ota_manifest", 6, bool(capabilities.get("ota_manifest"))),
            ("docs", 10, bool(capabilities.get("docs"))),
        ]
        total = sum(weight for _, weight, ok in criteria if ok)
        return {
            "ok": total >= 80,
            "score": total,
            "target": 80,
            "criteria": [
                {"name": name, "weight": weight, "pass": ok}
                for name, weight, ok in criteria
            ],
            "notes": "Score is capability-based. It does not count untested hardware behavior as complete.",
        }
