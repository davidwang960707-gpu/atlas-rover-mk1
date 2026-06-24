#!/usr/bin/env python3
"""Atlas 三条体验线验收台。

用于晚上 DualEye 真机接入时，一次性查看：
- 语音连续对话
- 双目宠物/应用
- Brain 工具与诊断

烧录前或设备不在手边时可加 `--skip-dualeye`，真机相关项会标记为
needs_device/skip，不会伪装为 pass。
"""

from __future__ import annotations

import argparse
import json
import time
import urllib.error
import urllib.request
from typing import Any, Optional


DEFAULT_BRAIN_URL = "http://127.0.0.1:8787"
DEFAULT_DUALEYE_URL = "http://192.168.4.1"
EXPECTED_TOOL_SCHEMA = "atlas.tools.v0.desk_apps"
EXPECTED_CHAT_MODES = {"pet_head", "text", "eyes_only"}
EXPECTED_APP_TOOLS = {
    "atlas.clock.show",
    "atlas.clock.status",
    "atlas.calendar.show",
    "atlas.calendar.today",
    "atlas.pomodoro.show",
    "atlas.pomodoro.start",
    "atlas.pomodoro.status",
    "atlas.ui.set_chat_mode",
    "atlas.pet.set_state",
    "atlas.pet.play_animation",
}
NO_PROXY_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def http_json(url: str, timeout: float, *, method: str = "GET", payload: Optional[dict[str, Any]] = None) -> dict[str, Any]:
    data = None
    headers = {"Cache-Control": "no-store"}
    if method == "POST":
        data = json.dumps(payload or {}, ensure_ascii=False).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    with NO_PROXY_OPENER.open(req, timeout=timeout) as resp:
        raw = resp.read().decode("utf-8", errors="replace").strip()
    return json.loads(raw)


def request_check(name: str,
                  url: str,
                  timeout: float,
                  *,
                  method: str = "GET",
                  payload: Optional[dict[str, Any]] = None,
                  required: bool = True,
                  skipped: bool = False,
                  skip_reason: str = "") -> dict[str, Any]:
    if skipped:
        return {
            "name": name,
            "status": "SKIP",
            "ok": False,
            "required": required,
            "url": url,
            "elapsed_ms": 0,
            "error": skip_reason,
            "payload": {},
        }
    started = time.time()
    try:
        parsed = http_json(url, timeout, method=method, payload=payload)
        return {
            "name": name,
            "status": "PASS" if parsed.get("ok", True) is not False else ("FAIL" if required else "WARN"),
            "ok": parsed.get("ok", True) is not False,
            "required": required,
            "url": url,
            "elapsed_ms": int((time.time() - started) * 1000),
            "error": "" if parsed.get("ok", True) is not False else str(parsed.get("error", "ok=false")),
            "payload": parsed,
        }
    except Exception as exc:
        return {
            "name": name,
            "status": "FAIL" if required else "WARN",
            "ok": False,
            "required": required,
            "url": url,
            "elapsed_ms": int((time.time() - started) * 1000),
            "error": str(exc),
            "payload": {},
        }


def names_from_tools(payload: dict[str, Any]) -> set[str]:
    tools = payload.get("tools", [])
    if not isinstance(tools, list):
        return set()
    return {str(tool.get("name", "")) for tool in tools if isinstance(tool, dict)}


def find_tool(payload: dict[str, Any], name: str) -> dict[str, Any]:
    tools = payload.get("tools", [])
    if not isinstance(tools, list):
        return {}
    for tool in tools:
        if isinstance(tool, dict) and tool.get("name") == name:
            return tool
    return {}


def enum_values(schema: Any) -> set[str]:
    if not isinstance(schema, dict):
        return set()
    values: set[str] = set()
    if isinstance(schema.get("enum"), list):
        values |= {str(item) for item in schema["enum"]}
    for key in ("properties", "$defs", "definitions"):
        nested = schema.get(key)
        if isinstance(nested, dict):
            for item in nested.values():
                values |= enum_values(item)
    for key in ("items", "oneOf", "anyOf", "allOf"):
        nested = schema.get(key)
        if isinstance(nested, dict):
            values |= enum_values(nested)
        elif isinstance(nested, list):
            for item in nested:
                values |= enum_values(item)
    return values


def check_tool_contract(payload: dict[str, Any]) -> tuple[list[str], list[str]]:
    missing: list[str] = []
    warnings: list[str] = []
    protocol = str(payload.get("protocol", ""))
    if protocol != EXPECTED_TOOL_SCHEMA:
        missing.append(f"tool schema mismatch: {protocol or 'missing'}")
    names = names_from_tools(payload)
    missing_tools = sorted(EXPECTED_APP_TOOLS - names)
    if missing_tools:
        missing.append(f"missing tools: {missing_tools}")
    chat_tool = find_tool(payload, "atlas.ui.set_chat_mode")
    chat_enums = enum_values(chat_tool.get("input_schema") or chat_tool.get("inputSchema") or {})
    if chat_tool and chat_enums and not EXPECTED_CHAT_MODES.issubset(chat_enums):
        warnings.append(f"chat modes incomplete: {sorted(chat_enums)}")
    elif not chat_tool:
        missing.append("atlas.ui.set_chat_mode missing")
    return missing, warnings


def compact_check(item: dict[str, Any]) -> dict[str, Any]:
    payload = item.get("payload") if isinstance(item.get("payload"), dict) else {}
    name = str(item.get("name", ""))
    compact: dict[str, Any] = {}
    if name == "Brain acceptance report":
        compact = {
            "summary": payload.get("summary", {}),
            "experience_status": payload.get("experience_status", {}),
        }
    elif name in {"Brain runtime diagnostics", "Brain runtime diagnostics after simulate"}:
        diagnostics = payload.get("diagnostics", {}) if isinstance(payload.get("diagnostics"), dict) else {}
        compact = {
            "stage": diagnostics.get("stage"),
            "ok": diagnostics.get("ok"),
            "latest_tool": diagnostics.get("latest_tool", {}).get("name") if isinstance(diagnostics.get("latest_tool"), dict) else "",
            "latest_turn": diagnostics.get("latest_turn", {}).get("kind") if isinstance(diagnostics.get("latest_turn"), dict) else "",
            "latest_failure": diagnostics.get("latest_failure", {}),
            "next_step": diagnostics.get("next_step", ""),
        }
    elif name in {"Brain tools", "DualEye tools"}:
        compact = {
            "protocol": payload.get("protocol"),
            "tool_count": payload.get("tool_count", len(payload.get("tools", []))) if isinstance(payload.get("tools", []), list) else payload.get("tool_count"),
            "tools_present": sorted(EXPECTED_APP_TOOLS & names_from_tools(payload)),
        }
    elif name == "DualEye status":
        compact = {
            "scene": payload.get("scene", {}),
            "ui": payload.get("ui", {}),
            "audio_stream": payload.get("audio_stream", {}),
            "voice_wake": payload.get("voice_wake", {}),
            "apps": payload.get("apps", {}),
        }
    elif name == "DualEye turn diagnostics":
        compact = {
            "turn": payload.get("turn", {}),
            "diagnostics": payload.get("diagnostics", payload),
        }
    elif name == "DualEye selftest":
        compact = {
            "ready_to_flash": payload.get("ready_to_flash"),
            "summary": payload.get("summary", {}),
            "fingerprint": payload.get("fingerprint", {}),
        }
    elif name == "Brain audio stream status":
        compact = {
            "stream": payload.get("stream", payload.get("last_stream", {})),
            "dualeye_stream": payload.get("dualeye_stream", {}),
        }
    elif name == "Brain providers":
        compact = {
            "summary": payload.get("summary", {}),
            "providers": payload.get("providers", {}),
        }
    elif name == "Brain simulate turn":
        compact = {
            "protocol": payload.get("protocol"),
            "ok": payload.get("ok"),
            "tool": payload.get("tool"),
            "reply": payload.get("reply"),
            "diagnostics": payload.get("diagnostics", {}),
        }
    else:
        compact = payload
    return {
        "name": item["name"],
        "status": item["status"],
        "elapsed_ms": item["elapsed_ms"],
        "url": item["url"],
        "error": item.get("error", ""),
        "payload": compact,
    }


def provider_configured(providers_payload: dict[str, Any]) -> bool:
    providers = providers_payload.get("providers", {})
    if isinstance(providers, list):
        by_kind = {str(item.get("kind", "")): item for item in providers if isinstance(item, dict)}
        return all(bool(by_kind.get(name, {}).get("configured")) for name in ("llm", "asr", "tts"))
    if not isinstance(providers, dict):
        return False
    return all(bool(providers.get(name, {}).get("enabled")) for name in ("llm", "asr", "tts"))


def line_status(state: str) -> str:
    if state == "ready":
        return "PASS"
    if state == "fail":
        return "FAIL"
    if state == "needs_device":
        return "NEEDS_DEVICE"
    if state == "skip":
        return "SKIP"
    return "WARN"


def build_lines(checks: dict[str, dict[str, Any]], *, skip_dualeye: bool) -> list[dict[str, Any]]:
    acceptance = checks["Brain acceptance report"].get("payload", {})
    experience = acceptance.get("experience_status", {}) if isinstance(acceptance.get("experience_status"), dict) else {}
    summary = experience.get("summary", {}) if isinstance(experience.get("summary"), dict) else {}
    lines = experience.get("lines", {}) if isinstance(experience.get("lines"), dict) else {}
    providers = checks["Brain providers"].get("payload", {})
    tools_payload = checks["Brain tools"].get("payload", {})
    diag_payload = checks["Brain runtime diagnostics after simulate"].get("payload", {})
    diagnostics = diag_payload.get("diagnostics", {}) if isinstance(diag_payload.get("diagnostics"), dict) else {}
    recent_tools = diagnostics.get("recent_tool_calls") if isinstance(diagnostics.get("recent_tool_calls"), list) else []
    recent_turns = diagnostics.get("recent_turns") if isinstance(diagnostics.get("recent_turns"), list) else []
    timeline = diagnostics.get("timeline") if isinstance(diagnostics.get("timeline"), list) else []
    missing_tools, tool_warnings = check_tool_contract(tools_payload)
    simulate_ok = bool(checks["Brain simulate turn"].get("ok"))
    brain_diag_ok = bool(diagnostics.get("protocol") == "atlas.runtime.diagnostics.v0")
    dualeye_status_ok = bool(checks["DualEye status"].get("ok"))
    dualeye_turn_ok = bool(checks["DualEye turn diagnostics"].get("ok"))
    dualeye_tools_ok = bool(checks["DualEye tools"].get("ok"))
    dualeye_selftest_ok = bool(checks["DualEye selftest"].get("ok"))

    voice_reasons: list[str] = []
    voice_next: list[str] = []
    if not provider_configured(providers):
        voice_reasons.append("LLM/ASR/TTS Provider 未全部配置")
        voice_next.append("配置 ATLAS_LLM_*、ATLAS_ASR_MODEL、ATLAS_TTS_* 后重启 Mac Brain")
    if not simulate_ok:
        voice_reasons.append(f"simulate-turn 失败：{checks['Brain simulate turn'].get('error', '')}")
    if not recent_turns:
        voice_reasons.append("runtime diagnostics 没有 recent_turns")
    if not timeline:
        voice_reasons.append("runtime diagnostics 没有 timeline")
    if skip_dualeye:
        voice_reasons.append("未接真机，OPUS 连续语音与播放链路待验证")
        voice_next.append("接入 DualEye 后去掉 --skip-dualeye，检查 /api/diagnostics/turn 和 OPUS stream")
        voice_state = "needs_device" if provider_configured(providers) else "needs_config"
    elif not dualeye_status_ok:
        voice_state = "fail"
        voice_reasons.append(f"DualEye /api/status 不可用：{checks['DualEye status'].get('error', '')}")
    elif not dualeye_turn_ok:
        voice_state = "fail"
        voice_reasons.append(f"DualEye /api/diagnostics/turn 不可用：{checks['DualEye turn diagnostics'].get('error', '')}")
    elif voice_reasons:
        voice_state = "needs_config" if not provider_configured(providers) else "warn"
    else:
        voice_state = "ready"

    pet_reasons: list[str] = []
    pet_next: list[str] = []
    if missing_tools:
        pet_reasons.extend(missing_tools)
    if tool_warnings:
        pet_reasons.extend(tool_warnings)
    if skip_dualeye:
        pet_reasons.append("未接真机，宠物/应用显示与触控待验证")
        pet_next.append("接入 DualEye 后检查 /api/status.ui.chat_mode、/api/tools/list、/api/selftest")
        pet_state = "needs_device"
    elif not dualeye_status_ok:
        pet_state = "fail"
        pet_reasons.append(f"DualEye /api/status 不可用：{checks['DualEye status'].get('error', '')}")
    elif not dualeye_tools_ok:
        pet_state = "fail"
        pet_reasons.append(f"DualEye /api/tools/list 不可用：{checks['DualEye tools'].get('error', '')}")
    elif not dualeye_selftest_ok:
        pet_state = "fail"
        pet_reasons.append(f"DualEye /api/selftest 不通过：{checks['DualEye selftest'].get('error', '')}")
    elif pet_reasons:
        pet_state = "warn"
    else:
        pet_state = "ready"

    tools_reasons: list[str] = []
    tools_next: list[str] = []
    if missing_tools:
        tools_reasons.extend(missing_tools)
    if not brain_diag_ok:
        tools_reasons.append("runtime diagnostics protocol 缺失")
    if not recent_tools:
        tools_reasons.append("simulate-turn 后没有 recent_tool_calls")
    if not timeline:
        tools_reasons.append("simulate-turn 后没有 timeline")
    if not simulate_ok:
        tools_reasons.append("simulate-turn 未通过")
    if tools_reasons:
        tools_state = "fail"
        tools_next.append("重启 Mac Brain 后重跑；若仍失败，检查 /api/runtime/diagnostics 原始 JSON")
    else:
        tools_state = "ready"

    def make_line(key: str, title: str, state: str, reasons: list[str], next_steps: list[str]) -> dict[str, Any]:
        base = lines.get(key, {}) if isinstance(lines.get(key), dict) else {}
        score = int(summary.get(key, base.get("score", 0)) or 0)
        if state == "fail":
            score = min(score, 40)
        elif state == "needs_device":
            score = min(score, 70)
        elif state == "needs_config":
            score = min(score, 60)
        elif state == "ready":
            score = max(score, 85)
        return {
            "key": key,
            "name": title,
            "state": state,
            "status": line_status(state),
            "score": score,
            "reasons": reasons or [str(base.get("detail", ""))],
            "next_steps": next_steps or ([str(base.get("next_step", ""))] if base.get("next_step") else []),
            "evidence": {
                "latest_turn": diagnostics.get("latest_turn", {}),
                "latest_tool": diagnostics.get("latest_tool", {}),
                "latest_failure": diagnostics.get("latest_failure", {}),
            } if key != "dualeye_pet_apps" else {
                "tool_count": tools_payload.get("tool_count", len(tools_payload.get("tools", []))),
                "dualeye_online": dualeye_status_ok,
            },
        }

    return [
        make_line("voice_continuous_conversation", "语音连续对话", voice_state, voice_reasons, voice_next),
        make_line("dualeye_pet_apps", "双目宠物/应用", pet_state, pet_reasons, pet_next),
        make_line("brain_tools_diagnostics", "Brain 工具与诊断", tools_state, tools_reasons, tools_next),
    ]


def summarize_lines(lines: list[dict[str, Any]]) -> dict[str, int]:
    summary = {"pass": 0, "warn": 0, "needs_device": 0, "skip": 0, "fail": 0}
    for line in lines:
        status = str(line.get("status", "")).lower()
        if status == "pass":
            summary["pass"] += 1
        elif status == "needs_device":
            summary["needs_device"] += 1
        elif status == "skip":
            summary["skip"] += 1
        elif status == "fail":
            summary["fail"] += 1
        else:
            summary["warn"] += 1
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description="Atlas 三条体验线验收台")
    parser.add_argument("--brain-url", default=DEFAULT_BRAIN_URL)
    parser.add_argument("--dualeye-url", default=DEFAULT_DUALEYE_URL)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--skip-dualeye", action="store_true", help="不检查真机端点，真机项标记为 needs_device/skip")
    parser.add_argument("--json", action="store_true", help="只输出 JSON")
    args = parser.parse_args()

    brain = args.brain_url.rstrip("/")
    dualeye = args.dualeye_url.rstrip("/")
    timeout = args.timeout
    checks_list = [
        request_check("Brain health", f"{brain}/health", timeout),
        request_check("Brain providers", f"{brain}/api/providers", timeout),
        request_check("Brain acceptance report", f"{brain}/api/acceptance/report{'?skip_device=1' if args.skip_dualeye else ''}", timeout),
        request_check("Brain tools", f"{brain}/api/tools/list", timeout),
        request_check("Brain audio stream status", f"{brain}/api/audio/stream/status", timeout, required=False),
        request_check("Brain runtime diagnostics", f"{brain}/api/runtime/diagnostics", timeout),
        request_check(
            "Brain simulate turn",
            f"{brain}/api/diagnostics/simulate-turn",
            max(timeout, 10.0),
            method="POST",
            payload={"text": "打开番茄页面并鼓励我一下", "speak": False},
        ),
        request_check("Brain runtime diagnostics after simulate", f"{brain}/api/runtime/diagnostics", timeout),
        request_check("DualEye status", f"{dualeye}/api/status", timeout, skipped=args.skip_dualeye, skip_reason="--skip-dualeye"),
        request_check("DualEye turn diagnostics", f"{dualeye}/api/diagnostics/turn", timeout, skipped=args.skip_dualeye, skip_reason="--skip-dualeye"),
        request_check("DualEye tools", f"{dualeye}/api/tools/list", timeout, skipped=args.skip_dualeye, skip_reason="--skip-dualeye"),
        request_check("DualEye selftest", f"{dualeye}/api/selftest", timeout, skipped=args.skip_dualeye, skip_reason="--skip-dualeye"),
    ]
    checks = {str(item["name"]): item for item in checks_list}
    lines = build_lines(checks, skip_dualeye=args.skip_dualeye)
    line_summary = summarize_lines(lines)
    required_failures = [
        item for item in checks_list
        if item.get("required", True) and item.get("status") == "FAIL" and not str(item.get("name", "")).startswith("DualEye")
    ]
    if not args.skip_dualeye:
        required_failures.extend([
            item for item in checks_list
            if item.get("required", True) and item.get("status") == "FAIL" and str(item.get("name", "")).startswith("DualEye")
        ])
    report = {
        "ok": not required_failures and line_summary["fail"] == 0,
        "protocol": "atlas.experience.acceptance.v0",
        "brain_url": brain,
        "dualeye_url": dualeye,
        "skip_dualeye": bool(args.skip_dualeye),
        "summary": line_summary,
        "lines": lines,
        "checks": [compact_check(item) for item in checks_list],
        "next_steps": [
            step
            for line in lines
            for step in line.get("next_steps", [])
            if step
        ],
    }

    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        print("Atlas 体验线验收台")
        print(f"Brain:   {brain}")
        print(f"DualEye: {dualeye}{' (skip)' if args.skip_dualeye else ''}")
        print(
            "汇总: "
            f"PASS={line_summary['pass']} WARN={line_summary['warn']} "
            f"NEEDS_DEVICE={line_summary['needs_device']} SKIP={line_summary['skip']} FAIL={line_summary['fail']}"
        )
        for line in lines:
            reason = "；".join([item for item in line.get("reasons", []) if item]) or "ok"
            print(f"[{line['status']}] {line['name']} score={line['score']} state={line['state']} - {reason}")
        print("\n关键检查:")
        for item in report["checks"]:
            tail = f" - {item['error']}" if item.get("error") else ""
            print(f"[{item['status']}] {item['name']} ({item['elapsed_ms']}ms){tail}")
        if report["next_steps"]:
            print("\n下一步:")
            for step in report["next_steps"]:
                print(f"- {step}")
        print("\nJSON 摘要:")
        print(json.dumps(report, ensure_ascii=False, indent=2))

    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
