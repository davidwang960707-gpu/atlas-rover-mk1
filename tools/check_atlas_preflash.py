#!/usr/bin/env python3
"""Atlas 烧录前/烧录后加固检查。

默认同时检查 DualEye 和本机 Atlas Brain。烧录前如果设备没在线，可以加
`--skip-dualeye`，只检查 Mac Brain 和 OTA 包清单。
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from typing import Any


DEFAULT_DUALEYE_URL = "http://192.168.4.1"
DEFAULT_BRAIN_URL = "http://127.0.0.1:8787"
EXPECTED_FIRMWARE_VERSION = "0.14.7-acceptance"
EXPECTED_TOOL_SCHEMA = "atlas.tools.v0.desk_apps"
EXPECTED_BRAIN_SESSION_PROTOCOL = "atlas.brain.session.v1"
EXPECTED_OTA_MANIFEST = "atlas.ota.manifest.v0"
EXPECTED_RUNTIME_SCORE = 80
EXPECTED_STATUS_KEYS = {"scene", "runtime", "audio", "audio_service", "audio_stream", "voice_wake", "apps", "ui", "wifi"}
EXPECTED_LITE_STATUS_KEYS = {"fingerprint", "features", "ui", "scene", "apps", "wifi", "audio_service", "audio_stream"}
EXPECTED_SCENE_KEYS = {
    "state",
    "label",
    "title",
    "subtitle",
    "hint",
    "left_role",
    "right_role",
    "severity",
    "page",
    "expression",
}
FORBIDDEN_UI_PLACEHOLDERS = {
    "异常表情",
    "巡游表情",
    "充电表情",
    "专注表情",
    "录音表情",
    "思考表情",
    "技能表情",
    "说话表情",
    "开心表情",
    "待命表情",
}
FORBIDDEN_UI_RAW_ERRORS = {
    "ESP_ERR",
    "HTTP_",
    "EHOST",
    "ECONN",
    "ENET",
}
EXPECTED_DESK_TOOLS = {
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
    "atlas.ui.set_chat_mode",
    "atlas.pet.set_state",
    "atlas.pet.play_animation",
}
NO_PROXY_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def http_json(url: str, timeout: float) -> dict[str, Any]:
    req = urllib.request.Request(url, headers={"Cache-Control": "no-store"})
    with NO_PROXY_OPENER.open(req, timeout=timeout) as resp:
        raw = resp.read().decode("utf-8", errors="replace").strip()
    return json.loads(raw)


def check_get(name: str, url: str, timeout: float, required: bool = True) -> dict[str, Any]:
    started = time.time()
    try:
        payload = http_json(url, timeout)
        return {
            "name": name,
            "ok": bool(payload.get("ok", True)),
            "required": required,
            "elapsed_ms": int((time.time() - started) * 1000),
            "url": url,
            "payload": payload,
        }
    except Exception as exc:
        return {
            "name": name,
            "ok": False,
            "required": required,
            "elapsed_ms": int((time.time() - started) * 1000),
            "url": url,
            "error": str(exc),
        }


def check_post_json(name: str, url: str, payload: dict[str, Any], timeout: float, required: bool = True) -> dict[str, Any]:
    started = time.time()
    try:
        body = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            url,
            data=body,
            headers={"Content-Type": "application/json", "Cache-Control": "no-store"},
            method="POST",
        )
        with NO_PROXY_OPENER.open(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", errors="replace").strip()
        parsed = json.loads(raw)
        return {
            "name": name,
            "ok": bool(parsed.get("ok", True)),
            "required": required,
            "elapsed_ms": int((time.time() - started) * 1000),
            "url": url,
            "payload": parsed,
        }
    except Exception as exc:
        return {
            "name": name,
            "ok": False,
            "required": required,
            "elapsed_ms": int((time.time() - started) * 1000),
            "url": url,
            "error": str(exc),
        }


def check_binary(
    name: str,
    url: str,
    timeout: float,
    required: bool = True,
    min_length: int = 1,
    first_byte_hex: str = "",
) -> dict[str, Any]:
    started = time.time()
    try:
        req = urllib.request.Request(url, headers={"Cache-Control": "no-store"})
        with NO_PROXY_OPENER.open(req, timeout=timeout) as resp:
            first = resp.read(1)
            content_length = int(resp.headers.get("Content-Length") or 0)
            content_type = resp.headers.get("Content-Type", "")
        ok = bool(first) and content_length >= min_length
        if first_byte_hex:
            ok = ok and first.hex().lower() == first_byte_hex.lower()
        return {
            "name": name,
            "ok": ok,
            "required": required,
            "elapsed_ms": int((time.time() - started) * 1000),
            "url": url,
            "payload": {
                "content_length": content_length,
                "content_type": content_type,
                "first_byte_hex": first.hex(),
                "expected_min_length": min_length,
                "expected_first_byte_hex": first_byte_hex,
            },
        }
    except Exception as exc:
        return {
            "name": name,
            "ok": False,
            "required": required,
            "elapsed_ms": int((time.time() - started) * 1000),
            "url": url,
            "error": str(exc),
        }


def validate_check(item: dict[str, Any]) -> dict[str, Any]:
    payload = item.get("payload")
    name = str(item.get("name", ""))
    if not isinstance(payload, dict):
        if name in {"Brain tools", "Brain MCP tools", "Brain OTA packages"} and item.get("error"):
            item["hint"] = "当前 8787 可能是旧版 Mac Brain，请重启 tools/atlas_brain_server.py"
        return item
    error = ""

    if name == "DualEye status":
        version = str(payload.get("fingerprint", {}).get("firmware_version", ""))
        if version != EXPECTED_FIRMWARE_VERSION:
            error = f"DualEye firmware mismatch: {version or 'missing'} != {EXPECTED_FIRMWARE_VERSION}"
        apps = payload.get("apps", {}) if isinstance(payload.get("apps"), dict) else {}
        if not error and apps.get("protocol") != "atlas.desk_apps.v0":
            error = "DualEye desk apps status missing or old"
        missing_status_keys = sorted(EXPECTED_STATUS_KEYS - set(payload.keys()))
        if not error and missing_status_keys:
            error = f"DualEye /api/status incomplete, missing: {missing_status_keys}"
        scene = payload.get("scene", {}) if isinstance(payload.get("scene"), dict) else {}
        missing_scene_keys = sorted(EXPECTED_SCENE_KEYS - set(scene.keys()))
        if not error and missing_scene_keys:
            error = f"DualEye scene contract incomplete, missing: {missing_scene_keys}"
        if not error:
            scene_text = "\n".join(str(scene.get(key, "")) for key in ("title", "subtitle", "hint", "left_role", "right_role"))
            leaked = sorted(word for word in FORBIDDEN_UI_PLACEHOLDERS if word in scene_text)
            if leaked:
                error = f"DualEye scene leaks placeholder UI text: {leaked}"
        if not error:
            scene_text = "\n".join(str(scene.get(key, "")) for key in ("title", "subtitle", "hint"))
            leaked_raw = sorted(word for word in FORBIDDEN_UI_RAW_ERRORS if word in scene_text)
            if leaked_raw:
                error = f"DualEye scene leaks raw low-level error text: {leaked_raw}"
        if not error:
            audio_service = payload.get("audio_service", {}) if isinstance(payload.get("audio_service"), dict) else {}
            if audio_service.get("mode") == "error" and scene.get("severity") != "error":
                error = "DualEye scene does not surface audio_service error"
        if not error:
            wifi = payload.get("wifi", {}) if isinstance(payload.get("wifi"), dict) else {}
            if (
                wifi.get("ap_started") is True
                and wifi.get("sta_connected") is False
                and scene.get("state") not in {"wifi_config", "audio_test", "brain_offline", "error"}
            ):
                error = f"DualEye scene hides Wi-Fi config state behind {scene.get('state')}"
    elif name == "DualEye status lite":
        version = str(payload.get("fingerprint", {}).get("firmware_version", ""))
        if version != EXPECTED_FIRMWARE_VERSION:
            error = f"DualEye lite firmware mismatch: {version or 'missing'} != {EXPECTED_FIRMWARE_VERSION}"
        missing_lite_keys = sorted(EXPECTED_LITE_STATUS_KEYS - set(payload.keys()))
        if not error and missing_lite_keys:
            error = f"DualEye /api/status/lite incomplete, missing: {missing_lite_keys}"
        features = payload.get("features", {}) if isinstance(payload.get("features"), dict) else {}
        if not error and not features.get("tools"):
            error = "DualEye lite status does not expose tool capability"
        scene = payload.get("scene", {}) if isinstance(payload.get("scene"), dict) else {}
        if not error:
            scene_text = "\n".join(str(scene.get(key, "")) for key in ("title", "subtitle", "hint"))
            leaked = sorted(word for word in FORBIDDEN_UI_PLACEHOLDERS if word in scene_text)
            if leaked:
                error = f"DualEye lite scene leaks placeholder UI text: {leaked}"
    elif name == "DualEye system info":
        fingerprint = payload.get("fingerprint", {}) if isinstance(payload.get("fingerprint"), dict) else {}
        if fingerprint.get("resource_version") != "dualeye-assets-v0.5-pet-head":
            error = "DualEye resource fingerprint missing or mismatched"
    elif name == "DualEye selftest":
        summary = payload.get("summary", {}) if isinstance(payload.get("summary"), dict) else {}
        if int(summary.get("fail", 1)) > 0:
            error = f"DualEye selftest has fail={summary.get('fail')}"
        version = str(payload.get("fingerprint", {}).get("firmware_version", ""))
        if not error and version != EXPECTED_FIRMWARE_VERSION:
            error = f"DualEye selftest firmware mismatch: {version or 'missing'}"
    elif name == "DualEye capabilities":
        schema = str(payload.get("fingerprint", {}).get("tool_schema_version", ""))
        if schema != EXPECTED_TOOL_SCHEMA:
            error = f"DualEye tool schema mismatch: {schema or 'missing'}"
        brain_channel = payload.get("brain_channel", {}) if isinstance(payload.get("brain_channel"), dict) else {}
        brain_protocol = str(brain_channel.get("protocol", ""))
        if not error and brain_protocol != EXPECTED_BRAIN_SESSION_PROTOCOL:
            error = f"DualEye brain session protocol mismatch: {brain_protocol or 'missing'}"
        if not error and not bool(brain_channel.get("opus_streaming")):
            error = "DualEye opus_streaming capability missing"
    elif name == "Brain health":
        if payload.get("service") != "atlas-brain":
            error = "Brain health service is not atlas-brain"
    elif name == "Brain acceptance report":
        summary = payload.get("summary", {}) if isinstance(payload.get("summary"), dict) else {}
        if int(summary.get("fail", 1)) > 0:
            error = f"Brain acceptance required failures: {summary.get('fail')}"
        elif payload.get("protocol") != "atlas.acceptance.v0":
            error = "Brain acceptance protocol mismatch"
    elif name == "Brain runtime score":
        score = payload.get("score", {}) if isinstance(payload.get("score"), dict) else {}
        ready_score = payload.get("ready_score", {}) if isinstance(payload.get("ready_score"), dict) else {}
        current = int(score.get("score", 0) or 0)
        ready = int(ready_score.get("score", 0) or 0)
        if payload.get("protocol") != "atlas.runtime.score.v0":
            error = "Brain runtime score protocol mismatch"
        elif ready < EXPECTED_RUNTIME_SCORE:
            error = f"Brain ready score too low: {ready}/{EXPECTED_RUNTIME_SCORE}"
        else:
            item["ok"] = True
            if current < EXPECTED_RUNTIME_SCORE:
                item["hint"] = f"当前实测分 {current}，ready score {ready}；跑 OPUS 真流或配置 Provider 后再看当前分"
    elif name in {"Brain tools", "Brain MCP tools"}:
        tool_count = int(payload.get("tool_count", len(payload.get("tools", []))) or 0)
        protocol = str(payload.get("protocol", ""))
        if protocol != EXPECTED_TOOL_SCHEMA:
            error = f"Brain tool schema endpoint is old or missing: {protocol or 'missing'}"
        elif tool_count < 10:
            error = f"Brain tool count too low: {tool_count}"
        else:
            names = {str(tool.get("name", "")) for tool in payload.get("tools", []) if isinstance(tool, dict)}
            missing_tools = sorted(EXPECTED_DESK_TOOLS - names)
            if missing_tools:
                error = f"Brain desk app tools missing: {missing_tools}"
    elif name == "Brain OTA manifest":
        protocol = str(payload.get("protocol", ""))
        status = str(payload.get("status", ""))
        packages = payload.get("packages", [])
        missing = payload.get("missing", [])
        if protocol != EXPECTED_OTA_MANIFEST:
            error = f"Brain OTA manifest protocol mismatch: {protocol or 'missing'}"
        elif status != "package_ready":
            error = f"Brain OTA manifest is not package_ready: {status or 'missing'}"
        elif not bool(payload.get("app_ota_supported")):
            error = "Brain OTA manifest does not advertise app OTA support"
        elif len(packages) < 5:
            error = f"Brain OTA manifest package count too low: {len(packages)}"
        elif missing:
            error = f"Brain OTA manifest has missing packages: {missing}"
    elif name == "Brain OTA packages":
        protocol = str(payload.get("protocol", ""))
        status = str(payload.get("status", ""))
        packages = payload.get("packages", [])
        if protocol != EXPECTED_OTA_MANIFEST:
            error = f"Brain OTA packages protocol mismatch: {protocol or 'missing'}"
        elif status != "package_ready":
            error = f"Brain OTA packages are not package_ready: {status or 'missing'}"
        elif len(packages) < 5:
            error = f"Brain OTA package count too low: {len(packages)}"
        elif not any(pkg.get("name") == "app_ota" for pkg in packages if isinstance(pkg, dict)):
            error = "Brain OTA packages missing app_ota package"
    elif name == "Brain OTA app package":
        content_length = int(payload.get("content_length", 0) or 0)
        first_byte = str(payload.get("first_byte_hex", ""))
        if content_length < 100000:
            error = f"Brain OTA app package too small: {content_length}"
        elif first_byte.lower() != "e9":
            error = f"Brain OTA app package does not look like ESP image: first={first_byte or 'missing'}"
    elif name == "Brain OTA SR model package":
        content_length = int(payload.get("content_length", 0) or 0)
        first_byte = str(payload.get("first_byte_hex", ""))
        if content_length < 200000:
            error = f"Brain OTA SR model package too small: {content_length}"
        elif first_byte.lower() != "01":
            error = f"Brain OTA SR model package does not look like srmodels.bin: first={first_byte or 'missing'}"
    elif name == "Brain tool call":
        if payload.get("protocol") != EXPECTED_TOOL_SCHEMA:
            error = "Brain tool call protocol mismatch"
        elif payload.get("tool") != "atlas.ota.check":
            error = "Brain tool call returned unexpected tool"
        result = payload.get("result", {}) if isinstance(payload.get("result"), dict) else {}
        if not error and not bool(result.get("ok")):
            error = f"Brain tool call result not ok: {result.get('error', 'unknown')}"
        manifest = result.get("manifest", {}) if isinstance(result.get("manifest"), dict) else {}
        manifest_protocol = result.get("manifest_protocol") or manifest.get("protocol")
        if not error and manifest_protocol != EXPECTED_OTA_MANIFEST:
            error = "Brain OTA tool result protocol mismatch"

    if error:
        item["ok"] = False
        item["error"] = error
        if "Brain" in name and "old or missing" in error:
            item["hint"] = "请重启 Mac Brain：python3 tools/atlas_brain_server.py --dualeye-url http://设备IP --port 8787"
    return item


def status_of(item: dict[str, Any]) -> str:
    if item.get("ok"):
        return "PASS"
    return "FAIL" if item.get("required", True) else "WARN"


def summarize(checks: list[dict[str, Any]]) -> dict[str, int]:
    summary = {"pass": 0, "warn": 0, "fail": 0}
    for item in checks:
        state = status_of(item).lower()
        summary[state] += 1
    return summary


def compact_payload(name: str, payload: dict[str, Any]) -> dict[str, Any]:
    if name == "DualEye selftest":
        return {
            "ready_to_flash": payload.get("ready_to_flash"),
            "summary": payload.get("summary"),
            "fingerprint": payload.get("fingerprint"),
        }
    if name == "DualEye status":
        return {
            "firmware": payload.get("firmware"),
            "fingerprint": payload.get("fingerprint"),
            "wifi": payload.get("wifi"),
            "ui": payload.get("ui"),
            "scene": payload.get("scene"),
            "audio": payload.get("audio"),
            "audio_service": payload.get("audio_service"),
            "audio_stream": payload.get("audio_stream"),
            "runtime": payload.get("runtime"),
            "apps": payload.get("apps"),
        }
    if name == "DualEye status lite":
        return {
            "firmware": payload.get("firmware"),
            "fingerprint": payload.get("fingerprint"),
            "features": payload.get("features"),
            "ui": payload.get("ui"),
            "scene": payload.get("scene"),
        }
    if name == "Brain health":
        return {
            "service": payload.get("service"),
            "llm_enabled": payload.get("llm_enabled"),
            "asr_enabled": payload.get("asr_enabled"),
            "tts_enabled": payload.get("tts_enabled"),
            "skill_count": payload.get("skill_count"),
            "platform_summary": payload.get("platform_summary"),
        }
    if name == "Brain acceptance report":
        return {
            "protocol": payload.get("protocol"),
            "ready_to_flash_test": payload.get("ready_to_flash_test"),
            "summary": payload.get("summary"),
            "runtime_score": payload.get("runtime_score", {}),
            "next_steps": payload.get("next_steps", []),
            "xiaozhi_gap": payload.get("xiaozhi_gap", {}),
        }
    if name == "Brain runtime score":
        score = payload.get("score", {}) if isinstance(payload.get("score"), dict) else {}
        ready_score = payload.get("ready_score", {}) if isinstance(payload.get("ready_score"), dict) else {}
        return {
            "protocol": payload.get("protocol"),
            "current_score": score.get("score"),
            "current_ok": score.get("ok"),
            "ready_score": ready_score.get("score"),
            "ready_ok": ready_score.get("ok"),
            "missing_tools": payload.get("missing_tools", []),
            "provider_config": payload.get("capabilities", {}).get("provider_config") if isinstance(payload.get("capabilities"), dict) else None,
            "latest_stream": payload.get("latest_stream", {}),
        }
    if name in {"Brain tools", "Brain MCP tools"}:
        return {
            "protocol": payload.get("protocol"),
            "tool_count": payload.get("tool_count", len(payload.get("tools", []))),
            "desk_tools_present": sorted(
                EXPECTED_DESK_TOOLS & {str(tool.get("name", "")) for tool in payload.get("tools", []) if isinstance(tool, dict)}
            ),
        }
    if name == "Brain OTA manifest":
        return {
            "protocol": payload.get("protocol"),
            "status": payload.get("status"),
            "package_count": len(payload.get("packages", [])),
            "missing": payload.get("missing", []),
        }
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description="Atlas 烧录前/烧录后加固检查")
    parser.add_argument("--dualeye-url", default=DEFAULT_DUALEYE_URL)
    parser.add_argument("--brain-url", default=DEFAULT_BRAIN_URL)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--skip-dualeye", action="store_true", help="设备未在线时只检查 Mac Brain")
    parser.add_argument("--json", action="store_true", help="只输出 JSON")
    args = parser.parse_args()

    dualeye = args.dualeye_url.rstrip("/")
    brain = args.brain_url.rstrip("/")
    checks: list[dict[str, Any]] = []

    if not args.skip_dualeye:
        checks.extend([
            check_get("DualEye status", f"{dualeye}/api/status", args.timeout),
            check_get("DualEye status lite", f"{dualeye}/api/status/lite", args.timeout),
            check_get("DualEye system info", f"{dualeye}/api/system/info", args.timeout),
            check_get("DualEye selftest", f"{dualeye}/api/selftest", args.timeout),
            check_get("DualEye capabilities", f"{dualeye}/api/capabilities", args.timeout),
            check_get("DualEye OTA status", f"{dualeye}/api/ota/status", args.timeout, required=False),
        ])

    checks.extend([
        check_get("Brain health", f"{brain}/health", args.timeout),
        check_get("Brain acceptance report", f"{brain}/api/acceptance/report{'?skip_device=1' if args.skip_dualeye else ''}", args.timeout),
        check_get("Brain runtime score", f"{brain}/api/runtime/score", args.timeout),
        check_get("Brain tools", f"{brain}/api/tools/list", args.timeout),
        check_get("Brain MCP tools", f"{brain}/mcp/tools/list", args.timeout, required=False),
        check_post_json("Brain tool call", f"{brain}/api/tools/call", {"name": "atlas.ota.check", "arguments": {}}, args.timeout),
        check_get("Brain OTA manifest", f"{brain}/ota/manifest", args.timeout),
        check_get("Brain OTA packages", f"{brain}/api/ota/packages", args.timeout),
        check_binary("Brain OTA app package", f"{brain}/ota/package/app_ota", args.timeout, min_length=100000, first_byte_hex="e9"),
        check_binary("Brain OTA SR model package", f"{brain}/ota/package/sr_model", args.timeout, min_length=200000, first_byte_hex="01"),
    ])
    checks = [validate_check(item) for item in checks]

    report = {
        "ok": all(item.get("ok") or not item.get("required", True) for item in checks),
        "dualeye_url": dualeye,
        "brain_url": brain,
        "summary": summarize(checks),
        "checks": [
            {
                "name": item["name"],
                "status": status_of(item),
                "elapsed_ms": item["elapsed_ms"],
                "url": item["url"],
                "error": item.get("error", ""),
                "hint": item.get("hint", ""),
                "payload": compact_payload(item["name"], item.get("payload", {})) if item.get("payload") else {},
            }
            for item in checks
        ],
    }

    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        print("Atlas 烧录前/烧录后加固检查")
        print(f"DualEye: {dualeye}")
        print(f"Brain:   {brain}")
        print(f"汇总: PASS={report['summary']['pass']} WARN={report['summary']['warn']} FAIL={report['summary']['fail']}")
        for item in report["checks"]:
            tail = f" - {item['error']}" if item.get("error") else ""
            if item.get("hint"):
                tail += f"；{item['hint']}"
            print(f"[{item['status']}] {item['name']} ({item['elapsed_ms']}ms){tail}")
        print("\nJSON 摘要:")
        print(json.dumps(report, ensure_ascii=False, indent=2))

    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
