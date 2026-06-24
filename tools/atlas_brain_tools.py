"""Tool registry and built-in Atlas Brain tools.

This module is intentionally independent from ``atlas_brain_server``.  It only
expects a bridge-like object with the small set of methods used by handlers, so
the main server can keep its public routes stable while the tool system becomes
reusable.
"""

from __future__ import annotations

import time
from typing import Any, Callable, Optional

from atlas_brain_providers import chat_choice_text, openai_chat_completion


ROLE_PROFILES: dict[str, dict[str, str]] = {
    "pet": {
        "label": "电子宠物",
        "theme": "pet",
        "chat_mode": "pet_head",
        "expression": "happy",
        "page": "chat",
        "tts_style": "playful",
        "prompt": "你是 Atlas Rover 的电子宠物人格。短句、活泼、亲近，像一个有心情的小桌面机器人。",
        "reply": "好呀，我切到电子宠物状态啦。",
    },
    "raptor": {
        "label": "猛禽",
        "theme": "raptor",
        "chat_mode": "eyes_only",
        "expression": "curious",
        "page": "eyes",
        "tts_style": "default",
        "prompt": "你是 Atlas Rover 的猛禽警戒人格。回复短、冷静、有一点压迫感，但不要越权控制底盘。",
        "reply": "猛禽模式已就绪。",
    },
    "mecha": {
        "label": "机械电子",
        "theme": "mecha",
        "chat_mode": "eyes_only",
        "expression": "thinking",
        "page": "status",
        "tts_style": "default",
        "prompt": "你是 Atlas Rover 的机械电子人格。清晰、克制，优先报告状态、诊断和执行结果。",
        "reply": "机械电子模式已启用。",
    },
    "goggle": {
        "label": "黄色护目镜",
        "theme": "goggle",
        "chat_mode": "eyes_only",
        "expression": "happy",
        "page": "eyes",
        "tts_style": "sweet",
        "prompt": "你是 Atlas Rover 的黄色护目镜人格。轻松、明亮、适合日常陪伴和音乐故事。",
        "reply": "护目镜模式来啦。",
    },
}

ROLE_ALIASES = {
    "pet": "pet",
    "电子宠物": "pet",
    "宠物": "pet",
    "巡游": "pet",
    "raptor": "raptor",
    "猛禽": "raptor",
    "野兽": "raptor",
    "mecha": "mecha",
    "机械": "mecha",
    "机械电子": "mecha",
    "电子眼": "mecha",
    "goggle": "goggle",
    "护目镜": "goggle",
    "小黄人": "goggle",
    "黄色": "goggle",
}

THEME_ALIASES = {
    **ROLE_ALIASES,
    "蓝眼": "classic",
    "经典": "classic",
    "classic": "classic",
    "琥珀": "amber",
    "amber": "amber",
    "薄荷": "mint",
    "薄荷友好": "mint",
    "mint": "mint",
    "警戒": "alert",
    "警报": "alert",
    "红色警戒": "alert",
    "alert": "alert",
    "夜间": "night",
    "夜航": "night",
    "低亮": "night",
    "night": "night",
    "蓝色瞳孔": "blue_pupil",
    "blue_pupil": "blue_pupil",
    "禁烟": "no_smoking",
    "电子烟": "no_smoking",
    "no_smoking": "no_smoking",
    "旋纹": "tomoe_spin",
    "tomoe": "tomoe_spin",
    "tomoe_spin": "tomoe_spin",
}


def pet_state_from_expression(expression: str) -> str:
    expr = str(expression or "").strip()
    return {
        "idle": "idle",
        "blink": "blink",
        "listen": "listen",
        "speaking": "speak",
        "thinking": "think",
        "curious": "think",
        "happy": "happy",
        "love": "happy",
        "cry": "cry",
        "sleepy": "sleepy",
        "wink": "sleepy",
        "surprised": "surprised",
        "angry": "think",
        "error": "offline",
        "charging": "idle",
        "money": "happy",
    }.get(expr, "idle")


def clamp_int(value: int, minimum: int, maximum: int) -> int:
    return max(minimum, min(maximum, value))


def _device_result_ok(results: list[dict[str, Any]]) -> bool:
    return all(isinstance(item, dict) and bool(item.get("ok")) for item in results)


def _send_chat_to_dualeye(bridge: Any, reply: str, page: str = "chat") -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    intents = [
        {
            "tool": "atlas_chat",
            "input": {
                "chat_text": reply[:150],
                "speech": reply[:150],
                "action": "chat",
            },
        },
        {"tool": "atlas_show_page", "input": {"page": page}},
    ]
    return intents, bridge.send_intents(intents)


def schema_object(properties: dict[str, dict[str, Any]],
                  required: Optional[list[str]] = None,
                  additional: bool = False) -> dict[str, Any]:
    return {
        "type": "object",
        "properties": properties,
        "required": required or [],
        "additionalProperties": additional,
    }


def enum_schema(values: list[str], description: str = "") -> dict[str, Any]:
    schema: dict[str, Any] = {"type": "string", "enum": values}
    if description:
        schema["description"] = description
    return schema


class SkillRegistry:
    def __init__(self, bridge: Any) -> None:
        self.bridge = bridge
        self._skills: dict[str, dict[str, Any]] = {}

    def register(self,
                 name: str,
                 title: str,
                 description: str,
                 handler: Callable[[dict[str, Any]], dict[str, Any]],
                 risk: str = "low",
                 args: Optional[dict[str, str]] = None,
                 input_schema: Optional[dict[str, Any]] = None,
                 target: str = "device_ui",
                 confirm_required: bool = False,
                 output_schema: Optional[dict[str, Any]] = None) -> None:
        arg_hints = args or {}
        if input_schema is None:
            input_schema = {
                "type": "object",
                "properties": {
                    key: {"type": "string", "description": desc}
                    for key, desc in arg_hints.items()
                },
                "additionalProperties": True,
            }
        self._skills[name] = {
            "schema_version": "atlas.tool_schema.v0",
            "name": name,
            "title": title,
            "description": description,
            "risk": risk,
            "target": target,
            "confirm_required": confirm_required,
            "args": arg_hints,
            "input_schema": input_schema,
            "output_schema": output_schema or {
                "type": "object",
                "properties": {
                    "ok": {"type": "boolean"},
                    "reply": {"type": "string"},
                    "error": {"type": "string"},
                    "results": {"type": "array"},
                },
                "additionalProperties": True,
            },
            "handler": handler,
        }

    def list_public(self) -> list[dict[str, Any]]:
        items = []
        for item in self._skills.values():
            public = dict(item)
            public.pop("handler", None)
            items.append(public)
        return sorted(items, key=lambda value: value["name"])

    def public_item(self, name: str) -> Optional[dict[str, Any]]:
        item = self._skills.get(name)
        if item is None:
            return None
        public = dict(item)
        public.pop("handler", None)
        return public

    def tools_list(self) -> list[dict[str, Any]]:
        tools = []
        for item in self.list_public():
            tools.append({
                "name": item["name"],
                "description": item["description"],
                "inputSchema": item.get("input_schema", {"type": "object"}),
                "outputSchema": item.get("output_schema", {"type": "object"}),
                "annotations": {
                    "title": item.get("title", item["name"]),
                    "risk": item.get("risk", "low"),
                    "target": item.get("target", "device_ui"),
                    "confirm_required": bool(item.get("confirm_required", False)),
                },
            })
        return tools

    def tool_schema_payload(self) -> dict[str, Any]:
        tools = self.tools_list()
        return {
            "ok": True,
            "protocol": "atlas.tools.v0.desk_apps",
            "mcp_like": True,
            "tool_count": len(tools),
            "tools": tools,
            "legacy_skills": self.list_public(),
            "call_endpoint": "/api/tools/call",
            "list_endpoint": "/api/tools/list",
            "notes": "Tool Schema V0 is MCP-like but intentionally local and small; desk apps expose clock/calendar/pomodoro as first-class tools; legacy /skill remains compatible.",
        }

    def execute(self, name: str, args: Optional[dict[str, Any]] = None) -> dict[str, Any]:
        skill = self._skills.get(name)
        if skill is None:
            return {"ok": False, "error": f"unknown skill: {name}", "skill": name}
        payload = args if isinstance(args, dict) else {}
        started = time.time()
        try:
            result = skill["handler"](payload)
        except Exception as exc:
            result = {"ok": False, "error": str(exc)}
        result.setdefault("skill", name)
        result.setdefault("risk", skill.get("risk", "low"))
        result["elapsed_ms"] = int((time.time() - started) * 1000)
        self.bridge.session.last_skill = {
            "name": name,
            "ok": bool(result.get("ok")),
            "elapsed_ms": result["elapsed_ms"],
            "error": str(result.get("error", "")),
        }
        return result


def build_skill_registry(bridge: Any,
                         *,
                         build_ota_manifest: Callable[[], dict[str, Any]],
                         rover_skills_enabled: bool = False) -> SkillRegistry:
    registry = SkillRegistry(bridge)
    page_schema = enum_schema(["eyes", "clock", "status", "voice", "music", "story", "chat", "calendar", "pomodoro", "photo"], "DualEye page id")
    expression_schema = enum_schema(["idle", "blink", "listen", "thinking", "speaking", "happy", "curious", "sleepy", "surprised", "wink", "love", "money", "angry", "charging", "error", "cry"], "DualEye expression id")
    theme_schema = enum_schema(["pet", "raptor", "mecha", "goggle", "blue_pupil", "tomoe_spin", "no_smoking"], "DualEye theme id")
    chat_mode_schema = enum_schema(["pet_head", "text", "eyes_only"], "DualEye chat display mode")
    pet_state_schema = enum_schema(["idle", "blink", "listen", "speak", "sing", "happy", "laugh", "cry", "sleepy", "think", "surprised", "offline"], "pet_head state/animation id")
    role_schema = enum_schema(sorted(ROLE_PROFILES.keys()), "Atlas role profile")

    def show_page(args: dict[str, Any]) -> dict[str, Any]:
        page = str(args.get("page", "")).strip()
        if not page:
            return {"ok": False, "error": "page required"}
        intents = [{"tool": "atlas_show_page", "input": {"page": page}}]
        results = bridge.send_intents(intents)
        return {
            "ok": _device_result_ok(results),
            "reply": f"已切到 {page} 页面。",
            "intents": intents,
            "results": results,
        }

    def set_expression(args: dict[str, Any]) -> dict[str, Any]:
        expression = str(args.get("expression", "")).strip()
        if not expression:
            return {"ok": False, "error": "expression required"}
        intents = [{"tool": "atlas_set_expression", "input": {"expression": expression}}]
        results = bridge.send_intents(intents)
        if _device_result_ok(results):
            bridge.session.current_expression = expression
            bridge.session.current_pet_state = pet_state_from_expression(expression)
            bridge.session.current_pet_animation = ""
        return {
            "ok": _device_result_ok(results),
            "reply": f"表情已切到 {expression}。",
            "intents": intents,
            "results": results,
        }

    def set_theme(args: dict[str, Any]) -> dict[str, Any]:
        theme = str(args.get("theme", "")).strip()
        theme = THEME_ALIASES.get(theme, theme)
        if not theme:
            return {"ok": False, "error": "theme required"}
        result = bridge.set_dualeye_theme(theme)
        return {
            "ok": bool(result.get("ok")),
            "reply": f"主题已切到 {theme}。" if result.get("ok") else f"主题切换失败：{result.get('error', 'unknown')}",
            "results": [result],
            "theme": theme,
        }

    def set_chat_mode(args: dict[str, Any]) -> dict[str, Any]:
        mode = str(args.get("mode") or args.get("chat_mode") or "").strip()
        if mode not in {"pet_head", "text", "eyes_only"}:
            return {"ok": False, "error": "mode must be pet_head/text/eyes_only"}
        config_result = bridge.set_dualeye_chat_mode(mode)
        intents = [{"tool": "atlas.ui.set_chat_mode", "input": {"mode": mode}}]
        results = [config_result] + bridge.send_intents(intents)
        label = {"pet_head": "土拨鼠头 + 文字", "text": "双屏文字", "eyes_only": "纯眼睛表情"}[mode]
        return {
            "ok": _device_result_ok(results),
            "reply": f"对话界面已切到{label}。",
            "chat_mode": mode,
            "intents": intents,
            "results": results,
        }

    def pet_set_state(args: dict[str, Any]) -> dict[str, Any]:
        state = str(args.get("state") or "idle").strip()
        text = str(args.get("right_text") or args.get("text") or "").strip()
        if state not in {"idle", "blink", "listen", "speak", "sing", "happy", "laugh", "cry", "sleepy", "think", "surprised", "offline"}:
            return {"ok": False, "error": "unknown pet state"}
        config_result = bridge.set_dualeye_chat_mode("pet_head")
        intent = {"tool": "atlas.pet.set_state", "input": {"state": state}}
        if text:
            intent["input"]["right_text"] = text[:90]
        results = [config_result] + bridge.send_intents([intent])
        if _device_result_ok(results):
            bridge.session.current_pet_state = state
            bridge.session.current_pet_animation = ""
            bridge.session.current_pet_view = "yaw_c" if state not in {"listen", "think"} else "yaw_l15/yaw_r15"
        return {
            "ok": _device_result_ok(results),
            "reply": f"土拨鼠状态已切到 {state}。",
            "state": state,
            "intents": [intent],
            "results": results,
        }

    def pet_play_animation(args: dict[str, Any]) -> dict[str, Any]:
        animation = str(args.get("animation") or "speak").strip()
        text = str(args.get("right_text") or args.get("text") or "").strip()
        if animation not in {"blink", "speak", "sing", "laugh"}:
            return {"ok": False, "error": "animation must be blink/speak/sing/laugh"}
        config_result = bridge.set_dualeye_chat_mode("pet_head")
        intent = {"tool": "atlas.pet.play_animation", "input": {"animation": animation}}
        if text:
            intent["input"]["right_text"] = text[:90]
        results = [config_result] + bridge.send_intents([intent])
        if _device_result_ok(results):
            bridge.session.current_pet_animation = animation
            bridge.session.current_pet_state = {"blink": "idle", "speak": "speak", "sing": "sing", "laugh": "laugh"}.get(animation, "idle")
            bridge.session.current_pet_view = "yaw_c"
        return {
            "ok": _device_result_ok(results),
            "reply": f"土拨鼠动画已播放：{animation}。",
            "animation": animation,
            "intents": [intent],
            "results": results,
        }

    def switch_role(args: dict[str, Any]) -> dict[str, Any]:
        role = str(args.get("role", "")).strip()
        role = ROLE_ALIASES.get(role, role)
        if role not in ROLE_PROFILES:
            return {"ok": False, "error": f"unknown role: {role or 'empty'}", "roles": list(ROLE_PROFILES)}
        profile = bridge.session.switch_role(role)
        theme_result = bridge.set_dualeye_theme(profile["theme"], chat_mode=profile.get("chat_mode", "pet_head"))
        intents = [
            {"tool": "atlas.ui.set_chat_mode", "input": {"mode": profile.get("chat_mode", "pet_head")}},
            {"tool": "atlas_set_expression", "input": {"expression": profile["expression"]}},
            {"tool": "atlas_show_page", "input": {"page": profile["page"]}},
        ]
        results = [theme_result] + bridge.send_intents(intents)
        if _device_result_ok(results):
            bridge.session.current_pet_state = pet_state_from_expression(profile["expression"])
            bridge.session.current_pet_animation = ""
        return {
            "ok": _device_result_ok(results),
            "reply": profile["reply"],
            "role": role,
            "profile": {key: value for key, value in profile.items() if key != "prompt"},
            "intents": intents,
            "results": results,
        }

    def device_app_status(app: str) -> dict[str, Any]:
        if bridge.dry_run:
            return {
                "ok": True,
                "dry_run": True,
                "app": app,
                "status": {},
                "ui": {},
                "apps": {},
            }
        status = bridge.status()
        apps = status.get("apps", {})
        if not isinstance(apps, dict):
            apps = {}
        app_status = apps.get(app, {})
        if not isinstance(app_status, dict):
            app_status = {}
        return {
            "ok": bool(status.get("ok", True)),
            "app": app,
            "status": app_status,
            "ui": status.get("ui", {}),
            "apps": apps,
        }

    def app_action_post(action: str, values: Optional[dict[str, Any]] = None) -> dict[str, Any]:
        form = {"action": action}
        for key, value in (values or {}).items():
            if value is None:
                continue
            form[key] = str(value)
        return bridge.post_dualeye_form("/api/app/action", form)

    def clock_show(args: dict[str, Any]) -> dict[str, Any]:
        result = app_action_post("clock.show")
        status = device_app_status("clock") if result.get("ok") else {}
        return {
            "ok": bool(result.get("ok")),
            "reply": "时钟页面已打开。",
            "results": [result],
            **status,
        }

    def clock_sync(args: dict[str, Any]) -> dict[str, Any]:
        epoch_ms = int(args.get("epoch_ms") or time.time() * 1000)
        result = app_action_post("clock.sync", {"epoch_ms": epoch_ms})
        status = device_app_status("clock") if result.get("ok") else {}
        return {
            "ok": bool(result.get("ok")),
            "reply": "时钟已按 Mac 时间校准。" if result.get("ok") else f"时钟校准失败：{result.get('error', 'unknown')}",
            "epoch_ms": epoch_ms,
            "results": [result],
            **status,
        }

    def clock_status(args: dict[str, Any]) -> dict[str, Any]:
        status = device_app_status("clock")
        return {
            **status,
            "reply": "时钟状态已读取。",
        }

    def pomodoro_show(args: dict[str, Any]) -> dict[str, Any]:
        result = app_action_post("pomodoro.show")
        status = device_app_status("pomodoro") if result.get("ok") else {}
        return {
            "ok": bool(result.get("ok")),
            "reply": "番茄专注页面已打开。",
            "results": [result],
            **status,
        }

    def pomodoro_start(args: dict[str, Any]) -> dict[str, Any]:
        focus_minutes = clamp_int(int(args.get("focus_minutes") or args.get("minutes") or 25), 1, 120)
        break_minutes = clamp_int(int(args.get("break_minutes") or 5), 1, 30)
        task = str(args.get("task_name") or args.get("task") or "巡检任务").strip() or "巡检任务"
        result = app_action_post("pomodoro.start", {
            "task_name": task,
            "focus_minutes": focus_minutes,
            "break_minutes": break_minutes,
        })
        status = device_app_status("pomodoro") if result.get("ok") else {}
        return {
            "ok": bool(result.get("ok")),
            "reply": f"开始 {focus_minutes} 分钟番茄，任务是{task}。",
            "results": [result],
            **status,
        }

    def pomodoro_stop(args: dict[str, Any]) -> dict[str, Any]:
        result = app_action_post("pomodoro.stop")
        status = device_app_status("pomodoro") if result.get("ok") else {}
        return {
            "ok": bool(result.get("ok")),
            "reply": "番茄专注已停止。",
            "results": [result],
            **status,
        }

    def pomodoro_status(args: dict[str, Any]) -> dict[str, Any]:
        status = device_app_status("pomodoro")
        return {
            **status,
            "reply": "番茄状态已读取。",
        }

    def calendar_show(args: dict[str, Any]) -> dict[str, Any]:
        result = app_action_post("calendar.show")
        status = device_app_status("calendar") if result.get("ok") else {}
        return {
            "ok": bool(result.get("ok")),
            "reply": "日历页面已打开。",
            "results": [result],
            **status,
        }

    def calendar_set_note(args: dict[str, Any]) -> dict[str, Any]:
        title = str(args.get("title") or "今日事项").strip()
        note = str(args.get("note") or args.get("content") or "").strip()
        if not note:
            return {"ok": False, "error": "note required"}
        result = app_action_post("calendar.set_note", {"title": title, "note": note})
        status = device_app_status("calendar") if result.get("ok") else {}
        return {
            "ok": bool(result.get("ok")),
            "reply": note[:90],
            "results": [result],
            **status,
        }

    def calendar_today(args: dict[str, Any]) -> dict[str, Any]:
        title = str(args.get("title") or "今日日历").strip()
        note = str(args.get("note") or "").strip()
        if not note:
            note = time.strftime("今天是 %Y-%m-%d，%A。")
            weekday_map = {
                "Monday": "星期一",
                "Tuesday": "星期二",
                "Wednesday": "星期三",
                "Thursday": "星期四",
                "Friday": "星期五",
                "Saturday": "星期六",
                "Sunday": "星期日",
            }
            for en, zh in weekday_map.items():
                note = note.replace(en, zh)
        result = app_action_post("calendar.today", {"title": title, "note": note})
        status = device_app_status("calendar") if result.get("ok") else {}
        return {
            "ok": bool(result.get("ok")),
            "reply": note,
            "results": [result],
            **status,
        }

    def weather_query(args: dict[str, Any]) -> dict[str, Any]:
        location = str(args.get("location") or "").strip()
        weather = bridge.query_weather(location)
        if weather.get("ok"):
            reply = str(weather.get("summary", "天气查询完成。"))
            intents = [
                {"tool": "atlas.calendar.set_note", "input": {"title": "天气", "note": reply[:120]}},
            ]
            results = bridge.send_intents(intents)
            return {
                "ok": _device_result_ok(results),
                "reply": reply,
                "weather": weather,
                "intents": intents,
                "results": results,
            }
        reply = f"天气查询失败：{weather.get('error', '未配置或网络不可用')}"
        intents, results = _send_chat_to_dualeye(bridge, reply)
        return {
            "ok": False,
            "reply": reply,
            "weather": weather,
            "intents": intents,
            "results": results,
        }

    def web_search(args: dict[str, Any]) -> dict[str, Any]:
        query = str(args.get("query") or "").strip()
        max_results = int(args.get("max_results") or 5)
        search = bridge.web_search(query, max_results=max_results)
        if search.get("ok"):
            answer = str(search.get("answer", "")).strip()
            result_lines = []
            for item in search.get("results", [])[:3]:
                if isinstance(item, dict):
                    result_lines.append(f"- {item.get('title', '')}: {item.get('content', '')}")
            raw_context = (answer + "\n" + "\n".join(result_lines)).strip()
            reply = answer or (result_lines[0].lstrip("- ").strip() if result_lines else "我找到了一些结果，但摘要为空。")
            if bridge.llm_enabled() and raw_context:
                try:
                    llm = openai_chat_completion(
                        bridge.llm_base_url,
                        bridge.llm_api_key,
                        bridge.llm_model,
                        [
                            {"role": "system", "content": "你是 Atlas Rover。请基于联网搜索结果，用不超过80字中文回答，说明这是搜索摘要。"},
                            {"role": "user", "content": f"问题：{query}\n搜索结果：\n{raw_context[:1600]}"},
                        ],
                        max_tokens=260,
                    )
                    llm_reply = chat_choice_text(llm).strip()
                    if llm_reply:
                        reply = llm_reply[:180]
                except Exception as exc:
                    search["summary_error"] = str(exc)
            intents, results = _send_chat_to_dualeye(bridge, reply)
            return {
                "ok": _device_result_ok(results),
                "reply": reply,
                "search": search,
                "intents": intents,
                "results": results,
            }
        reply = str(search.get("error", "联网搜索不可用"))
        intents, results = _send_chat_to_dualeye(bridge, reply)
        return {
            "ok": False,
            "reply": reply,
            "search": search,
            "intents": intents,
            "results": results,
        }

    def app_action(action: str, reply: str) -> Callable[[dict[str, Any]], dict[str, Any]]:
        def handler(args: dict[str, Any]) -> dict[str, Any]:
            intents = [{"tool": "atlas_app_action", "input": {"action": action}}]
            if action in {"music", "story", "chat"}:
                intents.append({"tool": "atlas_show_page", "input": {"page": action if action != "chat" else "chat"}})
            results = bridge.send_intents(intents)
            return {"ok": _device_result_ok(results), "reply": reply, "intents": intents, "results": results}
        return handler

    def rover_stop(args: dict[str, Any]) -> dict[str, Any]:
        intents = [{"tool": "atlas_rover_stop", "input": {}}]
        results = bridge.send_intents(intents)
        return {"ok": _device_result_ok(results), "reply": "已发送停止。", "intents": intents, "results": results}

    def rover_move(args: dict[str, Any]) -> dict[str, Any]:
        direction = str(args.get("direction") or "").strip()
        if direction not in {"forward", "backward", "left", "right"}:
            return {"ok": False, "error": "direction must be forward/backward/left/right"}
        speed = clamp_int(int(args.get("speed") or bridge.speed), 1, 80)
        duration_ms = clamp_int(int(args.get("duration_ms") or bridge.duration_ms), 100, 2000)
        intents = [{"tool": "atlas_rover_move", "input": {"direction": direction, "speed": speed, "duration_ms": duration_ms}}]
        results = bridge.send_intents(intents)
        return {"ok": _device_result_ok(results), "reply": f"移动指令已发送：{direction}。", "intents": intents, "results": results}

    registry.register("atlas.show_page", "切换页面", "切换 DualEye 功能页面。", show_page,
                      args={"page": "eyes/clock/status/chat/calendar/pomodoro 等"},
                      input_schema=schema_object({"page": page_schema}, ["page"]))
    registry.register("atlas.set_expression", "切换表情", "切换 DualEye 表情。", set_expression,
                      args={"expression": "happy/listen/sleepy/cry/love 等"},
                      input_schema=schema_object({"expression": expression_schema}, ["expression"]))
    registry.register("atlas.set_theme", "切换主题", "保存并切换 DualEye 双眼主题。", set_theme,
                      args={"theme": "pet/raptor/mecha/goggle 等"},
                      input_schema=schema_object({"theme": theme_schema}, ["theme"]))
    registry.register("atlas.ui.set_chat_mode", "切换对话界面", "在双屏文字、土拨鼠头、纯眼睛三种对话界面之间切换。", set_chat_mode,
                      args={"mode": "pet_head/text/eyes_only"},
                      input_schema=schema_object({"mode": chat_mode_schema}, ["mode"]),
                      target="display_mode")
    registry.register("atlas.pet.set_state", "土拨鼠状态", "切换左屏 2.5D 土拨鼠头关键帧状态，右屏可显示短文本。", pet_set_state,
                      args={"state": "idle/listen/speak/sing/happy/laugh/cry/sleepy/think/surprised/offline"},
                      input_schema=schema_object({
                          "state": pet_state_schema,
                          "right_text": {"type": "string", "maxLength": 90},
                      }, ["state"], additional=True),
                      target="pet_head")
    registry.register("atlas.pet.play_animation", "土拨鼠动画", "播放 blink/speak/sing/laugh 预渲染帧动画。", pet_play_animation,
                      args={"animation": "blink/speak/sing/laugh"},
                      input_schema=schema_object({
                          "animation": enum_schema(["blink", "speak", "sing", "laugh"], "pet_head animation id"),
                          "right_text": {"type": "string", "maxLength": 90},
                      }, ["animation"], additional=True),
                      target="pet_head")
    registry.register("atlas.role.switch", "角色切换", "联动角色 prompt、主题、表情、页面和 TTS 风格。", switch_role,
                      risk="medium",
                      args={"role": "pet/raptor/mecha/goggle"},
                      input_schema=schema_object({"role": role_schema}, ["role"]),
                      target="persona_ui_audio")
    registry.register("atlas.clock.show", "打开时钟", "打开 DualEye 桌面时钟应用。", clock_show,
                      input_schema=schema_object({}), target="clock_app")
    registry.register("atlas.clock.sync", "校准时钟", "用 Mac 当前时间校准 DualEye 时钟并打开时钟页。", clock_sync,
                      args={"epoch_ms": "可选 Unix 毫秒时间戳"},
                      input_schema=schema_object({
                          "epoch_ms": {"type": "integer", "description": "Unix epoch milliseconds; omit to use Mac current time"},
                      }, additional=True),
                      target="clock_app")
    registry.register("atlas.clock.status", "读取时钟状态", "读取 DualEye 时钟同步状态、日期和时间。", clock_status,
                      input_schema=schema_object({}), target="clock_app")
    registry.register("atlas.pomodoro.show", "打开番茄", "打开番茄专注应用但不改变计时状态。", pomodoro_show,
                      input_schema=schema_object({}), target="productivity_app")
    registry.register("atlas.pomodoro.start", "开始番茄", "启动番茄专注并打开番茄页面。", pomodoro_start,
                      args={"task_name": "任务名", "focus_minutes": "专注分钟"},
                      input_schema=schema_object({
                          "task_name": {"type": "string", "minLength": 1, "maxLength": 24},
                          "focus_minutes": {"type": "integer", "minimum": 1, "maximum": 120},
                          "break_minutes": {"type": "integer", "minimum": 1, "maximum": 30},
                      }, ["task_name"], additional=True),
                      target="productivity_app")
    registry.register("atlas.pomodoro.stop", "停止番茄", "停止当前番茄专注。", pomodoro_stop,
                      input_schema=schema_object({}), target="productivity_app")
    registry.register("atlas.pomodoro.status", "读取番茄状态", "读取番茄是否运行、剩余时间和进度。", pomodoro_status,
                      input_schema=schema_object({}), target="productivity_app")
    registry.register("atlas.calendar.show", "打开日历", "打开日历应用但不改写便签。", calendar_show,
                      input_schema=schema_object({}), target="calendar_app")
    registry.register("atlas.calendar.today", "今日日历", "显示今天日期和短便签。", calendar_today,
                      input_schema=schema_object({
                          "title": {"type": "string", "maxLength": 16},
                          "note": {"type": "string", "maxLength": 80},
                      }, additional=True),
                      target="calendar_app")
    registry.register("atlas.calendar.set_note", "设置日历便签", "设置日历标题/便签并打开日历应用。", calendar_set_note,
                      input_schema=schema_object({
                          "title": {"type": "string", "maxLength": 16},
                          "note": {"type": "string", "minLength": 1, "maxLength": 120},
                      }, ["note"], additional=True),
                      target="calendar_app")
    registry.register("atlas.weather.query", "天气查询", "查询城市天气并显示到 DualEye。", weather_query,
                      args={"location": "城市名，可空"},
                      input_schema=schema_object({"location": {"type": "string", "description": "城市名，空值使用默认城市济南"}}),
                      target="network_info")
    registry.register("atlas.web_search", "联网搜索", "调用搜索 Provider 并展示摘要。", web_search,
                      risk="medium",
                      args={"query": "搜索问题"},
                      input_schema=schema_object({
                          "query": {"type": "string", "minLength": 1, "maxLength": 120},
                          "max_results": {"type": "integer", "minimum": 1, "maximum": 8},
                      }, ["query"], additional=True),
                      target="network_info")
    registry.register("atlas.music.play", "音乐播放", "触发音乐页面/音乐表情入口。", app_action("music", "准备播放音乐。"),
                      input_schema=schema_object({}), target="media_app")
    registry.register("atlas.story.tell", "讲故事", "触发故事页面/故事表情入口。", app_action("story", "准备讲故事。"),
                      input_schema=schema_object({}), target="media_app")
    registry.register("atlas.chat", "对话", "打开对话页面。", app_action("chat", "进入对话模式。"),
                      input_schema=schema_object({}), target="chat_app")
    if rover_skills_enabled:
        registry.register("atlas.rover.stop", "底盘停止", "发送 STOP，最高优先级。", rover_stop,
                          risk="low", input_schema=schema_object({}), target="motion_control")
        registry.register("atlas.rover.move", "底盘短时移动", "发送短时移动指令，仍受 DualEye Safety Guard 约束。", rover_move,
                          risk="high",
                          args={"direction": "forward/backward/left/right"},
                          input_schema=schema_object({
                              "direction": enum_schema(["forward", "backward", "left", "right"]),
                              "speed": {"type": "integer", "minimum": 1, "maximum": 80},
                              "duration_ms": {"type": "integer", "minimum": 100, "maximum": 2000},
                          }, ["direction"], additional=True),
                          target="motion_control",
                          confirm_required=True)
    registry.register("atlas.ota.check", "检查 OTA", "返回本地 OTA manifest 和本地包哈希。", lambda args: {
        "ok": True,
        "reply": "已生成 OTA manifest；当前仍需 USB 烧录，真 OTA 尚未启用。",
        "manifest": build_ota_manifest(),
    }, risk="medium", input_schema=schema_object({}), target="firmware_package")
    return registry
