#!/usr/bin/env python3
"""Check Atlas TOC web voice console readiness without using a microphone."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import subprocess
import urllib.request
from pathlib import Path
from typing import Any, Optional


DEFAULT_BRAIN_URL = "http://127.0.0.1:8787"
NO_PROXY_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def git_commit() -> str:
    try:
        return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
        return "unknown"


def http_text(url: str, timeout: float = 5.0) -> str:
    req = urllib.request.Request(url, headers={"Cache-Control": "no-store"})
    with NO_PROXY_OPENER.open(req, timeout=timeout) as resp:
        return resp.read().decode("utf-8", errors="replace")


def http_json(url: str, timeout: float = 5.0, *, method: str = "GET", payload: Optional[dict[str, Any]] = None) -> dict[str, Any]:
    data = None
    headers = {"Cache-Control": "no-store"}
    if method == "POST":
        data = json.dumps(payload or {}, ensure_ascii=False).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    with NO_PROXY_OPENER.open(req, timeout=timeout) as resp:
        raw = resp.read().decode("utf-8", errors="replace")
    return json.loads(raw)


def check_item(name: str, ok: bool, detail: str = "", data: Optional[dict[str, Any]] = None, required: bool = True) -> dict[str, Any]:
    return {
        "name": name,
        "ok": bool(ok),
        "status": "PASS" if ok else ("FAIL" if required else "WARN"),
        "required": bool(required),
        "detail": detail,
        "data": data or {},
    }


def provider_missing(provider_payload: dict[str, Any]) -> list[str]:
    diagnostics = provider_payload.get("provider_diagnostics")
    missing: list[str] = []
    if not isinstance(diagnostics, dict):
        return ["provider_diagnostics missing"]
    for key in ("llm", "asr", "tts"):
        item = diagnostics.get(key, {})
        if isinstance(item, dict) and item.get("missing_env"):
            missing.extend(f"{key.upper()}:{env}" for env in item.get("missing_env", []))
    return missing


def render_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# Atlas Web Voice Console Check",
        "",
        f"- Generated at: `{report.get('generated_at')}`",
        f"- Service git commit: `{report.get('service_git_commit')}`",
        f"- Brain URL: `{report.get('brain_url')}`",
        f"- Summary: `PASS={report.get('summary', {}).get('pass', 0)} WARN={report.get('summary', {}).get('warn', 0)} FAIL={report.get('summary', {}).get('fail', 0)}`",
        "",
        "| Check | Status | Detail |",
        "| --- | --- | --- |",
    ]
    for item in report.get("checks", []):
        detail = str(item.get("detail", "")).replace("|", "\\|").replace("\n", "<br>")
        lines.append(f"| {item.get('name')} | {item.get('status')} | {detail} |")
    lines.extend(["", "## Next Steps", ""])
    for step in report.get("next_steps", []):
        lines.append(f"- {step}")
    lines.append("")
    return "\n".join(lines)


def write_text(path: str, content: str) -> None:
    output = Path(path)
    if output.parent and str(output.parent) != ".":
        output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(content, encoding="utf-8")


def summarize(checks: list[dict[str, Any]]) -> dict[str, int]:
    summary = {"pass": 0, "warn": 0, "fail": 0}
    for item in checks:
        summary[str(item.get("status", "FAIL")).lower()] += 1
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description="Check Atlas /app TOC continuous voice console")
    parser.add_argument("--brain-url", default=DEFAULT_BRAIN_URL)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--output-md", default="")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    brain = args.brain_url.rstrip("/")
    checks: list[dict[str, Any]] = []
    app_html = http_text(f"{brain}/app", args.timeout)
    runtime = http_json(f"{brain}/api/runtime", args.timeout)
    score = http_json(f"{brain}/api/runtime/score", args.timeout)
    providers = http_json(f"{brain}/api/providers", args.timeout)
    tools = http_json(f"{brain}/api/tools/list", args.timeout)
    tool_call = http_json(
        f"{brain}/api/tools/call",
        args.timeout,
        method="POST",
        payload={"name": "atlas.pomodoro.start", "arguments": {"task_name": "TOC 语音验收", "focus_minutes": 25, "break_minutes": 5}},
    )
    diagnostics = http_json(f"{brain}/api/runtime/diagnostics", args.timeout)

    required_snippets = {
        "continuous_button": "连续对话",
        "voice_loop_state": "voiceLoopState",
        "continuous_flag": "continuousVoice",
        "vad_auto_segment": "lastVoiceAt>900",
        "auto_restart": "scheduleContinuousRestart",
        "same_origin_tts": "audioUrlForBrowser",
        "tts_path_guard": "u.pathname.startsWith('/tts/')",
        "play_until_end": "waitUntilEnd",
        "mic_error_stage": "stage:'microphone'",
    }
    for name, snippet in required_snippets.items():
        checks.append(check_item(
            f"/app {name}",
            snippet in app_html,
            f"snippet={snippet}",
        ))

    provider_flags = {
        "llm_enabled": bool(runtime.get("runtime") is not None or providers.get("ok")),
        "provider_diagnostics": isinstance(providers.get("provider_diagnostics"), dict),
        "missing": provider_missing(providers),
    }
    checks.append(check_item(
        "Provider flags",
        bool(providers.get("ok")) and isinstance(providers.get("provider_diagnostics"), dict),
        "missing=" + (", ".join(provider_flags["missing"]) or "none"),
        provider_flags,
    ))
    runtime_score = score.get("score", {}) if isinstance(score.get("score"), dict) else {}
    ready_score = score.get("ready_score", {}) if isinstance(score.get("ready_score"), dict) else {}
    checks.append(check_item(
        "Runtime score",
        score.get("protocol") == "atlas.runtime.score.v0" and int(ready_score.get("score", 0) or 0) >= 80,
        f"current={runtime_score.get('score')} ready={ready_score.get('score')}",
        {"score": score},
    ))
    tool_names = {str(tool.get("name", "")) for tool in tools.get("tools", []) if isinstance(tool, dict)}
    checks.append(check_item(
        "Tools schema",
        tools.get("protocol") == "atlas.tools.v0.desk_apps" and "atlas.pomodoro.start" in tool_names and "atlas.ui.set_chat_mode" in tool_names,
        f"tool_count={tools.get('tool_count', len(tool_names))}",
    ))
    result = tool_call.get("result", {}) if isinstance(tool_call.get("result"), dict) else {}
    checks.append(check_item(
        "Tools call smoke",
        bool(tool_call.get("ok")) and bool(result.get("ok")),
        str(result.get("reply", result.get("error", ""))),
        {"tool_call": tool_call},
    ))
    diag = diagnostics.get("diagnostics", {}) if isinstance(diagnostics.get("diagnostics"), dict) else {}
    checks.append(check_item(
        "Runtime diagnostics audit",
        isinstance(diag.get("recent_tool_calls"), list) and bool(diag.get("latest_tool", {}).get("name")),
        f"stage={diag.get('stage')} latest_tool={diag.get('latest_tool', {}).get('name', '')}",
    ))

    summary = summarize(checks)
    next_steps = [
        "打开 /app，点“连续对话”，说一句短话，观察 ASR、回复、播报、恢复监听。",
        "若 TTS 不播，先看浏览器 Console、/api/runtime/diagnostics 和同源 /tts/latest.wav。",
        "若页面按钮未同步，点 /acceptance 的实机状态并同时跑 collect_atlas_realtime_trace.py。",
    ]
    report = {
        "ok": summary["fail"] == 0,
        "protocol": "atlas.web_voice_console.check.v0",
        "generated_at": utc_now(),
        "service_git_commit": git_commit(),
        "brain_url": brain,
        "summary": summary,
        "checks": checks,
        "provider_missing": provider_missing(providers),
        "runtime_score": {
            "current": runtime_score.get("score"),
            "ready": ready_score.get("score"),
        },
        "next_steps": next_steps,
    }
    if args.output_md:
        write_text(args.output_md, render_markdown(report))
        report["output_md"] = args.output_md

    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        print("Atlas Web Voice Console Check")
        print(f"Brain: {brain}")
        print(f"Commit: {report['service_git_commit']}")
        print(f"Summary: PASS={summary['pass']} WARN={summary['warn']} FAIL={summary['fail']}")
        for item in checks:
            print(f"[{item['status']}] {item['name']} - {item['detail']}")
        if args.output_md:
            print(f"Markdown: {args.output_md}")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
