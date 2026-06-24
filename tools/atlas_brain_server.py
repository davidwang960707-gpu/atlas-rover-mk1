#!/usr/bin/env python3
"""Atlas Brain server for Mac-side DualEye voice, tools and device control.

Run this service on a Mac or local host near the DualEye device. It owns the
ASR/LLM/TTS provider calls, device sessions, Web control pages, tool schema,
OPUS stream intake and OTA package manifest. Atlas Brain is the primary
architecture boundary.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import html
import json
import os
import re
import socket
import struct
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Optional

from atlas_brain_audio import (
    ATLAS_OPUS_FRAME_HEADER,
    ATLAS_OPUS_TURN_MAX_PACKETS,
    compact_audio_payload,
    decode_opus_packets_to_wav,
    latest_audio_stream_meta,
    latest_tts_meta,
    latest_tts_wav,
    latest_turn_meta,
    parse_atlas_opus_frame,
    remember_audio_stream,
    remember_turn,
    store_latest_tts,
)
from atlas_brain_audio_routes import (
    audio_stream_status_payload,
    handle_asr,
    handle_audio_stream_simulate,
    handle_browser_audio_turn,
    handle_device_wav_turn,
    handle_dualeye_opus_probe,
    handle_dualeye_opus_stream_start,
    handle_dualeye_opus_stream_stop,
    handle_speak,
)
from atlas_brain_core import (
    AppDescriptor,
    BrainProtocolDescriptor,
    PlatformBackend,
    ProviderDescriptor,
)
from atlas_brain_devices import DualEyeDeviceClient
from atlas_brain_providers import (
    chat_choice_audio,
    chat_choice_text,
    macos_say_tts,
    openai_asr,
    openai_chat_completion,
    openai_tts,
    prepare_tts_text,
    tts_style_prompt,
)
from atlas_brain_runtime import AtlasBrainRuntime
from atlas_brain_tools import (
    ROLE_ALIASES,
    ROLE_PROFILES,
    THEME_ALIASES,
    build_skill_registry as build_builtin_skill_registry,
    pet_state_from_expression,
)
from atlas_web_ui import render_admin_page, render_device_app_page, render_devices_page


DEFAULT_DUALEYE_URL = "http://192.168.4.1"
DEFAULT_PORT = 8787
DEFAULT_SPEED = 30
DEFAULT_DURATION_MS = 500
DEFAULT_LLM_BASE_URL = "https://api.xiaomimimo.com/v1"
DEFAULT_LLM_MODEL = "xiaomi/mimo-v2.5-pro"
DEFAULT_ASR_MODEL = "mimo-v2.5-asr"
DEFAULT_TTS_MODEL = "mimo-v2.5-tts"
DEFAULT_TTS_VOICE = "mimo_default"
DEFAULT_WEATHER_LOCATION = "济南"
ENABLE_ROVER_SKILLS = os.getenv("ATLAS_ENABLE_ROVER_SKILLS", "0").strip().lower() in {"1", "true", "yes", "on"}
NO_PROXY_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
BRAIN_EVENT_LOCK = threading.Lock()
BRAIN_EVENTS: list[dict[str, Any]] = []
WEATHER_CACHE_LOCK = threading.Lock()
WEATHER_CACHE: dict[str, tuple[float, dict[str, Any]]] = {}
WEATHER_CACHE_TTL_SEC = 600
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))


def _resolve_dualeye_build_dir() -> tuple[str, str]:
    env_path = os.getenv("ATLAS_FIRMWARE_BUILD_DIR", "").strip()
    if env_path:
        return os.path.abspath(os.path.expanduser(env_path)), "ATLAS_FIRMWARE_BUILD_DIR"

    repo_local = os.path.join(REPO_ROOT, "firmware", "dualeye", "build")
    if os.path.exists(repo_local):
        return repo_local, "repo_local"

    sibling = os.path.join(os.path.dirname(REPO_ROOT), "Atlas-One-Firmware", "firmware", "dualeye", "build")
    if os.path.exists(sibling):
        return os.path.abspath(sibling), "sibling_worktree"

    return repo_local, "repo_local_missing"


DUALEYE_BUILD_DIR, DUALEYE_BUILD_DIR_SOURCE = _resolve_dualeye_build_dir()
OTA_PACKAGE_FILES = [
    ("bootloader", "bootloader/bootloader.bin", "0x0"),
    ("partition_table", "partition_table/partition-table.bin", "0x8000"),
    ("ota_data_initial", "ota_data_initial.bin", "0xd000"),
    ("sr_model", "srmodels/srmodels.bin", "0x10000"),
    ("app_ota", "atlas_rover_dualeye.bin", "0x100000"),
    ("spiffs_storage", "storage.bin", "0xB00000"),
]

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

ALLOWED_TOOLS = {
    "atlas_show_page",
    "atlas_set_expression",
    "atlas_pomodoro",
    "atlas_calendar",
    "atlas_chat",
    "atlas_app_action",
    "atlas_pet_event",
    "atlas.ui.set_chat_mode",
    "atlas.pet.set_state",
    "atlas.pet.play_animation",
}
if ENABLE_ROVER_SKILLS:
    ALLOWED_TOOLS.update({"atlas_rover_move", "atlas_rover_stop"})

WEATHER_CODE_ZH = {
    0: "晴",
    1: "大致晴朗",
    2: "局部多云",
    3: "阴",
    45: "有雾",
    48: "雾凇",
    51: "小毛毛雨",
    53: "毛毛雨",
    55: "较强毛毛雨",
    61: "小雨",
    63: "中雨",
    65: "大雨",
    71: "小雪",
    73: "中雪",
    75: "大雪",
    80: "阵雨",
    81: "较强阵雨",
    82: "强阵雨",
    95: "雷雨",
}

WEATHER_LOCATION_ALIASES = {
    "济南市": "济南",
    "山东济南": "济南",
    "山东省济南": "济南",
    "山东省济南市": "济南",
    "jinan": "济南",
}

WEATHER_FILLER_WORDS = (
    "查一下", "查询", "查看", "看看", "帮我", "给我", "请", "麻烦",
    "现在", "当前", "今天", "当地", "附近", "一下", "天气预报", "天气",
    "温度", "下雨", "冷吗", "热吗", "风大", "怎么样", "如何", "的",
    "呃", "嗯", "啊", "哦", "喂", "好的", "好吧", "好", "那个", "就是",
)

TRIVIAL_VOICE_TEXTS = {
    "嗯", "嗯嗯", "呃", "啊", "哦", "喂", "好", "好的", "好吧",
    "ok", "okay", "oh", "hello",
}


def normalize_weather_location(text: str) -> str:
    location = str(text or "").strip()
    if not location:
        return ""
    location = re.sub(r"[\s：:，,。.!！?？、；;]+", "", location)
    lowered = location.lower()
    if lowered in WEATHER_LOCATION_ALIASES:
        return WEATHER_LOCATION_ALIASES[lowered]
    for word in WEATHER_FILLER_WORDS:
        location = location.replace(word, "")
    location = re.sub(r"[\s：:，,。.!！?？、；;]+", "", location).strip()
    lowered = location.lower()
    if lowered in WEATHER_LOCATION_ALIASES:
        return WEATHER_LOCATION_ALIASES[lowered]
    if not location or location in {"市", "省", "区", "县"}:
        return ""
    return location


def weather_location_candidates(location: str) -> list[str]:
    normalized = normalize_weather_location(location)
    if not normalized:
        normalized = DEFAULT_WEATHER_LOCATION
    candidates: list[str] = []
    for item in (normalized, WEATHER_LOCATION_ALIASES.get(normalized.lower(), "")):
        if item and item not in candidates:
            candidates.append(item)
    for suffix in ("市", "省", "区", "县"):
        if normalized.endswith(suffix) and len(normalized) > len(suffix) + 1:
            stripped = normalized[:-len(suffix)]
            if stripped and stripped not in candidates:
                candidates.append(stripped)
    if DEFAULT_WEATHER_LOCATION not in candidates and normalized in {"", "本地", "默认"}:
        candidates.append(DEFAULT_WEATHER_LOCATION)
    return candidates


def cache_weather_result(keys: list[str], weather: dict[str, Any]) -> None:
    now = time.time()
    with WEATHER_CACHE_LOCK:
        for key in keys:
            normalized = normalize_weather_location(key) or key
            if normalized:
                WEATHER_CACHE[normalized] = (now, dict(weather))


def cached_weather_result(keys: list[str]) -> Optional[dict[str, Any]]:
    now = time.time()
    with WEATHER_CACHE_LOCK:
        for key in keys:
            normalized = normalize_weather_location(key) or key
            cached = WEATHER_CACHE.get(normalized)
            if cached and now - cached[0] <= WEATHER_CACHE_TTL_SEC:
                result = dict(cached[1])
                result["cached"] = True
                return result
    return None


def is_trivial_voice_text(text: str) -> bool:
    normalized = re.sub(r"[\s：:，,。.!！?？、；;~～]+", "", str(text or "").strip()).lower()
    if not normalized:
        return True
    if normalized in TRIVIAL_VOICE_TEXTS:
        return True
    return len(normalized) <= 2 and normalized in {"嗯嗯", "哦哦", "啊啊", "呃呃"}


class RobotSession:
    def __init__(self, device_url: str) -> None:
        self.device_url = device_url
        self.device_id = "dualeye"
        self.current_role = "pet"
        self.current_theme = ""
        self.current_chat_mode = "pet_head"
        self.current_page = ""
        self.current_expression = ""
        self.current_pet_state = "idle"
        self.current_pet_animation = ""
        self.current_pet_view = "yaw_c"
        self.pet_asset_version = "0.3.0"
        self.pet_asset_background = "transparent"
        self.default_tts_style = ROLE_PROFILES["pet"]["tts_style"]
        self.system_prompt = ROLE_PROFILES["pet"]["prompt"]
        self.audio_state = "idle"
        self.last_skill: dict[str, Any] = {}
        self.turn_seq = 0
        self.created_at = int(time.time())

    def next_turn_id(self) -> str:
        self.turn_seq += 1
        return time.strftime("%Y%m%d-%H%M%S-") + f"{self.turn_seq:04d}"

    def update_from_status(self, status: dict[str, Any]) -> None:
        self.device_id = str(status.get("device_id") or status.get("mac") or self.device_id)
        ui = status.get("ui")
        if isinstance(ui, dict):
            self.current_theme = str(ui.get("theme") or self.current_theme)
            self.current_chat_mode = str(ui.get("chat_mode") or self.current_chat_mode)
            self.current_page = str(ui.get("page") or self.current_page)
            self.current_expression = str(ui.get("expression") or self.current_expression)
            self.current_pet_state = pet_state_from_expression(self.current_expression)

    def switch_role(self, role: str) -> dict[str, str]:
        profile = ROLE_PROFILES.get(role)
        if profile is None:
            raise ValueError(f"unknown role: {role}")
        self.current_role = role
        self.current_theme = profile["theme"]
        self.current_chat_mode = profile.get("chat_mode", "pet_head")
        self.current_expression = profile["expression"]
        self.current_page = profile["page"]
        self.default_tts_style = profile["tts_style"]
        self.system_prompt = profile["prompt"]
        return profile

    def snapshot(self) -> dict[str, Any]:
        return {
            "device_url": self.device_url,
            "device_id": self.device_id,
            "current_role": self.current_role,
            "current_theme": self.current_theme,
            "current_chat_mode": self.current_chat_mode,
            "current_page": self.current_page,
            "current_expression": self.current_expression,
            "pet_visual": self.pet_visual_snapshot(),
            "default_tts_style": self.default_tts_style,
            "audio_state": self.audio_state,
            "turn_seq": self.turn_seq,
            "last_skill": self.last_skill,
            "created_at": self.created_at,
        }

    def pet_visual_snapshot(self) -> dict[str, Any]:
        return {
            "state": self.current_pet_state or pet_state_from_expression(self.current_expression),
            "animation": self.current_pet_animation,
            "view": self.current_pet_view or "yaw_c",
            "asset_version": self.pet_asset_version,
            "background": self.pet_asset_background,
            "embedded_spiffs": True,
            "sdcard_required": False,
            "views": ["yaw_l30", "yaw_l15", "yaw_c", "yaw_r15", "yaw_r30"],
            "view_states": ["idle", "listen", "think", "speak"],
            "animations": ["blink", "speak", "sing", "laugh"],
            "transitions": ["turn_yaw_c_to_yaw_l30", "turn_yaw_l30_to_yaw_c", "turn_yaw_c_to_yaw_r30", "turn_yaw_r30_to_yaw_c"],
        }


def local_lan_ip() -> str:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))
        return sock.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        sock.close()


def remember_brain_event(event: dict[str, Any]) -> dict[str, Any]:
    stored = {
        "ts": time.time(),
        "event": event.get("event", "unknown"),
        "device_id": event.get("device_id", "dualeye"),
        "payload": event.get("payload", {}),
        "raw": event,
    }
    with BRAIN_EVENT_LOCK:
        BRAIN_EVENTS.append(stored)
        del BRAIN_EVENTS[:-40]
    return stored


def recent_brain_events() -> list[dict[str, Any]]:
    with BRAIN_EVENT_LOCK:
        return list(reversed(BRAIN_EVENTS[-20:]))


def recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise ConnectionError("websocket closed")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def ws_recv_frame(sock: socket.socket) -> tuple[int, bytes]:
    header = recv_exact(sock, 2)
    first, second = header[0], header[1]
    opcode = first & 0x0F
    masked = bool(second & 0x80)
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", recv_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", recv_exact(sock, 8))[0]
    mask = recv_exact(sock, 4) if masked else b""
    payload = recv_exact(sock, length) if length else b""
    if masked and payload:
        payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    return opcode, payload


def ws_send_frame(sock: socket.socket, opcode: int, payload: bytes) -> None:
    first = 0x80 | (opcode & 0x0F)
    length = len(payload)
    if length < 126:
        header = bytes([first, length])
    elif length <= 0xFFFF:
        header = bytes([first, 126]) + struct.pack("!H", length)
    else:
        header = bytes([first, 127]) + struct.pack("!Q", length)
    sock.sendall(header + payload)


def ws_send_json(sock: socket.socket, payload: dict[str, Any]) -> None:
    ws_send_frame(sock, 0x1, json.dumps(payload, ensure_ascii=False).encode("utf-8"))


def simulate_sr_probe(payload: dict[str, Any]) -> dict[str, Any]:
    threshold = clamp_int(int(payload.get("threshold", 36) or 36), 1, 100)
    hits_required = clamp_int(int(payload.get("hits_required", 1) or 1), 1, 8)
    playback_active = bool(payload.get("playback_active", False))
    levels = payload.get("levels")
    if not isinstance(levels, list) or not levels:
        levels = [12, 18, 44, 61, 52, 24]
    hit_count = 0
    triggered_at = -1
    timeline = []
    for index, raw_level in enumerate(levels):
        level = clamp_int(int(raw_level or 0), 0, 100)
        effective = max(0, level - 18) if playback_active else level
        hit = effective >= threshold
        hit_count = hit_count + 1 if hit else 0
        if triggered_at < 0 and hit_count >= hits_required:
            triggered_at = index
        timeline.append({"index": index, "level": level, "effective_level": effective, "hit": hit, "hit_count": hit_count})
    return {
        "ok": True,
        "stage": "P4_simulation",
        "engine": "energy_gate_vad",
        "esp_sr_wakenet": False,
        "aec": False,
        "playback_active": playback_active,
        "threshold": threshold,
        "hits_required": hits_required,
        "triggered": triggered_at >= 0,
        "triggered_at_index": triggered_at,
        "timeline": timeline,
        "notes": "当前只是门限/VAD 仿真；WakeNet/AEC 需要晚上的 DualEye 真机资源验证。",
    }


def http_json(url: str, timeout: float = 5.0) -> dict[str, Any]:
    with NO_PROXY_OPENER.open(url, timeout=timeout) as resp:
        raw = resp.read().decode("utf-8", errors="replace")
    return json.loads(raw)


def http_json_retry(url: str, timeout: float = 10.0, attempts: int = 2) -> dict[str, Any]:
    last_exc: Optional[Exception] = None
    for index in range(max(1, attempts)):
        try:
            return http_json(url, timeout=timeout)
        except Exception as exc:
            last_exc = exc
            if index + 1 < attempts:
                time.sleep(0.35)
    if last_exc is not None:
        raise last_exc
    raise RuntimeError("request failed")


def extract_json_object(text: str) -> dict[str, Any]:
    cleaned = text.strip()
    if cleaned.startswith("```"):
        cleaned = re.sub(r"^```(?:json)?\s*", "", cleaned, flags=re.I)
        cleaned = re.sub(r"\s*```$", "", cleaned)
    try:
        parsed = json.loads(cleaned)
        if isinstance(parsed, dict):
            return parsed
    except json.JSONDecodeError:
        pass

    candidates: list[dict[str, Any]] = []
    for start, char in enumerate(cleaned):
        if char != "{":
            continue
        depth = 0
        in_string = False
        escaped = False
        for end in range(start, len(cleaned)):
            c = cleaned[end]
            if in_string:
                if escaped:
                    escaped = False
                elif c == "\\":
                    escaped = True
                elif c == '"':
                    in_string = False
                continue
            if c == '"':
                in_string = True
            elif c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    try:
                        parsed = json.loads(cleaned[start:end + 1])
                    except json.JSONDecodeError:
                        break
                    if isinstance(parsed, dict):
                        candidates.append(parsed)
                    break

    for parsed in reversed(candidates):
        intents = parsed.get("intents")
        if not isinstance(intents, list):
            continue
        if str(parsed.get("reply", "")) == "short Chinese":
            continue
        tools = [str(item.get("tool", "")) for item in intents if isinstance(item, dict)]
        if "..." in tools:
            continue
        return parsed
    if candidates:
        return candidates[-1]
    raise ValueError("LLM did not return a JSON object")


def normalize_llm_intents(payload: dict[str, Any], original_text: str) -> list[dict[str, Any]]:
    raw_intents = payload.get("intents", [])
    intents: list[dict[str, Any]] = []
    if isinstance(raw_intents, dict):
        raw_intents = [raw_intents]
    if isinstance(raw_intents, list):
        for item in raw_intents:
            if not isinstance(item, dict):
                continue
            tool = str(item.get("tool", "")).strip()
            if tool not in ALLOWED_TOOLS:
                continue
            input_obj = item.get("input", {})
            if not isinstance(input_obj, dict):
                input_obj = {}
            intents.append({"tool": tool, "input": input_obj})

    reply = str(payload.get("reply", "") or "").strip()
    has_chat_intent = any(item.get("tool") == "atlas_chat" for item in intents)
    if reply and not has_chat_intent:
        intents.append({
            "tool": "atlas_chat",
            "input": {
                "chat_text": reply[:150],
                "speech": reply[:150],
                "action": "chat",
            },
        })
    if not intents:
        intents.append({
            "tool": "atlas_chat",
            "input": {
                "chat_text": original_text[:150],
                "speech": original_text[:150],
                "action": "chat",
            },
        })
    return intents


def reply_from_text_result(text_result: dict[str, Any], fallback: str = "") -> str:
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
    ack = ack_from_intents(text_result.get("intents", []))
    if ack:
        return ack
    return fallback.strip()


def ack_from_intents(intents: object) -> str:
    if not isinstance(intents, list):
        return ""
    for intent in intents:
        if not isinstance(intent, dict):
            continue
        tool = str(intent.get("tool", "")).strip()
        input_obj = intent.get("input", {})
        if not isinstance(input_obj, dict):
            input_obj = {}
        if tool == "atlas_rover_stop":
            return "已停止。"
        if tool == "atlas_rover_move":
            direction = str(input_obj.get("direction", "")).strip()
            names = {"forward": "前进", "backward": "后退", "left": "左转", "right": "右转"}
            return f"收到，{names.get(direction, '移动')}一下。"
        if tool == "atlas_pomodoro" or tool.startswith("atlas.pomodoro."):
            action = str(input_obj.get("action", "start")).strip()
            if action == "stop" or tool in {"atlas.pomodoro.stop", "atlas.pomodoro.reset"}:
                return "番茄专注已停止。"
            if tool in {"atlas.pomodoro.show", "atlas.pomodoro.status"}:
                return "番茄专注页面已打开。"
            minutes = input_obj.get("focus_minutes") or 25
            task = str(input_obj.get("task_name") or "当前任务").strip()
            return f"番茄已开始，{minutes} 分钟，任务是{task}。"
        if tool == "atlas_app_action":
            action = str(input_obj.get("action", "")).strip()
            if action == "music":
                return "音乐页面已打开。"
            if action == "story":
                return "故事页面已打开。"
            if action == "chat":
                return "对话页面已打开。"
        if tool == "atlas_calendar" or tool.startswith("atlas.calendar."):
            note = str(input_obj.get("note", "")).strip()
            return note[:90] if note else "日历页面已打开。"
        if tool.startswith("atlas.clock."):
            if tool == "atlas.clock.sync":
                return "时钟已校准。"
            return "时钟页面已打开。"
        if tool == "atlas_set_expression":
            expression = str(input_obj.get("expression", "")).strip()
            names = {
                "happy": "开心",
                "listen": "聆听",
                "sleepy": "困困",
                "cry": "大哭",
                "love": "爱心",
                "money": "爱钱",
                "thinking": "思考",
                "curious": "好奇",
            }
            return f"表情已切到{names.get(expression, expression or '新状态')}。"
        if tool == "atlas_show_page":
            page = str(input_obj.get("page", "")).strip()
            names = {
                "eyes": "双眼",
                "clock": "时钟",
                "status": "状态",
                "voice": "语音",
                "music": "音乐",
                "story": "故事",
                "chat": "对话",
                "calendar": "日历",
                "pomodoro": "番茄",
            }
            return f"{names.get(page, page or '目标')}页面已打开。"
    return ""


def log_snippet(value: object, limit: int = 80) -> str:
    text = str(value or "").replace("\n", " ").replace("\r", " ").strip()
    if len(text) > limit:
        text = text[:limit - 3] + "..."
    return ascii(text)


def is_default_chat_intents(intents: list[dict[str, Any]]) -> bool:
    if not intents:
        return False
    tools = [item.get("tool") for item in intents]
    return tools == ["atlas_chat", "atlas_show_page"]


def clamp_int(value: int, minimum: int, maximum: int) -> int:
    return max(minimum, min(maximum, value))


def first_minutes(text: str, default: int = 25) -> int:
    match = re.search(r"(\d{1,3})\s*(?:分钟|min|mins|minute|minutes)?", text, re.I)
    if not match:
        return default
    return clamp_int(int(match.group(1)), 1, 120)


def clean_task_name(text: str) -> str:
    task = re.sub(r"\d{1,3}\s*(?:分钟|min|mins|minute|minutes)?", "", text, flags=re.I)
    task = re.sub(r"(番茄|专注|开始|启动|计时|倒计时|任务是|任务|pomodoro|focus|start)", "", task, flags=re.I)
    task = task.strip(" ：:，,。.")
    return task or "巡检任务"


def text_to_intents(text: str, speed: int, duration_ms: int) -> list[dict[str, Any]]:
    raw = text.strip()
    lowered = raw.lower()
    intents: list[dict[str, Any]] = []

    def explicit_visual_command(*keywords: str) -> bool:
        visual_markers = ("表情", "眼睛", "眼神", "切换", "切到", "换成", "变成", "显示", "做个", "来个", "装")
        return any(marker in raw for marker in visual_markers) and any(keyword in raw for keyword in keywords)

    def chat_intents(chat_text: str) -> list[dict[str, Any]]:
        return [
            {
                "tool": "atlas_chat",
                "input": {
                    "chat_text": chat_text,
                    "speech": chat_text,
                    "action": "chat",
                },
            },
            {"tool": "atlas_show_page", "input": {"page": "chat"}},
        ]

    if any(word in raw for word in ("停止", "停下", "刹车")) or "stop" in lowered:
        if not ENABLE_ROVER_SKILLS:
            return chat_intents("本版实体先暂停动态底盘，STOP 只保留在固件安全层。")
        return [{"tool": "atlas_rover_stop", "input": {}}]

    motion_map = [
        (("前进", "向前", "往前", "朝前", "向前走", "往前走", "前走"), ("forward", "f"), "forward"),
        (("后退", "倒车", "向后", "往后", "朝后", "退后", "向后走", "往后走"), ("backward", "back", "b"), "backward"),
        (("左转", "向左", "往左", "朝左", "左拐", "左边"), ("left", "l"), "left"),
        (("右转", "向右", "往右", "朝右", "右拐", "右边"), ("right", "r"), "right"),
    ]
    for zh_words, en_words, direction in motion_map:
        english_hit = any(re.search(rf"\b{re.escape(word)}\b", lowered) for word in en_words)
        if any(word in raw for word in zh_words) or english_hit:
            if not ENABLE_ROVER_SKILLS:
                return chat_intents("这一版先不做动态移动，我会优先把语音、表情和桌面应用做好。")
            return [{
                "tool": "atlas_rover_move",
                "input": {
                    "direction": direction,
                    "speed": speed,
                    "duration_ms": duration_ms,
                },
            }]

    pomodoro_hit = any(word in raw for word in ("番茄", "专注", "计时")) or "pomodoro" in lowered or "focus" in lowered
    pomodoro_start_hit = (
        any(word in raw for word in ("开始", "启动", "开一个", "来一个", "计时", "倒计时", "专注一下", "专注模式")) or
        re.search(r"\b(start|begin|focus)\b", lowered) is not None or
        re.search(r"\d{1,3}\s*(?:分钟|min|mins|minute|minutes)", lowered, re.I) is not None
    )
    pomodoro_page_hit = (
        pomodoro_hit and
        (any(word in raw for word in ("打开", "切换", "显示", "进入", "看看", "页面", "界面")) or
         re.search(r"\b(open|show|switch|page)\b", lowered) is not None) and
        not pomodoro_start_hit
    )

    if any(word in raw for word in ("笑话", "冷笑话", "段子", "逗我笑")) or re.search(r"\bjokes?\b", lowered):
        intents.extend(chat_intents(raw))
    elif any(word in raw for word in ("音乐", "唱歌", "放歌", "来首歌")) or "music" in lowered:
        intents.append({"tool": "atlas_app_action", "input": {"action": "music"}})
    elif any(word in raw for word in ("故事", "讲故事")) or "story" in lowered:
        intents.append({"tool": "atlas_app_action", "input": {"action": "story"}})
    elif pomodoro_page_hit:
        intents.append({"tool": "atlas.pomodoro.show", "input": {}})
    elif pomodoro_hit:
        minutes = first_minutes(raw)
        intents.append({
            "tool": "atlas.pomodoro.start",
            "input": {
                "task_name": clean_task_name(raw),
                "focus_minutes": minutes,
                "break_minutes": 5,
            },
        })
    elif any(word in raw for word in ("时钟", "时间", "几点")) or "clock" in lowered:
        intents.append({"tool": "atlas.clock.show", "input": {}})
    elif any(word in raw for word in ("日历", "今日", "今天")) or "calendar" in lowered:
        intents.append({
            "tool": "atlas.calendar.today",
            "input": {"title": "今日", "note": raw},
        })
    elif any(word in raw for word in ("笑一下", "笑一个")) or explicit_visual_command("开心", "高兴", "笑"):
        intents.append({"tool": "atlas_set_expression", "input": {"expression": "happy"}})
    elif explicit_visual_command("听", "聆听", "监听") or "listen face" in lowered or "listen mode" in lowered:
        intents.append({"tool": "atlas_set_expression", "input": {"expression": "listen"}})
    elif explicit_visual_command("睡", "困") or "sleepy face" in lowered or "sleep mode" in lowered:
        intents.append({"tool": "atlas_set_expression", "input": {"expression": "sleepy"}})
    else:
        intents.extend(chat_intents(raw))

    return intents


class Bridge:
    def __init__(self,
                 dualeye_url: str,
                 pin: str,
                 speed: int,
                 duration_ms: int,
                 dry_run: bool,
                 llm_base_url: str,
                 llm_api_key: str,
                 llm_model: str,
                 asr_model: str,
                 tts_model: str,
                 tts_voice: str) -> None:
        self.dualeye_url = dualeye_url.rstrip("/")
        self.pin = pin.strip()
        self.speed = speed
        self.duration_ms = duration_ms
        self.dry_run = dry_run
        self.llm_base_url = llm_base_url.rstrip("/")
        self.llm_api_key = llm_api_key.strip()
        self.llm_model = llm_model.strip()
        self.asr_model = asr_model.strip()
        self.tts_model = tts_model.strip()
        self.tts_voice = tts_voice.strip()
        self.last_status: dict[str, Any] = {}
        self.session = RobotSession(self.dualeye_url)
        self.runtime = AtlasBrainRuntime()
        self.platform = PlatformBackend()
        self.device = DualEyeDeviceClient(
            base_url=self.dualeye_url,
            pin=self.pin,
            dry_run=self.dry_run,
            session=self.session,
            platform=self.platform,
            latest_audio_stream_meta=latest_audio_stream_meta,
        )
        self.skills = build_builtin_skill_registry(
            self,
            build_ota_manifest=build_ota_manifest,
            rover_skills_enabled=ENABLE_ROVER_SKILLS,
        )
        self.refresh_platform()

    def llm_enabled(self) -> bool:
        return bool(self.llm_base_url and self.llm_api_key and self.llm_model)

    def asr_enabled(self) -> bool:
        return bool(self.llm_base_url and self.llm_api_key and self.asr_model)

    def tts_enabled(self) -> bool:
        return bool(self.llm_base_url and self.llm_api_key and self.tts_model and self.tts_voice)

    def status(self, timeout: float = 1.2) -> dict[str, Any]:
        self.last_status = self.device.status(timeout=timeout)
        self.pin = self.device.pin
        return self.last_status

    def capabilities(self) -> dict[str, Any]:
        return self.device.capabilities()

    def ota_status(self) -> dict[str, Any]:
        return self.device.ota_status()

    def device_summary(self) -> dict[str, Any]:
        summary = self.device.device_summary()
        self.last_status = self.device.last_status
        self.pin = self.device.pin
        return summary

    def devices(self) -> list[dict[str, Any]]:
        return self.device.devices()

    def refresh_platform(self) -> dict[str, Any]:
        self.device_summary()
        self.platform.providers.upsert(ProviderDescriptor(
            name="mimo_llm",
            kind="llm",
            configured=self.llm_enabled(),
            status="ready" if self.llm_enabled() else "missing_config",
            model=self.llm_model,
            endpoint=self.llm_base_url,
            notes="OpenAI-compatible chat/completions provider",
        ))
        self.platform.providers.upsert(ProviderDescriptor(
            name="mimo_asr",
            kind="asr",
            configured=self.asr_enabled(),
            status="ready" if self.asr_enabled() else "missing_config",
            model=self.asr_model,
            endpoint=self.llm_base_url,
        ))
        self.platform.providers.upsert(ProviderDescriptor(
            name="mimo_tts",
            kind="tts",
            configured=self.tts_enabled(),
            status="ready" if self.tts_enabled() else "missing_config",
            model=self.tts_model,
            endpoint=self.llm_base_url,
        ))
        self.platform.providers.upsert(ProviderDescriptor(
            name="weather",
            kind="tool_provider",
            configured=True,
            status="ready",
            model=os.environ.get("ATLAS_WEATHER_PROVIDER", "open-meteo"),
            endpoint="https://geocoding-api.open-meteo.com + https://api.open-meteo.com",
            notes=f"默认城市：{DEFAULT_WEATHER_LOCATION}",
        ))
        self.platform.providers.upsert(ProviderDescriptor(
            name="web_search",
            kind="tool_provider",
            configured=bool(os.environ.get("ATLAS_TAVILY_API_KEY") or os.environ.get("ATLAS_SEARCH_ENDPOINT")),
            status="ready" if (os.environ.get("ATLAS_TAVILY_API_KEY") or os.environ.get("ATLAS_SEARCH_ENDPOINT")) else "missing_config",
            endpoint=os.environ.get("ATLAS_SEARCH_ENDPOINT", "tavily_or_custom"),
            notes="未配置时会给出明确诊断，不影响本地对话链路。",
        ))
        self.platform.providers.upsert(ProviderDescriptor(
            name="esp_sr_probe",
            kind="firmware_voice_probe",
            configured=True,
            status="probe_only",
            model="energy_gate_vad_now__esp_sr_pending",
            endpoint="/api/sr/status",
            notes="P4 探针：当前不是 WakeNet/AEC，只暴露资源与 fallback 状态。",
        ))
        self.platform.protocols.upsert(BrainProtocolDescriptor(
            name="device_ws_turn",
            transport="websocket_json_binary",
            endpoint="/ws/brain",
            direction="device_to_brain",
            stage="P1_ws_turn_primary",
            enabled=True,
            audio_streaming=False,
            notes="当前真机主链路：常驻 /ws/brain，JSON turn.audio.begin + binary WAV 上行 + binary WAV TTS 下行。",
        ))
        self.platform.protocols.upsert(BrainProtocolDescriptor(
            name="device_http_wav_compat",
            transport="http_wav",
            endpoint="/device/audio/wav",
            direction="device_to_brain",
            stage="compat_debug_only",
            enabled=True,
            audio_streaming=False,
            notes="兼容/调试入口；DualEye 固件语音 turn 不再把它作为主链路。",
        ))
        self.platform.protocols.upsert(BrainProtocolDescriptor(
            name="browser_json_turn",
            transport="http_json",
            endpoint="/turn/text",
            direction="browser_to_brain",
            stage="P0_stable",
            enabled=True,
            audio_streaming=False,
        ))
        self.platform.protocols.upsert(BrainProtocolDescriptor(
            name="brain_event_channel",
            transport="websocket_json_or_http_event",
            endpoint="Brain /ws/brain + /api/brain/events",
            direction="device_to_brain",
            stage="P1_long_lived_json_session",
            enabled=True,
            audio_streaming=False,
            notes="Atlas Brain 服务侧提供 atlas.brain.session.v1：hello/ping/listen/turn/speaking/failed 等 JSON 事件；HTTP 事件收集保留兜底。",
        ))
        self.platform.protocols.upsert(BrainProtocolDescriptor(
            name="opus_stream",
            transport="websocket_opus",
            endpoint="/ws/audio",
            direction="device_to_brain",
            stage="P2_dualeye_ws_opus_stream",
            enabled=True,
            audio_streaming=True,
            notes="Mac Brain 接收 DualEye AOP1 二进制帧：32字节头 + 60ms OPUS payload；turn=1 时流结束后封装 Ogg、解码 WAV 并进入 ASR/LLM/TTS。",
        ))
        self.platform.protocols.upsert(BrainProtocolDescriptor(
            name="esp_sr_wake",
            transport="firmware_local",
            endpoint="/api/sr/status",
            direction="device_local",
            stage="P3_resource_probe",
            enabled=True,
            notes="当前仍是 VAD 音量门限；WakeNet/AEC 需晚上实机资源验证。",
        ))
        self.platform.apps.upsert(AppDescriptor(
            app_id="dualeye-control",
            device_id=self.session.device_id or "dualeye",
            name="Atlas DualEye 日常操作台",
            route=f"/devices/{urllib.parse.quote(self.session.device_id or 'dualeye')}/app",
            features=["chat", "voice", "theme", "expression", "clock", "pomodoro", "calendar", "pet"],
        ))
        device_id = self.session.device_id or "dualeye"
        app_route = f"/devices/{urllib.parse.quote(device_id)}/app"
        self.platform.apps.upsert(AppDescriptor(
            app_id="atlas-clock",
            device_id=device_id,
            name="桌面时钟",
            route=f"{app_route}#clock",
            features=["show", "sync", "status", "quartz_face"],
        ))
        self.platform.apps.upsert(AppDescriptor(
            app_id="atlas-calendar",
            device_id=device_id,
            name="今日日历",
            route=f"{app_route}#calendar",
            features=["today", "set_note", "weather_card", "status"],
        ))
        self.platform.apps.upsert(AppDescriptor(
            app_id="atlas-pomodoro",
            device_id=device_id,
            name="番茄专注",
            route=f"{app_route}#pomodoro",
            features=["show", "start", "stop", "status", "pet_companion"],
        ))
        return self.platform.snapshot()

    def platform_snapshot(self) -> dict[str, Any]:
        return self.refresh_platform()

    def provider_status(self) -> dict[str, Any]:
        return {
            "llm": {
                "enabled": self.llm_enabled(),
                "base_url": self.llm_base_url if self.llm_base_url else "",
                "model": self.llm_model if self.llm_enabled() else "",
            },
            "asr": {
                "enabled": self.asr_enabled(),
                "model": self.asr_model if self.asr_enabled() else "",
            },
            "tts": {
                "enabled": self.tts_enabled(),
                "model": self.tts_model if self.tts_enabled() else "",
                "voice": self.tts_voice if self.tts_enabled() else "",
            },
            "search": {
                "provider": os.getenv("ATLAS_SEARCH_PROVIDER", "").strip() or "not_configured",
                "configured": bool(os.getenv("ATLAS_SEARCH_API_KEY", "").strip() or os.getenv("ATLAS_SEARCH_ENDPOINT", "").strip()),
            },
            "weather": {
                "provider": os.getenv("ATLAS_WEATHER_PROVIDER", "open_meteo").strip() or "open_meteo",
                "default_location": os.getenv("ATLAS_WEATHER_DEFAULT_LOCATION", DEFAULT_WEATHER_LOCATION).strip() or DEFAULT_WEATHER_LOCATION,
            },
        }

    def runtime_score_payload(self) -> dict[str, Any]:
        tools_payload = self.skills.tool_schema_payload()
        tool_names = {str(tool.get("name", "")) for tool in tools_payload.get("tools", []) if isinstance(tool, dict)}
        missing_tools = sorted(EXPECTED_DESK_APP_TOOLS - tool_names)
        latest_stream = self.runtime.latest_stream() or latest_audio_stream_meta()
        stream_ok = (
            str(latest_stream.get("stage", "")) == "P2_dualeye_ws_opus_stream"
            and int(latest_stream.get("atlas_frames", 0) or 0) > 0
            and int(latest_stream.get("sequence_gaps", 0) or 0) == 0
            and int(latest_stream.get("payload_len_mismatches", 0) or 0) == 0
        )
        ota_manifest = build_ota_manifest()
        status = self.last_status if isinstance(self.last_status, dict) else {}
        audio_service_status = status.get("audio_service") if isinstance(status.get("audio_service"), dict) else {}
        firmware_fingerprint = status.get("fingerprint") if isinstance(status.get("fingerprint"), dict) else {}
        source_audio_service_ready = os.path.exists(os.path.join(REPO_ROOT, "firmware", "dualeye", "main", "atlas_audio_service.c"))
        source_opus_ready = os.path.exists(os.path.join(REPO_ROOT, "firmware", "dualeye", "main", "atlas_opus_stream.c"))
        docs_ready = all(os.path.exists(path) for path in [
            os.path.join(REPO_ROOT, "README.md"),
            os.path.join(REPO_ROOT, "firmware", "dualeye", "README.md"),
            os.path.join(REPO_ROOT, "docs", "端到端能力对标_xiaozhi_Atlas_V0.12.md"),
        ])
        platform = self.platform_snapshot()
        capabilities = {
            "device_firmware": bool(firmware_fingerprint) or ota_manifest.get("status") == "package_ready",
            "audio_service": bool(audio_service_status) or source_audio_service_ready,
            "opus_uplink": stream_ok,
            "session_runtime": bool(self.runtime.snapshot().get("protocol") == "atlas.runtime.v0"),
            "tools": tools_payload.get("protocol") == "atlas.tools.v0.desk_apps" and not missing_tools,
            "provider_config": self.llm_enabled() and self.asr_enabled() and self.tts_enabled(),
            "web_console": True,
            "acceptance": True,
            "ota_manifest": ota_manifest.get("status") == "package_ready",
            "docs": docs_ready,
        }
        current_score = self.runtime.score(capabilities)
        ready_capabilities = dict(capabilities)
        ready_capabilities["opus_uplink"] = capabilities["opus_uplink"] or source_opus_ready
        ready_capabilities["provider_config"] = True
        ready_score = self.runtime.score(ready_capabilities)
        return {
            "ok": bool(current_score.get("ok")),
            "protocol": "atlas.runtime.score.v0",
            "score": current_score,
            "ready_score": ready_score,
            "capabilities": capabilities,
            "ready_capabilities": ready_capabilities,
            "missing_tools": missing_tools,
            "latest_stream": latest_stream,
            "provider_status": self.provider_status(),
            "platform_summary": platform.get("summary", {}),
            "ota_status": ota_manifest.get("status"),
            "notes": [
                "score 是当前实测/已配置状态。",
                "ready_score 把已实现但需要真机流或 API Key 的能力作为可达能力，用于烧录前判断代码成熟度。",
            ],
        }

    def ensure_pin(self) -> str:
        self.device.pin = self.pin
        self.pin = self.device.ensure_pin()
        return self.pin

    def post_dualeye_form(self, path: str, values: dict[str, str], timeout: float = 8.0) -> dict[str, Any]:
        self.device.pin = self.pin
        result = self.device.post_form(path, values, timeout=timeout)
        self.pin = self.device.pin
        self.last_status = self.device.last_status
        return result

    def play_latest_tts_on_dualeye(self, tts_url: str) -> dict[str, Any]:
        result = self.device.play_latest_tts(tts_url)
        self.pin = self.device.pin
        return result

    def run_dualeye_opus_probe(self, duration_ms: int = 1200) -> dict[str, Any]:
        result = self.device.run_opus_probe(duration_ms)
        self.pin = self.device.pin
        return result

    def start_dualeye_opus_stream(self, ws_url: str, duration_ms: int = 5000) -> dict[str, Any]:
        result = self.device.start_opus_stream(ws_url, duration_ms)
        self.pin = self.device.pin
        return result

    def stop_dualeye_opus_stream(self) -> dict[str, Any]:
        result = self.device.stop_opus_stream()
        self.pin = self.device.pin
        return result

    def dualeye_opus_stream_status(self) -> dict[str, Any]:
        return self.device.opus_stream_status()

    def send_intent(self, intent: dict[str, Any]) -> dict[str, Any]:
        result = self.device.send_intent(intent)
        self.pin = self.device.pin
        self.last_status = self.device.last_status
        return result

    def send_intents(self, intents: list[dict[str, Any]]) -> list[dict[str, Any]]:
        results = self.device.send_intents(intents)
        self.pin = self.device.pin
        self.last_status = self.device.last_status
        return results

    def set_dualeye_theme(self,
                          theme: str,
                          brightness: Optional[int] = None,
                          volume: Optional[int] = None,
                          chat_mode: Optional[str] = None) -> dict[str, Any]:
        result = self.device.set_theme(theme, brightness=brightness, volume=volume, chat_mode=chat_mode)
        self.pin = self.device.pin
        self.last_status = self.device.last_status
        return result

    def set_dualeye_chat_mode(self, chat_mode: str) -> dict[str, Any]:
        result = self.device.set_chat_mode(chat_mode)
        self.pin = self.device.pin
        self.last_status = self.device.last_status
        return result

    def execute_skill(self, name: str, args: Optional[dict[str, Any]] = None) -> dict[str, Any]:
        return self.skills.execute(name, args)

    def text_to_skill(self, text: str) -> Optional[tuple[str, dict[str, Any]]]:
        raw = text.strip()
        lowered = raw.lower()
        if not raw:
            return None

        def find_alias(alias_map: dict[str, str]) -> str:
            for alias, value in alias_map.items():
                if alias and (alias in raw or alias in lowered):
                    return value
            return ""

        if any(word in raw for word in ("土拨鼠头", "宠物头", "pet_head")):
            return "atlas.ui.set_chat_mode", {"mode": "pet_head"}
        if any(word in raw for word in ("双屏文字", "文字模式", "显示文字")):
            return "atlas.ui.set_chat_mode", {"mode": "text"}
        if any(word in raw for word in ("纯眼睛", "只显示眼睛", "眼睛模式")):
            return "atlas.ui.set_chat_mode", {"mode": "eyes_only"}
        if any(word in raw for word in ("大笑", "笑一下", "哈哈")):
            return "atlas.pet.play_animation", {"animation": "laugh", "right_text": "哈哈，我在"}
        if any(word in raw for word in ("唱歌", "唱一首")):
            return "atlas.pet.play_animation", {"animation": "sing", "right_text": "准备开唱"}

        if any(word in raw for word in ("角色", "人格", "模式")) or "role" in lowered:
            role = find_alias(ROLE_ALIASES)
            if role:
                return "atlas.role.switch", {"role": role}

        if any(word in raw for word in ("主题", "皮肤", "眼睛")) or "theme" in lowered:
            theme = find_alias(THEME_ALIASES)
            if theme:
                return "atlas.set_theme", {"theme": theme}

        if any(word in raw for word in ("天气", "温度", "下雨", "冷吗", "热吗", "风大")) or "weather" in lowered:
            location = normalize_weather_location(raw)
            return "atlas.weather.query", {"location": location}

        search_hit = (
            any(word in raw for word in ("联网搜索", "搜索", "搜一下", "查资料", "查一下最新", "最新消息", "最新资料")) or
            re.search(r"\b(search|google)\b", lowered) is not None or
            re.search(r"\bweb\s+search\b", lowered) is not None
        )
        if search_hit:
            query = re.sub(r"(联网搜索|搜索|搜一下|查资料|查一下|帮我|最新消息|最新资料)", "", raw)
            query = query.strip(" ：:，,。.?？")
            return "atlas.web_search", {"query": query or raw}

        return None

    def skill_result_to_text_result(self, name: str, result: dict[str, Any]) -> dict[str, Any]:
        reply = str(result.get("reply") or result.get("speech") or "").strip()
        intents = result.get("intents", [])
        if not isinstance(intents, list):
            intents = []
        device_results = result.get("results", [])
        if not isinstance(device_results, list):
            device_results = []
        return {
            "ok": bool(result.get("ok")),
            "source": "skill",
            "skill": name,
            "llm": {
                "mode": "skill",
                "reply": reply,
            },
            "intents": intents,
            "results": device_results,
            "skill_result": result,
        }

    def query_weather(self, location: str = "") -> dict[str, Any]:
        default_location = os.getenv("ATLAS_WEATHER_DEFAULT_LOCATION", DEFAULT_WEATHER_LOCATION).strip() or DEFAULT_WEATHER_LOCATION
        requested_location = str(location or "").strip()
        candidates = weather_location_candidates(requested_location or default_location)
        provider = os.getenv("ATLAS_WEATHER_PROVIDER", "open_meteo").strip().lower() or "open_meteo"
        if provider not in {"open_meteo", "open-meteo"}:
            return {"ok": False, "error": f"unsupported weather provider: {provider}", "location": requested_location or default_location}
        try:
            results = None
            geo_location = candidates[0] if candidates else default_location
            for candidate in candidates:
                geo_query = urllib.parse.urlencode({"name": candidate, "count": 1, "language": "zh", "format": "json"})
                geo = http_json_retry(f"https://geocoding-api.open-meteo.com/v1/search?{geo_query}", timeout=12.0, attempts=2)
                maybe_results = geo.get("results")
                if isinstance(maybe_results, list) and maybe_results:
                    results = maybe_results
                    geo_location = candidate
                    break
            if not isinstance(results, list) or not results:
                return {"ok": False, "error": f"没有找到城市：{requested_location or default_location}", "location": requested_location or default_location}
            city = results[0]
            latitude = float(city["latitude"])
            longitude = float(city["longitude"])
            city_name = str(city.get("name") or location)
            admin = str(city.get("admin1") or "")
            forecast_query = urllib.parse.urlencode({
                "latitude": f"{latitude:.5f}",
                "longitude": f"{longitude:.5f}",
                "current": "temperature_2m,weather_code,wind_speed_10m",
                "timezone": "auto",
            })
            forecast = http_json_retry(f"https://api.open-meteo.com/v1/forecast?{forecast_query}", timeout=12.0, attempts=2)
            current = forecast.get("current", {})
            if not isinstance(current, dict):
                return {"ok": False, "error": "天气响应格式异常", "location": location}
            temp = current.get("temperature_2m")
            wind = current.get("wind_speed_10m")
            code = int(current.get("weather_code", -1))
            condition = WEATHER_CODE_ZH.get(code, f"天气代码{code}")
            if admin and (city_name in admin or admin in city_name):
                display_location = admin
            elif admin:
                display_location = f"{admin}{city_name}"
            else:
                display_location = city_name
            summary = f"{display_location}现在{condition}，气温{temp}℃，风速{wind} km/h。"
            if isinstance(temp, (int, float)):
                if temp <= 5:
                    summary += " 出门记得加衣服。"
                elif temp >= 30:
                    summary += " 有点热，记得补水。"
                elif code in {61, 63, 65, 80, 81, 82, 95}:
                    summary += " 可能要带伞。"
            result = {
                "ok": True,
                "provider": "open_meteo",
                "query_location": requested_location or default_location,
                "geo_location": geo_location,
                "location": display_location,
                "condition": condition,
                "temperature_c": temp,
                "wind_kmh": wind,
                "weather_code": code,
                "summary": summary,
            }
            cache_weather_result(candidates + [requested_location, default_location, display_location], result)
            return result
        except Exception as exc:
            cached = cached_weather_result(candidates + [requested_location, default_location])
            if cached is not None:
                cached["warning"] = f"天气 API 暂时不稳定，已使用最近缓存：{exc}"
                return cached
            return {"ok": False, "error": str(exc), "location": requested_location or default_location}

    def web_search(self, query: str, max_results: int = 5) -> dict[str, Any]:
        query = query.strip()
        if not query:
            return {"ok": False, "error": "query required"}
        provider = os.getenv("ATLAS_SEARCH_PROVIDER", "tavily").strip().lower() or "tavily"
        api_key = os.getenv("ATLAS_SEARCH_API_KEY", "").strip()
        endpoint = os.getenv("ATLAS_SEARCH_ENDPOINT", "").strip()
        max_results = clamp_int(int(max_results or 5), 1, 8)
        if endpoint:
            try:
                body = json.dumps({"query": query, "max_results": max_results}, ensure_ascii=False).encode("utf-8")
                req = urllib.request.Request(endpoint, data=body, method="POST", headers={"Content-Type": "application/json"})
                with NO_PROXY_OPENER.open(req, timeout=20.0) as resp:
                    payload = json.loads(resp.read().decode("utf-8", errors="replace"))
                payload.setdefault("ok", True)
                payload.setdefault("provider", "custom")
                return payload
            except Exception as exc:
                return {"ok": False, "provider": "custom", "error": str(exc)}
        if provider == "tavily" and api_key:
            try:
                body = json.dumps({
                    "api_key": api_key,
                    "query": query,
                    "max_results": max_results,
                    "include_answer": True,
                }, ensure_ascii=False).encode("utf-8")
                req = urllib.request.Request("https://api.tavily.com/search", data=body, method="POST", headers={"Content-Type": "application/json"})
                with NO_PROXY_OPENER.open(req, timeout=20.0) as resp:
                    data = json.loads(resp.read().decode("utf-8", errors="replace"))
                results = data.get("results") if isinstance(data.get("results"), list) else []
                simplified = []
                for item in results[:max_results]:
                    if not isinstance(item, dict):
                        continue
                    simplified.append({
                        "title": str(item.get("title", ""))[:120],
                        "url": str(item.get("url", "")),
                        "content": str(item.get("content", ""))[:300],
                    })
                return {
                    "ok": True,
                    "provider": "tavily",
                    "query": query,
                    "answer": str(data.get("answer", "")),
                    "results": simplified,
                }
            except Exception as exc:
                return {"ok": False, "provider": "tavily", "error": str(exc)}
        return {
            "ok": False,
            "provider": provider,
            "query": query,
            "error": "联网搜索未配置。请在 Mac 环境变量设置 ATLAS_SEARCH_API_KEY，或设置 ATLAS_SEARCH_ENDPOINT。",
        }

    def llm_text_to_intents(self, text: str) -> tuple[list[dict[str, Any]], dict[str, Any]]:
        motion_hint = (
            "明确前进/后退/左转/右转/停止->atlas_rover_move或atlas_rover_stop。"
            if ENABLE_ROVER_SKILLS else
            "前进/后退/左转/右转/停止等动态底盘请求->atlas_chat，回复本版动态底盘已暂停。"
        )
        system_prompt = (
            "只输出一行JSON。不要解释。格式含reply和intents。"
            "只有用户明确说切换/显示/变成某种表情或眼睛时，才用atlas_set_expression；普通情绪表达走atlas_chat；"
            "爱心/爱钱/哭/听/困等表情词也必须满足明确视觉控制语境；"
            "时钟/状态/日历/番茄/聊天->atlas_show_page；"
            "番茄任务->atlas_pomodoro；"
            "音乐/故事/对话->atlas_app_action；"
            "普通聊天->atlas_chat；"
            f"{motion_hint}"
            "工具input只放必要字段。"
        )
        response = openai_chat_completion(
            self.llm_base_url,
            self.llm_api_key,
            self.llm_model,
            [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": text},
            ],
            max_tokens=1200,
        )
        content = chat_choice_text(response)
        try:
            payload = extract_json_object(str(content))
        except Exception as exc:
            raise ValueError(f"{exc}: {json.dumps(response, ensure_ascii=False)[:600]}") from exc
        return normalize_llm_intents(payload, text), {
            "model": response.get("model", self.llm_model),
            "raw_content": content,
            "parsed": payload,
        }

    def llm_chat_reply(self, text: str) -> tuple[list[dict[str, Any]], dict[str, Any]]:
        response = openai_chat_completion(
            self.llm_base_url,
            self.llm_api_key,
            self.llm_model,
            [
                {"role": "system", "content": self.session.system_prompt + " 用简短、自然的中文回复，不要展示思考过程。不要使用 emoji、Markdown 或换行，默认 60 个汉字以内。"},
                {"role": "user", "content": text},
            ],
            max_tokens=180,
        )
        reply = chat_choice_text(response).strip()
        if not reply:
            reply = "我听到了，但这次没有拿到稳定回复。"
        reply = re.sub(r"```.*?```", "", reply, flags=re.S).strip()
        reply = re.sub(r"[\r\n]+", " ", reply).strip()
        if len(reply) > 90:
            reply = reply[:87] + "..."
        intents = [
            {
                "tool": "atlas_chat",
                "input": {
                    "chat_text": reply,
                    "speech": reply,
                    "action": "chat",
                },
            },
            {"tool": "atlas_show_page", "input": {"page": "chat"}},
        ]
        return intents, {
            "model": response.get("model", self.llm_model),
            "reply": reply,
        }

    def send_text(self, text: str) -> dict[str, Any]:
        turn_id = self.session.next_turn_id()
        skill_match = self.text_to_skill(text)
        if skill_match is not None:
            skill_name, skill_args = skill_match
            skill_result = self.execute_skill(skill_name, skill_args)
            text_result = self.skill_result_to_text_result(skill_name, skill_result)
            text_result["turn_id"] = turn_id
            return text_result

        intents = text_to_intents(text, self.speed, self.duration_ms)
        source = "rules"
        llm_meta: dict[str, Any] = {"mode": "rules"}
        if self.llm_enabled() and is_default_chat_intents(intents):
            try:
                intents, llm_meta = self.llm_chat_reply(text)
                source = "llm_chat"
            except Exception as exc:
                fallback_reply = "我听到了，但大脑响应慢了一点。你可以再说一遍吗？"
                intents = [
                    {
                        "tool": "atlas_chat",
                        "input": {
                            "chat_text": fallback_reply,
                            "speech": fallback_reply,
                            "action": "chat",
                        },
                    },
                    {"tool": "atlas_show_page", "input": {"page": "chat"}},
                ]
                llm_meta = {"error": str(exc), "reply": fallback_reply, "fallback": True}
                source = "llm_fallback"
        results = [self.send_intent(intent) for intent in intents]
        device_sync_ok = all(item.get("ok") for item in results)
        if not str(llm_meta.get("reply", "")).strip():
            ack = ack_from_intents(intents)
            if ack:
                llm_meta["reply"] = ack
        reply_ready = bool(str(llm_meta.get("reply", "")).strip())
        conversation_ok = device_sync_ok or (source in {"llm_chat", "llm_fallback"} and reply_ready)
        return {
            "ok": conversation_ok,
            "turn_id": turn_id,
            "source": source,
            "llm": llm_meta,
            "intents": intents,
            "results": results,
            "device_sync_ok": device_sync_ok,
            "device_sync_warning": "" if device_sync_ok else "DualEye 离线或忙碌；本轮对话已完成，但未同步到设备屏幕。",
        }

    def transcribe_audio(self, audio_data_url: str, language: str = "auto") -> dict[str, Any]:
        if not self.asr_enabled():
            return {"ok": False, "error": "ASR is not configured"}
        if not audio_data_url.startswith(("data:audio/wav;base64,", "data:audio/mpeg;base64,", "data:audio/mp3;base64,")):
            return {"ok": False, "error": "ASR only accepts wav/mp3 data URLs"}
        last_error = ""
        for attempt in range(2):
            try:
                response = openai_asr(self.llm_base_url, self.llm_api_key, self.asr_model, audio_data_url, language)
                text = chat_choice_text(response).strip()
                return {"ok": bool(text), "text": text, "model": response.get("model", self.asr_model)}
            except Exception as exc:
                last_error = str(exc)
                if attempt == 0:
                    time.sleep(0.35)
        return {
            "ok": False,
            "error": f"ASR provider request failed: {last_error}",
            "model": self.asr_model,
            "retry_count": 1,
        }

    def fallback_voice_payload(self,
                               reply: str,
                               reason: str,
                               speak: bool,
                               tts_voice: str = "",
                               tts_style: str = "default",
                               tts_singing: bool = False,
                               asr: Optional[dict[str, Any]] = None) -> dict[str, Any]:
        text_result = {
            "ok": True,
            "source": "voice_fallback",
            "llm": {"reply": reply, "fallback": True, "error": reason},
            "intents": [
                {
                    "tool": "atlas_chat",
                    "input": {
                        "chat_text": reply,
                        "speech": reply,
                        "action": "chat",
                    },
                },
                {"tool": "atlas_show_page", "input": {"page": "chat"}},
            ],
            "results": [],
        }
        payload: dict[str, Any] = {
            "ok": False,
            "fallback": True,
            "error": reason,
            "asr": asr or {"ok": False, "error": reason},
            "text_result": text_result,
        }
        if speak:
            tts = self.synthesize_speech(reply,
                                         voice=tts_voice,
                                         style=tts_style,
                                         singing=tts_singing)
            payload["tts"] = tts
            payload["ok"] = bool(tts.get("ok"))
        return payload

    def synthesize_speech(self,
                          text: str,
                          voice: str = "",
                          audio_format: str = "wav",
                          style: str = "default",
                          singing: bool = False) -> dict[str, Any]:
        text = text.strip()
        if not text:
            return {"ok": False, "error": "text required"}
        style_key = (style or "default").strip()
        styled_text = prepare_tts_text(text, style_key, singing)
        style_prompt = tts_style_prompt("singing" if singing else style_key)
        cloud_error = ""
        if self.tts_enabled():
            try:
                response = openai_tts(
                    self.llm_base_url,
                    self.llm_api_key,
                    self.tts_model,
                    styled_text[:500],
                    voice.strip() or self.tts_voice,
                    audio_format,
                    style_prompt=style_prompt,
                )
                audio_base64 = chat_choice_audio(response)
                if not audio_base64:
                    cloud_error = "TTS response did not contain audio"
                else:
                    mime = "audio/wav" if audio_format == "wav" else "audio/mpeg"
                    if audio_base64.startswith("data:audio/"):
                        audio_url = audio_base64
                    else:
                        audio_url = f"data:{mime};base64,{audio_base64}"
                    return {
                        "ok": True,
                        "provider": "mimo",
                        "text": text,
                        "tts_text": styled_text,
                        "style": "singing" if singing else style_key,
                        "singing": bool(singing or style_key == "singing"),
                        "model": response.get("model", self.tts_model),
                        "voice": voice.strip() or self.tts_voice,
                        "format": audio_format,
                        "audio_url": audio_url,
                    }
            except urllib.error.HTTPError as exc:
                detail = exc.read().decode("utf-8", errors="replace")[:500]
                cloud_error = f"HTTP {exc.code}: {detail or exc.reason}"
            except Exception as exc:
                cloud_error = str(exc)

        fallback = macos_say_tts(text, voice="")
        fallback["style"] = "singing" if singing else style_key
        fallback["singing"] = bool(singing or style_key == "singing")
        if cloud_error:
            fallback["cloud_error"] = cloud_error
        if not fallback.get("ok") and not self.tts_enabled():
            fallback.setdefault("error", "TTS is not configured")
        return fallback

    def send_audio(self,
                   audio_data_url: str,
                   language: str = "auto",
                   speak: bool = False,
                   tts_voice: str = "",
                   tts_style: str = "default",
                   tts_singing: bool = False) -> dict[str, Any]:
        try:
            asr = self.transcribe_audio(audio_data_url, language)
        except Exception as exc:
            reason = f"ASR timeout/error: {exc}"
            return self.fallback_voice_payload("我刚才没听清楚，可以再说一遍吗？",
                                               reason,
                                               speak,
                                               tts_voice,
                                               tts_style,
                                               tts_singing)
        if not asr.get("ok"):
            return self.fallback_voice_payload("我刚才没听清楚，可以再说一遍吗？",
                                               str(asr.get("error", "ASR returned empty text")),
                                               speak,
                                               tts_voice,
                                               tts_style,
                                               tts_singing,
                                               asr=asr)
        if is_trivial_voice_text(str(asr.get("text", ""))):
            voice_text = str(asr.get("text", "")).strip()
            if speak:
                return self.fallback_voice_payload("我在呢，刚才没听清楚。你可以再说一遍吗？",
                                                   f"voice ignored: {voice_text or 'empty'}",
                                                   speak,
                                                   tts_voice,
                                                   tts_style,
                                                   tts_singing,
                                                   asr=asr)
            return {
                "ok": True,
                "ignored": True,
                "asr": asr,
                "text_result": {
                    "ok": True,
                    "source": "voice_ignored",
                    "llm": {"reply": ""},
                    "intents": [],
                    "results": [],
                },
            }
        try:
            text_result = self.send_text(str(asr.get("text", "")))
        except Exception as exc:
            reason = f"LLM/intent timeout/error: {exc}"
            return self.fallback_voice_payload("我听到了，但大脑响应慢了一点。你可以再说一遍吗？",
                                               reason,
                                               speak,
                                               tts_voice,
                                               tts_style,
                                               tts_singing,
                                               asr=asr)
        payload = {"ok": text_result.get("ok", False), "asr": asr, "text_result": text_result}
        if speak:
            reply = reply_from_text_result(text_result)
            if reply:
                try:
                    payload["tts"] = self.synthesize_speech(reply,
                                                            voice=tts_voice,
                                                            style=tts_style,
                                                            singing=tts_singing)
                except Exception as exc:
                    reason = f"TTS timeout/error: {exc}"
                    return self.fallback_voice_payload("我已经想好啦，但刚刚发声失败了。再试一次吧。",
                                                       reason,
                                                       True,
                                                       tts_voice,
                                                       tts_style,
                                                       tts_singing,
                                                       asr=asr)
        return payload


def _file_sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as fp:
        for chunk in iter(lambda: fp.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_ota_manifest() -> dict[str, Any]:
    packages: list[dict[str, Any]] = []
    missing: list[str] = []
    for name, rel_path, offset in OTA_PACKAGE_FILES:
        abs_path = os.path.join(DUALEYE_BUILD_DIR, rel_path)
        rel_repo = os.path.relpath(abs_path, REPO_ROOT)
        if os.path.exists(abs_path):
            stat = os.stat(abs_path)
            packages.append({
                "name": name,
                "path": rel_repo,
                "flash_offset": offset,
                "size": stat.st_size,
                "sha256": _file_sha256(abs_path),
                "mtime": int(stat.st_mtime),
            })
        else:
            missing.append(rel_repo)
    ready = len(missing) == 0
    flash_args = [
        "python3", "-m", "esptool",
        "--chip", "esp32s3",
        "-b", "460800",
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_size", "16MB",
        "--flash_freq", "80m",
    ]
    for package in packages:
        flash_args.extend([str(package["flash_offset"]), str(package["path"])])
    return {
        "ok": True,
        "protocol": "atlas.ota.manifest.v0",
        "device_model": "waveshare-dualeye-s3-1.28",
        "project": "atlas-rover-mk1",
        "channel": "dev",
        "version": "0.14.7-acceptance",
        "status": "package_ready" if ready else "missing_build_artifacts",
        "ota_supported": True,
        "app_ota_supported": True,
        "full_image_ota_supported": False,
        "apply_endpoint": "/api/ota/apply",
        "package_management": True,
        "transport": "http_app_ota_plus_usb_full_flash",
        "partition_layout": "dual_ota_app_plus_model_plus_storage",
        "build_dir": os.path.relpath(DUALEYE_BUILD_DIR, REPO_ROOT),
        "build_dir_source": DUALEYE_BUILD_DIR_SOURCE,
        "packages": packages,
        "missing": missing,
        "flash_args": flash_args if ready else [],
        "notes": "P5 supports app OTA via DualEye /api/ota/apply. Bootloader, partition table, model and SPIFFS storage still require USB full flash.",
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


def build_acceptance_report(bridge: Bridge, audio_ws_url: str = "", skip_device: bool = False) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []
    started = time.time()

    health = {
        "service": "atlas-brain",
        "llm_enabled": bridge.llm_enabled(),
        "asr_enabled": bridge.asr_enabled(),
        "tts_enabled": bridge.tts_enabled(),
        "rover_skills_enabled": ENABLE_ROVER_SKILLS,
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
        status: dict[str, Any] = {}
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
    dualeye_stream: dict[str, Any] = {}
    if skip_device:
        dualeye_stream = {"ok": False, "skipped": True, "reason": "skip_device"}
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
    runtime_score = bridge.runtime_score_payload()
    return {
        "ok": len(required_failed) == 0,
        "ready_to_flash_test": len(required_failed) == 0 and not skip_device,
        "device_checks_skipped": skip_device,
        "protocol": "atlas.acceptance.v0",
        "generated_at": int(time.time()),
        "elapsed_ms": int((time.time() - started) * 1000),
        "summary": summary,
        "runtime_score": runtime_score,
        "xiaozhi_gap": {
            "objective_maturity_estimate": "Atlas 本轮按 80 分可验收口径补强：会话运行时、OPUS 真流入口、工具化应用、平台后端和验收页已具备；流式 ASR/TTS、AEC/WakeNet、生产级 OTA 仍是后续超过 xiaozhi 的核心差距。",
            "atlas_advantage": "双屏表情、桌面宠物应用、可视化主题与本地 Mac Brain 可调试性。",
            "remaining_gaps": ["流式 ASR", "流式 TTS 播放", "真实 WakeNet/AEC", "多设备账号体系", "生产级 OTA/回滚", "长期压测"],
        },
        "checks": checks,
        "next_steps": next_steps[:6],
    }


def read_request_json(handler: BaseHTTPRequestHandler) -> dict[str, Any]:
    length = int(handler.headers.get("Content-Length", "0") or "0")
    raw = handler.rfile.read(length).decode("utf-8", errors="replace") if length > 0 else ""
    ctype = handler.headers.get("Content-Type", "")
    if "application/json" in ctype:
        return json.loads(raw or "{}")
    form = urllib.parse.parse_qs(raw, keep_blank_values=True)
    return {key: values[-1] if values else "" for key, values in form.items()}


def make_handler(bridge: Bridge) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        server_version = "AtlasBrain/0.2"

        def send_json(self, payload: dict[str, Any], status: HTTPStatus = HTTPStatus.OK) -> None:
            body = json.dumps(payload, ensure_ascii=False, indent=2).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def send_html(self, html_text: str, status: HTTPStatus = HTTPStatus.OK) -> None:
            body = html_text.encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def send_binary_file(self, path: str, download_name: str) -> None:
            try:
                stat = os.stat(path)
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Disposition", f'attachment; filename="{download_name}"')
                self.send_header("Content-Length", str(stat.st_size))
                self.end_headers()
                with open(path, "rb") as fp:
                    for chunk in iter(lambda: fp.read(1024 * 256), b""):
                        self.wfile.write(chunk)
            except (BrokenPipeError, ConnectionResetError):
                return
            except Exception as exc:
                self.send_json({"ok": False, "error": str(exc)}, HTTPStatus.INTERNAL_SERVER_ERROR)

        def latest_tts_url(self) -> str:
            return f"http://{local_lan_ip()}:{self.server.server_address[1]}/tts/latest.wav"

        def audio_ws_url(self) -> str:
            return f"ws://{local_lan_ip()}:{self.server.server_address[1]}/ws/audio"

        def health_payload(self) -> dict[str, Any]:
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
                "rover_skills_enabled": ENABLE_ROVER_SKILLS,
                "dry_run": bridge.dry_run,
                "latest_tts": latest_tts_meta(),
                "last_turn": latest_turn_meta(),
                "platform_summary": platform["summary"],
                "protocols": platform["protocols"],
            }

        def live_device_payload(self) -> dict[str, Any]:
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

        def cache_tts_and_push(self, tts_payload: dict[str, Any]) -> tuple[dict[str, Any], str, dict[str, Any]]:
            tts_store = store_latest_tts(tts_payload)
            if not tts_store.get("ready"):
                return tts_store, "", {"ok": False, "error": str(tts_store.get("error", "tts not ready"))}
            tts_url = self.latest_tts_url()
            return tts_store, tts_url, bridge.play_latest_tts_on_dualeye(tts_url)

        def accept_websocket(self) -> bool:
            key = self.headers.get("Sec-WebSocket-Key", "").strip()
            upgrade = self.headers.get("Upgrade", "").lower()
            if not key or upgrade != "websocket":
                self.send_json({"ok": False, "error": "websocket upgrade required"}, HTTPStatus.BAD_REQUEST)
                return False
            accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode("ascii")).digest()).decode("ascii")
            self.send_response(HTTPStatus.SWITCHING_PROTOCOLS)
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", accept)
            self.end_headers()
            return True

        def brain_session_payload(self) -> dict[str, Any]:
            return {
                "ok": True,
                "protocol": "atlas.brain.session.v1",
                "stage": "P1_long_lived_json_session",
                "service": "atlas-brain",
                "device_id": bridge.session.device_id or "dualeye",
                "runtime": bridge.runtime.snapshot(),
                "session": bridge.session.snapshot(),
                "recent_events": recent_brain_events(),
                "endpoints": {
                    "json_ws": "/ws/brain",
                    "audio_ws": "/ws/audio",
                    "event_http": "/api/brain/events",
                    "tools": "/api/tools/call",
                    "runtime": "/api/runtime",
                },
                "audio": {
                    "codec": "opus",
                    "sample_rate": 16000,
                    "channels": 1,
                    "frame_ms": 60,
                    "wire_format": "AOP1",
                },
                "notes": [
                    "DualEye 常驻连接 /ws/brain；语音 turn 使用 turn.audio.begin + binary WAV，不再依赖 HTTP WAV 主链路。",
                    "TTS 音频通过同一条 /ws/brain 以 binary WAV 下行，减少零散 HTTP 往返。",
                    "OPUS 二进制音频继续走 /ws/audio，避免控制事件和音频帧互相阻塞。",
                ],
            }

        def handle_brain_session_ws(self) -> None:
            if not self.accept_websocket():
                return
            runtime_session = bridge.runtime.open_session("dualeye", "websocket_brain_json")
            runtime_session.transition("connecting", "brain websocket opened")
            ws_send_json(self.connection, {
                "ok": True,
                "type": "hello",
                "protocol": "atlas.brain.session.v1",
                "stage": "P1_long_lived_json_session",
                "session_id": runtime_session.session_id,
                "audio_ws": self.audio_ws_url(),
                "tools_endpoint": "/api/tools/call",
                "heartbeat_ms": 15000,
            })
            self.connection.settimeout(60)
            pending_audio_turn: dict[str, Any] | None = None
            try:
                while True:
                    opcode, payload = ws_recv_frame(self.connection)
                    if opcode == 0x8:
                        runtime_session.transition("closed", "client_closed")
                        bridge.runtime.close_session(runtime_session.session_id, "client_closed")
                        ws_send_frame(self.connection, 0x8, b"")
                        break
                    if opcode == 0x9:
                        ws_send_frame(self.connection, 0xA, payload)
                        continue
                    if opcode in {0x2, 0x0}:
                        if not pending_audio_turn:
                            ws_send_json(self.connection, {"ok": False, "type": "turn.error", "error": "binary frame without turn.audio.begin"})
                            continue
                        request_id = int(pending_audio_turn.get("request_id", 0) or 0)
                        expected_bytes = int(pending_audio_turn.get("bytes", 0) or 0)
                        speak = bool(pending_audio_turn.get("speak", True))
                        language = str(pending_audio_turn.get("language", "auto") or "auto")
                        audio_buffer = pending_audio_turn.get("_buffer")
                        if not isinstance(audio_buffer, bytearray):
                            audio_buffer = bytearray()
                            pending_audio_turn["_buffer"] = audio_buffer
                        if expected_bytes and len(audio_buffer) + len(payload) > expected_bytes:
                            ws_send_json(self.connection, {
                                "ok": False,
                                "type": "turn.error",
                                "request_id": request_id,
                                "error": f"wav size mismatch: expected {expected_bytes}, got {len(audio_buffer) + len(payload)}",
                            })
                            pending_audio_turn = None
                            continue
                        audio_buffer.extend(payload)
                        if expected_bytes and len(audio_buffer) < expected_bytes:
                            continue
                        payload = bytes(audio_buffer)
                        if len(payload) < 44 or not payload.startswith(b"RIFF"):
                            ws_send_json(self.connection, {
                                "ok": False,
                                "type": "turn.error",
                                "request_id": request_id,
                                "error": "WAV binary frame required",
                            })
                            pending_audio_turn = None
                            continue

                        audio_data_url = "data:audio/wav;base64," + base64.b64encode(payload).decode("ascii")
                        result = bridge.send_audio(audio_data_url, language=language, speak=speak)
                        tts_store = {"ready": False}
                        if speak and isinstance(result.get("tts"), dict):
                            tts_store = store_latest_tts(result["tts"])
                        reply = reply_from_text_result(result.get("text_result", {}))
                        asr_text = str(result.get("asr", {}).get("text", ""))
                        turn_error = str(result.get("asr", {}).get("error", "") or result.get("error", "") or tts_store.get("error", ""))
                        device_intent_ok = bool(result.get("ok"))
                        turn_ok = bool(device_intent_ok or tts_store.get("ready") or reply)
                        remember_turn({
                            "kind": "device_audio_ws",
                            "ok": turn_ok,
                            "device_intent_ok": device_intent_ok,
                            "asr_text": "" if result.get("ignored") else asr_text,
                            "reply": reply,
                            "source": str(result.get("text_result", {}).get("source", "")),
                            "intent_count": len(result.get("text_result", {}).get("intents", [])),
                            "tts_ready": bool(tts_store.get("ready")),
                            "tts_bytes": int(tts_store.get("bytes", 0) or 0),
                            "error": turn_error,
                            "transport": "ws_brain_binary",
                        })
                        sys.stderr.write(
                            "[voice-turn-ws] "
                            f"ok={turn_ok} request_id={request_id} "
                            f"asr={log_snippet(asr_text)} "
                            f"reply={log_snippet(reply)} "
                            f"tts_ready={bool(tts_store.get('ready'))} "
                            f"error={log_snippet(turn_error, 120)}\n"
                        )
                        if tts_store.get("ready"):
                            tts_audio, _tts_meta = latest_tts_wav()
                            if tts_audio:
                                ws_send_json(self.connection, {
                                    "ok": True,
                                    "type": "turn.tts.begin",
                                    "request_id": request_id,
                                    "content_type": "audio/wav",
                                    "bytes": len(tts_audio),
                                    "transport": "ws_binary",
                                })
                                ws_send_frame(self.connection, 0x2, tts_audio)
                        ws_send_json(self.connection, {
                            "ok": turn_ok,
                            "type": "turn.result" if turn_ok else "turn.error",
                            "request_id": request_id,
                            "transport": "ws_brain_binary",
                            "device_intent_ok": device_intent_ok,
                            "asr_text": "" if result.get("ignored") else asr_text,
                            "reply": reply,
                            "source": str(result.get("text_result", {}).get("source", "")),
                            "intent_count": len(result.get("text_result", {}).get("intents", [])),
                            "tts_ready": bool(tts_store.get("ready")),
                            "tts_bytes": int(tts_store.get("bytes", 0) or 0),
                            "tts_transport": "ws_binary" if tts_store.get("ready") else "none",
                            "error": turn_error,
                        })
                        runtime_session.transition("idle" if turn_ok else "error", "turn.result")
                        pending_audio_turn = None
                        continue
                    if opcode != 0x1:
                        ws_send_json(self.connection, {"ok": False, "error": f"unsupported opcode: {opcode}"})
                        continue
                    try:
                        event = json.loads(payload.decode("utf-8", errors="replace") or "{}")
                    except Exception as exc:
                        ws_send_json(self.connection, {"ok": False, "error": f"bad json: {exc}"})
                        continue
                    event_type = str(event.get("type", event.get("event", "event")) or "event")
                    device_id = str(event.get("device_id", "dualeye") or "dualeye")
                    if event_type == "hello":
                        runtime_session.transition("idle", "hello")
                    elif event_type == "turn.audio.begin":
                        pending_audio_turn = {
                            "request_id": int(event.get("request_id", 0) or 0),
                            "bytes": int(event.get("bytes", 0) or 0),
                            "language": str(event.get("language", "auto") or "auto"),
                            "speak": bool(event.get("speak", True)),
                        }
                        runtime_session.turn_count += 1
                        runtime_session.transition("recording", "turn.audio.begin")
                        stored = remember_brain_event({"event": event_type, "device_id": device_id, "payload": event})
                        ws_send_json(self.connection, {
                            "ok": True,
                            "type": "ack",
                            "event": event_type,
                            "request_id": pending_audio_turn["request_id"],
                            "binary": "wav",
                            "stored": stored,
                            "session": runtime_session.to_dict(),
                        })
                        continue
                    elif event_type in {"listen", "listening"}:
                        state = str(event.get("state", "start") or "start")
                        runtime_session.transition("listening" if state != "stop" else "idle", state)
                    elif event_type in {"turn.started", "start"}:
                        runtime_session.turn_count += 1
                        runtime_session.transition("recording", event_type)
                    elif event_type in {"thinking", "turn.thinking"}:
                        runtime_session.transition("thinking", event_type)
                    elif event_type in {"speaking", "turn.speaking"}:
                        runtime_session.transition("speaking", event_type)
                    elif event_type in {"turn.done", "played", "idle"}:
                        runtime_session.transition("idle", event_type)
                    elif event_type in {"failed", "turn.failed", "error"}:
                        runtime_session.error_count += 1
                        runtime_session.last_error = str(event.get("error", event.get("detail", "")) or "")
                        runtime_session.transition("error", runtime_session.last_error or event_type)
                    stored = remember_brain_event({"event": event_type, "device_id": device_id, "payload": event})
                    ws_send_json(self.connection, {
                        "ok": True,
                        "type": "ack",
                        "event": event_type,
                        "session": runtime_session.to_dict(),
                        "stored": stored,
                        "server_time": int(time.time()),
                    })
            except Exception as exc:
                runtime_session.error_count += 1
                runtime_session.last_error = str(exc)
                bridge.runtime.close_session(runtime_session.session_id, f"exception: {exc}")

        def handle_audio_stream_ws(self) -> None:
            if not self.accept_websocket():
                return

            parsed_ws = urllib.parse.urlparse(self.path)
            ws_query = urllib.parse.parse_qs(parsed_ws.query)
            stream_turn_enabled = str(ws_query.get("turn", ["0"])[0]).strip().lower() in {"1", "true", "yes", "on"}
            stream_speak = str(ws_query.get("speak", ["1" if stream_turn_enabled else "0"])[0]).strip().lower() in {"1", "true", "yes", "on"}
            stream_language = str(ws_query.get("language", ["zh"])[0] or "zh")
            stream_tts_style = str(ws_query.get("tts_style", [bridge.session.default_tts_style])[0] or bridge.session.default_tts_style)
            stream_tts_voice = str(ws_query.get("tts_voice", [""])[0] or "")
            stream_tts_singing = str(ws_query.get("tts_singing", ["0"])[0]).strip().lower() in {"1", "true", "yes", "on"}

            runtime_session = bridge.runtime.open_session("dualeye", "websocket_audio")
            runtime_stream = bridge.runtime.start_audio_stream(runtime_session.session_id, "dualeye")
            opus_packets: list[bytes] = []
            stream = {
                "ok": True,
                "stage": "P2_dualeye_ws_opus_stream",
                "session_id": runtime_session.session_id,
                "codec": "opus",
                "sample_rate": 16000,
                "channels": 1,
                "frame_ms": 60,
                "frames": 0,
                "bytes": 0,
                "wire_bytes": 0,
                "atlas_frames": 0,
                "legacy_binary_frames": 0,
                "binary_messages": 0,
                "text_messages": 0,
                "sequence_gaps": 0,
                "last_seq": 0,
                "last_packet_bytes": 0,
                "mic_level": 0,
                "mic_rms": 0,
                "mic_peak": 0,
                "started_at": time.time(),
                "ended_at": 0.0,
                "closed": False,
                "turn_requested": stream_turn_enabled,
                "turn_processed": False,
                "decode": {},
                "turn": {},
                "notes": "接收 DualEye AOP1 二进制 OPUS 帧；turn=1 时流结束后解码 WAV 并进入 ASR/LLM/TTS。",
            }

            def sync_runtime_stream(extra: Optional[dict[str, Any]] = None) -> dict[str, Any]:
                stream.update(runtime_stream.to_dict())
                stream["turn_requested"] = stream_turn_enabled
                stream["packet_cache_count"] = len(opus_packets)
                stream["notes"] = "接收 DualEye AOP1 二进制 OPUS 帧；运行时会话会统计缺帧、payload mismatch 和语音段。turn=1 可自动进入语音 turn。"
                if extra:
                    stream.update(extra)
                return stream

            def process_opus_turn() -> dict[str, Any]:
                if stream.get("turn_processed"):
                    return stream.get("turn", {}) if isinstance(stream.get("turn"), dict) else {"ok": False, "error": "already processed"}
                stream["turn_processed"] = True
                if not stream_turn_enabled:
                    return {"ok": True, "skipped": True, "reason": "turn not requested"}
                runtime_session.transition("thinking", "decode opus turn")
                bridge.send_intents([
                    {"tool": "atlas_show_page", "input": {"page": "voice"}},
                    {"tool": "atlas_set_expression", "input": {"expression": "thinking"}},
                ])
                decode = decode_opus_packets_to_wav(
                    opus_packets,
                    sample_rate=int(stream.get("sample_rate", 16000) or 16000),
                    channels=int(stream.get("channels", 1) or 1),
                    frame_ms=int(stream.get("frame_ms", 60) or 60),
                )
                stream["decode"] = {k: v for k, v in decode.items() if k != "wav"}
                if not decode.get("ok"):
                    runtime_session.error_count += 1
                    runtime_session.last_error = str(decode.get("error", "opus decode failed"))
                    bridge.send_intents([
                        {"tool": "atlas_chat", "input": {"chat_text": "刚才的声音没解开，可以再说一遍吗？", "speech": "刚才的声音没解开，可以再说一遍吗？", "action": "chat"}},
                        {"tool": "atlas_set_expression", "input": {"expression": "curious"}},
                        {"tool": "atlas_show_page", "input": {"page": "chat"}},
                    ])
                    return {"ok": False, "stage": "decode_failed", "decode": stream["decode"]}

                audio_data = "data:audio/wav;base64," + base64.b64encode(decode["wav"]).decode("ascii")
                result = bridge.send_audio(
                    audio_data,
                    language=stream_language,
                    speak=stream_speak,
                    tts_voice=stream_tts_voice,
                    tts_style=stream_tts_style,
                    tts_singing=stream_tts_singing,
                )
                if stream_speak and isinstance(result.get("tts"), dict):
                    tts_store, tts_url, dualeye_play = self.cache_tts_and_push(result["tts"])
                    result["tts_cached"] = tts_store
                    result["tts_url"] = tts_url
                    result["dualeye_play"] = dualeye_play
                    runtime_session.transition("speaking" if dualeye_play.get("ok") else "error",
                                               "tts pushed" if dualeye_play.get("ok") else "tts push failed")
                else:
                    runtime_session.transition("idle" if result.get("ok") else "error", "opus turn done")

                turn_meta = {
                    "kind": "opus_turn",
                    "ok": bool(result.get("ok")),
                    "asr_text": str(result.get("asr", {}).get("text", "")),
                    "reply": reply_from_text_result(result.get("text_result", {}), fallback=""),
                    "source": str(result.get("text_result", {}).get("source", "")),
                    "intent_count": len(result.get("text_result", {}).get("intents", [])) if isinstance(result.get("text_result"), dict) else 0,
                    "tts_ready": bool(result.get("tts_cached", {}).get("ready")),
                    "dualeye_play": result.get("dualeye_play", {}),
                    "decode": stream["decode"],
                    "error": str(result.get("asr", {}).get("error", "") or result.get("error", "")),
                }
                remember_turn(turn_meta)
                compact = compact_audio_payload(result)
                stream["turn"] = {
                    "ok": bool(compact.get("ok")),
                    "asr_text": str(compact.get("asr", {}).get("text", "")),
                    "reply": reply_from_text_result(compact.get("text_result", {}), fallback=""),
                    "tts_ready": bool(compact.get("tts_cached", {}).get("ready")),
                    "dualeye_play": compact.get("dualeye_play", {}),
                    "error": str(compact.get("asr", {}).get("error", "") or compact.get("error", "")),
                }
                return compact

            sync_runtime_stream()
            ws_send_json(self.connection, {"ok": True, "protocol": "atlas.audio.stream.v0", "stage": "P2_dualeye_ws_opus_stream", "message": "ready", "binary_header": "AOP1"})
            self.connection.settimeout(15)

            try:
                while True:
                    opcode, payload = ws_recv_frame(self.connection)
                    if opcode == 0x8:
                        bridge.runtime.close_session(runtime_session.session_id, "client_closed")
                        sync_runtime_stream({"closed": True, "ended_at": time.time()})
                        remember_audio_stream(stream)
                        ws_send_frame(self.connection, 0x8, b"")
                        break
                    if opcode == 0x9:
                        ws_send_frame(self.connection, 0xA, payload)
                        continue
                    if opcode == 0x1:
                        stream["text_messages"] = int(stream["text_messages"]) + 1
                        try:
                            event = json.loads(payload.decode("utf-8", errors="replace") or "{}")
                        except Exception:
                            event = {"type": "text", "raw": payload.decode("utf-8", errors="replace")}
                        event_type = str(event.get("type", event.get("event", "event")) or "event")
                        runtime_stream.note_text(event)
                        if event_type == "hello":
                            runtime_session.transition("connecting", "hello")
                        elif event_type == "start":
                            runtime_session.turn_count += 1
                            runtime_session.transition("recording", "turn started")
                        elif event_type == "listen":
                            state = str(event.get("state", event.get("mode", "")) or "")
                            runtime_session.transition("listening" if state != "stop" else "thinking", state or "listen")
                        elif event_type == "end":
                            runtime_session.transition("thinking", "turn ended")
                        if event_type == "start":
                            stream["codec"] = str(event.get("codec", stream["codec"]) or stream["codec"]).lower()
                            stream["sample_rate"] = int(event.get("sample_rate", stream["sample_rate"]) or stream["sample_rate"])
                            stream["channels"] = int(event.get("channels", stream["channels"]) or stream["channels"])
                            stream["frame_ms"] = int(event.get("frame_ms", stream["frame_ms"]) or stream["frame_ms"])
                            stream["turn_id"] = str(event.get("turn_id", ""))
                        elif event_type == "end":
                            stream["ended_at"] = time.time()
                            stream["estimated_audio_ms"] = int(stream["frames"]) * int(stream["frame_ms"])
                            runtime_stream.close()
                            sync_runtime_stream({"ended_at": time.time()})
                            turn_result = process_opus_turn()
                            sync_runtime_stream({"ended_at": time.time(), "turn": stream.get("turn", turn_result)})
                            remember_audio_stream(stream)
                        remember_brain_event({"event": f"audio_stream.{event_type}", "device_id": event.get("device_id", "dualeye"), "payload": event})
                        sync_runtime_stream()
                        ws_send_json(self.connection, {"ok": True, "type": "ack", "event": event_type, "stream": stream})
                        continue
                    if opcode == 0x2:
                        stream["binary_messages"] = int(stream["binary_messages"]) + 1
                        stream["wire_bytes"] = int(stream.get("wire_bytes", 0) or 0) + len(payload)
                        frame = parse_atlas_opus_frame(payload)
                        if frame.get("ok"):
                            stream["atlas_frames"] = int(stream.get("atlas_frames", 0) or 0) + 1
                            stream["frames"] = int(stream["frames"]) + 1
                            stream["bytes"] = int(stream["bytes"]) + int(frame.get("actual_payload_len", 0) or 0)
                            stream["last_packet_bytes"] = int(frame.get("actual_payload_len", 0) or 0)
                            stream["sample_rate"] = int(frame.get("sample_rate", stream["sample_rate"]) or stream["sample_rate"])
                            stream["channels"] = int(frame.get("channels", stream["channels"]) or stream["channels"])
                            stream["frame_ms"] = int(frame.get("frame_ms", stream["frame_ms"]) or stream["frame_ms"])
                            stream["mic_level"] = int(frame.get("mic_level", 0) or 0)
                            stream["mic_rms"] = int(frame.get("mic_rms", 0) or 0)
                            stream["mic_peak"] = int(frame.get("mic_peak", 0) or 0)
                            seq = int(frame.get("seq", 0) or 0)
                            last_seq = int(stream.get("last_seq", 0) or 0)
                            if last_seq and seq > last_seq + 1:
                                stream["sequence_gaps"] = int(stream.get("sequence_gaps", 0) or 0) + (seq - last_seq - 1)
                            stream["last_seq"] = seq
                            if not bool(frame.get("payload_len_match", True)):
                                stream["payload_len_mismatches"] = int(stream.get("payload_len_mismatches", 0) or 0) + 1
                            if bool(frame.get("payload_len_match", True)) and len(opus_packets) < ATLAS_OPUS_TURN_MAX_PACKETS:
                                header_len = int(frame.get("header_len", ATLAS_OPUS_FRAME_HEADER.size) or ATLAS_OPUS_FRAME_HEADER.size)
                                opus_packets.append(payload[header_len:])
                        else:
                            stream["legacy_binary_frames"] = int(stream.get("legacy_binary_frames", 0) or 0) + 1
                            stream["frames"] = int(stream["frames"]) + 1
                            stream["bytes"] = int(stream["bytes"]) + len(payload)
                            stream["last_binary_error"] = str(frame.get("error", "unknown binary frame"))
                        runtime_stream.note_binary(len(payload), len(payload), frame)
                        sync_runtime_stream()
                        if int(stream["frames"]) % 10 == 0:
                            remember_audio_stream(stream)
                            ws_send_json(self.connection, {"ok": True, "type": "chunk_ack", "frames": stream["frames"], "bytes": stream["bytes"], "last_seq": stream.get("last_seq", 0), "gaps": stream.get("sequence_gaps", 0)})
                        continue
                    ws_send_json(self.connection, {"ok": False, "error": f"unsupported opcode: {opcode}"})
            except Exception as exc:
                runtime_session.error_count += 1
                runtime_session.last_error = str(exc)
                bridge.runtime.close_session(runtime_session.session_id, f"exception: {exc}")
                sync_runtime_stream({"ok": False, "error": str(exc), "ended_at": time.time()})
                remember_audio_stream(stream)

        def do_OPTIONS(self) -> None:
            self.send_response(HTTPStatus.NO_CONTENT)
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.end_headers()

        def do_GET(self) -> None:
            parsed = urllib.parse.urlparse(self.path)
            path = parsed.path
            if path == "/favicon.ico":
                self.send_response(HTTPStatus.NO_CONTENT)
                self.send_header("Cache-Control", "public, max-age=86400")
                self.end_headers()
                return
            if path == "/ws/audio":
                self.handle_audio_stream_ws()
                return
            if path == "/ws/brain":
                self.handle_brain_session_ws()
                return
            if path == "/health":
                self.send_json(self.health_payload())
                return
            if path == "/diagnostics":
                payload = self.health_payload()
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
                self.send_json(payload)
                return
            if path == "/capabilities":
                payload = self.health_payload()
                payload["brain"] = {
                    "skills": bridge.skills.list_public(),
                    "roles": {key: {k: v for k, v in value.items() if k != "prompt"} for key, value in ROLE_PROFILES.items()},
                    "endpoints": ["/health", "/diagnostics", "/api/acceptance/report", "/api/runtime", "/api/runtime/sessions", "/api/runtime/score", "/api/platform", "/api/devices", "/api/device/live", "/api/device/scene", "/api/device/selftest", "/api/device/system-info", "/api/device/opus-probe", "/api/device/opus-stream/start", "/api/device/opus-turn/start", "/api/device/opus-stream/stop", "/api/device/opus-stream/status", "/devices", "/acceptance", "/skills", "/skill", "/api/tools", "/api/tools/list", "/api/tools/call", "/mcp/tools/list", "/mcp/tools/call", "/turn/text", "/turn/audio", "/api/brain/session", "/api/brain/events", "/ws/brain", "/ws/audio", "/api/audio/stream/status", "/api/audio/stream/simulate", "/api/sr/status", "/api/sr/simulate", "/role/switch", "/ota/manifest", "/api/ota/packages", "/ota/package/app_ota"],
                }
                try:
                    payload["dualeye_capabilities"] = bridge.capabilities()
                except Exception as exc:
                    payload["dualeye_capabilities_error"] = str(exc)
                self.send_json(payload)
                return
            if path == "/api/platform":
                self.send_json({"ok": True, "platform": bridge.platform_snapshot(), "last_turn": latest_turn_meta(), "recent_events": recent_brain_events()})
                return
            if path == "/api/runtime":
                self.send_json({"ok": True, "runtime": bridge.runtime.snapshot(), "score": bridge.runtime_score_payload()})
                return
            if path == "/api/runtime/sessions":
                snapshot = bridge.runtime.snapshot()
                self.send_json({"ok": True, "protocol": snapshot["protocol"], "sessions": snapshot["sessions"], "recent_events": snapshot["recent_events"]})
                return
            if path == "/api/runtime/score":
                self.send_json(bridge.runtime_score_payload())
                return
            if path == "/api/acceptance/report":
                query = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
                skip_device = str((query.get("skip_device") or ["0"])[-1]).lower() in {"1", "true", "yes", "on"}
                self.send_json(build_acceptance_report(bridge, self.audio_ws_url(), skip_device=skip_device))
                return
            if path == "/api/providers":
                snapshot = bridge.platform_snapshot()
                self.send_json({"ok": True, "providers": snapshot["providers"], "summary": snapshot["summary"]})
                return
            if path == "/api/protocols":
                snapshot = bridge.platform_snapshot()
                self.send_json({"ok": True, "protocols": snapshot["protocols"], "summary": snapshot["summary"]})
                return
            if path == "/api/brain/session":
                self.send_json(self.brain_session_payload())
                return
            if path == "/api/brain/events":
                self.send_json({"ok": True, "protocol": "atlas.brain.session.v1", "stage": "P1_long_lived_json_session", "events": recent_brain_events(), "ws_endpoint": "/ws/brain"})
                return
            if path == "/api/audio/stream/status":
                self.send_json(audio_stream_status_payload(bridge, self.audio_ws_url()))
                return
            if path == "/api/sr/status":
                try:
                    dualeye_sr = http_json(f"{bridge.dualeye_url}/api/sr/status", timeout=2.0)
                except Exception as exc:
                    dualeye_sr = {"ok": False, "error": str(exc), "stage": "P3_resource_probe", "fallback": "energy_gate_vad"}
                self.send_json({
                    "ok": True,
                    "stage": "P3_resource_probe",
                    "dualeye": dualeye_sr,
                    "mac_simulation_endpoint": "/api/sr/simulate",
                    "notes": "WakeNet/AEC 当前是资源验证阶段；实机看 DualEye /api/sr/status 的 ESP-SR、模型分区和堆内存结果。",
                })
                return
            if path == "/ota/manifest":
                manifest = build_ota_manifest()
                manifest["package_base_url"] = f"http://{local_lan_ip()}:{self.server.server_address[1]}/ota/package"
                for package in manifest.get("packages", []):
                    if isinstance(package, dict):
                        package["url"] = f"{manifest['package_base_url']}/{package.get('name', '')}"
                self.send_json(manifest)
                return
            if path == "/api/ota/packages":
                manifest = build_ota_manifest()
                package_base_url = f"http://{local_lan_ip()}:{self.server.server_address[1]}/ota/package"
                for package in manifest.get("packages", []):
                    if isinstance(package, dict):
                        package["url"] = f"{package_base_url}/{package.get('name', '')}"
                self.send_json({
                    "ok": True,
                    "protocol": manifest["protocol"],
                    "status": manifest["status"],
                    "packages": manifest["packages"],
                    "missing": manifest["missing"],
                    "flash_args": manifest["flash_args"],
                    "package_base_url": package_base_url,
                })
                return
            if path.startswith("/ota/package/"):
                name = urllib.parse.unquote(path.rsplit("/", 1)[-1])
                package_map = {pkg_name: rel_path for pkg_name, rel_path, _offset in OTA_PACKAGE_FILES}
                rel_path = package_map.get(name)
                if rel_path is None:
                    self.send_json({"ok": False, "error": "unknown package"}, HTTPStatus.NOT_FOUND)
                    return
                abs_path = os.path.abspath(os.path.join(DUALEYE_BUILD_DIR, rel_path))
                if not abs_path.startswith(os.path.abspath(DUALEYE_BUILD_DIR) + os.sep) or not os.path.exists(abs_path):
                    self.send_json({"ok": False, "error": "package not built"}, HTTPStatus.NOT_FOUND)
                    return
                self.send_binary_file(abs_path, os.path.basename(abs_path))
                return
            if path == "/status":
                try:
                    self.send_json({"ok": True, "dualeye": bridge.status()})
                except Exception as exc:
                    self.send_json({"ok": False, "error": str(exc)}, HTTPStatus.BAD_GATEWAY)
                return
            if path == "/skills":
                self.send_json({
                    "ok": True,
                    "protocol": "atlas.skills.legacy.v1",
                    "skills": bridge.skills.list_public(),
                    "roles": {key: {k: v for k, v in value.items() if k != "prompt"} for key, value in ROLE_PROFILES.items()},
                })
                return
            if path in {"/api/tools", "/api/tools/list", "/mcp/tools/list"}:
                payload = bridge.skills.tool_schema_payload()
                if path == "/mcp/tools/list":
                    self.send_json({
                        "ok": True,
                        "protocol": "atlas.tools.v0.desk_apps",
                        "tools": payload["tools"],
                    })
                else:
                    self.send_json(payload)
                return
            if path == "/api/devices" or path == "/devices.json":
                snapshot = bridge.platform_snapshot()
                self.send_json({
                    "ok": True,
                    "devices": snapshot["devices"],
                    "admin_path": "/admin",
                    "platform": {
                        "service": "atlas-brain",
                        "device_count": snapshot["summary"]["device_count"],
                        "rover_skills_enabled": ENABLE_ROVER_SKILLS,
                    },
                })
                return
            if path in {"/api/device/live", "/api/device/state"}:
                self.send_json(self.live_device_payload())
                return
            if path == "/api/device/scene":
                try:
                    status = bridge.status()
                    self.send_json({
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
                    self.send_json({
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
                return
            if path == "/api/device/selftest":
                try:
                    self.send_json(http_json(f"{bridge.dualeye_url}/api/selftest", timeout=3.0))
                except Exception as exc:
                    self.send_json({"ok": False, "error": str(exc), "dualeye_url": bridge.dualeye_url}, HTTPStatus.BAD_GATEWAY)
                return
            if path == "/api/device/system-info":
                try:
                    self.send_json(http_json(f"{bridge.dualeye_url}/api/system/info", timeout=3.0))
                except Exception as exc:
                    self.send_json({"ok": False, "error": str(exc), "dualeye_url": bridge.dualeye_url}, HTTPStatus.BAD_GATEWAY)
                return
            if path == "/api/device/opus-stream/status":
                try:
                    self.send_json(bridge.dualeye_opus_stream_status())
                except Exception as exc:
                    self.send_json({"ok": False, "error": str(exc), "dualeye_url": bridge.dualeye_url}, HTTPStatus.BAD_GATEWAY)
                return
            if path.startswith("/api/devices/"):
                device_id = urllib.parse.unquote(path.removeprefix("/api/devices/")).strip("/")
                snapshot = bridge.platform_snapshot()
                for device in snapshot["devices"]:
                    if str(device.get("id")) == device_id:
                        self.send_json({"ok": True, "device": device})
                        return
                self.send_json({"ok": False, "error": f"device not found: {device_id}"}, HTTPStatus.NOT_FOUND)
                return
            if path == "/devices":
                self.send_html(render_devices_page(bridge.devices()))
                return
            if path == "/acceptance":
                self.send_html(f"""<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Atlas 烧录验收</title>
<style>body{{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;background:#0a1017;color:#eef3f8}}main{{max-width:1040px;margin:0 auto;padding:16px}}section{{border:1px solid #26384a;background:#101a24;border-radius:8px;padding:13px;margin:10px 0}}button{{font:inherit;border:1px solid #3f6078;background:#162333;color:#eef3f8;border-radius:8px;padding:9px 11px;cursor:pointer;margin:4px 6px 4px 0}}button.primary{{border-color:#3fc9ff}}pre{{white-space:pre-wrap;background:#070b10;border:1px solid #26384a;border-radius:8px;padding:10px;max-height:280px;overflow:auto}}.grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:10px}}.card{{border:1px solid #26384a;background:#0b121b;border-radius:8px;padding:10px;min-height:96px}}.muted{{color:#a9b7c6;font-size:13px;line-height:1.45}}.pass{{color:#74e0a3}}.warn{{color:#ffd166}}.fail{{color:#ff8a75}}</style>
<main><h1>Atlas 烧录验收页</h1><p class="muted">DualEye：{html.escape(bridge.dualeye_url)} · Brain：本机 {html.escape(str(self.server.server_address[1]))} · <a style="color:#9ae3ff" href="/admin">管理端</a> · <a style="color:#9ae3ff" href="/devices">设备列表</a></p>
	<section><button class="primary" onclick="runReport()">生成验收报告</button><button onclick="runAll()">运行逐项验收</button><button onclick="runOne('/api/runtime/score')">80 分评分</button><button onclick="runOne('/api/runtime')">运行时</button><button onclick="runOne('/api/device/selftest')">DualEye 自检</button><button onclick="runPost('/api/device/opus-probe',{{duration_ms:1200}})">OPUS 真机探针</button><button onclick="runPost('/api/device/opus-stream/start',{{duration_ms:1800}})">OPUS 真流 1.8s</button><button onclick="runPost('/api/device/opus-turn/start',{{duration_ms:2200,speak:true,language:'zh'}})">OPUS 语音 Turn</button><button onclick="runPost('/api/device/opus-stream/stop',{{}})">停止真流</button><button onclick="runOne('/api/audio/stream/status')">流状态</button><button onclick="runOne('/api/device/system-info')">系统信息</button><button onclick="runOne('/api/tools/list')">工具表</button><button onclick="runOne('/ota/manifest')">OTA Manifest</button><button onclick="runOne('/health')">Brain Health</button></section>
<section><h2>验收摘要</h2><div id="cards" class="grid"></div></section>
<section><h2>原始结果</h2><pre id="out">等待验收...</pre></section></main>
<script>
const checks=[
  ['DualEye 自检','/api/device/selftest',true],
  ['DualEye OPUS 真机探针','/api/device/opus-probe',true,'POST'],
  ['DualEye OPUS WebSocket 真流','/api/device/opus-stream/start',true,'POST',{{duration_ms:1800}}],
  ['DualEye OPUS 语音 Turn','/api/device/opus-turn/start',true,'POST',{{duration_ms:2200,speak:false,language:'zh'}}],
  ['运行时评分','/api/runtime/score',true],
  ['OPUS 流状态','/api/audio/stream/status',true],
  ['DualEye 系统信息','/api/device/system-info',true],
  ['Brain Health','/health',true],
  ['工具表','/api/tools/list',true],
  ['MCP 工具表','/mcp/tools/list',false],
  ['OTA Manifest','/ota/manifest',true],
  ['OTA 包清单','/api/ota/packages',true],
  ['协议通道','/api/protocols',true]
];
function h(s){{return String(s??'').replace(/[&<>"']/g,m=>({{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}}[m]))}}
async function getJson(url,method='GET',payload=null){{const opt={{method,cache:'no-store'}};if(method==='POST'){{opt.headers={{'Content-Type':'application/json'}};opt.body=JSON.stringify(payload||{{duration_ms:1200}})}}const r=await fetch(url,opt);const text=await r.text();let data;try{{data=JSON.parse(text)}}catch(e){{data={{ok:false,raw:text,error:e.message||String(e)}}}}return {{status:r.status,ok:r.ok&&data.ok!==false,data,url}}}}
function compact(name,res){{const d=res.data||{{}};if(name.includes('自检'))return 'ready='+d.ready_to_flash+' pass='+(d.summary?.pass)+' warn='+(d.summary?.warn)+' fail='+(d.summary?.fail);if(name.includes('真机探针')){{const p=d.probe||{{}};return 'frames='+(p.frames_encoded||0)+' bytes='+(p.encoded_bytes||0)+' avg='+(p.avg_packet_bytes||0)+' err='+(p.last_error||'')}}if(name.includes('真流')||name.includes('流状态')){{const s=(d.stream||d.last_stream||{{}});const ds=(d.dualeye_stream&&d.dualeye_stream.stream)||{{}};return 'rx_frames='+(s.frames||0)+' rx_bytes='+(s.bytes||0)+' last_seq='+(s.last_seq||0)+' gaps='+(s.sequence_gaps||0)+' device='+((ds.stage)||'')}}if(name.includes('系统'))return (d.fingerprint?.firmware_version||d.firmware||'')+' '+(d.storage?.assets_version||'');if(name.includes('Health'))return 'skills='+d.skill_count+' llm='+d.llm_enabled+' asr='+d.asr_enabled+' tts='+d.tts_enabled;if(name.includes('工具'))return 'protocol='+(d.protocol||'')+' count='+(d.tool_count||((d.tools||[]).length));if(name.includes('OTA'))return 'status='+(d.status||'')+' packages='+(d.packages||[]).length+' missing='+(d.missing||[]).length;return d.error||'ok'}}
function card(name,res,required){{const cls=res.ok?'pass':(required?'fail':'warn');const label=res.ok?'PASS':(required?'FAIL':'WARN');return '<div class="card"><b class="'+cls+'">'+label+' · '+h(name)+'</b><p class="muted">'+h(compact(name,res))+'</p><p class="muted">'+h(res.url)+' · HTTP '+res.status+'</p></div>'}}
async function runAll(){{const results=[];document.getElementById('cards').innerHTML='运行中...';for(const c of checks){{try{{const res=await getJson(c[1],c[3]||'GET',c[4]||null);res.name=c[0];res.required=c[2];results.push(res)}}catch(e){{results.push({{name:c[0],required:c[2],url:c[1],status:0,ok:false,data:{{error:e.message||String(e)}}}})}}}}document.getElementById('cards').innerHTML=results.map(r=>card(r.name,r,r.required)).join('');document.getElementById('out').textContent=JSON.stringify(results,null,2)}}
async function runReport(){{const res=await getJson('/api/acceptance/report');const checks=(res.data&&res.data.checks)||[];document.getElementById('cards').innerHTML=checks.map(c=>'<div class="card"><b class="'+c.status+'">'+String(c.status).toUpperCase()+' · '+h(c.name)+'</b><p class="muted">'+h(c.detail||'')+'</p><p class="muted">'+h(c.next_step||'')+'</p></div>').join('');document.getElementById('out').textContent=JSON.stringify(res.data||res,null,2)}}
async function runOne(url){{const res=await getJson(url);document.getElementById('out').textContent=JSON.stringify(res,null,2)}}
async function runPost(url,payload){{const res=await getJson(url,'POST',payload);document.getElementById('out').textContent=JSON.stringify(res,null,2)}}
runAll();
</script>""")
                return
            if path == "/app" or (path.startswith("/devices/") and path.endswith("/app")):
                device = bridge.device_summary()
                self.send_html(render_device_app_page(
                    device,
                    dualeye_url=bridge.dualeye_url,
                    lan_url=f"http://{local_lan_ip()}:{self.server.server_address[1]}",
                    rover_enabled=ENABLE_ROVER_SKILLS,
                ))
                return
            if path == "/tools":
                legacy_tools = [
                    "atlas_show_page",
                    "atlas_set_expression",
                    "atlas_pomodoro",
                    "atlas_calendar",
                    "atlas_chat",
                    "atlas_app_action",
                ]
                if ENABLE_ROVER_SKILLS:
                    legacy_tools = ["atlas_rover_move", "atlas_rover_stop"] + legacy_tools
                self.send_json({
                    "ok": True,
                    "legacy_tools": legacy_tools,
                    "skills": bridge.skills.list_public(),
                    "tool_schema": bridge.skills.tool_schema_payload(),
                })
                return
            if path == "/tts/latest.wav":
                audio, meta = latest_tts_wav()
                if not audio:
                    self.send_json({"ok": False, "error": "no cached tts audio"}, HTTPStatus.NOT_FOUND)
                    return
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "audio/wav")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("X-Atlas-Audio-Bytes", str(meta.get("bytes", len(audio))))
                self.send_header("Content-Length", str(len(audio)))
                self.end_headers()
                self.wfile.write(audio)
                return
            if path == "/":
                device = bridge.device_summary()
                self.send_html(render_device_app_page(
                    device,
                    dualeye_url=bridge.dualeye_url,
                    lan_url=f"http://{local_lan_ip()}:{self.server.server_address[1]}",
                    rover_enabled=ENABLE_ROVER_SKILLS,
                ))
                return
            if path == "/admin":
                health = self.health_payload()
                self.send_html(render_admin_page(
                    dualeye_url=bridge.dualeye_url,
                    llm_model=bridge.llm_model if bridge.llm_enabled() else "",
                    asr_model=bridge.asr_model if bridge.asr_enabled() else "",
                    tts_model=bridge.tts_model if bridge.tts_enabled() else "",
                    provider_summary=health,
                ))
                return
            self.send_json({"ok": False, "error": "not found"}, HTTPStatus.NOT_FOUND)

        def do_POST(self) -> None:
            parsed = urllib.parse.urlparse(self.path)
            path = parsed.path
            query = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
            if path == "/device/audio/wav":
                try:
                    length = int(self.headers.get("Content-Length", "0") or "0")
                    raw = self.rfile.read(length) if length > 0 else b""
                    handle_device_wav_turn(self, bridge, raw, query, self.latest_tts_url())
                except Exception as exc:
                    sys.stderr.write(f"[voice-turn] exception={str(exc)[:200]!r}\n")
                    self.send_json({"ok": False, "error": str(exc)}, HTTPStatus.INTERNAL_SERVER_ERROR)
                return
            try:
                payload = read_request_json(self)
            except Exception as exc:
                self.send_json({"ok": False, "error": f"bad request: {exc}"}, HTTPStatus.BAD_REQUEST)
                return
            if path == "/api/brain/events":
                event = remember_brain_event(payload)
                self.send_json({
                    "ok": True,
                    "protocol": "atlas.brain.session.v1",
                    "stage": "P1_long_lived_json_session",
                    "stored": event,
                    "recent": recent_brain_events(),
                    "ws_endpoint": "/ws/brain",
                })
                return
            if path == "/api/audio/stream/simulate":
                handle_audio_stream_simulate(self, payload)
                return
            if path == "/api/device/opus-probe":
                handle_dualeye_opus_probe(self, bridge, payload)
                return
            if path in {"/api/device/opus-stream/start", "/api/device/opus-turn/start"}:
                handle_dualeye_opus_stream_start(self, bridge, path, payload, self.audio_ws_url())
                return
            if path == "/api/device/opus-stream/stop":
                handle_dualeye_opus_stream_stop(self, bridge)
                return
            if path == "/api/sr/simulate":
                result = simulate_sr_probe(payload)
                self.send_json({"ok": True, "result": result})
                return
            if path == "/text" or path == "/turn/text":
                text = str(payload.get("text", "")).strip()
                if not text:
                    self.send_json({"ok": False, "error": "text required"}, HTTPStatus.BAD_REQUEST)
                    return
                result = bridge.send_text(text)
                if bool(payload.get("speak", False)):
                    reply = reply_from_text_result(result, fallback=text)
                    result["tts"] = bridge.synthesize_speech(
                        reply,
                        voice=str(payload.get("tts_voice", "")).strip(),
                        style=str(payload.get("tts_style", bridge.session.default_tts_style) or bridge.session.default_tts_style),
                        singing=bool(payload.get("tts_singing", False)),
                    )
                    tts_store, tts_url, dualeye_play = self.cache_tts_and_push(result["tts"])
                    result["tts_cached"] = tts_store
                    result["tts_url"] = tts_url
                    result["dualeye_play"] = dualeye_play
                remember_turn({
                    "kind": "text",
                    "ok": bool(result.get("ok")),
                    "text": text,
                    "reply": reply_from_text_result(result, fallback=""),
                    "source": str(result.get("source", "")),
                    "intent_count": len(result.get("intents", [])),
                    "tts_ready": bool(result.get("tts_cached", {}).get("ready")),
                    "dualeye_play": result.get("dualeye_play", {}),
                })
                self.send_json(compact_audio_payload(result))
                return
            if path == "/audio" or path == "/turn/audio":
                handle_browser_audio_turn(self, bridge, payload, self.latest_tts_url())
                return
            if path in {"/api/tools/call", "/mcp/tools/call"}:
                tool_name = str(payload.get("name") or payload.get("tool") or "").strip()
                args = payload.get("arguments", payload.get("args", payload.get("input", {})))
                if not isinstance(args, dict):
                    args = {}
                if not tool_name:
                    self.send_json({"ok": False, "error": "tool name required"}, HTTPStatus.BAD_REQUEST)
                    return
                tool_meta = bridge.skills.public_item(tool_name)
                if tool_meta is None:
                    self.send_json({"ok": False, "error": f"unknown tool: {tool_name}", "tool": tool_name}, HTTPStatus.NOT_FOUND)
                    return
                if bool(tool_meta.get("confirm_required")) and not bool(payload.get("confirmed", False)):
                    self.send_json({
                        "ok": False,
                        "error": "confirmation required",
                        "tool": tool_name,
                        "risk": tool_meta.get("risk", "unknown"),
                        "confirm_required": True,
                    }, HTTPStatus.CONFLICT)
                    return
                result = bridge.execute_skill(tool_name, args)
                reply = str(result.get("reply") or result.get("speech") or "").strip()
                if bool(payload.get("speak", False)) and reply:
                    result["tts"] = bridge.synthesize_speech(
                        reply,
                        voice=str(payload.get("tts_voice", "")).strip(),
                        style=str(payload.get("tts_style", bridge.session.default_tts_style) or bridge.session.default_tts_style),
                        singing=bool(payload.get("tts_singing", False)),
                    )
                    tts_store, tts_url, dualeye_play = self.cache_tts_and_push(result["tts"])
                    result["tts_cached"] = tts_store
                    result["tts_url"] = tts_url
                    result["dualeye_play"] = dualeye_play
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
                self.send_json(compact_audio_payload({
                    "ok": bool(result.get("ok")),
                    "protocol": "atlas.tools.v0.desk_apps",
                    "tool": tool_name,
                    "result": result,
                }), HTTPStatus.OK if result.get("ok") else HTTPStatus.BAD_GATEWAY)
                return
            if path == "/skill":
                skill_name = str(payload.get("skill") or payload.get("name") or "").strip()
                args = payload.get("args", {})
                if not isinstance(args, dict):
                    args = {}
                if not skill_name:
                    self.send_json({"ok": False, "error": "skill required"}, HTTPStatus.BAD_REQUEST)
                    return
                result = bridge.execute_skill(skill_name, args)
                reply = str(result.get("reply") or result.get("speech") or "").strip()
                if bool(payload.get("speak", False)) and reply:
                    result["tts"] = bridge.synthesize_speech(
                        reply,
                        voice=str(payload.get("tts_voice", "")).strip(),
                        style=str(payload.get("tts_style", bridge.session.default_tts_style) or bridge.session.default_tts_style),
                        singing=bool(payload.get("tts_singing", False)),
                    )
                    tts_store, tts_url, dualeye_play = self.cache_tts_and_push(result["tts"])
                    result["tts_cached"] = tts_store
                    result["tts_url"] = tts_url
                    result["dualeye_play"] = dualeye_play
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
                self.send_json(compact_audio_payload(result), HTTPStatus.OK if result.get("ok") else HTTPStatus.BAD_GATEWAY)
                return
            if path == "/role/switch":
                role = str(payload.get("role", "")).strip()
                result = bridge.execute_skill("atlas.role.switch", {"role": role})
                remember_turn({
                    "kind": "role",
                    "ok": bool(result.get("ok")),
                    "role": role,
                    "reply": str(result.get("reply", "")),
                    "error": str(result.get("error", "")),
                })
                self.send_json(result, HTTPStatus.OK if result.get("ok") else HTTPStatus.BAD_REQUEST)
                return
            if path == "/asr":
                handle_asr(self, bridge, payload)
                return
            if path == "/speak":
                handle_speak(self, bridge, payload, self.latest_tts_url())
                return
            if path == "/intent":
                intent = payload.get("intent", payload)
                if isinstance(intent, str):
                    intent = json.loads(intent)
                if not isinstance(intent, dict):
                    self.send_json({"ok": False, "error": "intent object required"}, HTTPStatus.BAD_REQUEST)
                    return
                self.send_json({"ok": True, "result": bridge.send_intent(intent), "intent": intent})
                return
            self.send_json({"ok": False, "error": "not found"}, HTTPStatus.NOT_FOUND)

        def log_message(self, fmt: str, *args: object) -> None:
            ts = time.strftime("%H:%M:%S")
            sys.stderr.write(f"[{ts}] {self.address_string()} {fmt % args}\n")

    return Handler


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Atlas Brain server for DualEye")
    parser.add_argument("--dualeye-url", default=os.environ.get("ATLAS_DUALEYE_URL", DEFAULT_DUALEYE_URL))
    parser.add_argument("--pin", default=os.environ.get("ATLAS_PAIRING_PIN", ""))
    parser.add_argument("--host", default=os.environ.get("ATLAS_BRIDGE_HOST", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("ATLAS_BRIDGE_PORT", DEFAULT_PORT)))
    parser.add_argument("--speed", type=int, default=int(os.environ.get("ATLAS_BRIDGE_SPEED", DEFAULT_SPEED)))
    parser.add_argument("--duration-ms", type=int, default=int(os.environ.get("ATLAS_BRIDGE_DURATION_MS", DEFAULT_DURATION_MS)))
    parser.add_argument("--llm-base-url", default=os.environ.get("ATLAS_LLM_BASE_URL", DEFAULT_LLM_BASE_URL))
    parser.add_argument("--llm-api-key", default=os.environ.get("ATLAS_LLM_API_KEY", ""))
    parser.add_argument("--llm-model", default=os.environ.get("ATLAS_LLM_MODEL", DEFAULT_LLM_MODEL))
    parser.add_argument("--asr-model", default=os.environ.get("ATLAS_ASR_MODEL", DEFAULT_ASR_MODEL))
    parser.add_argument("--tts-model", default=os.environ.get("ATLAS_TTS_MODEL", DEFAULT_TTS_MODEL))
    parser.add_argument("--tts-voice", default=os.environ.get("ATLAS_TTS_VOICE", DEFAULT_TTS_VOICE))
    parser.add_argument("--dry-run", action="store_true", help="parse commands without posting to DualEye")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    bridge = Bridge(
        dualeye_url=args.dualeye_url,
        pin=args.pin,
        speed=clamp_int(args.speed, 1, 80),
        duration_ms=clamp_int(args.duration_ms, 100, 2000),
        dry_run=args.dry_run,
        llm_base_url=args.llm_base_url,
        llm_api_key=args.llm_api_key,
        llm_model=args.llm_model,
        asr_model=args.asr_model,
        tts_model=args.tts_model,
        tts_voice=args.tts_voice,
    )
    if bridge.pin:
        print(f"DualEye configured: {bridge.dualeye_url}, pairing={bridge.pin}")
    else:
        try:
            bridge.status()
            print(f"DualEye connected: {bridge.dualeye_url}, pairing={bridge.pin or 'unknown'}")
        except Exception as exc:
            print(f"DualEye status unavailable for now: {exc}")
    handler = make_handler(bridge)
    server = ThreadingHTTPServer((args.host, args.port), handler)
    print(f"Atlas Brain server: http://127.0.0.1:{args.port}")
    print(f"LAN URL for DualEye/phone on same Wi-Fi: http://{local_lan_ip()}:{args.port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
