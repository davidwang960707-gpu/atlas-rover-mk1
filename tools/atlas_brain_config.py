"""Configuration and CLI bootstrap helpers for Atlas Brain."""

from __future__ import annotations

import argparse
import os
from dataclasses import dataclass
from typing import Sequence


DEFAULT_DUALEYE_URL = "http://192.168.4.1"
DEFAULT_PORT = 8787
DEFAULT_SPEED = 30
DEFAULT_DURATION_MS = 500
DEFAULT_LLM_BASE_URL = "https://api.xiaomimimo.com/v1"
DEFAULT_LLM_MODEL = "xiaomi/mimo-v2.5-pro"
DEFAULT_ASR_MODEL = "mimo-v2.5-asr"
DEFAULT_TTS_MODEL = "mimo-v2.5-tts"
DEFAULT_TTS_VOICE = "mimo_default"

TRUE_VALUES = {"1", "true", "yes", "on"}


@dataclass(frozen=True)
class AtlasBrainConfig:
    dualeye_url: str
    pin: str
    host: str
    port: int
    speed: int
    duration_ms: int
    dry_run: bool
    llm_base_url: str
    llm_api_key: str
    llm_model: str
    asr_model: str
    tts_model: str
    tts_voice: str
    rover_skills_enabled: bool


def clamp_int(value: int, minimum: int, maximum: int) -> int:
    return max(minimum, min(maximum, value))


def env_flag(name: str, default: str = "0") -> bool:
    return os.getenv(name, default).strip().lower() in TRUE_VALUES


ENABLE_ROVER_SKILLS = env_flag("ATLAS_ENABLE_ROVER_SKILLS")


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Atlas Brain server for DualEye")
    parser.add_argument("--dualeye-url", default=os.environ.get("ATLAS_DUALEYE_URL", DEFAULT_DUALEYE_URL))
    parser.add_argument("--pin", default=os.environ.get("ATLAS_PAIRING_PIN", ""))
    parser.add_argument("--host", default=os.environ.get("ATLAS_BRIDGE_HOST", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("ATLAS_BRIDGE_PORT", DEFAULT_PORT)))
    parser.add_argument("--speed", type=int, default=int(os.environ.get("ATLAS_BRIDGE_SPEED", DEFAULT_SPEED)))
    parser.add_argument("--duration-ms", type=int, default=int(os.environ.get("ATLAS_BRIDGE_DURATION_MS", DEFAULT_DURATION_MS)))
    parser.add_argument("--llm-base-url", default=os.environ.get("ATLAS_LLM_BASE_URL", DEFAULT_LLM_BASE_URL))
    parser.add_argument("--llm-api-key", default=os.environ.get("ATLAS_LLM_API_KEY", ""))
    parser.add_argument("--llm-model", default=os.environ.get("ATLAS_LLM_MODEL", DEFAULT_LLM_MODEL))
    parser.add_argument("--asr-model", default=os.environ.get("ATLAS_ASR_MODEL", DEFAULT_ASR_MODEL))
    parser.add_argument("--tts-model", default=os.environ.get("ATLAS_TTS_MODEL", DEFAULT_TTS_MODEL))
    parser.add_argument("--tts-voice", default=os.environ.get("ATLAS_TTS_VOICE", DEFAULT_TTS_VOICE))
    parser.add_argument("--dry-run", action="store_true", help="parse commands without posting to DualEye")
    return parser


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    return build_arg_parser().parse_args(argv)


def config_from_args(args: argparse.Namespace) -> AtlasBrainConfig:
    return AtlasBrainConfig(
        dualeye_url=str(args.dualeye_url),
        pin=str(args.pin),
        host=str(args.host),
        port=int(args.port),
        speed=clamp_int(int(args.speed), 1, 80),
        duration_ms=clamp_int(int(args.duration_ms), 100, 2000),
        dry_run=bool(args.dry_run),
        llm_base_url=str(args.llm_base_url),
        llm_api_key=str(args.llm_api_key),
        llm_model=str(args.llm_model),
        asr_model=str(args.asr_model),
        tts_model=str(args.tts_model),
        tts_voice=str(args.tts_voice),
        rover_skills_enabled=ENABLE_ROVER_SKILLS,
    )


def load_config(argv: Sequence[str] | None = None) -> AtlasBrainConfig:
    return config_from_args(parse_args(argv))
