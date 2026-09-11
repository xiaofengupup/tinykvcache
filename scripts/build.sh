#!/usr/bin/env bash
set -euo pipefail

# 本脚本用于开发过程中构建 TinyKVCache。
# 用法：
#   ./scripts/build.sh
#
# 产物：
#   build/tinykv_server
#   build/tinykv_client

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTINYKV_BUILD_TESTS=ON

cmake --build "${BUILD_DIR}" -j

echo
echo "build finished."
echo "Server: ${BUILD_DIR}/tinykv_server"
echo "Client: ${BUILD_DIR}/tinykv_client"