#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""验证慢客户端不会阻塞其他客户端。"""

import argparse
import socket
import struct
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
            raise RuntimeError("连接关闭")

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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7777)

    args = parser.parse_args()

    control = socket.create_connection(
        (args.host, args.port),
        timeout=3.0,
    )

    slow = socket.create_connection(
        (args.host, args.port),
        timeout=3.0,
    )

    # 尽量缩小慢客户端接收缓冲区。
    slow.setsockopt(
        socket.SOL_SOCKET,
        socket.SO_RCVBUF,
        4096,
    )

    value = "x" * (32 * 1024)

    result = command(
        control,
        f"SET large_value {value}",
    )

    if result != "+OK":
        raise RuntimeError(
            f"SET failed: {result}"
        )

    # 慢客户端只发送请求，不读取响应。
    request = encode_frame("GET large_value")

    for _ in range(256):
        try:
            slow.sendall(request)
        except OSError:
            # 服务端可能因为达到硬上限主动关闭慢连接。
            break

    begin = time.perf_counter()

    result = command(control, "PING")

    elapsed_ms = (
        time.perf_counter() - begin
    ) * 1000.0

    if result != "+PONG":
        raise RuntimeError(
            f"control PING failed: {result}"
        )

    print(
        f"normal client response: {result}, "
        f"latency={elapsed_ms:.2f} ms"
    )

    if elapsed_ms > 1000.0:
        raise RuntimeError(
            "slow client significantly blocked normal client"
        )

    slow.close()
    command(control, "QUIT")
    control.close()

    print("slow client isolation test passed")


if __name__ == "__main__":
    main()