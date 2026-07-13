#!/usr/bin/env bash
set -euo pipefail

# 启动 TinyKVCache 服务端。
#
# 用法：
#   ./scripts/run_server.sh
#   ./scripts/run_server.sh 0.0.0.0 7777
#
# 默认监听：
#   0.0.0.0:7777

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-release"

HOST="${1:-0.0.0.0}"
PORT="${2:-7777}"

SERVER="${BUILD_DIR}/tinykv_server"

if [[ ! -x "${SERVER}" ]]; then
  echo "server binary not found: ${SERVER}"
  echo "please run: ./scripts/build_release.sh"
  exit 1
fi

exec "${SERVER}" "${HOST}" "${PORT}"