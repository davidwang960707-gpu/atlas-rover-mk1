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
    ui: {
      page: "eyes",
      expression: "idle",
      motion: "stop",
      moving: false,
      last_ack: 0,
      theme: "classic",
      brightness: 70,
      volume: 60,
      chat_text: "",
    },
    wifi: {
      mode: "ap",
      sta_connected: false,
      sta_ip: "",
      ap_started: true,
      ap_ip: "192.168.4.1",
      ap_ssid: "AtlasRover-PREVIEW"
    },
    llm: {
      mode: "off",
      label: "关闭",
      provider: "openai_compatible",
      base_url: "",
      model: "",
      configured: false,
      api_key_set: false,
    },
    safety: {
      motion_enabled: true,
      control_mode: "manual",
      max_speed_percent: 40,
      max_duration_ms: 700,
    },
    chat: {
      text: "",
    },
    calendar: {
      enabled: true,
      title: "电子宠物日历",
      note: "今天先来一句状态更新吧",
    },
    pomodoro: {
      enabled: true,
      running: false,
      in_break: false,
      task: "巡检任务",
      focus_minutes: 25,
      break_minutes: 5,
      progress_percent: 0,
      remaining_ms: 0,
    },
    pet: {
      label: "活跃模式",
      phase: "idle",
      mood: 90,
      energy: 82,
      curiosity: 76,
      asleep: false,
      asset_id: "default"
    },
    features: {
      eyes: true,
      clock_page: true,
      status_page: true,
      voice_ui: false,
      music_ui: true,
      story_ui: true,
      chat_ui: true,
      calendar_ui: true,
      pomodoro_ui: true,
      photo_ui: false,
      alarm_ui: false,
      pet: true,
    }
  };
  const runtime = {
    pomodoro: {
      start_ms: 0,
      interval_ms: 0,
    },
  };
  const labels = { off: "关闭", host: "Atlas Brain / Mac 桥接", cloud: "云端大模型", embedded: "端侧 Agent（后续）" };
  const expressions = new Set(["idle", "happy", "listen", "thinking", "speaking", "moving", "curious", "sleepy", "surprised", "wink", "love", "money", "angry", "charging", "error", "cry"]);
  const pages = new Set(["eyes", "clock", "status", "voice", "settings", "alarm", "photo", "music", "story", "chat", "calendar", "pomodoro"]);

  const clamp = (value, min, max, fallback) => {
    const n = Number(value);
    if (!Number.isFinite(n)) return fallback;
    return Math.max(min, Math.min(max, n));
  };

  const setPomodoroFromConfig = () => {
    if (!state.pomodoro.in_break) {
      runtime.interval_ms = state.pomodoro.focus_minutes * 60 * 1000;
    } else {
      runtime.interval_ms = state.pomodoro.break_minutes * 60 * 1000;
    }
    if (runtime.interval_ms <= 0) {
      runtime.interval_ms = state.pomodoro.focus_minutes > 0 ? state.pomodoro.focus_minutes * 60 * 1000 : 25 * 60 * 1000;
    }
  };

  const tickPomodoro = (now) => {
    if (!state.pomodoro.running) {
      state.pomodoro.progress_percent = 0;
      state.pomodoro.remaining_ms = 0;
      return;
    }

    if (runtime.interval_ms <= 0) {
      setPomodoroFromConfig();
    }
    if (runtime.interval_ms <= 0) {
      state.pomodoro.running = false;
      state.pomodoro.in_break = false;
      runtime.interval_ms = 0;
      runtime.start_ms = 0;
      state.pomodoro.progress_percent = 0;
      state.pomodoro.remaining_ms = 0;
      return;
    }

    if (runtime.start_ms === 0) {
      runtime.start_ms = now;
    }

    const elapsed = Math.max(0, now - runtime.start_ms);
    const remain = elapsed >= runtime.interval_ms ? 0 : runtime.interval_ms - elapsed;
    const percent = elapsed >= runtime.interval_ms ? 100 : Math.floor((elapsed * 100) / runtime.interval_ms);
    state.pomodoro.progress_percent = Math.min(100, Math.max(0, percent));
    state.pomodoro.remaining_ms = remain;

    if (elapsed < runtime.interval_ms) {
      return;
    }

    if (!state.pomodoro.in_break && state.pomodoro.break_minutes > 0) {
      state.pomodoro.in_break = true;
      runtime.start_ms = now;
      setPomodoroFromConfig();
      state.pomodoro.progress_percent = 0;
      state.pomodoro.remaining_ms = runtime.interval_ms;
      return;
    }

    state.pomodoro.running = false;
    state.pomodoro.in_break = false;
    runtime.start_ms = 0;
    runtime.interval_ms = 0;
    state.pomodoro.progress_percent = 0;
    state.pomodoro.remaining_ms = 0;
  };

  const resetPomodoro = (now) => {
    state.pomodoro.running = false;
    state.pomodoro.in_break = false;
    state.pomodoro.progress_percent = 0;
    state.pomodoro.remaining_ms = 0;
    runtime.start_ms = 0;
    runtime.interval_ms = 0;
    if (now) {
      state.pomodoro.last_ack_at = now;
    }
  };

  const startPomodoro = (payload, now) => {
    state.pomodoro.focus_minutes = clamp(payload.focus_minutes, 1, 120, state.pomodoro.focus_minutes || 25);
    state.pomodoro.break_minutes = clamp(payload.break_minutes, 1, 30, state.pomodoro.break_minutes || 5);
    if (payload.task_name) {
      state.pomodoro.task = payload.task_name;
    }
    state.pomodoro.running = true;
    runtime.start_ms = now;
    setPomodoroFromConfig();
    tickPomodoro(now);
  };

  const updatePetLabelFromEvent = (event) => {
    if (event === "touch") {
      state.pet.mood = Math.min(100, state.pet.mood + 5);
      state.pet.phase = "interact";
      state.pet.label = "开心模式";
    } else if (event === "play") {
      state.pet.curiosity = Math.min(100, state.pet.curiosity + 7);
      state.pet.phase = "play";
      state.pet.label = "玩耍中";
    } else if (event === "feed") {
      state.pet.energy = Math.min(100, state.pet.energy + 8);
      state.pet.phase = "happy";
      state.pet.label = "刚补能";
    } else if (event === "rest") {
      state.pet.asleep = true;
      state.pet.phase = "rest";
      state.pet.label = "休息中";
    } else if (event === "patrol") {
      state.pet.phase = "patrol";
      state.pet.label = "巡游中";
    } else if (["music", "story", "chat"].includes(event)) {
      state.pet.phase = "interaction";
      state.pet.label = `${event === "music" ? "听歌" : event === "story" ? "讲故事" : "对话"}状态`;
    }
  };

  const jsonResponse = (payload, status = 200) => new Response(JSON.stringify(payload), {
    status,
    headers: { "Content-Type": "application/json; charset=utf-8" }
  });
  const statusPayload = () => {
    tickPomodoro(Date.now());
    return {
      ok: true,
      pairing_hint: "preview pin is 123456",
      ...state,
      chat: { text: state.chat.text || "" },
      calendar: {
        enabled: state.calendar.enabled,
        title: state.calendar.title,
        note: state.calendar.note,
      },
      pomodoro: {
        enabled: state.pomodoro.enabled,
        running: state.pomodoro.running,
        in_break: state.pomodoro.in_break,
        task: state.pomodoro.task,
        focus_minutes: state.pomodoro.focus_minutes,
        break_minutes: state.pomodoro.break_minutes,
        progress_percent: state.pomodoro.progress_percent,
        remaining_ms: state.pomodoro.remaining_ms,
      },
    };
  };
  const formFrom = opts => new URLSearchParams((opts && opts.body) || "");
  const requirePin = form => form.get("pin") === previewPin
    ? null
    : { payload: { ok: false, error: "pairing required; preview pin is 123456" }, status: 403 };
  const pageSupport = page => ({
    eyes: true,
    clock: true,
    status: true,
    voice: false,
    music: true,
    story: true,
    chat: true,
    calendar: true,
    pomodoro: true,
    photo: false,
    alarm: false
  })[page] === true;
  const actionSupport = action => ({
    clock: true,
    "clock.show": true,
    music: true,
    story: true,
    chat: true,
    calendar: true,
    "calendar.show": true,
    pomodoro: true,
    "pomodoro.start": true,
    "pomodoro.stop": true,
    "calendar.add_reminder": false,
    alarm: false
  })[action] === true;

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
      if (!pageSupport(page)) return jsonResponse({ ok: false, error: "feature not available yet" }, 501);
      state.ui.page = page;
      if (page === "voice") {
        state.ui.expression = "listen";
      } else if (["clock", "alarm", "photo", "calendar", "pomodoro"].includes(page)) {
        state.ui.expression = "curious";
      } else if (["chat"].includes(page)) {
        state.ui.expression = "listen";
      } else {
        state.ui.expression = "idle";
      }
      return jsonResponse({ ok: true, app: "page" });
    }
    if (path === "/api/app/action") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      const action = form.get("action") || "";
  const map = {
        clock: ["clock", "curious"],
        "clock.show": ["clock", "curious"],
        music: ["music", "speaking"],
        story: ["story", "speaking"],
        chat: ["chat", "listen"],
        alarm: ["alarm", "curious"],
        calendar: ["calendar", "curious"],
        "calendar.show": ["calendar", "curious"],
        pomodoro: ["pomodoro", "curious"],
        "pomodoro.start": ["pomodoro", "speaking"],
        "pomodoro.stop": ["pomodoro", "curious"],
        photo: ["photo", "idle"],
      };
      if (!map[action]) return jsonResponse({ ok: false, error: "bad action" }, 400);
      if (!actionSupport(action)) return jsonResponse({ ok: false, error: "feature not available yet" }, 501);
      if (action === "chat") {
        const chatText = form.get("chat_text");
        if (chatText !== null) {
          state.chat.text = chatText;
          state.ui.chat_text = chatText;
        }
      } else if (action === "calendar" || action === "calendar.show") {
        const title = form.get("title");
        const note = form.get("note");
        if (title !== null) state.calendar.title = title;
        if (note !== null) state.calendar.note = note;
      } else if (action === "pomodoro" || action === "pomodoro.start" || action === "pomodoro.stop") {
        const payload = {
          task_name: form.get("task_name") || undefined,
          focus_minutes: clamp(form.get("focus_minutes"), 1, 120, state.pomodoro.focus_minutes),
          break_minutes: clamp(form.get("break_minutes"), 1, 30, state.pomodoro.break_minutes),
        };
        state.pomodoro.focus_minutes = payload.focus_minutes;
        state.pomodoro.break_minutes = payload.break_minutes;
        if (payload.task_name) state.pomodoro.task = payload.task_name;

        if (action === "pomodoro.start") {
          startPomodoro(payload, Date.now());
            state.ui.expression = "speaking";
            state.ui.moving = false;
            state.ui.motion = "stop";
            Object.assign(state.ui, { page: map.pomodoro[0], expression: map.pomodoro[1], moving: false, motion: "stop" });
            return jsonResponse({ ok: true, app: "action", state: { running: state.pomodoro.running, in_break: state.pomodoro.in_break } });
        }

        if (action === "pomodoro.stop") {
          resetPomodoro(Date.now());
          state.ui.expression = "curious";
          Object.assign(state.ui, { page: map.pomodoro[0], expression: "curious", moving: false, motion: "stop" });
          return jsonResponse({ ok: true, app: "action", state: { running: state.pomodoro.running, in_break: state.pomodoro.in_break } });
        }

        resetPomodoro(Date.now());
      }
      Object.assign(state.ui, { page: map[action][0], expression: map[action][1], moving: false, motion: "stop" });
      return jsonResponse({ ok: true, app: "action", note: "intent placeholder" });
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
    if (path === "/api/config/calendar") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      const title = form.get("title");
      const note = form.get("note");
      const enabledRaw = form.get("enabled");
      if (title !== null) {
        state.calendar.title = title;
      }
      if (note !== null) {
        state.calendar.note = note;
      }
      if (enabledRaw !== null) {
        state.calendar.enabled = ["1", "true", "yes", "on"].includes(enabledRaw.toLowerCase());
      }
      return jsonResponse({ ok: true, saved: "calendar" });
    }
    if (path === "/api/config/pomodoro") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      const taskName = form.get("task_name");
      const focusMinutes = clamp(form.get("focus_minutes"), 1, 120, state.pomodoro.focus_minutes);
      const breakMinutes = clamp(form.get("break_minutes"), 1, 30, state.pomodoro.break_minutes);
      const enabledRaw = form.get("enabled");
      if (taskName !== null) {
        state.pomodoro.task = taskName;
      }
      state.pomodoro.focus_minutes = focusMinutes;
      state.pomodoro.break_minutes = breakMinutes;
      if (enabledRaw !== null) {
        state.pomodoro.enabled = ["1", "true", "yes", "on"].includes(enabledRaw.toLowerCase());
      }
      setPomodoroFromConfig();
      if (!state.pomodoro.running) {
        state.pomodoro.remaining_ms = 0;
        state.pomodoro.progress_percent = 0;
      }
      return jsonResponse({ ok: true, saved: "pomodoro" });
    }
    if (path === "/api/pet/event") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      const event = form.get("event") || "";
      updatePetLabelFromEvent(event);
      return jsonResponse({
        ok: true,
        pet_event: event,
        phase: state.pet.phase,
        expression: state.ui.expression,
      });
    }
    if (path === "/api/intent" || path === "/api/brain/intent") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      const raw = form.get("intent") || "{}";
      let intent = {};
      try {
        intent = JSON.parse(raw);
      } catch (e) {
        return jsonResponse({ ok: false, error: "bad json intent" }, 400);
      }
      if (typeof intent.speech === "string" && intent.speech) {
        state.chat.text = intent.speech;
      }
      if (typeof intent.chat_text === "string" && intent.chat_text) {
        state.chat.text = intent.chat_text;
      }
      if (intent.chat && typeof intent.chat.text === "string") {
        state.chat.text = intent.chat.text;
      }
      if (typeof intent.calendar_title === "string") {
        state.calendar.title = intent.calendar_title;
      }
      if (typeof intent.calendar_note === "string") {
        state.calendar.note = intent.calendar_note;
      }
      if (typeof intent.pomodoro_task_name === "string") {
        state.pomodoro.task = intent.pomodoro_task_name;
      }
      if (typeof intent.pomodoro_focus_minutes === "number") {
        state.pomodoro.focus_minutes = clamp(intent.pomodoro_focus_minutes, 1, 120, state.pomodoro.focus_minutes);
      }
      if (typeof intent.pomodoro_break_minutes === "number") {
        state.pomodoro.break_minutes = clamp(intent.pomodoro_break_minutes, 1, 30, state.pomodoro.break_minutes);
      }
      if (typeof intent.pomodoro_running === "boolean") {
        if (intent.pomodoro_running) {
          startPomodoro({
            task_name: intent.pomodoro_task_name,
            focus_minutes: intent.pomodoro_focus_minutes,
            break_minutes: intent.pomodoro_break_minutes,
          }, Date.now());
        } else {
          resetPomodoro(Date.now());
        }
      }
      if (typeof intent.task_name === "string" && !intent.pomodoro_running && !intent.pomodoro_task_name) {
        state.pomodoro.task = intent.task_name;
      }
      const action = intent.action || "";
      if (["chat", "story", "music", "calendar", "calendar.show", "pomodoro", "pomodoro.start", "pomodoro.stop"].includes(action)) {
        if (action === "chat") {
          state.ui.page = "chat";
          state.ui.expression = "listen";
        } else if (action === "story" || action === "music") {
          state.ui.page = action;
          state.ui.expression = "speaking";
        } else if (action.startsWith("calendar") || action === "calendar") {
          state.ui.page = "calendar";
          state.ui.expression = "curious";
        } else if (action.startsWith("pomodoro")) {
          state.ui.page = "pomodoro";
          state.ui.expression = state.pomodoro.running ? "speaking" : "curious";
        }
      }
      return jsonResponse({ ok: true, note: "intent processed" });
    }
    if (path === "/api/config/reset") {
      const denied = requirePin(form);
      if (denied) return jsonResponse(denied.payload, denied.status);
      Object.assign(state.wifi, { mode: "ap", sta_connected: false, sta_ip: "" });
      Object.assign(state.llm, { mode: "off", label: "关闭", base_url: "", model: "", configured: false, api_key_set: false });
      state.ui.chat_text = "";
      state.chat.text = "";
      state.calendar.title = "电子宠物日历";
      state.calendar.note = "今天先来一句状态更新吧";
      state.pomodoro.task = "巡检任务";
      state.pomodoro.focus_minutes = 25;
      state.pomodoro.break_minutes = 5;
      state.pomodoro.running = false;
      state.pomodoro.in_break = false;
      state.pomodoro.progress_percent = 0;
      state.pomodoro.remaining_ms = 0;
      runtime.start_ms = 0;
      runtime.interval_ms = 0;
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


def extract_embedded_html(which: str = "app") -> str:
    source = ADMIN_SOURCE.read_text(encoding="utf-8")
    matches = re.finditer(
        r"static\s+const\s+char\s+\*html\s*=(?P<body>.*?);\s*\n\s*httpd_resp_set_type",
        source,
        re.S,
    )
    candidates = [m.group("body") for m in matches]
    if not candidates:
        raise RuntimeError(f"未找到嵌入式管理页 HTML: {ADMIN_SOURCE}")
    index = 0 if which == "app" else 1
    if index >= len(candidates):
        index = len(candidates) - 1
    body = candidates[index]

    parts = re.findall(r'"((?:\\.|[^"\\])*)"', body, re.S)
    html = "".join(ast.literal_eval(f'"{part}"') for part in parts)
    banner = (
        "<div style=\"position:sticky;top:0;z-index:10;padding:10px 18px;"
        "background:#16324a;color:#efe9df;border-bottom:1px solid #3fc9ff\">"
        "本地预览模式：配对码 <strong>123456</strong>，后端为 Mac mock API，"
        "不会真的控制底盘。</div>"
    )
    return html.replace("<body>", f"<body>{banner}", 1)


def make_standalone_html() -> str:
    html = extract_embedded_html("app")
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
    "pet": {
        "label": "活跃模式",
        "phase": "idle",
        "mood": 90,
        "energy": 82,
        "curiosity": 76,
        "asleep": False,
        "asset_id": "default",
    },
    "chat": {
        "text": "",
    },
    "calendar": {
        "enabled": True,
        "title": "电子宠物日历",
        "note": "今天先来一句状态更新吧",
    },
    "pomodoro": {
        "enabled": True,
        "running": False,
        "in_break": False,
        "task": "巡检任务",
        "focus_minutes": 25,
        "break_minutes": 5,
        "progress_percent": 0,
        "remaining_ms": 0,
        "interval_started_ms": 0,
        "progress_ms": 0,
    },
    "features": {
        "eyes": True,
        "clock_page": True,
        "status_page": True,
        "voice_ui": False,
        "music_ui": True,
        "story_ui": True,
        "chat_ui": True,
        "calendar_ui": True,
        "pomodoro_ui": True,
        "photo_ui": False,
        "alarm_ui": False,
        "pet": True,
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

PAGES = {"eyes", "clock", "status", "voice", "alarm", "photo", "music", "story", "chat", "calendar", "pomodoro"}
SUPPORTED_PAGES = {"eyes", "clock", "status", "music", "story", "chat", "calendar", "pomodoro"}
SUPPORTED_ACTIONS = {
    "clock",
    "clock.show",
    "music",
    "story",
    "chat",
    "calendar",
    "calendar.show",
    "pomodoro",
    "pomodoro.start",
    "pomodoro.stop",
}


def is_supported_page(page: str) -> bool:
    return page in SUPPORTED_PAGES


def is_supported_action(action: str) -> bool:
    return action in SUPPORTED_ACTIONS


def _safe_int(value: str, default: int, minimum: int | None = None, maximum: int | None = None) -> int:
    try:
        parsed = int(value)
    except Exception:
        return default
    if minimum is not None:
        parsed = max(minimum, parsed)
    if maximum is not None:
        parsed = min(maximum, parsed)
    return parsed


def _tick_pomodoro(now_ms: int | None = None) -> None:
    if now_ms is None:
        now_ms = int(time.time() * 1000)
    pomodoro = STATE["pomodoro"]
    if not pomodoro.get("running", False):
        pomodoro["remaining_ms"] = 0
        pomodoro["progress_percent"] = 0
        pomodoro["progress_ms"] = 0
        return

    focus_ms = _safe_int(str(pomodoro.get("focus_minutes", 25)), 25, 1, 120) * 60_000
    break_ms = _safe_int(str(pomodoro.get("break_minutes", 5)), 5, 0, 30) * 60_000
    in_break = bool(pomodoro.get("in_break", False))
    interval_ms = break_ms if in_break else focus_ms
    interval_ms = max(interval_ms, 1)
    started_ms = int(pomodoro.get("interval_started_ms", 0) or 0)
    if started_ms <= 0:
        started_ms = now_ms
        pomodoro["interval_started_ms"] = started_ms
    elapsed = max(0, now_ms - started_ms)
    progress_ms = min(elapsed, interval_ms)
    pomodoro["progress_ms"] = progress_ms
    pomodoro["progress_percent"] = 0 if interval_ms <= 0 else min(100, int((progress_ms * 100) / interval_ms))
    remaining_ms = max(0, interval_ms - progress_ms)
    pomodoro["remaining_ms"] = remaining_ms

    if remaining_ms > 0:
        return

    if in_break:
        pomodoro["running"] = False
        pomodoro["in_break"] = False
        pomodoro["interval_started_ms"] = 0
        return

    if _safe_int(str(pomodoro.get("break_minutes", 5)), 5, 1, 30) > 0:
        pomodoro["in_break"] = True
        pomodoro["interval_started_ms"] = now_ms
        pomodoro["progress_ms"] = 0
        pomodoro["progress_percent"] = 0
        pomodoro["remaining_ms"] = break_ms
        return

    pomodoro["running"] = False
    pomodoro["in_break"] = False
    pomodoro["interval_started_ms"] = 0
    pomodoro["progress_ms"] = 0
    pomodoro["progress_percent"] = 0
    pomodoro["remaining_ms"] = 0


def llm_label(mode: str) -> str:
    return {
        "off": "关闭",
        "host": "外部宿主/调试桥",
        "cloud": "云端大模型",
        "embedded": "端侧 Agent（后续）",
    }.get(mode, "未知模式")


def status_payload() -> dict:
    _tick_pomodoro()
    return {
        "ok": True,
        "pairing_hint": "preview pin is 123456",
        **STATE,
        "chat": {
            "text": STATE["chat"]["text"],
        },
        "calendar": {
            "enabled": STATE["calendar"]["enabled"],
            "title": STATE["calendar"]["title"],
            "note": STATE["calendar"]["note"],
        },
        "pomodoro": {
            "enabled": STATE["pomodoro"]["enabled"],
            "running": STATE["pomodoro"]["running"],
            "in_break": STATE["pomodoro"]["in_break"],
            "task": STATE["pomodoro"]["task"],
            "focus_minutes": STATE["pomodoro"]["focus_minutes"],
            "break_minutes": STATE["pomodoro"]["break_minutes"],
            "progress_percent": STATE["pomodoro"]["progress_percent"],
            "remaining_ms": STATE["pomodoro"]["remaining_ms"],
        },
    }


class PreviewHandler(BaseHTTPRequestHandler):
    app_html = ""
    admin_html = ""
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
        if self.path.startswith("/?"):
            self.send_text(self.app_html, "text/html; charset=utf-8")
            return
        if self.path in ("/", "/app"):
            self.send_text(self.app_html, "text/html; charset=utf-8")
            return
        if self.path == "/admin":
            self.send_text(self.admin_html, "text/html; charset=utf-8")
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
            if not is_supported_page(page):
                self.send_json({"ok": False, "error": "feature not available yet"}, HTTPStatus.NOT_IMPLEMENTED)
                return
            expression = "listen" if page == "voice" else "curious" if page in {"clock", "alarm", "photo", "calendar", "pomodoro"} else "idle"
            STATE["ui"].update({"page": page, "expression": expression})
            self.send_json({"ok": True, "app": "page"})
            return

        if self.path == "/api/app/action":
            if not self.require_pin(form):
                return
            action = form.get("action", "")
            if not is_supported_action(action):
                self.send_json({"ok": False, "error": "feature not available yet"}, HTTPStatus.NOT_IMPLEMENTED)
                return
            mapping = {
                "clock": ("clock", "curious"),
                "clock.show": ("clock", "curious"),
                "music": ("music", "speaking"),
                "story": ("story", "speaking"),
                "chat": ("voice", "listen"),
                "alarm": ("alarm", "curious"),
                "calendar": ("calendar", "curious"),
                "calendar.show": ("calendar", "curious"),
                "pomodoro": ("pomodoro", "curious"),
                "pomodoro.start": ("pomodoro", "speaking"),
                "pomodoro.stop": ("pomodoro", "curious"),
            }
            if action not in mapping:
                self.send_json({"ok": False, "error": "bad action"}, HTTPStatus.BAD_REQUEST)
                return

            if action in {"pomodoro", "pomodoro.start", "pomodoro.stop"}:
                task_name = form.get("task_name")
                focus_minutes = _safe_int(form.get("focus_minutes", str(STATE["pomodoro"]["focus_minutes"])),
                                          STATE["pomodoro"]["focus_minutes"], 1, 120)
                break_minutes = _safe_int(form.get("break_minutes", str(STATE["pomodoro"]["break_minutes"])),
                                          STATE["pomodoro"]["break_minutes"], 1, 30)
                STATE["pomodoro"]["focus_minutes"] = focus_minutes
                STATE["pomodoro"]["break_minutes"] = break_minutes
                if task_name:
                    STATE["pomodoro"]["task"] = task_name
                now_ms = int(time.time() * 1000)
                if action == "pomodoro.start":
                    STATE["pomodoro"]["running"] = True
                    STATE["pomodoro"]["in_break"] = False
                    STATE["pomodoro"]["interval_started_ms"] = now_ms
                    STATE["pomodoro"]["progress_ms"] = 0
                    STATE["pomodoro"]["progress_percent"] = 0
                    STATE["pomodoro"]["remaining_ms"] = focus_minutes * 60_000
                    _tick_pomodoro(now_ms)
                elif action == "pomodoro.stop":
                    STATE["pomodoro"]["running"] = False
                    STATE["pomodoro"]["in_break"] = False
                    STATE["pomodoro"]["interval_started_ms"] = 0
                    STATE["pomodoro"]["progress_ms"] = 0
                    STATE["pomodoro"]["progress_percent"] = 0
                    STATE["pomodoro"]["remaining_ms"] = 0

            page, expression = mapping[action]
            STATE["ui"].update({"page": page, "expression": expression, "moving": False, "motion": "stop"})
            self.send_json({"ok": True, "app": "action", "note": "intent placeholder"})
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

    PreviewHandler.app_html = extract_embedded_html("app")
    PreviewHandler.admin_html = extract_embedded_html("admin")
    PreviewHandler.html = PreviewHandler.app_html
    with socketserver.TCPServer((args.host, args.port), PreviewHandler) as server:
        print(f"Atlas Rover Web 预览: http://{args.host}:{args.port}")
        print(f"预览配对码: {PREVIEW_PIN}")
        server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
