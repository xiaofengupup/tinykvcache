#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
TinyKVCache 多连接性能压测脚本。

测试模型：
    - 每个 worker 使用一个独立 TCP 连接；
    - 多个 worker 并发运行；
    - 每个 worker 顺序执行 PING / SET / GET；
    - 每轮包含 3 个请求；
    - 统计 QPS、平均延迟、P50、P95、P99 和最大延迟。

示例：
    python3 scripts/benchmark.py \
        --host 127.0.0.1 \
        --port 7777 \
        --connections 100 \
        --requests 1000

其中：
    --requests 表示每个连接执行的 PING/SET/GET 轮数。

理论请求总数：
    connections * requests * 3
"""

import argparse
import json
import socket
import struct
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Optional


def encode_frame(payload: str) -> bytes:
    data = payload.encode("utf-8")
    return struct.pack("!I", len(data)) + data


def recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks = []
    remaining = size

    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise RuntimeError("连接在完整 frame 到达前关闭")

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


def timed_command(sock: socket.socket, command: str) -> tuple[str, float]:
    begin_ns = time.perf_counter_ns()
    response = send_command(sock, command)
    elapsed_ms = (time.perf_counter_ns() - begin_ns) / 1_000_000.0
    return response, elapsed_ms


def percentile(values: list[float], ratio: float) -> float:
    if not values:
        return 0.0

    ordered = sorted(values)
    position = ratio * (len(ordered) - 1)
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    fraction = position - lower

    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


@dataclass
class WorkerResult:
    requests: int = 0
    errors: int = 0
    elapsed: float = 0.0
    latencies_ms: list[float] = field(default_factory=list)
    error_message: Optional[str] = None


def worker(
    worker_id: int,
    host: str,
    port: int,
    requests: int,
    value_size: int,
    connect_timeout: float,
    socket_timeout: float,
    barrier_timeout: float,
    start_barrier: threading.Barrier,
    results: list[WorkerResult],
) -> None:
    result = WorkerResult()
    value = "x" * value_size

    try:
        with socket.create_connection((host, port), timeout=connect_timeout) as sock:
            sock.settimeout(socket_timeout)

            try:
                start_barrier.wait(timeout=barrier_timeout)
            except threading.BrokenBarrierError as exc:
                raise RuntimeError("benchmark start barrier broken") from exc

            begin = time.perf_counter()

            for i in range(requests):
                key = f"bench:{worker_id}:{i}"

                response, latency_ms = timed_command(sock, "PING")
                result.requests += 1
                result.latencies_ms.append(latency_ms)
                if response != "+PONG":
                    result.errors += 1

                response, latency_ms = timed_command(sock, f"SET {key} {value}")
                result.requests += 1
                result.latencies_ms.append(latency_ms)
                if response != "+OK":
                    result.errors += 1

                response, latency_ms = timed_command(sock, f"GET {key}")
                result.requests += 1
                result.latencies_ms.append(latency_ms)
                if response != "$" + value:
                    result.errors += 1

            result.elapsed = time.perf_counter() - begin

            response = send_command(sock, "QUIT")
            if response != "+BYE":
                result.errors += 1

    except Exception as exc:
        result.errors += 1
        result.error_message = str(exc)

        try:
            start_barrier.abort()
        except Exception:
            pass

    finally:
        results[worker_id] = result


def validate_arguments(args: argparse.Namespace) -> None:
    if not args.host:
        raise ValueError("host must not be empty")

    if args.port < 1 or args.port > 65535:
        raise ValueError("port must be between 1 and 65535")

    if args.connections <= 0:
        raise ValueError("connections must be greater than zero")

    if args.requests <= 0:
        raise ValueError("requests must be greater than zero")

    if args.value_size < 0:
        raise ValueError("value-size must not be negative")

    if args.connect_timeout <= 0:
        raise ValueError("connect-timeout must be greater than zero")

    if args.socket_timeout <= 0:
        raise ValueError("socket-timeout must be greater than zero")

    if args.barrier_timeout <= 0:
        raise ValueError("barrier-timeout must be greater than zero")


def print_worker_errors(results: list[WorkerResult]) -> None:
    errors = [
        (worker_id, result.error_message)
        for worker_id, result in enumerate(results)
        if result.error_message
    ]

    if not errors:
        return

    print()
    print("worker errors")
    print("-------------")

    for worker_id, message in errors:
        print(f"worker[{worker_id}]: {message}")


def build_report(
    args: argparse.Namespace,
    results: list[WorkerResult],
    global_elapsed: float,
) -> dict:
    total_requests = sum(result.requests for result in results)
    total_errors = sum(result.errors for result in results)
    expected_requests = args.connections * args.requests * 3

    all_latencies_ms = []
    for result in results:
        all_latencies_ms.extend(result.latencies_ms)

    qps = total_requests / global_elapsed if global_elapsed > 0 else 0.0

    avg_latency_ms = (
        sum(all_latencies_ms) / len(all_latencies_ms)
        if all_latencies_ms
        else 0.0
    )

    completed_workers = sum(1 for result in results if result.error_message is None)
    failed_workers = args.connections - completed_workers

    return {
        "host": args.host,
        "port": args.port,
        "connections": args.connections,
        "requests_per_connection": args.requests,
        "value_size": args.value_size,
        "expected_requests": expected_requests,
        "total_requests": total_requests,
        "total_errors": total_errors,
        "completed_workers": completed_workers,
        "failed_workers": failed_workers,
        "elapsed_seconds": global_elapsed,
        "qps": qps,
        "latency_average_ms": avg_latency_ms,
        "latency_p50_ms": percentile(all_latencies_ms, 0.50),
        "latency_p95_ms": percentile(all_latencies_ms, 0.95),
        "latency_p99_ms": percentile(all_latencies_ms, 0.99),
        "latency_max_ms": max(all_latencies_ms) if all_latencies_ms else 0.0,
    }


def print_report(report: dict) -> None:
    print()
    print("benchmark result")
    print("----------------")
    print(f"connections       : {report['connections']}")
    print(f"completed_workers : {report['completed_workers']}")
    print(f"failed_workers    : {report['failed_workers']}")
    print(f"expected_requests : {report['expected_requests']}")
    print(f"total_requests    : {report['total_requests']}")
    print(f"total_errors      : {report['total_errors']}")
    print(f"elapsed_sec       : {report['elapsed_seconds']:.3f}")
    print(f"qps               : {report['qps']:.2f}")
    print(f"avg_latency_ms    : {report['latency_average_ms']:.3f}")
    print(f"latency_p50_ms    : {report['latency_p50_ms']:.3f}")
    print(f"latency_p95_ms    : {report['latency_p95_ms']:.3f}")
    print(f"latency_p99_ms    : {report['latency_p99_ms']:.3f}")
    print(f"latency_max_ms    : {report['latency_max_ms']:.3f}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="TinyKVCache multi-connection benchmark")

    parser.add_argument("--host", default="127.0.0.1", help="TinyKVCache 服务端地址")
    parser.add_argument("--port", type=int, default=7777, help="TinyKVCache 服务端端口")
    parser.add_argument("--connections", type=int, default=10, help="并发 TCP 连接数量")
    parser.add_argument("--requests", type=int, default=1000, help="每个连接执行的测试轮数")
    parser.add_argument("--value-size", type=int, default=32, help="SET Value 大小")
    parser.add_argument("--connect-timeout", type=float, default=5.0, help="连接超时")
    parser.add_argument("--socket-timeout", type=float, default=10.0, help="Socket I/O 超时")
    parser.add_argument("--barrier-timeout", type=float, default=10.0, help="Barrier 超时")
    parser.add_argument("--output-json", default="", help="JSON 结果输出路径")

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        validate_arguments(args)
    except ValueError as exc:
        print(f"invalid argument: {exc}", file=sys.stderr)
        return 2

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
                args.connect_timeout,
                args.socket_timeout,
                args.barrier_timeout,
                barrier,
                results,
            ),
            name=f"benchmark-worker-{worker_id}",
        )
        thread.start()
        threads.append(thread)

    print(
        f"benchmark preparing: host={args.host}, port={args.port}, "
        f"connections={args.connections}, requests={args.requests}, "
        f"value_size={args.value_size}"
    )

    try:
        barrier.wait(timeout=args.barrier_timeout)
    except threading.BrokenBarrierError:
        for thread in threads:
            thread.join()

        print("benchmark failed before all workers became ready", file=sys.stderr)
        print_worker_errors(results)
        return 1

    print("benchmark start")
    global_begin = time.perf_counter()

    for thread in threads:
        thread.join()

    global_elapsed = time.perf_counter() - global_begin

    report = build_report(args, results, global_elapsed)

    print_report(report)
    print_worker_errors(results)

    if args.output_json:
        try:
            with open(args.output_json, "w", encoding="utf-8") as output_file:
                json.dump(report, output_file, ensure_ascii=False, indent=2)
        except OSError as exc:
            print(f"failed to write benchmark report: {exc}", file=sys.stderr)
            return 1

        print()
        print(f"benchmark report written to: {args.output_json}")

    if report["total_requests"] != report["expected_requests"]:
        print("benchmark did not complete all expected requests", file=sys.stderr)
        return 1

    return 0 if report["total_errors"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
