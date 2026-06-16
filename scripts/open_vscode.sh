#!/usr/bin/env zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
open -a "Visual Studio Code" "$ROOT_DIR/Atlas One.code-workspace"
