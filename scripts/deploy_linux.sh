#!/usr/bin/env bash
set -euo pipefail

# 将项目同步到 Linux 服务器并执行 Release 构建。
#
# 用法：
#   ./scripts/deploy_linux.sh user@host
#   ./scripts/deploy_linux.sh user@host 22 ~/tinykv-cache
#
# 参数：
#   $1: 远程登录地址，例如 hkust@1.2.3.4
#   $2: SSH 端口，默认 22
#   $3: 远程目录，默认 ~/tinykv-cache

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <user@host> [ssh_port] [remote_dir]"
  exit 1
fi

REMOTE="$1"
SSH_PORT="${2:-22}"
REMOTE_DIR="${3:-~/tinykv-cache}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Deploy TinyKVCache"
echo "  local      : ${ROOT_DIR}"
echo "  remote     : ${REMOTE}"
echo "  ssh port   : ${SSH_PORT}"
echo "  remote dir : ${REMOTE_DIR}"
echo

rsync -az --delete \
  --exclude "build" \
  --exclude "build-release" \
  --exclude ".git" \
  --exclude ".DS_Store" \
  --exclude "*.dSYM" \
  -e "ssh -p ${SSH_PORT}" \
  "${ROOT_DIR}/" \
  "${REMOTE}:${REMOTE_DIR}/"

ssh -p "${SSH_PORT}" "${REMOTE}" \
  "cd ${REMOTE_DIR} && bash scripts/build_release.sh"

echo
echo "Deploy finished."
echo "Run server on remote:"
echo "  ssh -p ${SSH_PORT} ${REMOTE}"
echo "  cd ${REMOTE_DIR}"
echo "  ./scripts/run_server.sh 0.0.0.0 7777"