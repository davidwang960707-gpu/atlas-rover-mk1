#!/usr/bin/env python3
"""Check Mac <-> DualEye <-> Atlas Brain reachability."""

from __future__ import annotations

import argparse
import json
import socket
import subprocess
import urllib.error
import urllib.request

NO_PROXY_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def ipconfig_addr(interface: str) -> str:
    try:
        return subprocess.check_output(
            ["ipconfig", "getifaddr", interface],
            text=True,
            stderr=subprocess.DEVNULL,
            timeout=2,
        ).strip()
    except Exception:
        return ""


def route_addr() -> str:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))
        return sock.getsockname()[0]
    except OSError:
        return ""
    finally:
        sock.close()


def get_json(url: str, timeout: float = 4.0) -> tuple[bool, str]:
    try:
        with NO_PROXY_OPENER.open(url, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
        try:
            payload = json.loads(raw)
            return True, json.dumps(payload, ensure_ascii=False, indent=2)[:900]
        except json.JSONDecodeError:
            return True, raw[:900]
    except urllib.error.HTTPError as exc:
        return False, f"HTTP {exc.code}: {exc.reason}"
    except Exception as exc:
        return False, str(exc)


def main() -> int:
    parser = argparse.ArgumentParser(description="Atlas Brain network check")
    parser.add_argument("--dualeye-url", default="http://192.168.4.1")
    parser.add_argument("--bridge-url", default="http://127.0.0.1:8787")
    args = parser.parse_args()

    print("Mac IP candidates:")
    seen: set[str] = set()
    for name, addr in [
        ("default-route", route_addr()),
        ("Wi-Fi en0", ipconfig_addr("en0")),
        ("Wi-Fi en1", ipconfig_addr("en1")),
        ("Ethernet en2", ipconfig_addr("en2")),
        ("Ethernet en3", ipconfig_addr("en3")),
    ]:
        if addr and addr not in seen:
            seen.add(addr)
            print(f"  {name}: {addr}")
    if not seen:
        print("  no active IPv4 address found")

    checks = [
        ("DualEye status", args.dualeye_url.rstrip("/") + "/api/status"),
        ("Atlas Brain", args.bridge_url.rstrip("/") + "/health"),
    ]
    print("\nReachability:")
    ok_all = True
    for label, url in checks:
        ok, detail = get_json(url)
        ok_all = ok_all and ok
        print(f"- {label}: {'OK' if ok else 'FAIL'} {url}")
        print(detail)
    print("\nUse in Web:")
    print("  /app Atlas Brain URL: http://<Mac IP above>:8787")
    print("  /admin LLM mode: host")
    print("  /admin Base URL: http://<Mac IP above>:8787")
    return 0 if ok_all else 1


if __name__ == "__main__":
    raise SystemExit(main())
