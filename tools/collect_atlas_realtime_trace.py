#!/usr/bin/env python3
"""Collect short Atlas Brain + DualEye realtime traces.

The trace is intentionally endpoint-level and lightweight: it samples status and
diagnostic JSON for 30-120 second field debugging sessions, without touching
firmware files or local secret env files.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import subprocess
import time
import urllib.request
from pathlib import Path
from typing import Any


DEFAULT_BRAIN_URL = "http://127.0.0.1:8787"
DEFAULT_DUALEYE_URL = "http://192.168.4.1"
NO_PROXY_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))

BRAIN_ENDPOINTS = [
    ("runtime_diagnostics", "/api/runtime/diagnostics"),
    ("audio_stream_status", "/api/audio/stream/status"),
    ("providers", "/api/providers"),
]
DUALEYE_ENDPOINTS = [
    ("status_lite", "/api/status/lite"),
    ("turn_diagnostics", "/api/diagnostics/turn"),
    ("opus_stream_status", "/api/audio/opus-stream/status"),
]


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def service_git_commit() -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=REPO_ROOT,
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except Exception:
        return "unknown"


def http_json(url: str, timeout: float) -> dict[str, Any]:
    req = urllib.request.Request(url, headers={"Cache-Control": "no-store"})
    with NO_PROXY_OPENER.open(req, timeout=timeout) as resp:
        raw = resp.read().decode("utf-8", errors="replace").strip()
    return json.loads(raw)


def fetch_endpoint(base_url: str, path: str, timeout: float) -> dict[str, Any]:
    started = time.time()
    url = f"{base_url.rstrip('/')}{path}"
    try:
        payload = http_json(url, timeout)
        return {
            "ok": payload.get("ok", True) is not False,
            "elapsed_ms": int((time.time() - started) * 1000),
            "url": url,
            "payload": payload,
            "error": "" if payload.get("ok", True) is not False else str(payload.get("error", "ok=false")),
        }
    except Exception as exc:
        return {
            "ok": False,
            "elapsed_ms": int((time.time() - started) * 1000),
            "url": url,
            "payload": {},
            "error": str(exc),
        }


def read_path(value: Any, path: str, default: Any = "") -> Any:
    current = value
    for part in path.split("."):
        if isinstance(current, dict) and part in current:
            current = current[part]
        else:
            return default
    return current


def first_dict(*values: Any) -> dict[str, Any]:
    for value in values:
        if isinstance(value, dict):
            return value
    return {}


def compact_brain(sample: dict[str, Any]) -> dict[str, Any]:
    brain = sample.get("brain", {}) if isinstance(sample.get("brain"), dict) else {}
    diagnostics_payload = read_path(brain, "runtime_diagnostics.payload", {})
    diagnostics = diagnostics_payload.get("diagnostics", {}) if isinstance(diagnostics_payload, dict) else {}
    audio_payload = read_path(brain, "audio_stream_status.payload", {})
    stream = first_dict(
        read_path(audio_payload, "stream", {}),
        read_path(audio_payload, "last_stream", {}),
    )
    dualeye_stream = first_dict(read_path(audio_payload, "dualeye_stream.stream", {}))
    providers_payload = read_path(brain, "providers.payload", {})
    provider_diag = providers_payload.get("provider_diagnostics", {}) if isinstance(providers_payload, dict) else {}
    return {
        "runtime_stage": diagnostics.get("stage", ""),
        "runtime_ok": diagnostics.get("ok"),
        "latest_tool": read_path(diagnostics, "latest_tool.name", ""),
        "latest_turn_kind": read_path(diagnostics, "latest_turn.kind", ""),
        "latest_failure_stage": read_path(diagnostics, "latest_failure.stage", ""),
        "latest_failure_detail": read_path(diagnostics, "latest_failure.detail", ""),
        "stream_frames": int(stream.get("frames", stream.get("atlas_frames", 0)) or 0),
        "stream_bytes": int(stream.get("bytes", 0) or 0),
        "stream_gaps": int(stream.get("sequence_gaps", 0) or 0),
        "dualeye_stream_stage": dualeye_stream.get("stage", ""),
        "provider_missing": provider_missing(provider_diag),
    }


def compact_dualeye(sample: dict[str, Any]) -> dict[str, Any]:
    dualeye = sample.get("dualeye", {}) if isinstance(sample.get("dualeye"), dict) else {}
    status = read_path(dualeye, "status_lite.payload", {})
    turn = read_path(dualeye, "turn_diagnostics.payload", {})
    opus = read_path(dualeye, "opus_stream_status.payload", {})
    stream = first_dict(opus.get("stream", {}) if isinstance(opus, dict) else {}, opus)
    return {
        "scene_state": read_path(status, "scene.state", ""),
        "scene_title": read_path(status, "scene.title", ""),
        "page": read_path(status, "ui.page", ""),
        "chat_mode": read_path(status, "ui.chat_mode", ""),
        "last_page_change": read_path(status, "ui.last_page_change", read_path(status, "last_page_change", "")),
        "brain_ws_connected": read_path(status, "brain_ws.connected", ""),
        "brain_ws_stage": read_path(status, "brain_ws.stage", ""),
        "brain_ws_error": read_path(status, "brain_ws.last_error", ""),
        "turn_stage": read_path(turn, "stage", read_path(turn, "diagnostics.stage", "")),
        "turn_error": read_path(turn, "error", read_path(turn, "diagnostics.error", "")),
        "opus_stage": stream.get("stage", ""),
        "opus_frames": int(stream.get("frames", stream.get("frames_encoded", 0)) or 0),
        "opus_bytes": int(stream.get("bytes", stream.get("encoded_bytes", 0)) or 0),
        "capture_failures": int(stream.get("capture_failures", 0) or 0),
        "encode_failures": int(stream.get("encode_failures", 0) or 0),
        "send_failures": int(stream.get("send_failures", 0) or 0),
        "muted_frames": int(stream.get("muted_frames", 0) or 0),
    }


def provider_missing(provider_diag: dict[str, Any]) -> list[str]:
    missing: list[str] = []
    for name in ("llm", "asr", "tts"):
        item = provider_diag.get(name, {}) if isinstance(provider_diag, dict) else {}
        if isinstance(item, dict) and item.get("missing_env"):
            missing.extend(f"{name.upper()}:{env}" for env in item.get("missing_env", []))
    return missing


def sample_once(seq: int,
                started_at: float,
                *,
                brain_url: str,
                dualeye_url: str,
                timeout: float,
                skip_brain: bool,
                skip_dualeye: bool,
                selftest_phase: str = "") -> dict[str, Any]:
    sample: dict[str, Any] = {
        "seq": seq,
        "ts": utc_now(),
        "elapsed_sec": round(time.time() - started_at, 3),
        "skip_brain": bool(skip_brain),
        "skip_dualeye": bool(skip_dualeye),
        "brain": {},
        "dualeye": {},
    }
    if skip_brain:
        sample["brain"] = {"skipped": True, "reason": "--skip-brain"}
    else:
        sample["brain"] = {
            name: fetch_endpoint(brain_url, path, timeout)
            for name, path in BRAIN_ENDPOINTS
        }
    if skip_dualeye:
        sample["dualeye"] = {"skipped": True, "reason": "--skip-dualeye"}
    else:
        sample["dualeye"] = {
            name: fetch_endpoint(dualeye_url, path, timeout)
            for name, path in DUALEYE_ENDPOINTS
        }
        if selftest_phase:
            sample["dualeye"][f"selftest_{selftest_phase}"] = fetch_endpoint(dualeye_url, "/api/selftest", timeout)
    sample["summary"] = {
        "brain": compact_brain(sample),
        "dualeye": compact_dualeye(sample),
    }
    return sample


def write_jsonl(path: str, samples: list[dict[str, Any]]) -> None:
    output = Path(path)
    if output.parent and str(output.parent) != ".":
        output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as handle:
        for sample in samples:
            handle.write(json.dumps(sample, ensure_ascii=False, sort_keys=True) + "\n")


def changes_for(samples: list[dict[str, Any]], summary_path: str) -> list[str]:
    values = []
    for sample in samples:
        value = read_path(sample, summary_path, "")
        values.append(value)
    changes: list[str] = []
    previous = object()
    for sample, value in zip(samples, values):
        if value != previous:
            changes.append(f"t+{sample.get('elapsed_sec')}s -> {value!r}")
            previous = value
    return changes


def metric_delta(samples: list[dict[str, Any]], path: str) -> dict[str, Any]:
    values = [read_path(sample, path, 0) for sample in samples]
    nums = []
    for value in values:
        try:
            nums.append(int(value or 0))
        except Exception:
            nums.append(0)
    if not nums:
        nums = [0]
    return {"start": nums[0], "end": nums[-1], "delta": nums[-1] - nums[0], "max": max(nums)}


def collect_errors(samples: list[dict[str, Any]]) -> list[str]:
    errors: list[str] = []
    for sample in samples:
        for side in ("brain", "dualeye"):
            group = sample.get(side, {})
            if not isinstance(group, dict) or group.get("skipped"):
                continue
            for name, item in group.items():
                if not isinstance(item, dict):
                    continue
                error = str(item.get("error", "") or "")
                if error:
                    errors.append(f"t+{sample.get('elapsed_sec')}s {side}.{name}: {error}")
    seen: set[str] = set()
    unique: list[str] = []
    for error in errors:
        if error not in seen:
            unique.append(error)
            seen.add(error)
    return unique


def render_markdown(samples: list[dict[str, Any]], meta: dict[str, Any]) -> str:
    first = samples[0] if samples else {}
    last = samples[-1] if samples else {}
    brain = last.get("summary", {}).get("brain", {}) if isinstance(last.get("summary"), dict) else {}
    dualeye = last.get("summary", {}).get("dualeye", {}) if isinstance(last.get("summary"), dict) else {}
    errors = collect_errors(samples)
    lines = [
        "# Atlas Realtime Trace",
        "",
        f"- Generated at: `{meta.get('generated_at')}`",
        f"- Service git commit: `{meta.get('service_git_commit')}`",
        f"- Brain URL: `{meta.get('brain_url')}`",
        f"- DualEye URL: `{meta.get('dualeye_url')}`",
        f"- Duration: `{meta.get('duration_sec')}s`, interval: `{meta.get('interval_sec')}s`, samples: `{len(samples)}`",
        f"- Skip Brain: `{meta.get('skip_brain')}`, Skip DualEye: `{meta.get('skip_dualeye')}`",
        "",
        "## Last State",
        "",
        f"- Brain runtime stage: `{brain.get('runtime_stage', '')}`",
        f"- Brain latest failure: `{brain.get('latest_failure_stage', '')}` `{brain.get('latest_failure_detail', '')}`",
        f"- Provider missing: `{', '.join(brain.get('provider_missing', [])) or 'none'}`",
        f"- DualEye scene/page: `{dualeye.get('scene_state', '')}` / `{dualeye.get('page', '')}`",
        f"- DualEye brain_ws: connected=`{dualeye.get('brain_ws_connected', '')}` stage=`{dualeye.get('brain_ws_stage', '')}` error=`{dualeye.get('brain_ws_error', '')}`",
        f"- DualEye turn/opus stage: `{dualeye.get('turn_stage', '')}` / `{dualeye.get('opus_stage', '')}`",
        "",
        "## Key Changes",
        "",
    ]
    for label, path in (
        ("Brain runtime stage", "summary.brain.runtime_stage"),
        ("Brain latest tool", "summary.brain.latest_tool"),
        ("DualEye scene", "summary.dualeye.scene_state"),
        ("DualEye page", "summary.dualeye.page"),
        ("DualEye last_page_change", "summary.dualeye.last_page_change"),
        ("DualEye brain_ws", "summary.dualeye.brain_ws_stage"),
        ("DualEye opus stage", "summary.dualeye.opus_stage"),
        ("DualEye turn stage", "summary.dualeye.turn_stage"),
    ):
        changes = changes_for(samples, path)
        lines.append(f"- {label}: {'; '.join(changes) if changes else 'no samples'}")
    brain_frames = metric_delta(samples, "summary.brain.stream_frames")
    dual_frames = metric_delta(samples, "summary.dualeye.opus_frames")
    capture_failures = metric_delta(samples, "summary.dualeye.capture_failures")
    encode_failures = metric_delta(samples, "summary.dualeye.encode_failures")
    send_failures = metric_delta(samples, "summary.dualeye.send_failures")
    lines.extend([
        "",
        "## Frames And Failures",
        "",
        f"- Brain stream frames: `{brain_frames['start']} -> {brain_frames['end']}` delta `{brain_frames['delta']}`",
        f"- DualEye OPUS frames: `{dual_frames['start']} -> {dual_frames['end']}` delta `{dual_frames['delta']}`",
        f"- capture_failures: `{capture_failures['start']} -> {capture_failures['end']}` delta `{capture_failures['delta']}`",
        f"- encode_failures: `{encode_failures['start']} -> {encode_failures['end']}` delta `{encode_failures['delta']}`",
        f"- send_failures: `{send_failures['start']} -> {send_failures['end']}` delta `{send_failures['delta']}`",
        "",
        "## Errors",
        "",
    ])
    if errors:
        lines.extend(f"- {error}" for error in errors[:80])
    else:
        lines.append("- none")
    lines.extend([
        "",
        "## Sample Index",
        "",
        "| seq | elapsed | brain_stage | page | opus_stage | frames | errors |",
        "| ---: | ---: | --- | --- | --- | ---: | --- |",
    ])
    for sample in samples:
        b = sample.get("summary", {}).get("brain", {})
        d = sample.get("summary", {}).get("dualeye", {})
        sample_errors = [
            err for err in errors
            if f"t+{sample.get('elapsed_sec')}s " in err
        ]
        lines.append(
            f"| {sample.get('seq')} | {sample.get('elapsed_sec')} | "
            f"{b.get('runtime_stage', '')} | {d.get('page', '')} | "
            f"{d.get('opus_stage', '')} | {d.get('opus_frames', 0)} | "
            f"{'; '.join(sample_errors[:3])} |"
        )
    lines.append("")
    return "\n".join(lines)


def write_text(path: str, content: str) -> None:
    output = Path(path)
    if output.parent and str(output.parent) != ".":
        output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Collect Atlas Brain + DualEye realtime trace")
    parser.add_argument("--brain-url", default=DEFAULT_BRAIN_URL)
    parser.add_argument("--dualeye-url", default=DEFAULT_DUALEYE_URL)
    parser.add_argument("--duration-sec", type=float, default=30.0)
    parser.add_argument("--interval-sec", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--output-jsonl", default="", help="Write JSONL samples to this path")
    parser.add_argument("--output-md", default="", help="Write Markdown summary to this path")
    parser.add_argument("--skip-brain", action="store_true")
    parser.add_argument("--skip-dualeye", action="store_true")
    args = parser.parse_args()

    duration_sec = max(0.0, float(args.duration_sec))
    interval_sec = max(0.2, float(args.interval_sec))
    brain_url = args.brain_url.rstrip("/")
    dualeye_url = args.dualeye_url.rstrip("/")
    started = time.time()
    samples: list[dict[str, Any]] = []
    seq = 0
    while True:
        elapsed = time.time() - started
        phase = "start" if seq == 0 else ""
        sample = sample_once(
            seq,
            started,
            brain_url=brain_url,
            dualeye_url=dualeye_url,
            timeout=float(args.timeout),
            skip_brain=bool(args.skip_brain),
            skip_dualeye=bool(args.skip_dualeye),
            selftest_phase=phase,
        )
        samples.append(sample)
        if elapsed >= duration_sec:
            break
        seq += 1
        time.sleep(min(interval_sec, max(0.0, duration_sec - (time.time() - started))))
    if not args.skip_dualeye and samples:
        samples[-1]["dualeye"]["selftest_end"] = fetch_endpoint(dualeye_url, "/api/selftest", float(args.timeout))

    meta = {
        "generated_at": utc_now(),
        "service_git_commit": service_git_commit(),
        "brain_url": brain_url,
        "dualeye_url": dualeye_url,
        "duration_sec": duration_sec,
        "interval_sec": interval_sec,
        "skip_brain": bool(args.skip_brain),
        "skip_dualeye": bool(args.skip_dualeye),
        "sample_count": len(samples),
        "output_jsonl": args.output_jsonl,
        "output_md": args.output_md,
    }
    if samples:
        samples[0]["trace_meta"] = meta

    if args.output_jsonl:
        write_jsonl(args.output_jsonl, samples)
    else:
        for sample in samples:
            print(json.dumps(sample, ensure_ascii=False, sort_keys=True))
    if args.output_md:
        write_text(args.output_md, render_markdown(samples, meta))

    summary = {
        "ok": True,
        "protocol": "atlas.realtime_trace.v0",
        "sample_count": len(samples),
        "generated_at": meta["generated_at"],
        "service_git_commit": meta["service_git_commit"],
        "skip_brain": meta["skip_brain"],
        "skip_dualeye": meta["skip_dualeye"],
        "output_jsonl": args.output_jsonl,
        "output_md": args.output_md,
        "errors": collect_errors(samples),
        "last": samples[-1].get("summary", {}) if samples else {},
    }
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
