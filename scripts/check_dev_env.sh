#!/usr/bin/env zsh
set -euo pipefail

IDF_PATH="${IDF_PATH:-$HOME/.espressif/esp-idf-v5.5.2}"
export IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/python_env/idf5.5_py3.9_env}"

if [[ ! -f "$IDF_PATH/export.sh" ]]; then
  echo "未找到 ESP-IDF export.sh: $IDF_PATH/export.sh" >&2
  exit 1
fi

source "$IDF_PATH/export.sh" >/dev/null

echo "ESP-IDF: $(idf.py --version)"
echo "CMake: $(cmake --version | head -n 1)"
echo "Ninja: $(ninja --version)"
echo "SDL2: $(sdl2-config --version)"
echo "Python: $(python --version 2>&1)"
echo "VS Code App: /Applications/Visual Studio Code.app"

if [[ -x "/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code" ]]; then
  "/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code" --version | head -n 1 | sed 's/^/VS Code: /'
fi

echo
echo "可用串口："
ls /dev/cu.* 2>/dev/null | sed 's/^/  /' || true
