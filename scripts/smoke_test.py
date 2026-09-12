#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
TinyKVCache 冒烟测试脚本。

用途：
    验证服务端是否可以正常处理基础命令。

示例：
    python3 scripts/smoke_test.py --host 127.0.0.1 --port 7777
"""

import argparse
import socket
import struct
import sys
import time


def encode_frame(payload: str) -> bytes:
    data = payload.encode("utf-8")
    return struct.pack("!I", len(data)) + data


def recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks = []
    remaining = size

    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise RuntimeError("连接已关闭")

        chunks.append(chunk)
        remaining -= len(chunk)

    return b"".join(chunks)


def recv_frame(sock: socket.socket) -> str:
    header = recv_exact(sock, 4)
    (length,) = struct.unpack("!I", header)
    body = recv_exact(sock, length)
    return body.decode("utf-8")


def send_command(sock: socket.socket, command: str) -> str:
    sock.sendall(encode_frame(command))
    return recv_frame(sock)


def expect(sock: socket.socket, command: str, expected: str) -> None:
    actual = send_command(sock, command)

    if actual != expected:
        raise AssertionError(
            f"命令 {command!r} 期望 {expected!r}，实际 {actual!r}"
        )

    print(f"[OK] {command} -> {actual}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7777)
    parser.add_argument("--timeout", type=float, default=3.0)

    args = parser.parse_args()

    with socket.create_connection(
        (args.host, args.port),
        timeout=args.timeout
    ) as sock:
        expect(sock, "PING", "+PONG")
        expect(sock, "SET name xiaofeng", "+OK")
        expect(sock, "GET name", "$xiaofeng")
        expect(sock, "SET sentence hello tiny kv cache", "+OK")
        expect(sock, "GET sentence", "$hello tiny kv cache")
        expect(sock, "EXPIRE name 1", "+OK")

        ttl_response = send_command(sock, "TTL name")
        if not ttl_response.startswith("$"):
            raise AssertionError(f"TTL 响应格式错误: {ttl_response!r}")

        print(f"[OK] TTL name -> {ttl_response}")

        time.sleep(1.2)

        expect(sock, "GET name", "$nil")

        stats = send_command(sock, "STATS")
        required_fields = [
            "+keys=1",
            "persistent=1",
            "expiring=0",
            "connections_active=",
            "connections_accepted=",
            "bytes_received=",
            "bytes_sent=",
            "commands=",
            "protocol_errors=",
        ]
        for field in required_fields:
            if field not in stats:
                raise AssertionError(f"STATS 缺少字段 {field!r}: {stats!r}")
        print(f"[OK] STATS -> {stats}")

        expect(sock, "QUIT", "+BYE")

    print("smoke test passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"smoke test failed: {exc}", file=sys.stderr)
        raise SystemExit(1)