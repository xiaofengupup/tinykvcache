#!/usr/bin/env bash
set -euo pipefail

# 启动 TinyKVCache 服务端。
#
# 用法：
#
#   ./scripts/run_server.sh
#
#   ./scripts/run_server.sh --config config/tinykv.toml
#
#   ./scripts/run_server.sh --host 0.0.0.0 --port 7777
#
#   ./scripts/run_server.sh --config config/tinykv.toml --log-level debug

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-release"
SERVER="${BUILD_DIR}/tinykv_server"

if [[ ! -x "${SERVER}" ]]; then
    echo "server binary not found: ${SERVER}"
    echo "please run: ./scripts/build_release.sh"
    exit 1
fi

exec "${SERVER}" "$@"