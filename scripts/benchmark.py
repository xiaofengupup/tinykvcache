#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
TinyKVCache 简单压测脚本。

特点：
    1. 使用多个 TCP 连接；
    2. 每个连接顺序发送多个 SET/GET/PING；
    3. 统计总请求数、耗时、QPS、平均延迟。

示例：
    python3 scripts/benchmark.py --host 127.0.0.1 --port 7777 --connections 10 --requests 1000
"""

import argparse
import socket
import struct
import threading
import time
from dataclasses import dataclass


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


@dataclass
class WorkerResult:
    requests: int = 0
    errors: int = 0
    elapsed: float = 0.0


def worker(
    worker_id: int,
    host: str,
    port: int,
    requests: int,
    value_size: int,
    start_barrier: threading.Barrier,
    results: list[WorkerResult],
) -> None:
    result = WorkerResult()

    value = "x" * value_size

    try:
        with socket.create_connection((host, port), timeout=5.0) as sock:
            start_barrier.wait()

            begin = time.perf_counter()

            for i in range(requests):
                key = f"bench:{worker_id}:{i}"

                # 三类命令混合：
                #   PING 用于最小请求；
                #   SET/GET 用于验证 KV 路径；
                #   这里每轮计 3 个请求。
                resp = send_command(sock, "PING")
                if resp != "+PONG":
                    result.errors += 1

                resp = send_command(sock, f"SET {key} {value}")
                if resp != "+OK":
                    result.errors += 1

                resp = send_command(sock, f"GET {key}")
                if resp != "$" + value:
                    result.errors += 1

                result.requests += 3

            send_command(sock, "QUIT")

            result.elapsed = time.perf_counter() - begin

    except Exception:
        result.errors += 1
        result.elapsed = 0.0

    results[worker_id] = result


def main() -> int:
    parser = argparse.ArgumentParser()

    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7777)
    parser.add_argument("--connections", type=int, default=10)
    parser.add_argument("--requests", type=int, default=1000)
    parser.add_argument("--value-size", type=int, default=32)

    args = parser.parse_args()

    results = [WorkerResult() for _ in range(args.connections)]
    barrier = threading.Barrier(args.connections + 1)

    threads = []

    for worker_id in range(args.connections):
        thread = threading.Thread(
            target=worker,
            args=(
                worker_id,
                args.host,
                args.port,
                args.requests,
                args.value_size,
                barrier,
                results,
            ),
        )

        thread.start()
        threads.append(thread)

    print(
        f"benchmark start: "
        f"host={args.host}, "
        f"port={args.port}, "
        f"connections={args.connections}, "
        f"requests_per_connection={args.requests}"
    )

    global_begin = time.perf_counter()
    barrier.wait()

    for thread in threads:
        thread.join()

    global_elapsed = time.perf_counter() - global_begin

    total_requests = sum(r.requests for r in results)
    total_errors = sum(r.errors for r in results)

    qps = total_requests / global_elapsed if global_elapsed > 0 else 0.0
    avg_latency_ms = (
        global_elapsed * 1000.0 * args.connections / total_requests
        if total_requests > 0
        else 0.0
    )

    print()
    print("benchmark result")
    print("----------------")
    print(f"total_requests : {total_requests}")
    print(f"total_errors   : {total_errors}")
    print(f"elapsed_sec    : {global_elapsed:.3f}")
    print(f"qps            : {qps:.2f}")
    print(f"avg_latency_ms : {avg_latency_ms:.3f}")

    return 0 if total_errors == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())