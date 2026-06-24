#!/usr/bin/env python3
"""Run one-shot Atlas field acceptance and write a Markdown report.

This script is a thin orchestrator for existing acceptance tools. It does not
read local secret env files and it never treats skipped DualEye checks as pass.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any


DEFAULT_BRAIN_URL = "http://127.0.0.1:8787"
DEFAULT_DUALEYE_URL = "http://192.168.4.1"
REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = REPO_ROOT / "tools"


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def git_commit() -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=str(REPO_ROOT),
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except Exception:
        return "unknown"


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def write_text(path: Path, content: str) -> None:
    ensure_dir(path.parent)
    path.write_text(content, encoding="utf-8")


def read_json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        return {"ok": False, "error": f"failed to read json: {exc}", "path": str(path)}


def command_result(name: str, cmd: list[str], *, json_path: Path | None = None) -> dict[str, Any]:
    env = os.environ.copy()
    env.setdefault("NO_PROXY", "127.0.0.1,localhost")
    env.setdefault("no_proxy", "127.0.0.1,localhost")
    started = dt.datetime.now(dt.timezone.utc)
    proc = subprocess.run(
        cmd,
        cwd=str(REPO_ROOT),
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    elapsed_ms = int((dt.datetime.now(dt.timezone.utc) - started).total_seconds() * 1000)
    parsed: dict[str, Any] = {}
    if json_path and json_path.exists():
        parsed = read_json(json_path)
    else:
        parsed = parse_stdout_json(proc.stdout)
    return {
        "name": name,
        "cmd": cmd,
        "returncode": proc.returncode,
        "ok": proc.returncode == 0,
        "elapsed_ms": elapsed_ms,
        "stdout_tail": tail(proc.stdout),
        "stderr_tail": tail(proc.stderr),
        "json_path": str(json_path) if json_path else "",
        "parsed": parsed,
    }


def parse_stdout_json(stdout: str) -> dict[str, Any]:
    text = stdout.strip()
    if not text:
        return {}
    try:
        return json.loads(text)
    except Exception:
        marker = "\n{"
        idx = text.rfind(marker)
        if idx >= 0:
            try:
                return json.loads(text[idx + 1:])
            except Exception:
                return {}
    return {}


def tail(text: str, limit: int = 4000) -> str:
    text = text.strip()
    if len(text) <= limit:
        return text
    return "..." + text[-limit:]


def compact_summary(parsed: dict[str, Any]) -> str:
    summary = parsed.get("summary") if isinstance(parsed.get("summary"), dict) else {}
    if {"pass", "warn", "fail"}.issubset(summary.keys()):
        parts = [
            f"PASS={summary.get('pass', 0)}",
            f"WARN={summary.get('warn', 0)}",
        ]
        if "needs_device" in summary:
            parts.append(f"NEEDS_DEVICE={summary.get('needs_device', 0)}")
        if "skip" in summary:
            parts.append(f"SKIP={summary.get('skip', 0)}")
        parts.append(f"FAIL={summary.get('fail', 0)}")
        return " ".join(parts)
    return "ok" if parsed.get("ok") else "no summary"


def provider_missing(*reports: dict[str, Any]) -> list[str]:
    missing: list[str] = []
    for report in reports:
        direct = report.get("provider_missing")
        if isinstance(direct, list):
            missing.extend(str(item) for item in direct if item)
        for check in report.get("checks", []) if isinstance(report.get("checks"), list) else []:
            payload = check.get("payload") if isinstance(check, dict) else {}
            if not isinstance(payload, dict):
                continue
            diagnostics = payload.get("provider_diagnostics")
            if isinstance(diagnostics, dict):
                for key in ("llm", "asr", "tts"):
                    item = diagnostics.get(key, {})
                    if isinstance(item, dict) and item.get("missing_env"):
                        missing.extend(f"{key.upper()}:{env}" for env in item.get("missing_env", []))
    seen: set[str] = set()
    unique: list[str] = []
    for item in missing:
        if item not in seen:
            unique.append(item)
            seen.add(item)
    return unique


def runtime_score(web_report: dict[str, Any], preflash_report: dict[str, Any]) -> dict[str, Any]:
    score = web_report.get("runtime_score")
    if isinstance(score, dict):
        return {
            "current": score.get("current"),
            "ready": score.get("ready"),
        }
    for check in preflash_report.get("checks", []) if isinstance(preflash_report.get("checks"), list) else []:
        if not isinstance(check, dict) or check.get("name") != "Brain runtime score":
            continue
        payload = check.get("payload") if isinstance(check.get("payload"), dict) else {}
        return {
            "current": payload.get("current_score"),
            "ready": payload.get("ready_score"),
        }
    return {"current": None, "ready": None}


def experience_lines(report: dict[str, Any]) -> list[dict[str, Any]]:
    lines = report.get("lines")
    return lines if isinstance(lines, list) else []


def next_steps(results: list[dict[str, Any]], experience_report: dict[str, Any], *, skip_dualeye: bool) -> list[str]:
    steps: list[str] = []
    failed = [item for item in results if not item.get("ok")]
    if failed:
        steps.append("先打开主报告的 Failed Commands，按退出码和 stderr 定位失败脚本。")
    for line in experience_lines(experience_report):
        for step in line.get("next_steps", []) if isinstance(line, dict) else []:
            if step and str(step) not in steps:
                steps.append(str(step))
    if skip_dualeye:
        steps.append("接入 DualEye 后去掉 --skip-dualeye，并保留 trace JSONL/Markdown 给复盘。")
    else:
        steps.append("若页面没切或语音没播，优先看 trace 的 last_page_change、brain_ws、OPUS frames 和 runtime stage。")
    return steps


def md_escape(value: Any) -> str:
    return str(value if value is not None else "").replace("|", "\\|").replace("\n", "<br>")


def render_markdown(report: dict[str, Any]) -> str:
    score = report.get("runtime_score", {})
    files = report.get("files", {})
    lines = [
        "# Atlas 现场一键验收报告",
        "",
        f"- Generated at: `{md_escape(report.get('generated_at'))}`",
        f"- Service git commit: `{md_escape(report.get('service_git_commit'))}`",
        f"- Brain URL: `{md_escape(report.get('brain_url'))}`",
        f"- DualEye URL: `{md_escape(report.get('dualeye_url'))}`",
        f"- Skip DualEye: `{md_escape(report.get('skip_dualeye'))}`",
        f"- Runtime score: current=`{md_escape(score.get('current'))}` ready=`{md_escape(score.get('ready'))}`",
        f"- Provider missing: `{md_escape(', '.join(report.get('provider_missing', [])) or 'none')}`",
        "",
        "## Artifact Files",
        "",
    ]
    for label, path in files.items():
        if path:
            lines.append(f"- {label}: `{md_escape(path)}`")
    lines.extend([
        "",
        "## Command Summary",
        "",
        "| Check | Status | Summary | Elapsed |",
        "| --- | --- | --- | ---: |",
    ])
    for item in report.get("commands", []):
        status = "PASS" if item.get("ok") else "FAIL"
        lines.append(
            f"| {md_escape(item.get('name'))} | {status} | "
            f"{md_escape(compact_summary(item.get('parsed', {})))} | "
            f"{md_escape(item.get('elapsed_ms'))} ms |"
        )
    lines.extend([
        "",
        "## Experience Lines",
        "",
        "| Line | Status | State | Score | Reasons |",
        "| --- | --- | --- | ---: | --- |",
    ])
    for line in report.get("experience_lines", []):
        reasons = "; ".join(str(item) for item in line.get("reasons", []) if item)
        lines.append(
            f"| {md_escape(line.get('name'))} | {md_escape(line.get('status'))} | "
            f"{md_escape(line.get('state'))} | {md_escape(line.get('score'))} | "
            f"{md_escape(reasons)} |"
        )
    failed = [item for item in report.get("commands", []) if not item.get("ok")]
    if failed:
        lines.extend(["", "## Failed Commands", ""])
        for item in failed:
            lines.extend([
                f"### {item.get('name')}",
                "",
                f"- Exit code: `{item.get('returncode')}`",
                f"- Command: `{md_escape(' '.join(item.get('cmd', [])))}`",
                f"- stderr: `{md_escape(item.get('stderr_tail') or 'empty')}`",
                f"- stdout: `{md_escape(item.get('stdout_tail') or 'empty')}`",
                "",
            ])
    lines.extend(["", "## Next Steps", ""])
    for step in report.get("next_steps", []):
        lines.append(f"- {step}")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Atlas field acceptance bundle")
    parser.add_argument("--brain-url", default=DEFAULT_BRAIN_URL)
    parser.add_argument("--dualeye-url", default=DEFAULT_DUALEYE_URL)
    parser.add_argument("--duration-sec", type=float, default=30.0)
    parser.add_argument("--output-dir", default="/tmp/atlas_field_acceptance")
    parser.add_argument("--skip-dualeye", action="store_true")
    parser.add_argument("--skip-trace", action="store_true")
    args = parser.parse_args()

    output_dir = Path(args.output_dir).expanduser().resolve()
    ensure_dir(output_dir)
    brain_url = args.brain_url.rstrip("/")
    dualeye_url = args.dualeye_url.rstrip("/")

    preflash_json = output_dir / "atlas_preflash.json"
    web_json = output_dir / "atlas_web_voice_console.json"
    web_md = output_dir / "atlas_web_voice_console.md"
    experience_json = output_dir / "atlas_experience.json"
    experience_md = output_dir / "atlas_experience.md"
    trace_jsonl = output_dir / "atlas_trace.jsonl"
    trace_md = output_dir / "atlas_trace.md"
    field_json = output_dir / "atlas_field_acceptance.json"
    field_md = output_dir / "atlas_field_acceptance.md"

    py = sys.executable
    commands: list[tuple[str, list[str], Path | None]] = [
        (
            "preflash",
            [
                py,
                str(TOOLS_DIR / "check_atlas_preflash.py"),
                "--brain-url",
                brain_url,
                "--dualeye-url",
                dualeye_url,
                "--json",
            ] + (["--skip-dualeye"] if args.skip_dualeye else []),
            preflash_json,
        ),
        (
            "web voice console",
            [
                py,
                str(TOOLS_DIR / "check_atlas_web_voice_console.py"),
                "--brain-url",
                brain_url,
                "--output-md",
                str(web_md),
                "--json",
            ],
            web_json,
        ),
        (
            "experience",
            [
                py,
                str(TOOLS_DIR / "check_atlas_experience.py"),
                "--brain-url",
                brain_url,
                "--dualeye-url",
                dualeye_url,
                "--output-json",
                str(experience_json),
                "--output-md",
                str(experience_md),
                "--json",
            ] + (["--skip-dualeye"] if args.skip_dualeye else []),
            experience_json,
        ),
    ]
    if not args.skip_trace:
        commands.append((
            "realtime trace",
            [
                py,
                str(TOOLS_DIR / "collect_atlas_realtime_trace.py"),
                "--brain-url",
                brain_url,
                "--dualeye-url",
                dualeye_url,
                "--duration-sec",
                str(max(0.0, float(args.duration_sec))),
                "--interval-sec",
                "1",
                "--output-jsonl",
                str(trace_jsonl),
                "--output-md",
                str(trace_md),
            ] + (["--skip-dualeye"] if args.skip_dualeye else []),
            None,
        ))

    results: list[dict[str, Any]] = []
    for name, cmd, json_path in commands:
        result = command_result(name, cmd, json_path=json_path)
        if json_path and not json_path.exists() and result.get("parsed"):
            write_text(json_path, json.dumps(result["parsed"], ensure_ascii=False, indent=2) + "\n")
        results.append(result)

    preflash_report = read_json(preflash_json)
    web_report = read_json(web_json)
    experience_report = read_json(experience_json)
    summary = {
        "pass": sum(1 for item in results if item.get("ok")),
        "fail": sum(1 for item in results if not item.get("ok")),
        "skip": 1 if args.skip_trace else 0,
    }
    files = {
        "field_markdown": str(field_md),
        "field_json": str(field_json),
        "preflash_json": str(preflash_json),
        "web_voice_markdown": str(web_md),
        "web_voice_json": str(web_json),
        "experience_markdown": str(experience_md),
        "experience_json": str(experience_json),
        "trace_markdown": "" if args.skip_trace else str(trace_md),
        "trace_jsonl": "" if args.skip_trace else str(trace_jsonl),
    }
    report = {
        "ok": summary["fail"] == 0,
        "protocol": "atlas.field_acceptance.v0",
        "generated_at": utc_now(),
        "service_git_commit": git_commit(),
        "brain_url": brain_url,
        "dualeye_url": dualeye_url,
        "skip_dualeye": bool(args.skip_dualeye),
        "skip_trace": bool(args.skip_trace),
        "duration_sec": float(args.duration_sec),
        "summary": summary,
        "provider_missing": provider_missing(web_report, experience_report, preflash_report),
        "runtime_score": runtime_score(web_report, preflash_report),
        "experience_lines": experience_lines(experience_report),
        "commands": results,
        "files": files,
        "next_steps": next_steps(results, experience_report, skip_dualeye=bool(args.skip_dualeye)),
    }
    write_text(field_json, json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    write_text(field_md, render_markdown(report))

    print("Atlas 现场一键验收")
    print(f"Brain:   {brain_url}")
    print(f"DualEye: {dualeye_url}{' (skip)' if args.skip_dualeye else ''}")
    print(f"Commit:  {report['service_git_commit']}")
    print(f"Summary: PASS={summary['pass']} FAIL={summary['fail']} SKIP={summary['skip']}")
    print(f"Report:  {field_md}")
    for item in results:
        status = "PASS" if item.get("ok") else "FAIL"
        print(f"[{status}] {item['name']} - {compact_summary(item.get('parsed', {}))}")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
