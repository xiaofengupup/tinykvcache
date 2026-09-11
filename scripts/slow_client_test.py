#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""验证慢客户端不会阻塞同一个 Sub Reactor 中的正常客户端。"""

import argparse
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
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
            raise RuntimeError("连接在完整响应到达前关闭")

        chunks.append(chunk)
        remaining -= len(chunk)

    return b"".join(chunks)


def recv_frame(sock: socket.socket) -> str:
    header = recv_exact(sock, 4)
    (size,) = struct.unpack("!I", header)
    return recv_exact(sock, size).decode("utf-8")


def command(sock: socket.socket, text: str) -> str:
    sock.sendall(encode_frame(text))
    return recv_frame(sock)


def reserve_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_until_ready(
    process: subprocess.Popen,
    host: str,
    port: int,
    timeout: float = 5.0,
) -> None:
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(
                f"服务端启动前提前退出，exit code={process.returncode}"
            )

        try:
            with socket.create_connection((host, port), timeout=0.1):
                return
        except OSError:
            time.sleep(0.05)

    raise TimeoutError("等待 TinyKVCache 启动超时")


def stop_process(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return

    try:
        process.send_signal(signal.SIGTERM)
        process.wait(timeout=5.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3.0)


def run_test(server_path: str, poller: str) -> None:
    host = "127.0.0.1"
    port = reserve_free_port()
    failure: Optional[BaseException] = None

    with tempfile.TemporaryFile(mode="w+t") as server_log:
        process = subprocess.Popen(
            [
                server_path,
                "--host",
                host,
                "--port",
                str(port),
                "--poller",
                poller,
                "--sub-reactors",
                "1",
                "--log-level",
                "warn",
            ],
            stdout=server_log,
            stderr=subprocess.STDOUT,
            text=True,
        )

        control: Optional[socket.socket] = None
        slow: Optional[socket.socket] = None

        try:
            wait_until_ready(process, host, port)

            control = socket.create_connection((host, port), timeout=3.0)
            control.settimeout(3.0)

            slow = socket.create_connection((host, port), timeout=3.0)
            slow.settimeout(3.0)

            # 缩小慢客户端接收缓冲区，使服务端更快产生写侧背压。
            slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)

            response = command(control, "PING")
            if response != "+PONG":
                raise AssertionError(f"initial PING failed: {response!r}")

            # 32 KiB Value，用于快速制造大量待发送数据。
            value = "x" * (32 * 1024)

            response = command(control, f"SET large_value {value}")
            if response != "+OK":
                raise AssertionError(f"SET large_value failed: {response!r}")

            # slow client 连续请求大 Value，但故意不读取响应。
            request = encode_frame("GET large_value")
            sent_requests = 0

            for _ in range(256):
                try:
                    slow.sendall(request)
                    sent_requests += 1
                except OSError:
                    # 服务端可能因 write hard limit 主动关闭 slow client。
                    break

            if sent_requests == 0:
                raise AssertionError("slow client 未成功发送任何请求")

            # 给服务端短暂时间处理请求并积累 write buffer。
            time.sleep(0.05)

            begin = time.perf_counter()
            response = command(control, "PING")
            elapsed_ms = (time.perf_counter() - begin) * 1000.0

            if response != "+PONG":
                raise AssertionError(f"control PING failed: {response!r}")

            print(
                f"normal client response: {response}, latency={elapsed_ms:.2f} ms"
            )

            # 这里只判断是否发生明显阻塞，并非严格性能指标。
            if elapsed_ms > 1000.0:
                raise AssertionError(
                    f"slow client blocked normal client: {elapsed_ms:.2f} ms"
                )

            response = command(control, "PING")
            if response != "+PONG":
                raise AssertionError(f"second control PING failed: {response!r}")

            response = command(control, "QUIT")
            if response != "+BYE":
                raise AssertionError(f"QUIT failed: {response!r}")

            print(
                f"slow client isolation test passed, sent_requests={sent_requests}"
            )

        except BaseException as exc:
            failure = exc

        finally:
            if slow is not None:
                try:
                    slow.close()
                except OSError:
                    pass

            if control is not None:
                try:
                    control.close()
                except OSError:
                    pass

            stop_process(process)

        if failure is not None:
            server_log.flush()
            server_log.seek(0)

            print("----- tinykv_server log -----", file=sys.stderr)
            print(server_log.read(), file=sys.stderr)
            print("-----------------------------", file=sys.stderr)

            raise failure


def main() -> int:
    parser = argparse.ArgumentParser(
        description="TinyKVCache slow client isolation test"
    )
    parser.add_argument("--server", required=True, help="tinykv_server 可执行文件路径")
    parser.add_argument(
        "--poller",
        default="auto",
        choices=["auto", "poll", "epoll"],
        help="Poller backend，默认 auto",
    )

    args = parser.parse_args()
    run_test(args.server, args.poller)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"slow client isolation test failed: {exc}", file=sys.stderr)
        raise SystemExit(1)