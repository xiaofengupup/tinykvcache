#!/usr/bin/env bash
set -euo pipefail

# 本脚本用于 Release 构建 TinyKVCache。
# 用法：
#   ./scripts/build_release.sh
#
# 产物：
#   build-release/tinykv_server
#   build-release/tinykv_client

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-release"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DTINYKV_BUILD_TESTS=ON

cmake --build "${BUILD_DIR}" -j

ctest --test-dir "${BUILD_DIR}" --output-on-failure

echo
echo "Release build finished."
echo "Server: ${BUILD_DIR}/tinykv_server"
echo "Client: ${BUILD_DIR}/tinykv_client"