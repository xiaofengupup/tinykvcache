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
import json
from dataclasses import dataclass, field


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

def timed_command(sock: socket.socket, command_text: str) -> tuple[str, float]:
    begin_ns = time.perf_counter_ns()
    response = send_command(sock, command_text)
    elapsed_ms = (time.perf_counter_ns() - begin_ns) / 1_000_000.0

    return response, elapsed_ms

def percentile(values: list[float], ratio: float,) -> float:
    if not values:
        return 0.0

    ordered = sorted(values)
    position = ratio * (len(ordered) - 1)
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1,)
    fraction = position - lower

    return (
        ordered[lower]
        * (1.0 - fraction)
        + ordered[upper]
        * fraction
    )

@dataclass
class WorkerResult:
    requests: int = 0
    errors: int = 0
    elapsed: float = 0.0
    latencies_ms: list[float] = field(default_factory=list)


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
                resp, latency_ms = timed_command(sock, "PING")
                result.latencies_ms.append(latency_ms)
                if resp != "+PONG":
                    result.errors += 1
                result.requests += 1

                resp, latency_ms = timed_command(sock, f"SET {key} {value}")
                result.latencies_ms.append(latency_ms)
                if resp != "+OK":
                    result.errors += 1
                result.requests += 1

                resp, latency_ms = timed_command(sock, f"GET {key}")
                result.latencies_ms.append(latency_ms)
                if resp != "$" + value:
                    result.errors += 1
                result.requests += 1

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
    parser.add_argument("--output-json", default="",)

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

    all_latencies_ms = []
    for result in results:
        all_latencies_ms.extend(result.latencies_ms)

    p50_ms = percentile(all_latencies_ms, 0.50,)
    p95_ms = percentile(all_latencies_ms, 0.95,)
    p99_ms = percentile(all_latencies_ms, 0.99,)
    max_ms = (max(all_latencies_ms) if all_latencies_ms else 0.0)

    print()
    print("benchmark result")
    print("----------------")
    print(f"total_requests : {total_requests}")
    print(f"total_errors   : {total_errors}")
    print(f"elapsed_sec    : {global_elapsed:.3f}")
    print(f"qps            : {qps:.2f}")
    print(f"avg_latency_ms : {avg_latency_ms:.3f}")
    print(f"latency_p50_ms  : {p50_ms:.3f}")
    print(f"latency_p95_ms  : {p95_ms:.3f}")
    print(f"latency_p99_ms  : {p99_ms:.3f}")
    print(f"latency_max_ms  : {max_ms:.3f}")

    report = {
        "host": args.host,
        "port": args.port,
        "connections": args.connections,
        "requests_per_connection": args.requests,
        "value_size": args.value_size,
        "total_requests": total_requests,
        "total_errors": total_errors,
        "elapsed_seconds": global_elapsed,
        "qps": qps,
        "latency_average_ms": avg_latency_ms,
        "latency_p50_ms": p50_ms,
        "latency_p95_ms": p95_ms,
        "latency_p99_ms": p99_ms,
        "latency_max_ms": max_ms,
    }

    if args.output_json:
        with open(args.output_json, "w", encoding="utf-8") as output_file:
            json.dump(report, output_file, ensure_ascii=False, indent=2)

    return 0 if total_errors == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())