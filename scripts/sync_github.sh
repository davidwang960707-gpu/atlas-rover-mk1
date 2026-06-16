#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

MESSAGE="${1:-}"
if [[ -z "$MESSAGE" ]]; then
  echo "用法：./scripts/sync_github.sh \"说明这次推进了什么\""
  exit 1
fi

if ! git remote get-url origin >/dev/null 2>&1; then
  echo "还没有配置 origin 远端。先用 gh repo create 或 git remote add 配置 GitHub 仓库。"
  exit 1
fi

echo "当前变更："
git status --short

if [[ -z "$(git status --short)" ]]; then
  echo "没有需要同步的变更。"
  exit 0
fi

git add .
git commit -m "$MESSAGE"
git push origin main

echo "已同步到 GitHub：$MESSAGE"
