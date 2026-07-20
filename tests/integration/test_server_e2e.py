#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""TinyKVCache 端到端集成测试。"""

import argparse
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
            raise RuntimeError("连接在完整 frame 到达前关闭")

        chunks.append(chunk)
        remaining -= len(chunk)

    return b"".join(chunks)


def recv_frame(sock: socket.socket) -> str:
    header = recv_exact(sock, 4)
    (body_size,) = struct.unpack("!I", header)

    body = recv_exact(sock, body_size)
    return body.decode("utf-8")


def send_command(sock: socket.socket, command: str) -> str:
    sock.sendall(encode_frame(command))
    return recv_frame(sock)


def expect(
    sock: socket.socket,
    command: str,
    expected: str,
) -> None:
    actual = send_command(sock, command)

    if actual != expected:
        raise AssertionError(
            f"{command!r}: expected {expected!r}, got {actual!r}"
        )


def reserve_free_port() -> int:
    """申请一个当前可用的本地端口。"""

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_until_ready(
    process: subprocess.Popen,
    host: str,
    port: int,
    timeout: float,
) -> None:
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(
                f"服务端提前退出，exit code={process.returncode}"
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


def stop_process(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return

    process.terminate()

    try:
        process.wait(timeout=3.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3.0)


def run_test(server_path: str) -> None:
    host = "127.0.0.1"
    port = reserve_free_port()

    failure: Optional[BaseException] = None

    with tempfile.TemporaryFile(mode="w+t") as server_log:
        process = subprocess.Popen(
            [server_path, host, str(port)],
            stdout=server_log,
            stderr=subprocess.STDOUT,
            text=True,
        )

        try:
            wait_until_ready(process, host, port, timeout=5.0)

            with socket.create_connection(
                (host, port),
                timeout=2.0,
            ) as client_a:
                client_a.settimeout(2.0)

                stats = send_command(client_a, "STATS");
                required_fields = [
                    "+keys=",
                    "connections_active=",
                    "connections_accepted=",
                    "bytes_received=",
                    "bytes_sent=",
                    "commands=",
                    "protocol_errors=",
                    "latency_avg_us=",
                    "latency_p95_upper_us=",
                ]
                for field in required_fields:
                    if field not in stats:
                        raise AssertionError(
                            f"missing STATS field {field!r}: "
                            f"{stats!r}"
                        )

                # A 连接后保持空闲，验证 B 仍然可以被正常处理。
                with socket.create_connection(
                    (host, port),
                    timeout=2.0,
                ) as client_b:
                    client_b.settimeout(2.0)

                    expect(client_b, "PING", "+PONG")
                    expect(client_b, "SET shared 42", "+OK")

                    # 多客户端共享同一个 KVStore。
                    expect(client_a, "GET shared", "$42")

                    # 模拟半包：长度头也被拆成两次发送。
                    fragmented = encode_frame("PING")
                    client_a.sendall(fragmented[:2])
                    time.sleep(0.01)
                    client_a.sendall(fragmented[2:])

                    actual = recv_frame(client_a)
                    if actual != "+PONG":
                        raise AssertionError(
                            f"fragmented frame: got {actual!r}"
                        )

                    # 模拟粘包：两条 frame 一次发送。
                    client_b.sendall(
                        encode_frame("PING")
                        + encode_frame("STATS")
                    )

                    first = recv_frame(client_b)
                    second = recv_frame(client_b)

                    if first != "+PONG":
                        raise AssertionError(
                            f"pipelined PING: got {first!r}"
                        )

                    if not second.startswith("+keys="):
                        raise AssertionError(
                            f"pipelined STATS: got {second!r}"
                        )

                    # QUIT 只关闭 B，不影响 A 和服务端。
                    expect(client_b, "QUIT", "+BYE")
                    expect(client_a, "PING", "+PONG")
                    expect(client_a, "QUIT", "+BYE")

        except BaseException as exc:
            failure = exc

        finally:
            stop_process(process)

        if failure is not None:
            server_log.flush()
            server_log.seek(0)

            print(
                "----- tinykv_server log -----",
                file=sys.stderr,
            )
            print(server_log.read(), file=sys.stderr)
            print(
                "-----------------------------",
                file=sys.stderr,
            )

            raise failure


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)

    args = parser.parse_args()

    run_test(args.server)

    print("server e2e test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())