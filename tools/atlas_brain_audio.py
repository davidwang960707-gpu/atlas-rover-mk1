"""Audio utilities and state stores for Atlas Brain.

This module owns the concrete audio details used by the Mac-side brain:
TTS WAV normalization/cache, recent voice turn metadata, OPUS/AOP1 frame
parsing, Ogg wrapping for OPUS turns, and stream diagnostics.
"""

from __future__ import annotations

import array
import base64
import io
import os
import struct
import subprocess
import sys
import tempfile
import threading
import time
import wave
import zlib
from typing import Any


ATLAS_OPUS_FRAME_HEADER = struct.Struct("!4sBBBBIIHHHBBII")
ATLAS_OPUS_FRAME_MAGIC = b"AOP1"
ATLAS_OPUS_TURN_MAX_PACKETS = 5000

LAST_TTS_LOCK = threading.Lock()
LAST_TTS_WAV = b""
LAST_TTS_META: dict[str, Any] = {}
LAST_TURN_LOCK = threading.Lock()
LAST_TURN_META: dict[str, Any] = {}
AUDIO_STREAM_LOCK = threading.Lock()
AUDIO_STREAMS: list[dict[str, Any]] = []
LAST_AUDIO_STREAM_META: dict[str, Any] = {}


def clamp_int(value: int, minimum: int, maximum: int) -> int:
    return max(minimum, min(maximum, value))


def decode_audio_data_url(audio_url: str) -> tuple[str, bytes]:
    if not audio_url.startswith("data:") or "," not in audio_url:
        raise ValueError("audio_url is not a data URL")
    header, payload = audio_url.split(",", 1)
    mime = header[5:].split(";", 1)[0] or "application/octet-stream"
    return mime, base64.b64decode(payload)


def _pcm_to_i16_samples(raw: bytes, sample_width: int) -> list[int]:
    if sample_width == 1:
        return [((b - 128) << 8) for b in raw]
    if sample_width == 2:
        samples = array.array("h")
        samples.frombytes(raw)
        if sys.byteorder != "little":
            samples.byteswap()
        return list(samples)
    if sample_width == 4:
        samples32 = array.array("i")
        samples32.frombytes(raw)
        if sys.byteorder != "little":
            samples32.byteswap()
        return [max(-32768, min(32767, int(v) >> 16)) for v in samples32]
    raise ValueError(f"unsupported WAV sample width: {sample_width}")


def _i16_samples_to_wav(samples: list[int], sample_rate: int = 16000, channels: int = 1) -> bytes:
    out = array.array("h", (max(-32768, min(32767, int(v))) for v in samples))
    if sys.byteorder != "little":
        out.byteswap()
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(out.tobytes())
    return buf.getvalue()


def normalize_wav_for_dualeye(wav_bytes: bytes) -> bytes:
    with wave.open(io.BytesIO(wav_bytes), "rb") as wf:
        channels = wf.getnchannels()
        sample_width = wf.getsampwidth()
        sample_rate = wf.getframerate()
        frame_count = wf.getnframes()
        raw = wf.readframes(frame_count)

    if channels < 1:
        raise ValueError("invalid WAV channel count")
    samples = _pcm_to_i16_samples(raw, sample_width)
    if channels > 1:
        mono: list[int] = []
        usable = len(samples) - (len(samples) % channels)
        for i in range(0, usable, channels):
            mono.append(int(sum(samples[i:i + channels]) / channels))
    else:
        mono = samples

    if sample_rate != 16000 and mono:
        out_len = max(1, int(len(mono) * 16000 / sample_rate))
        resampled: list[int] = []
        for i in range(out_len):
            src = min(len(mono) - 1, int(i * sample_rate / 16000))
            resampled.append(mono[src])
        mono = resampled

    if mono:
        peak = max(abs(v) for v in mono)
        if peak > 0 and peak < 30000:
            # Cloud TTS is often conservative. Normalize before the tiny speaker path.
            gain = min(3.0, 30000.0 / peak)
            mono = [max(-32768, min(32767, int(v * gain))) for v in mono]

    return _i16_samples_to_wav(mono, sample_rate=16000, channels=1)


def store_latest_tts(tts_payload: dict[str, Any]) -> dict[str, Any]:
    global LAST_TTS_WAV, LAST_TTS_META
    if not tts_payload.get("ok"):
        return {"ready": False, "error": str(tts_payload.get("error", "tts failed"))}
    try:
        mime, audio_bytes = decode_audio_data_url(str(tts_payload.get("audio_url", "")))
        if "wav" not in mime and not audio_bytes.startswith(b"RIFF"):
            return {"ready": False, "error": f"unsupported tts mime: {mime}"}
        wav_bytes = normalize_wav_for_dualeye(audio_bytes)
    except Exception as exc:
        return {"ready": False, "error": str(exc)}

    meta = {
        "updated_at": int(time.time()),
        "bytes": len(wav_bytes),
        "sample_rate": 16000,
        "channels": 1,
        "format": "pcm_s16le_wav",
    }
    with LAST_TTS_LOCK:
        LAST_TTS_WAV = wav_bytes
        LAST_TTS_META = meta
    return {"ready": True, **meta}


def latest_tts_meta() -> dict[str, Any]:
    with LAST_TTS_LOCK:
        meta = dict(LAST_TTS_META)
        meta["ready"] = bool(LAST_TTS_WAV)
        return meta


def latest_tts_wav() -> tuple[bytes, dict[str, Any]]:
    with LAST_TTS_LOCK:
        return bytes(LAST_TTS_WAV), dict(LAST_TTS_META)


def remember_turn(meta: dict[str, Any]) -> None:
    global LAST_TURN_META
    safe_meta = dict(meta)
    safe_meta.setdefault("updated_at", int(time.time()))
    with LAST_TURN_LOCK:
        LAST_TURN_META = safe_meta


def latest_turn_meta() -> dict[str, Any]:
    with LAST_TURN_LOCK:
        return dict(LAST_TURN_META)


def remember_audio_stream(meta: dict[str, Any]) -> dict[str, Any]:
    stored = dict(meta)
    stored.setdefault("ts", time.time())
    with AUDIO_STREAM_LOCK:
        global LAST_AUDIO_STREAM_META
        LAST_AUDIO_STREAM_META = stored
        AUDIO_STREAMS.append(stored)
        del AUDIO_STREAMS[:-20]
    return stored


def latest_audio_stream_meta() -> dict[str, Any]:
    with AUDIO_STREAM_LOCK:
        return dict(LAST_AUDIO_STREAM_META)


def recent_audio_streams() -> list[dict[str, Any]]:
    with AUDIO_STREAM_LOCK:
        return list(reversed(AUDIO_STREAMS[-10:]))


def parse_atlas_opus_frame(payload: bytes) -> dict[str, Any]:
    if len(payload) < ATLAS_OPUS_FRAME_HEADER.size:
        return {"ok": False, "error": "frame too short", "wire_bytes": len(payload)}
    (
        magic,
        version,
        header_len,
        flags,
        channels,
        seq,
        timestamp_ms,
        sample_rate,
        frame_ms,
        payload_len,
        mic_level,
        _reserved,
        mic_rms,
        mic_peak,
    ) = ATLAS_OPUS_FRAME_HEADER.unpack_from(payload, 0)
    if magic != ATLAS_OPUS_FRAME_MAGIC:
        return {"ok": False, "error": "unknown binary frame", "wire_bytes": len(payload)}
    if header_len < ATLAS_OPUS_FRAME_HEADER.size or header_len > len(payload):
        return {
            "ok": False,
            "error": "invalid AOP1 header length",
            "wire_bytes": len(payload),
            "header_len": header_len,
            "seq": seq,
        }
    actual_payload_len = max(0, len(payload) - header_len)
    return {
        "ok": True,
        "protocol": "atlas.audio.stream.v0",
        "header": "AOP1",
        "version": version,
        "header_len": header_len,
        "flags": flags,
        "channels": channels,
        "seq": seq,
        "timestamp_ms": timestamp_ms,
        "sample_rate": sample_rate,
        "frame_ms": frame_ms,
        "payload_len": payload_len,
        "actual_payload_len": actual_payload_len,
        "payload_len_match": payload_len == actual_payload_len,
        "mic_level": mic_level,
        "mic_rms": mic_rms,
        "mic_peak": mic_peak,
        "wire_bytes": len(payload),
    }


def _ogg_lacing_values(packet: bytes) -> list[int]:
    remaining = len(packet)
    values: list[int] = []
    while remaining >= 255:
        values.append(255)
        remaining -= 255
    values.append(remaining)
    return values


def _ogg_crc(data: bytes) -> int:
    # Ogg uses a non-reflected CRC-32 with polynomial 0x04C11DB7.
    reg = 0
    for byte in data:
        reg ^= byte << 24
        for _ in range(8):
            if reg & 0x80000000:
                reg = ((reg << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                reg = (reg << 1) & 0xFFFFFFFF
    return reg


def _build_ogg_page(packet: bytes,
                    *,
                    serial: int,
                    sequence: int,
                    granule_position: int,
                    header_type: int) -> bytes:
    segments = _ogg_lacing_values(packet)
    if len(segments) > 255:
        raise ValueError("single opus packet too large for one Ogg page")
    segment_table = bytes(segments)
    header = struct.pack(
        "<4sBBqIIIB",
        b"OggS",
        0,
        header_type,
        granule_position,
        serial & 0xFFFFFFFF,
        sequence & 0xFFFFFFFF,
        0,
        len(segments),
    )
    page = bytearray(header + segment_table + packet)
    crc = _ogg_crc(bytes(page))
    page[22:26] = struct.pack("<I", crc)
    return bytes(page)


def build_ogg_opus(packets: list[bytes],
                   *,
                   sample_rate: int = 16000,
                   channels: int = 1,
                   frame_ms: int = 60) -> bytes:
    valid_packets = [packet for packet in packets if packet]
    if not valid_packets:
        raise ValueError("no opus packets")

    serial = zlib.crc32(valid_packets[0] + struct.pack("<d", time.time())) & 0xFFFFFFFF
    sequence = 0
    pages: list[bytes] = []
    opus_head = b"OpusHead" + struct.pack("<BBHIhB", 1, channels, 312, sample_rate, 0, 0)
    vendor = b"Atlas Brain"
    opus_tags = b"OpusTags" + struct.pack("<I", len(vendor)) + vendor + struct.pack("<I", 0)

    pages.append(_build_ogg_page(opus_head, serial=serial, sequence=sequence, granule_position=0, header_type=0x02))
    sequence += 1
    pages.append(_build_ogg_page(opus_tags, serial=serial, sequence=sequence, granule_position=0, header_type=0x00))
    sequence += 1

    granule = 0
    granule_step = max(1, int(frame_ms * 48))
    for index, packet in enumerate(valid_packets):
        granule += granule_step
        header_type = 0x04 if index == len(valid_packets) - 1 else 0x00
        pages.append(_build_ogg_page(packet, serial=serial, sequence=sequence, granule_position=granule, header_type=header_type))
        sequence += 1
    return b"".join(pages)


def decode_opus_packets_to_wav(packets: list[bytes],
                               *,
                               sample_rate: int = 16000,
                               channels: int = 1,
                               frame_ms: int = 60) -> dict[str, Any]:
    if not packets:
        return {"ok": False, "error": "no opus packets"}
    ffmpeg = "/opt/homebrew/bin/ffmpeg" if os.path.exists("/opt/homebrew/bin/ffmpeg") else "ffmpeg"
    try:
        ogg_bytes = build_ogg_opus(packets, sample_rate=sample_rate, channels=channels, frame_ms=frame_ms)
        with tempfile.TemporaryDirectory(prefix="atlas_opus_turn_") as tmpdir:
            ogg_path = os.path.join(tmpdir, "turn.opus")
            wav_path = os.path.join(tmpdir, "turn.wav")
            with open(ogg_path, "wb") as handle:
                handle.write(ogg_bytes)
            proc = subprocess.run(
                [
                    ffmpeg,
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-y",
                    "-i",
                    ogg_path,
                    "-ac",
                    "1",
                    "-ar",
                    str(sample_rate),
                    wav_path,
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=20,
                check=False,
            )
            if proc.returncode != 0:
                return {
                    "ok": False,
                    "error": proc.stderr.decode("utf-8", errors="replace")[:500] or f"ffmpeg exited {proc.returncode}",
                    "ogg_bytes": len(ogg_bytes),
                    "packets": len(packets),
                }
            with open(wav_path, "rb") as handle:
                wav = handle.read()
        return {
            "ok": len(wav) > 44,
            "wav": wav,
            "wav_bytes": len(wav),
            "ogg_bytes": len(ogg_bytes),
            "packets": len(packets),
            "sample_rate": sample_rate,
            "channels": 1,
            "frame_ms": frame_ms,
        }
    except Exception as exc:
        return {"ok": False, "error": str(exc), "packets": len(packets)}


def simulate_audio_stream(payload: dict[str, Any]) -> dict[str, Any]:
    codec = str(payload.get("codec", "opus") or "opus").lower()
    duration_ms = clamp_int(int(payload.get("duration_ms", 1800) or 1800), 60, 60000)
    frame_ms = clamp_int(int(payload.get("frame_ms", 60) or 60), 10, 120)
    sample_rate = clamp_int(int(payload.get("sample_rate", 16000) or 16000), 8000, 48000)
    channels = clamp_int(int(payload.get("channels", 1) or 1), 1, 2)
    bitrate_bps = clamp_int(int(payload.get("bitrate_bps", 24000) or 24000), 8000, 128000)
    frames = (duration_ms + frame_ms - 1) // frame_ms
    pcm_bytes_per_frame = int(sample_rate * channels * 2 * frame_ms / 1000)
    estimated_encoded_bytes_per_frame = max(16, int(bitrate_bps * frame_ms / 8000))
    encoded_bytes = frames * estimated_encoded_bytes_per_frame
    meta = {
        "ok": True,
        "stage": "P3_simulation",
        "codec": codec,
        "duration_ms": duration_ms,
        "frame_ms": frame_ms,
        "sample_rate": sample_rate,
        "channels": channels,
        "frames": frames,
        "pcm_bytes_per_frame": pcm_bytes_per_frame,
        "estimated_encoded_bytes_per_frame": estimated_encoded_bytes_per_frame,
        "estimated_encoded_bytes": encoded_bytes,
        "notes": "这是协议和吞吐模拟，不代表已经在 DualEye 上完成 OPUS 编码/解码。",
    }
    return remember_audio_stream(meta)


def compact_audio_payload(value: Any) -> Any:
    if isinstance(value, dict):
        compact: dict[str, Any] = {}
        for key, item in value.items():
            if key == "audio_url" and isinstance(item, str) and item.startswith("data:audio/"):
                compact["audio_url_omitted"] = True
                compact["audio_url_bytes"] = len(item)
                continue
            compact[key] = compact_audio_payload(item)
        return compact
    if isinstance(value, list):
        return [compact_audio_payload(item) for item in value]
    return value
