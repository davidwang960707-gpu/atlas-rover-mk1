#!/usr/bin/env python3
"""Local preview server for the DualEye embedded Web UI.

The ESP32 firmware keeps the page as C string literals. This helper extracts
that HTML and serves a tiny mock API, so the page can be previewed on a Mac
before flashing the board.
"""

from __future__ import annotations

import argparse
import ast
import json
import re
import socketserver
import sys
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler
from pathlib import Path
from urllib.parse import parse_qs


ROOT = Path(__file__).resolve().parents[1]
ADMIN_SOURCE = ROOT / "firmware" / "dualeye" / "main" / "atlas_admin_http.c"
PREVIEW_PIN = "123456"


MOCK_FETCH_SCRIPT = r"""
<script>
(() => {
  const previewPin = "123456";
  const state = {
    ui: { page: "eyes", expression: "idle", motion: "stop", moving: false, last_ack: 0 },
    wifi: { mode: "ap", sta_connected: false, sta_ip: "", ap_started: true, ap_ip: "192.168.4.1", ap_ssid: "AtlasRover-PREVIEW" },
    llm: { mode: "off", label: "关闭", provider: "openai_compatible", base_url: "", model: "", configured: false, api_key_set: false },
    safety: { motion_enabled: true, control_mode: "manual", max_speed_percent: 40, max_duration_ms: 700 }
  };
  const labels = { off: "关闭", host: "电脑宿主 MiniClaw", cloud: "云端大模型", embedded: "端侧 MimiClaw" };
  const expressions = new Set(["idle", "happy", "listen", "thinking", "speaking", "moving", "curious", "sleepy", "surprised", "wink", "angry", "charging", "error"]);
  const pages = new Set(["eyes", "clock", "status", "voice", "settings", "alarm", "photo", "music", "story"]);
  const jsonResponse = (payload, status = 200) => new Response(JSON.stringify(payload), {
    status,
    headers: { "Content-Type": "application/json; charset=utf-8" }
  });
  const statusPayload = () => ({ ok: true, pairing_hint: "preview pin is 123456", ...state });
  const formFrom = opts => new URLSearchParams((opts && opts.body) || "");
  const requirePin = form => form.get("pin") === previewPin
    ? null
    : { payload: { ok: false, error: "pairing required; preview pin is 123456" }, status: 403 };

  window.fetch = async (url, opts = {}) => {
    const path = String(url);
    if (path === "/api/status") return jsonResponse(statusPayload());
    const form = formFrom(opts);

    if (path === "/api/rover/stop") {
      Object.assign(state.ui, { expression: "idle", motion: "stop", moving: false, last_ack: 1 });
      return jsonResponse({ ok: true, action: "stop" });
    }
    if (path === "/api/rover/move") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      const motion = { F: "forward", B: "backward", L: "turn_left", R: "turn_right" }[form.get("dir")];
      if (!motion) return jsonResponse({ ok: false, error: "bad direction" }, 400);
      if (!state.safety.motion_enabled) return jsonResponse({ ok: false, error: "motion disabled" }, 423);
      if (state.safety.control_mode !== "manual") return jsonResponse({ ok: false, error: "manual mode required" }, 409);
      Object.assign(state.ui, { expression: "moving", motion, moving: true, last_ack: 1 });
      return jsonResponse({
        ok: true,
        action: "move",
        speed: Math.min(Number(form.get("speed") || 30), state.safety.max_speed_percent),
        duration_ms: Math.min(Number(form.get("duration") || 500), state.safety.max_duration_ms)
      });
    }
    if (path === "/api/config/wifi") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      if (!form.get("ssid")) return jsonResponse({ ok: false, error: "ssid required" }, 400);
      Object.assign(state.wifi, { mode: "sta", sta_connected: true, sta_ip: "192.168.1.88" });
      return jsonResponse({ ok: true, saved: "wifi", note: `preview saved ssid ${form.get("ssid")}` });
    }
    if (path === "/api/app/expression") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      const expression = form.get("expression") || "idle";
      if (!expressions.has(expression)) return jsonResponse({ ok: false, error: "bad expression" }, 400);
      Object.assign(state.ui, { page: "eyes", expression });
      return jsonResponse({ ok: true, app: "expression" });
    }
    if (path === "/api/app/page") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      const page = form.get("page") || "eyes";
      if (!pages.has(page)) return jsonResponse({ ok: false, error: "bad page" }, 400);
      state.ui.page = page;
      state.ui.expression = page === "voice" ? "listen" : ["alarm", "photo"].includes(page) ? "curious" : "idle";
      return jsonResponse({ ok: true, app: "page" });
    }
    if (path === "/api/app/action") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      const action = form.get("action") || "";
      const map = {
        music: ["music", "speaking"],
        story: ["story", "speaking"],
        chat: ["voice", "listen"],
        alarm: ["alarm", "curious"]
      };
      if (!map[action]) return jsonResponse({ ok: false, error: "bad action" }, 400);
      Object.assign(state.ui, { page: map[action][0], expression: map[action][1], moving: false, motion: "stop" });
      return jsonResponse({ ok: true, app: "action", note: "mimiclaw placeholder" });
    }
    if (path === "/api/config/llm") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      const mode = form.get("mode") || "off";
      Object.assign(state.llm, {
        mode,
        label: labels[mode] || "未知模式",
        provider: form.get("provider") || "openai_compatible",
        base_url: form.get("base_url") || "",
        model: form.get("model") || ""
      });
      if (form.get("api_key")) state.llm.api_key_set = true;
      state.llm.configured = (mode === "host" && !!state.llm.base_url) ||
        (["cloud", "embedded"].includes(mode) && !!state.llm.base_url && !!state.llm.model);
      return jsonResponse({ ok: true, saved: "llm" });
    }
    if (path === "/api/config/safety") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      Object.assign(state.safety, {
        motion_enabled: ["1", "true"].includes(form.get("motion_enabled")),
        control_mode: ["manual", "ai"].includes(form.get("control_mode")) ? form.get("control_mode") : "manual",
        max_speed_percent: Math.min(Math.max(Number(form.get("max_speed") || 40), 1), 80),
        max_duration_ms: Math.min(Math.max(Number(form.get("max_duration") || 700), 100), 2000)
      });
      return jsonResponse({ ok: true, saved: "safety" });
    }
    if (path === "/api/voice/text") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      const text = (form.get("text") || "").toLowerCase();
      const event = text.includes("stop") || text.includes("停") ? "stop" :
        (text.includes("forward") || text.includes("前")) ? "move_forward" :
        (text.includes("back") || text.includes("后")) ? "move_backward" :
        (text.includes("left") || text.includes("左")) ? "turn_left" :
        (text.includes("right") || text.includes("右")) ? "turn_right" : "thinking";
      const isMotion = ["move_forward", "move_backward", "turn_left", "turn_right"].includes(event);
      if (isMotion && !state.safety.motion_enabled) return jsonResponse({ ok: false, error: "motion disabled" }, 423);
      if (isMotion && state.safety.control_mode !== "ai") return jsonResponse({ ok: false, error: "ai mode required" }, 409);
      state.ui.expression = event === "stop" ? "idle" : (isMotion ? "moving" : "thinking");
      return jsonResponse({ ok: true, source: "preview", used_llm: false, event });
    }
    if (path === "/api/config/reset") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      Object.assign(state.wifi, { mode: "ap", sta_connected: false, sta_ip: "" });
      Object.assign(state.llm, { mode: "off", label: "关闭", base_url: "", model: "", configured: false, api_key_set: false });
      return jsonResponse({ ok: true, cleared: "network_llm", note: "preview reset" });
    }
    if (path === "/api/system/reboot") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      return jsonResponse({ ok: true, rebooting: true, note: "preview only" });
    }
    return jsonResponse({ ok: false, error: "not found" }, 404);
  };
})();
</script>
"""


def extract_embedded_html() -> str:
    source = ADMIN_SOURCE.read_text(encoding="utf-8")
    match = re.search(
        r"static\s+const\s+char\s+\*html\s*=(?P<body>.*?);\s*\n\s*httpd_resp_set_type",
        source,
        re.S,
    )
    if not match:
        raise RuntimeError(f"未找到嵌入式管理页 HTML: {ADMIN_SOURCE}")

    parts = re.findall(r'"((?:\\.|[^"\\])*)"', match.group("body"), re.S)
    html = "".join(ast.literal_eval(f'"{part}"') for part in parts)
    banner = (
        "<div style=\"position:sticky;top:0;z-index:10;padding:10px 18px;"
        "background:#16324a;color:#efe9df;border-bottom:1px solid #3fc9ff\">"
        "本地预览模式：配对码 <strong>123456</strong>，后端为 Mac mock API，"
        "不会真的控制底盘。</div>"
    )
    return html.replace("<body>", f"<body>{banner}", 1)


def make_standalone_html() -> str:
    html = extract_embedded_html()
    return html.replace("<script>", MOCK_FETCH_SCRIPT + "<script>", 1)


STATE = {
    "ui": {
        "page": "eyes",
        "expression": "idle",
        "motion": "stop",
        "moving": False,
        "last_ack": 0,
    },
    "wifi": {
        "mode": "ap",
        "sta_connected": False,
        "sta_ip": "",
        "ap_started": True,
        "ap_ip": "192.168.4.1",
        "ap_ssid": "AtlasRover-PREVIEW",
    },
    "llm": {
        "mode": "off",
        "label": "关闭",
        "provider": "openai_compatible",
        "base_url": "",
        "model": "",
        "configured": False,
        "api_key_set": False,
    },
    "safety": {
        "motion_enabled": True,
        "control_mode": "manual",
        "max_speed_percent": 40,
        "max_duration_ms": 700,
    },
}

EXPRESSIONS = {
    "idle",
    "happy",
    "listen",
    "thinking",
    "speaking",
    "moving",
    "curious",
    "sleepy",
    "surprised",
    "wink",
    "angry",
    "charging",
    "error",
}

PAGES = {"eyes", "clock", "status", "voice", "settings", "alarm", "photo", "music", "story"}


def llm_label(mode: str) -> str:
    return {
        "off": "关闭",
        "host": "电脑宿主 MiniClaw",
        "cloud": "云端大模型",
        "embedded": "端侧 MimiClaw",
    }.get(mode, "未知模式")


def status_payload() -> dict:
    return {
        "ok": True,
        "pairing_hint": "preview pin is 123456",
        **STATE,
    }


class PreviewHandler(BaseHTTPRequestHandler):
    html = ""

    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("[%s] %s\n" % (time.strftime("%H:%M:%S"), fmt % args))

    def send_text(self, body: str, content_type: str, status: HTTPStatus = HTTPStatus.OK) -> None:
        data = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def send_json(self, payload: dict, status: HTTPStatus = HTTPStatus.OK) -> None:
        self.send_text(json.dumps(payload, ensure_ascii=False), "application/json; charset=utf-8", status)

    def read_form(self) -> dict[str, str]:
        length = int(self.headers.get("Content-Length", "0") or "0")
        raw = self.rfile.read(length).decode("utf-8") if length else ""
        parsed = parse_qs(raw, keep_blank_values=True)
        return {key: values[-1] if values else "" for key, values in parsed.items()}

    def require_pin(self, form: dict[str, str]) -> bool:
        if form.get("pin") == PREVIEW_PIN:
            return True
        self.send_json({"ok": False, "error": "pairing required; preview pin is 123456"}, HTTPStatus.FORBIDDEN)
        return False

    def do_GET(self) -> None:
        if self.path == "/" or self.path == "/app" or self.path.startswith("/?"):
            self.send_text(self.html, "text/html; charset=utf-8")
            return
        if self.path == "/api/status":
            self.send_json(status_payload())
            return
        self.send_json({"ok": False, "error": "not found"}, HTTPStatus.NOT_FOUND)

    def do_POST(self) -> None:
        form = self.read_form()

        if self.path == "/api/rover/stop":
            STATE["ui"].update({"expression": "idle", "motion": "stop", "moving": False, "last_ack": 1})
            self.send_json({"ok": True, "action": "stop"})
            return

        if self.path == "/api/rover/move":
            if not self.require_pin(form):
                return
            if not STATE["safety"]["motion_enabled"]:
                self.send_json({"ok": False, "error": "motion disabled"}, HTTPStatus.LOCKED)
                return
            if STATE["safety"]["control_mode"] != "manual":
                self.send_json({"ok": False, "error": "manual mode required"}, HTTPStatus.CONFLICT)
                return
            direction = form.get("dir", "")
            speed = min(int(form.get("speed") or 30), STATE["safety"]["max_speed_percent"])
            duration = min(int(form.get("duration") or 500), STATE["safety"]["max_duration_ms"])
            motion = {"F": "forward", "B": "backward", "L": "turn_left", "R": "turn_right"}.get(direction)
            if motion is None:
                self.send_json({"ok": False, "error": "bad direction"}, HTTPStatus.BAD_REQUEST)
                return
            STATE["ui"].update({"expression": "moving", "motion": motion, "moving": True, "last_ack": 1})
            self.send_json({"ok": True, "action": "move", "speed": speed, "duration_ms": duration})
            return

        if self.path == "/api/config/wifi":
            if not self.require_pin(form):
                return
            ssid = form.get("ssid", "")
            if not ssid:
                self.send_json({"ok": False, "error": "ssid required"}, HTTPStatus.BAD_REQUEST)
                return
            STATE["wifi"].update({"mode": "sta", "sta_connected": True, "sta_ip": "192.168.1.88"})
            self.send_json({"ok": True, "saved": "wifi", "note": f"preview saved ssid {ssid}"})
            return

        if self.path == "/api/app/expression":
            if not self.require_pin(form):
                return
            expression = form.get("expression", "idle")
            if expression not in EXPRESSIONS:
                self.send_json({"ok": False, "error": "bad expression"}, HTTPStatus.BAD_REQUEST)
                return
            STATE["ui"].update({"page": "eyes", "expression": expression})
            self.send_json({"ok": True, "app": "expression"})
            return

        if self.path == "/api/app/page":
            if not self.require_pin(form):
                return
            page = form.get("page", "eyes")
            if page not in PAGES:
                self.send_json({"ok": False, "error": "bad page"}, HTTPStatus.BAD_REQUEST)
                return
            expression = "listen" if page == "voice" else "curious" if page in {"alarm", "photo"} else "idle"
            STATE["ui"].update({"page": page, "expression": expression})
            self.send_json({"ok": True, "app": "page"})
            return

        if self.path == "/api/app/action":
            if not self.require_pin(form):
                return
            action = form.get("action", "")
            mapping = {
                "music": ("music", "speaking"),
                "story": ("story", "speaking"),
                "chat": ("voice", "listen"),
                "alarm": ("alarm", "curious"),
            }
            if action not in mapping:
                self.send_json({"ok": False, "error": "bad action"}, HTTPStatus.BAD_REQUEST)
                return
            page, expression = mapping[action]
            STATE["ui"].update({"page": page, "expression": expression, "moving": False, "motion": "stop"})
            self.send_json({"ok": True, "app": "action", "note": "mimiclaw placeholder"})
            return

        if self.path == "/api/config/llm":
            if not self.require_pin(form):
                return
            mode = form.get("mode", "off")
            STATE["llm"].update(
                {
                    "mode": mode,
                    "label": llm_label(mode),
                    "provider": form.get("provider", "openai_compatible"),
                    "base_url": form.get("base_url", ""),
                    "model": form.get("model", ""),
                }
            )
            if form.get("api_key"):
                STATE["llm"]["api_key_set"] = True
            STATE["llm"]["configured"] = mode == "host" and bool(STATE["llm"]["base_url"]) or (
                mode in {"cloud", "embedded"} and bool(STATE["llm"]["base_url"]) and bool(STATE["llm"]["model"])
            )
            self.send_json({"ok": True, "saved": "llm"})
            return

        if self.path == "/api/config/safety":
            if not self.require_pin(form):
                return
            STATE["safety"].update(
                {
                    "motion_enabled": form.get("motion_enabled") in {"1", "true"},
                    "control_mode": form.get("control_mode") if form.get("control_mode") in {"manual", "ai"} else "manual",
                    "max_speed_percent": min(max(int(form.get("max_speed") or 40), 1), 80),
                    "max_duration_ms": min(max(int(form.get("max_duration") or 700), 100), 2000),
                }
            )
            self.send_json({"ok": True, "saved": "safety"})
            return

        if self.path == "/api/voice/text":
            if not self.require_pin(form):
                return
            text = form.get("text", "").lower()
            if "stop" in text or "停" in text:
                event = "stop"
            elif "forward" in text or "前" in text:
                event = "move_forward"
            elif "back" in text or "后" in text:
                event = "move_backward"
            elif "left" in text or "左" in text:
                event = "turn_left"
            elif "right" in text or "右" in text:
                event = "turn_right"
            else:
                event = "thinking"
            is_motion = event in {"move_forward", "move_backward", "turn_left", "turn_right"}
            if is_motion and not STATE["safety"]["motion_enabled"]:
                self.send_json({"ok": False, "error": "motion disabled"}, HTTPStatus.LOCKED)
                return
            if is_motion and STATE["safety"]["control_mode"] != "ai":
                self.send_json({"ok": False, "error": "ai mode required"}, HTTPStatus.CONFLICT)
                return
            STATE["ui"]["expression"] = "idle" if event == "stop" else "moving" if is_motion else "thinking"
            self.send_json({"ok": True, "source": "preview", "used_llm": False, "event": event})
            return

        if self.path == "/api/config/reset":
            if not self.require_pin(form):
                return
            STATE["wifi"].update({"mode": "ap", "sta_connected": False, "sta_ip": ""})
            STATE["llm"].update({"mode": "off", "label": "关闭", "base_url": "", "model": "", "configured": False, "api_key_set": False})
            self.send_json({"ok": True, "cleared": "network_llm", "note": "preview reset"})
            return

        if self.path == "/api/system/reboot":
            if not self.require_pin(form):
                return
            self.send_json({"ok": True, "rebooting": True, "note": "preview only"})
            return

        self.send_json({"ok": False, "error": "not found"}, HTTPStatus.NOT_FOUND)


def main() -> int:
    parser = argparse.ArgumentParser(description="Preview Atlas Rover DualEye admin page")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=8765, type=int)
    parser.add_argument("--export-html", type=Path, help="Write a standalone mock preview HTML file and exit")
    args = parser.parse_args()

    if args.export_html:
        args.export_html.write_text(make_standalone_html(), encoding="utf-8")
        print(f"已生成本地预览 HTML: {args.export_html}")
        return 0

    PreviewHandler.html = extract_embedded_html()
    with socketserver.TCPServer((args.host, args.port), PreviewHandler) as server:
        print(f"Atlas Rover Web 预览: http://{args.host}:{args.port}")
        print(f"预览配对码: {PREVIEW_PIN}")
        server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
