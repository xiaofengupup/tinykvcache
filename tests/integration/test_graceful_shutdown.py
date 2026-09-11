#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""验证 SIGTERM 可以让 TinyKVCache 优雅退出。"""

import argparse
import signal
import socket
import struct
import subprocess
import sys
import tempfile
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
            raise RuntimeError(
                "连接在完整响应到达前关闭"
            )

        chunks.append(chunk)
        remaining -= len(chunk)

    return b"".join(chunks)


def recv_frame(sock: socket.socket) -> str:
    header = recv_exact(sock, 4)
    (size,) = struct.unpack("!I", header)

    return recv_exact(
        sock,
        size,
    ).decode("utf-8")


def send_command(
    sock: socket.socket,
    command: str,
) -> str:
    sock.sendall(encode_frame(command))
    return recv_frame(sock)


def reserve_free_port() -> int:
    with socket.socket(
        socket.AF_INET,
        socket.SOCK_STREAM,
    ) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_until_ready(
    process: subprocess.Popen,
    host: str,
    port: int,
) -> None:
    deadline = time.monotonic() + 5.0

    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(
                "服务端启动前提前退出"
            )

        try:
            with socket.create_connection(
                (host, port),
                timeout=0.1,
            ):
                return
        except OSError:
            time.sleep(0.05)

    raise TimeoutError("等待服务端启动超时")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--poller", default="auto", choices=["auto", "poll", "epoll"])

    args = parser.parse_args()

    host = "127.0.0.1"
    port = reserve_free_port()

    with tempfile.TemporaryFile(
        mode="w+t"
    ) as server_log:
        process = subprocess.Popen(
            [
                args.server,
                "--host", host,
                "--port", str(port),
                "--poller", args.poller,
                "--sub-reactors", "2",
                "--log-level", "warn",
            ],
            stdout=server_log,
            stderr=subprocess.STDOUT,
            text=True,
        )

        client = None

        try:
            wait_until_ready(
                process,
                host,
                port,
            )

            client = socket.create_connection(
                (host, port),
                timeout=2.0,
            )

            client.settimeout(2.0)

            response = send_command(
                client,
                "PING",
            )

            if response != "+PONG":
                raise AssertionError(
                    f"unexpected PING response: {response}"
                )

            begin = time.monotonic()

            process.send_signal(signal.SIGTERM)

            exitCode = process.wait(timeout=5.0)

            elapsed = time.monotonic() - begin

            if exitCode != 0:
                raise AssertionError(
                    f"server exit code is {exitCode}"
                )

            if elapsed > 4.5:
                raise AssertionError(
                    "server shutdown took too long"
                )

            # 服务端退出后，连接应关闭。
            data = client.recv(1)

            if data != b"":
                raise AssertionError(
                    "client connection was not closed"
                )

        except Exception:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=3.0)

            server_log.flush()
            server_log.seek(0)

            print(
                "----- tinykv_server log -----",
                file=sys.stderr,
            )

            print(
                server_log.read(),
                file=sys.stderr,
            )

            raise

        finally:
            if client is not None:
                client.close()

    print("graceful shutdown test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())