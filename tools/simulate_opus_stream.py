#!/usr/bin/env python3
"""Simulate Atlas P2 websocket audio streaming.

This sends masked WebSocket frames like a real client:
1. JSON start event
2. AOP1 binary fake OPUS frames, default 60 ms each
3. JSON end event

The payload is intentionally fake; this validates protocol shape, frame
accounting, and server diagnostics before/alongside real DualEye OPUS encoding.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import socket
import struct
import time
import urllib.parse


WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
AOP1_HEADER = struct.Struct("!4sBBBBIIHHHBBII")
AOP1_MAGIC = b"AOP1"


def recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks: list[bytes] = []
    while size > 0:
        chunk = sock.recv(size)
        if not chunk:
            raise ConnectionError("socket closed")
        chunks.append(chunk)
        size -= len(chunk)
    return b"".join(chunks)


def recv_frame(sock: socket.socket) -> tuple[int, bytes]:
    first, second = recv_exact(sock, 2)
    opcode = first & 0x0F
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", recv_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", recv_exact(sock, 8))[0]
    payload = recv_exact(sock, length) if length else b""
    return opcode, payload


def send_frame(sock: socket.socket, opcode: int, payload: bytes) -> None:
    mask = os.urandom(4)
    first = 0x80 | (opcode & 0x0F)
    length = len(payload)
    if length < 126:
        header = bytes([first, 0x80 | length])
    elif length <= 0xFFFF:
        header = bytes([first, 0x80 | 126]) + struct.pack("!H", length)
    else:
        header = bytes([first, 0x80 | 127]) + struct.pack("!Q", length)
    masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    sock.sendall(header + mask + masked)


def send_json(sock: socket.socket, payload: dict) -> None:
    send_frame(sock, 0x1, json.dumps(payload, ensure_ascii=False).encode("utf-8"))


def build_aop1_frame(seq: int,
                     payload: bytes,
                     frame_ms: int,
                     sample_rate: int = 16000,
                     mic_level: int = 42,
                     mic_rms: int = 360,
                     mic_peak: int = 1800,
                     timestamp_ms: int | None = None) -> bytes:
    header = AOP1_HEADER.pack(
        AOP1_MAGIC,
        1,
        AOP1_HEADER.size,
        0,
        1,
        seq,
        int(time.time() * 1000 if timestamp_ms is None else timestamp_ms) & 0xFFFFFFFF,
        sample_rate,
        frame_ms,
        len(payload),
        max(0, min(100, mic_level)),
        0,
        max(0, mic_rms),
        max(0, mic_peak),
    )
    return header + payload


def read_json(sock: socket.socket) -> dict:
    opcode, payload = recv_frame(sock)
    if opcode != 0x1:
        return {"ok": False, "opcode": opcode, "raw_len": len(payload)}
    try:
        return json.loads(payload.decode("utf-8"))
    except Exception as exc:
        return {"ok": False, "error": str(exc), "raw": payload.decode("utf-8", errors="replace")}


def connect(url: str) -> socket.socket:
    parsed = urllib.parse.urlparse(url)
    host = parsed.hostname or "127.0.0.1"
    port = parsed.port or (443 if parsed.scheme == "wss" else 80)
    path = parsed.path or "/ws/audio"
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    sock = socket.create_connection((host, port), timeout=5)
    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    )
    sock.sendall(request.encode("ascii"))
    response = b""
    while b"\r\n\r\n" not in response:
        response += sock.recv(4096)
    header = response.decode("iso-8859-1", errors="replace")
    expected = base64.b64encode(hashlib.sha1((key + WS_GUID).encode("ascii")).digest()).decode("ascii")
    if " 101 " not in header or expected not in header:
        raise RuntimeError(f"websocket handshake failed:\n{header}")
    return sock


def main() -> None:
    parser = argparse.ArgumentParser(description="Simulate Atlas P3 OPUS websocket frames")
    parser.add_argument("--url", default="ws://127.0.0.1:8787/ws/audio")
    parser.add_argument("--duration-ms", type=int, default=1800)
    parser.add_argument("--frame-ms", type=int, default=60)
    parser.add_argument("--payload-bytes", type=int, default=180)
    parser.add_argument("--codec", default="opus")
    parser.add_argument("--legacy", action="store_true", help="send raw legacy binary payloads instead of AOP1 frames")
    parser.add_argument("--realtime", action="store_true")
    args = parser.parse_args()

    frames = max(1, (args.duration_ms + args.frame_ms - 1) // args.frame_ms)
    sock = connect(args.url)
    try:
        base_timestamp_ms = int(time.time() * 1000)
        ready = read_json(sock)
        send_json(sock, {
            "type": "start",
            "turn_id": time.strftime("sim-%Y%m%d-%H%M%S"),
            "codec": args.codec,
            "sample_rate": 16000,
            "channels": 1,
            "frame_ms": args.frame_ms,
        })
        start_ack = read_json(sock)
        for index in range(frames):
            payload = bytes((index + offset) % 256 for offset in range(args.payload_bytes))
            wire = payload if args.legacy else build_aop1_frame(
                index + 1,
                payload,
                args.frame_ms,
                timestamp_ms=base_timestamp_ms + index * args.frame_ms,
            )
            send_frame(sock, 0x2, wire)
            if args.realtime:
                time.sleep(args.frame_ms / 1000)
            if (index + 1) % 10 == 0:
                _ = read_json(sock)
        send_json(sock, {"type": "end"})
        end_ack = read_json(sock)
        send_frame(sock, 0x8, b"")
        print(json.dumps({
            "ok": True,
            "ready": ready,
            "start_ack": start_ack,
            "end_ack": end_ack,
            "frames_sent": frames,
            "payload_bytes": frames * args.payload_bytes,
            "wire_format": "legacy" if args.legacy else "AOP1",
        }, ensure_ascii=False, indent=2))
    finally:
        sock.close()


if __name__ == "__main__":
    main()
