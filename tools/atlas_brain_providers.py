#!/usr/bin/env python3
"""Provider adapters for Atlas Brain.

This module is intentionally small and dependency-light: it owns cloud LLM,
ASR, TTS calls plus the local macOS TTS fallback. Session state, tools, HTTP
routes and device control stay outside this provider layer.
"""

from __future__ import annotations

import base64
import json
import os
import subprocess
import tempfile
import urllib.request
from typing import Any


TTS_STYLE_PROMPTS = {
    "default": "温暖、清晰、像一只性格友好的桌面机器人。",
    "jiazi": "用可爱、撒娇、轻微夹子音的语气说话，声音明亮偏甜，尾音轻轻上扬，但不要夸张到听不清。",
    "sweet": "用甜美、清亮、亲近的语气说话，语速中等，情绪温柔愉快。",
    "playful": "用俏皮、活泼、有一点调皮的语气说话，节奏轻快，重点词微微上扬。",
    "excited": "用兴奋、开心、充满能量的语气说话，语速稍快，像发现了很棒的新东西。",
    "singing": "用自然轻快的歌唱方式演唱，旋律感明显，声音甜美活泼，吐字尽量清楚。",
}

TTS_STYLE_TAGS = {
    "jiazi": "夹子音",
    "sweet": "甜美",
    "playful": "俏皮",
    "excited": "兴奋",
    "singing": "唱歌",
}


def openai_chat_completion(base_url: str,
                           api_key: str,
                           model: str,
                           messages: list[dict[str, str]],
                           timeout: float = 30.0,
                           max_tokens: int = 360) -> dict[str, Any]:
    url = base_url.rstrip("/") + "/chat/completions"
    body = json.dumps({
        "model": model,
        "messages": messages,
        "temperature": 0.1,
        "max_completion_tokens": max_tokens,
        "enable_thinking": False,
        "thinking": {"type": "disabled"},
    }, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(url, data=body, method="POST", headers={
        "Content-Type": "application/json",
        "Authorization": f"Bearer {api_key}",
        "api-key": api_key,
    })
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        raw = resp.read().decode("utf-8", errors="replace")
    return json.loads(raw)


def openai_asr(base_url: str,
               api_key: str,
               model: str,
               audio_data_url: str,
               language: str = "auto",
               timeout: float = 45.0) -> dict[str, Any]:
    url = base_url.rstrip("/") + "/chat/completions"
    body = json.dumps({
        "model": model,
        "messages": [{
            "role": "user",
            "content": [{
                "type": "input_audio",
                "input_audio": {"data": audio_data_url},
            }],
        }],
        "asr_options": {"language": language or "auto"},
    }, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(url, data=body, method="POST", headers={
        "Content-Type": "application/json",
        "Authorization": f"Bearer {api_key}",
        "api-key": api_key,
    })
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8", errors="replace"))


def openai_tts(base_url: str,
               api_key: str,
               model: str,
               text: str,
               voice: str,
               audio_format: str = "wav",
               style_prompt: str = TTS_STYLE_PROMPTS["default"],
               timeout: float = 60.0) -> dict[str, Any]:
    url = base_url.rstrip("/") + "/chat/completions"
    body = json.dumps({
        "model": model,
        "messages": [
            {"role": "user", "content": style_prompt},
            {"role": "assistant", "content": text},
        ],
        "audio": {
            "format": audio_format,
            "voice": voice,
        },
    }, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(url, data=body, method="POST", headers={
        "Content-Type": "application/json",
        "Authorization": f"Bearer {api_key}",
        "api-key": api_key,
    })
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8", errors="replace"))


def macos_say_tts(text: str, voice: str = "", default_voice: str = "mimo_default", timeout: float = 45.0) -> dict[str, Any]:
    text = text.strip()
    if not text:
        return {"ok": False, "error": "text required"}
    say_voice = (voice.strip() if voice and voice.strip() != default_voice else "") or os.getenv("ATLAS_MACOS_SAY_VOICE", "Tingting")
    try:
        with tempfile.TemporaryDirectory(prefix="atlas_tts_") as tmpdir:
            aiff_path = os.path.join(tmpdir, "speech.aiff")
            wav_path = os.path.join(tmpdir, "speech.wav")
            subprocess.run(
                ["/usr/bin/say", "-v", say_voice, "-o", aiff_path, text[:500]],
                check=True,
                timeout=timeout,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
            )
            subprocess.run(
                ["/usr/bin/afconvert", "-f", "WAVE", "-d", "LEI16@16000", aiff_path, wav_path],
                check=True,
                timeout=timeout,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
            )
            with open(wav_path, "rb") as fh:
                wav_bytes = fh.read()
    except Exception as exc:
        return {"ok": False, "provider": "macos_say", "error": str(exc)}
    return {
        "ok": True,
        "provider": "macos_say",
        "text": text,
        "model": "macOS say",
        "voice": say_voice,
        "format": "wav",
        "audio_url": "data:audio/wav;base64," + base64.b64encode(wav_bytes).decode("ascii"),
    }


def chat_choice_text(response: dict[str, Any]) -> str:
    choice = response.get("choices", [{}])[0]
    message = choice.get("message", {})
    candidates = [
        message.get("content"),
        message.get("reasoning_content"),
        choice.get("text"),
        response.get("output_text"),
    ]
    for value in candidates:
        if isinstance(value, str) and value.strip():
            return value
    if isinstance(message.get("content"), list):
        parts: list[str] = []
        for item in message["content"]:
            if isinstance(item, dict):
                text = item.get("text") or item.get("content")
                if isinstance(text, str):
                    parts.append(text)
            elif isinstance(item, str):
                parts.append(item)
        if parts:
            return "\n".join(parts)
    return ""


def chat_choice_audio(response: dict[str, Any]) -> str:
    choice = response.get("choices", [{}])[0]
    message = choice.get("message", {})
    audio = message.get("audio")
    if isinstance(audio, dict):
        data = audio.get("data")
        if isinstance(data, str) and data:
            return data
    return ""


def tts_style_prompt(style: str) -> str:
    return TTS_STYLE_PROMPTS.get((style or "default").strip(), TTS_STYLE_PROMPTS["default"])


def prepare_tts_text(text: str, style: str = "", singing: bool = False) -> str:
    text = text.strip()
    style_key = (style or "default").strip()
    if singing or style_key == "singing":
        if not text.startswith("(唱歌)"):
            text = "(唱歌)" + text
        return text
    tag = TTS_STYLE_TAGS.get(style_key, "")
    if tag and not text.startswith("("):
        return f"({tag}){text}"
    return text
