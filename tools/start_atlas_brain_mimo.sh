#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if [[ -f ".atlas-brain.env" ]]; then
  set -a
  # shellcheck disable=SC1091
  source ".atlas-brain.env"
  set +a
fi

: "${ATLAS_LLM_BASE_URL:=https://api.xiaomimimo.com/v1}"
: "${ATLAS_LLM_MODEL:=xiaomi/mimo-v2.5-pro}"
: "${ATLAS_ASR_MODEL:=mimo-v2.5-asr}"
: "${ATLAS_TTS_MODEL:=mimo-v2.5-tts}"
: "${ATLAS_TTS_VOICE:=mimo_default}"
: "${ATLAS_DUALEYE_URL:=http://192.168.4.1}"
: "${ATLAS_BRIDGE_HOST:=0.0.0.0}"
: "${ATLAS_BRIDGE_PORT:=8787}"

export ATLAS_LLM_BASE_URL
export ATLAS_LLM_MODEL
export ATLAS_ASR_MODEL
export ATLAS_TTS_MODEL
export ATLAS_TTS_VOICE

if [[ -z "${ATLAS_LLM_API_KEY:-}" ]]; then
  echo "ATLAS_LLM_API_KEY is missing. Create .atlas-brain.env from .atlas-brain.env.example first." >&2
  exit 2
fi

exec python3 tools/atlas_brain_server.py \
  --dualeye-url "$ATLAS_DUALEYE_URL" \
  --host "$ATLAS_BRIDGE_HOST" \
  --port "$ATLAS_BRIDGE_PORT" \
  "$@"
