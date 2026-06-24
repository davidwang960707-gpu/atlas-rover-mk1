"""Weather provider utilities for Atlas Brain."""

from __future__ import annotations

import json
import os
import re
import threading
import time
import urllib.parse
import urllib.request
from typing import Any


DEFAULT_WEATHER_LOCATION = "济南"
WEATHER_CACHE_LOCK = threading.Lock()
WEATHER_CACHE: dict[str, tuple[float, dict[str, Any]]] = {}
WEATHER_CACHE_TTL_SEC = 600
NO_PROXY_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))

WEATHER_CODE_ZH = {
    0: "晴",
    1: "大致晴朗",
    2: "局部多云",
    3: "阴",
    45: "有雾",
    48: "雾凇",
    51: "小毛毛雨",
    53: "毛毛雨",
    55: "较强毛毛雨",
    61: "小雨",
    63: "中雨",
    65: "大雨",
    71: "小雪",
    73: "中雪",
    75: "大雪",
    80: "阵雨",
    81: "较强阵雨",
    82: "强阵雨",
    95: "雷雨",
}

WEATHER_LOCATION_ALIASES = {
    "济南市": "济南",
    "山东济南": "济南",
    "山东省济南": "济南",
    "山东省济南市": "济南",
    "jinan": "济南",
}

WEATHER_FILLER_WORDS = (
    "查一下", "查询", "查看", "看看", "帮我", "给我", "请", "麻烦",
    "现在", "当前", "今天", "当地", "附近", "一下", "天气预报", "天气",
    "温度", "下雨", "冷吗", "热吗", "风大", "怎么样", "如何", "的",
    "呃", "嗯", "啊", "哦", "喂", "好的", "好吧", "好", "那个", "就是",
)


def default_weather_location() -> str:
    return os.getenv("ATLAS_WEATHER_DEFAULT_LOCATION", DEFAULT_WEATHER_LOCATION).strip() or DEFAULT_WEATHER_LOCATION


def weather_provider_status() -> dict[str, Any]:
    return {
        "provider": os.getenv("ATLAS_WEATHER_PROVIDER", "open_meteo").strip() or "open_meteo",
        "default_location": default_weather_location(),
    }


def normalize_weather_location(text: str) -> str:
    location = str(text or "").strip()
    if not location:
        return ""
    location = re.sub(r"[\s：:，,。.!！?？、；;]+", "", location)
    lowered = location.lower()
    if lowered in WEATHER_LOCATION_ALIASES:
        return WEATHER_LOCATION_ALIASES[lowered]
    for word in WEATHER_FILLER_WORDS:
        location = location.replace(word, "")
    location = re.sub(r"[\s：:，,。.!！?？、；;]+", "", location).strip()
    lowered = location.lower()
    if lowered in WEATHER_LOCATION_ALIASES:
        return WEATHER_LOCATION_ALIASES[lowered]
    if not location or location in {"市", "省", "区", "县"}:
        return ""
    return location


def weather_location_candidates(location: str) -> list[str]:
    normalized = normalize_weather_location(location)
    if not normalized:
        normalized = DEFAULT_WEATHER_LOCATION
    candidates: list[str] = []
    for item in (normalized, WEATHER_LOCATION_ALIASES.get(normalized.lower(), "")):
        if item and item not in candidates:
            candidates.append(item)
    for suffix in ("市", "省", "区", "县"):
        if normalized.endswith(suffix) and len(normalized) > len(suffix) + 1:
            stripped = normalized[:-len(suffix)]
            if stripped and stripped not in candidates:
                candidates.append(stripped)
    if DEFAULT_WEATHER_LOCATION not in candidates and normalized in {"", "本地", "默认"}:
        candidates.append(DEFAULT_WEATHER_LOCATION)
    return candidates


def cache_weather_result(keys: list[str], weather: dict[str, Any]) -> None:
    now = time.time()
    with WEATHER_CACHE_LOCK:
        for key in keys:
            normalized = normalize_weather_location(key) or key
            if normalized:
                WEATHER_CACHE[normalized] = (now, dict(weather))


def cached_weather_result(keys: list[str]) -> dict[str, Any] | None:
    now = time.time()
    with WEATHER_CACHE_LOCK:
        for key in keys:
            normalized = normalize_weather_location(key) or key
            cached = WEATHER_CACHE.get(normalized)
            if cached and now - cached[0] <= WEATHER_CACHE_TTL_SEC:
                result = dict(cached[1])
                result["cached"] = True
                return result
    return None


def http_json(url: str, timeout: float = 5.0) -> dict[str, Any]:
    with NO_PROXY_OPENER.open(url, timeout=timeout) as resp:
        raw = resp.read().decode("utf-8", errors="replace")
    return json.loads(raw)


def http_json_retry(url: str, timeout: float = 10.0, attempts: int = 2) -> dict[str, Any]:
    last_exc: Exception | None = None
    for index in range(max(1, attempts)):
        try:
            return http_json(url, timeout=timeout)
        except Exception as exc:
            last_exc = exc
            if index + 1 < attempts:
                time.sleep(0.35)
    if last_exc is not None:
        raise last_exc
    raise RuntimeError("request failed")


def query_weather(location: str = "") -> dict[str, Any]:
    default_location = default_weather_location()
    requested_location = str(location or "").strip()
    candidates = weather_location_candidates(requested_location or default_location)
    provider = os.getenv("ATLAS_WEATHER_PROVIDER", "open_meteo").strip().lower() or "open_meteo"
    if provider not in {"open_meteo", "open-meteo"}:
        return {"ok": False, "error": f"unsupported weather provider: {provider}", "location": requested_location or default_location}
    try:
        results = None
        geo_location = candidates[0] if candidates else default_location
        for candidate in candidates:
            geo_query = urllib.parse.urlencode({"name": candidate, "count": 1, "language": "zh", "format": "json"})
            geo = http_json_retry(f"https://geocoding-api.open-meteo.com/v1/search?{geo_query}", timeout=12.0, attempts=2)
            maybe_results = geo.get("results")
            if isinstance(maybe_results, list) and maybe_results:
                results = maybe_results
                geo_location = candidate
                break
        if not isinstance(results, list) or not results:
            return {"ok": False, "error": f"没有找到城市：{requested_location or default_location}", "location": requested_location or default_location}
        city = results[0]
        latitude = float(city["latitude"])
        longitude = float(city["longitude"])
        city_name = str(city.get("name") or location)
        admin = str(city.get("admin1") or "")
        forecast_query = urllib.parse.urlencode({
            "latitude": f"{latitude:.5f}",
            "longitude": f"{longitude:.5f}",
            "current": "temperature_2m,weather_code,wind_speed_10m",
            "timezone": "auto",
        })
        forecast = http_json_retry(f"https://api.open-meteo.com/v1/forecast?{forecast_query}", timeout=12.0, attempts=2)
        current = forecast.get("current", {})
        if not isinstance(current, dict):
            return {"ok": False, "error": "天气响应格式异常", "location": location}
        temp = current.get("temperature_2m")
        wind = current.get("wind_speed_10m")
        code = int(current.get("weather_code", -1))
        condition = WEATHER_CODE_ZH.get(code, f"天气代码{code}")
        if admin and (city_name in admin or admin in city_name):
            display_location = admin
        elif admin:
            display_location = f"{admin}{city_name}"
        else:
            display_location = city_name
        summary = f"{display_location}现在{condition}，气温{temp}℃，风速{wind} km/h。"
        if isinstance(temp, (int, float)):
            if temp <= 5:
                summary += " 出门记得加衣服。"
            elif temp >= 30:
                summary += " 有点热，记得补水。"
            elif code in {61, 63, 65, 80, 81, 82, 95}:
                summary += " 可能要带伞。"
        result = {
            "ok": True,
            "provider": "open_meteo",
            "query_location": requested_location or default_location,
            "geo_location": geo_location,
            "location": display_location,
            "condition": condition,
            "temperature_c": temp,
            "wind_kmh": wind,
            "weather_code": code,
            "summary": summary,
        }
        cache_weather_result(candidates + [requested_location, default_location, display_location], result)
        return result
    except Exception as exc:
        cached = cached_weather_result(candidates + [requested_location, default_location])
        if cached is not None:
            cached["warning"] = f"天气 API 暂时不稳定，已使用最近缓存：{exc}"
            return cached
        return {"ok": False, "error": str(exc), "location": requested_location or default_location}
