#!/usr/bin/env python3
"""Validate Atlas Brain cloud providers through the local Brain HTTP API."""

from __future__ import annotations

import argparse
import base64
import json
import sys
import urllib.error
import urllib.request
from typing import Any

NO_PROXY_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def request_json(url: str,
                 payload: dict[str, Any] | None = None,
                 timeout: float = 60.0) -> tuple[int, dict[str, Any]]:
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method="POST" if payload is not None else "GET")
    try:
        with NO_PROXY_OPENER.open(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
            return resp.status, json.loads(raw)
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8", errors="replace")
        try:
            parsed = json.loads(raw)
        except json.JSONDecodeError:
            parsed = {"ok": False, "error": raw or exc.reason}
        return exc.code, parsed


def request_bytes(url: str, timeout: float = 30.0) -> tuple[int, bytes]:
    req = urllib.request.Request(url, headers={"Accept": "audio/wav"})
    with NO_PROXY_OPENER.open(req, timeout=timeout) as resp:
        return resp.status, resp.read()


def compact_error(payload: dict[str, Any]) -> str:
    if payload.get("error"):
        return str(payload["error"])
    if isinstance(payload.get("asr"), dict) and payload["asr"].get("error"):
        return str(payload["asr"]["error"])
    return json.dumps(payload, ensure_ascii=False)[:300]


def main() -> int:
    parser = argparse.ArgumentParser(description="Check Atlas Brain LLM/TTS/ASR provider readiness.")
    parser.add_argument("--brain-url", default="http://127.0.0.1:8787", help="Atlas Brain base URL")
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON only")
    args = parser.parse_args()

    base = args.brain_url.rstrip("/")
    checks: list[dict[str, Any]] = []

    def add(name: str, ok: bool, **data: Any) -> None:
        checks.append({"name": name, "ok": ok, **data})

    try:
        status, health = request_json(f"{base}/health", timeout=args.timeout)
        providers = health.get("providers") if isinstance(health.get("providers"), dict) else {}
        add("health", status == 200 and bool(health.get("ok")), providers=providers)

        llm_ready = bool(health.get("llm_enabled"))
        tts_ready = bool(health.get("tts_enabled"))
        asr_ready = bool(health.get("asr_enabled"))
        add("provider_flags", llm_ready and tts_ready and asr_ready,
            llm=llm_ready, tts=tts_ready, asr=asr_ready)

        status, turn = request_json(
            f"{base}/turn/text",
            {"text": "请用一句中文回答：1+1等于多少？", "speak": False},
            timeout=args.timeout,
        )
        reply = str((turn.get("llm") or {}).get("reply", "") or turn.get("reply", ""))
        add("llm_chat", status == 200 and bool(turn.get("ok")) and bool(reply),
            source=turn.get("source", ""), reply=reply[:120], error=compact_error(turn) if not turn.get("ok") else "")

        status, speak = request_json(
            f"{base}/speak",
            {"text": "你好，我是阿特拉斯。", "tts_style": "playful"},
            timeout=args.timeout,
        )
        tts_cached = speak.get("tts_cached") if isinstance(speak.get("tts_cached"), dict) else {}
        tts_url = str(speak.get("tts_url") or f"{base}/tts/latest.wav")
        add("tts_generate", status == 200 and bool(speak.get("ok")) and bool(tts_cached.get("ready")),
            provider=speak.get("provider", ""), bytes=tts_cached.get("bytes", 0), url=tts_url,
            error=compact_error(speak) if not speak.get("ok") else "")

        wav_url = tts_url
        try:
            wav_status, wav = request_bytes(wav_url, timeout=args.timeout)
        except Exception:
            wav_url = f"{base}/tts/latest.wav"
            wav_status, wav = request_bytes(wav_url, timeout=args.timeout)
        add("tts_fetch_wav", wav_status == 200 and len(wav) > 44, bytes=len(wav), url=wav_url)

        status, asr = request_json(
            f"{base}/asr",
            {"audio_data": "data:audio/wav;base64," + base64.b64encode(wav).decode("ascii"), "language": "zh"},
            timeout=args.timeout,
        )
        asr_payload = asr.get("asr") if isinstance(asr.get("asr"), dict) else {}
        add("asr_transcribe", status == 200 and bool(asr_payload.get("ok")) and bool(asr_payload.get("text")),
            text=str(asr_payload.get("text", ""))[:120],
            error=compact_error(asr) if not asr_payload.get("ok") else "")

        status, score = request_json(f"{base}/api/runtime/score", timeout=args.timeout)
        caps = score.get("capabilities") if isinstance(score.get("capabilities"), dict) else {}
        add("runtime_provider_score", status == 200 and bool(caps.get("provider_config")),
            score=(score.get("score") or {}).get("score"))
    except Exception as exc:
        add("exception", False, error=str(exc))

    ok = all(item["ok"] for item in checks)
    result = {"ok": ok, "brain_url": base, "checks": checks}

    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print(f"Atlas Provider Check: {'PASS' if ok else 'FAIL'}")
        for item in checks:
            marker = "PASS" if item["ok"] else "FAIL"
            details = {k: v for k, v in item.items() if k not in {"name", "ok"} and v not in ("", None)}
            print(f"- {marker} {item['name']}: {json.dumps(details, ensure_ascii=False)}")

    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())
