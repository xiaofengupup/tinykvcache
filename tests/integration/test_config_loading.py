#!/usr/bin/env python3
"""
TinyKVCache 配置加载集成测试

测试范围：
1. 完整 TOML 配置：覆盖所有配置项，验证服务可以启动、监听并优雅退出。
2. 部分 TOML 配置：验证未提供字段时可以使用默认值。
3. --config / --config=... / -c 三种配置文件参数形式。
4. CLI override：验证命令行参数覆盖 TOML 配置。
5. 无配置文件 + CLI：验证内置默认值路径。
6. 配置文件相关错误：
   - 文件不存在
   - TOML 语法错误
   - 重复指定配置文件
   - --config 缺失参数
7. 每个配置项的错误值/边界值。
8. 每个配置项的 TOML 类型错误。
9. CLI 参数错误。
10. 可选：未知 TOML 字段必须失败（--strict-unknown-fields）。

脚本只使用 Python 标准库。

示例：
    python3 tests/integration/test_config_loading.py \
        --server ./build/tinykv_server

如果 poller/sub_reactors 已经放入独立 [reactor]：
    python3 tests/integration/test_config_loading.py \
        --server ./build/tinykv_server \
        --reactor-section reactor
"""

from __future__ import annotations

import argparse
import os
import signal
import socket
import subprocess
import sys
import tempfile
import textwrap
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, Iterable, List, Optional, Sequence, Tuple


STARTUP_TIMEOUT_SECONDS = 4.0
FAILURE_TIMEOUT_SECONDS = 3.0
SHUTDOWN_TIMEOUT_SECONDS = 4.0
CONNECT_RETRY_INTERVAL_SECONDS = 0.03


@dataclass
class TestResult:
    name: str
    passed: bool
    detail: str = ""


class IntegrationTestFailure(RuntimeError):
    pass


def find_free_port() -> int:
    """向操作系统申请一个当前空闲的本地 TCP 端口。"""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def can_connect(host: str, port: int, timeout: float = 0.15) -> bool:
    """检查指定地址当前是否已经开始监听。"""
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def read_process_output(proc: subprocess.Popen[str]) -> str:
    """读取已退出进程的 stdout/stderr，便于失败诊断。"""
    try:
        stdout, stderr = proc.communicate(timeout=0.2)
    except subprocess.TimeoutExpired:
        return ""

    output: List[str] = []
    if stdout:
        output.append("[stdout]\n" + stdout.rstrip())
    if stderr:
        output.append("[stderr]\n" + stderr.rstrip())
    return "\n".join(output)


def terminate_process(proc: subprocess.Popen[str]) -> None:
    """测试结束或失败时兜底清理子进程。"""
    if proc.poll() is not None:
        return

    try:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=1.0)
        return
    except (ProcessLookupError, subprocess.TimeoutExpired):
        pass

    try:
        proc.kill()
    except ProcessLookupError:
        return

    try:
        proc.wait(timeout=1.0)
    except subprocess.TimeoutExpired:
        pass


def wait_until_listening(
    proc: subprocess.Popen[str],
    host: str,
    port: int,
    timeout: float = STARTUP_TIMEOUT_SECONDS,
) -> None:
    """等待服务真正开始监听，而不是固定 sleep。"""
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        return_code = proc.poll()
        if return_code is not None:
            output = read_process_output(proc)
            raise IntegrationTestFailure(
                f"服务在开始监听前退出，returncode={return_code}\n{output}"
            )

        if can_connect(host, port):
            return

        time.sleep(CONNECT_RETRY_INTERVAL_SECONDS)

    raise IntegrationTestFailure(
        f"等待服务监听超时：{host}:{port}，timeout={timeout}s"
    )


def graceful_stop(proc: subprocess.Popen[str]) -> None:
    """发送 SIGTERM，并要求服务完成 graceful shutdown。"""
    if proc.poll() is not None:
        output = read_process_output(proc)
        raise IntegrationTestFailure(
            f"发送 SIGTERM 前服务已经退出，returncode={proc.returncode}\n{output}"
        )

    proc.send_signal(signal.SIGTERM)

    try:
        return_code = proc.wait(timeout=SHUTDOWN_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired as exc:
        terminate_process(proc)
        raise IntegrationTestFailure(
            f"服务收到 SIGTERM 后 {SHUTDOWN_TIMEOUT_SECONDS}s 内未退出"
        ) from exc

    output = read_process_output(proc)

    if return_code != 0:
        raise IntegrationTestFailure(
            f"服务 graceful shutdown 返回非 0：returncode={return_code}\n{output}"
        )


def start_server(
    server: Path,
    args: Sequence[str],
) -> subprocess.Popen[str]:
    """启动 tinykv_server。"""
    command = [str(server), *args]

    return subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )


def assert_start_success(
    server: Path,
    args: Sequence[str],
    host: str,
    port: int,
    should_not_listen_port: Optional[int] = None,
) -> None:
    """断言配置有效：服务启动、监听正确端口，并能优雅退出。"""
    proc = start_server(server, args)

    try:
        wait_until_listening(proc, host, port)

        if should_not_listen_port is not None:
            if can_connect(host, should_not_listen_port):
                raise IntegrationTestFailure(
                    "服务错误地监听了本应被 CLI override 覆盖的端口："
                    f"{should_not_listen_port}"
                )

        graceful_stop(proc)
    finally:
        terminate_process(proc)


def assert_start_failure(
    server: Path,
    args: Sequence[str],
    timeout: float = FAILURE_TIMEOUT_SECONDS,
) -> None:
    """
    断言配置无效：进程必须在启动阶段失败并返回非 0。

    如果错误配置下进程仍然运行，则测试失败。
    """
    proc = start_server(server, args)

    try:
        try:
            return_code = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            raise IntegrationTestFailure(
                "错误配置没有导致启动失败；服务仍在运行。"
                "请检查配置验证是否缺失，或错误类型是否被 value_or() 静默回退。"
            ) from exc

        output = read_process_output(proc)

        if return_code == 0:
            raise IntegrationTestFailure(
                f"错误配置却返回成功状态 0\n{output}"
            )
    finally:
        terminate_process(proc)


def toml_string(value: str) -> str:
    """生成简单 TOML 字符串字面量。"""
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def make_full_config(
    port: int,
    overrides: Optional[Dict[str, str]] = None,
    extra_text: str = "",
) -> str:
    values: Dict[str, str] = {
        "server.host": toml_string("127.0.0.1"),
        "server.port": str(port),

        "reactor.poller": toml_string("auto"),
        "reactor.sub_reactors": "2",

        "network.max_read_buffer_bytes": str(
            2 * 1024 * 1024 + 4
        ),
        "network.write_high_watermark_bytes": str(
            768 * 1024
        ),
        "network.write_low_watermark_bytes": str(
            256 * 1024
        ),
        "network.write_hard_limit_bytes": str(
            2 * 1024 * 1024
        ),
        "network.max_write_bytes_per_event": str(
            32 * 1024
        ),
        "network.max_accepts_per_event": "32",

        "shutdown.graceful_timeout_ms": "1500",
        "ttl.sweep_interval_ms": "250",
        "logging.level": toml_string("debug"),
    }

    if overrides:
        values.update(overrides)

    return (
        "[server]\n"
        f'host = {values["server.host"]}\n'
        f'port = {values["server.port"]}\n'
        "\n"
        "[reactor]\n"
        f'poller = {values["reactor.poller"]}\n'
        f'sub_reactors = {values["reactor.sub_reactors"]}\n'
        "\n"
        "[network]\n"
        f'max_read_buffer_bytes = '
        f'{values["network.max_read_buffer_bytes"]}\n'
        f'write_high_watermark_bytes = '
        f'{values["network.write_high_watermark_bytes"]}\n'
        f'write_low_watermark_bytes = '
        f'{values["network.write_low_watermark_bytes"]}\n'
        f'write_hard_limit_bytes = '
        f'{values["network.write_hard_limit_bytes"]}\n'
        f'max_write_bytes_per_event = '
        f'{values["network.max_write_bytes_per_event"]}\n'
        f'max_accepts_per_event = '
        f'{values["network.max_accepts_per_event"]}\n'
        "\n"
        "[shutdown]\n"
        f'graceful_timeout_ms = '
        f'{values["shutdown.graceful_timeout_ms"]}\n'
        "\n"
        "[ttl]\n"
        f'sweep_interval_ms = '
        f'{values["ttl.sweep_interval_ms"]}\n'
        "\n"
        "[logging]\n"
        f'level = {values["logging.level"]}\n'
        + (
            f"\n{extra_text.strip()}\n"
            if extra_text.strip()
            else ""
        )
    )

def write_config(directory: Path, name: str, content: str) -> Path:
    """写入临时 TOML 文件。"""
    path = directory / name
    path.write_text(content, encoding="utf-8")
    return path


def run_case(name: str, func: Callable[[], None]) -> TestResult:
    """执行单个测试，并以类似 GoogleTest 的格式打印结果。"""
    print(f"[ RUN      ] {name}")

    try:
        func()
    except Exception as exc:
        print(f"[  FAILED  ] {name}")
        print(textwrap.indent(str(exc), "             "))
        return TestResult(name=name, passed=False, detail=str(exc))

    print(f"[       OK ] {name}")
    return TestResult(name=name, passed=True)


def valid_full_config_case(
    server: Path,
    temp_dir: Path,
) -> None:
    """正确路径：全部配置项均显式给出并使用非默认值。"""
    port = find_free_port()
    config = write_config(
        temp_dir,
        "full-valid.toml",
        make_full_config(port),
    )

    assert_start_success(
        server,
        ["--config", str(config)],
        "127.0.0.1",
        port,
    )


def partial_config_case(
    server: Path,
    temp_dir: Path,
) -> None:
    """
    正确路径：只提供部分配置，其余字段依赖 AppConfig 默认值。

    端口使用 CLI 覆盖，避免默认端口与本机其他服务冲突。
    """
    port = find_free_port()

    config = write_config(
        temp_dir,
        "partial-valid.toml",
        '[logging]\nlevel = "info"\n',
    )

    assert_start_success(
        server,
        [
            "--config",
            str(config),
            "--host",
            "127.0.0.1",
            "--port",
            str(port),
        ],
        "127.0.0.1",
        port,
    )


def config_option_forms_case(
    server: Path,
    temp_dir: Path,
) -> None:
    """正确路径：覆盖 --config、--config=... 和 -c 三种形式。"""
    forms: List[Tuple[str, Callable[[Path], List[str]]]] = [
        ("long", lambda path: ["--config", str(path)]),
        ("inline", lambda path: [f"--config={path}"]),
        ("short", lambda path: ["-c", str(path)]),
    ]

    for suffix, make_args in forms:
        port = find_free_port()
        config = write_config(
            temp_dir,
            f"config-form-{suffix}.toml",
            make_full_config(port),
        )

        assert_start_success(
            server,
            make_args(config),
            "127.0.0.1",
            port,
        )


def cli_override_case(
    server: Path,
    temp_dir: Path,
) -> None:
    """
    正确路径：CLI 必须覆盖 TOML。

    TOML 使用 config_port，CLI 使用 override_port。
    最终必须监听 override_port。
    """
    config_port = find_free_port()
    override_port = find_free_port()

    while override_port == config_port:
        override_port = find_free_port()

    config = write_config(
        temp_dir,
        "cli-override.toml",
        make_full_config(config_port),
    )

    assert_start_success(
        server,
        [
            "--config",
            str(config),
            "--host",
            "127.0.0.1",
            "--port",
            str(override_port),
            "--poller",
            "auto",
            "--sub-reactors",
            "1",
            "--log-level",
            "info",
        ],
        "127.0.0.1",
        override_port,
        should_not_listen_port=config_port,
    )


def no_config_cli_case(server: Path) -> None:
    """正确路径：不指定配置文件，只使用默认配置 + CLI override。"""
    port = find_free_port()

    assert_start_success(
        server,
        [
            "--host",
            "127.0.0.1",
            "--port",
            str(port),
            "--poller",
            "auto",
            "--sub-reactors",
            "1",
            "--log-level",
            "warn",
        ],
        "127.0.0.1",
        port,
    )


def config_file_error_cases(
    server: Path,
    temp_dir: Path,
) -> Iterable[Tuple[str, Callable[[], None]]]:
    """配置文件路径和 TOML 文本本身的错误路径。"""
    missing = temp_dir / "does-not-exist.toml"

    yield (
        "config.missing_file",
        lambda: assert_start_failure(
            server,
            ["--config", str(missing)],
        ),
    )

    malformed = write_config(
        temp_dir,
        "malformed.toml",
        '[server\nhost = "127.0.0.1"\nport = 6379\n',
    )

    yield (
        "config.malformed_toml",
        lambda: assert_start_failure(
            server,
            ["--config", str(malformed)],
        ),
    )

    first = write_config(
        temp_dir,
        "duplicate-a.toml",
        make_full_config(find_free_port()),
    )

    second = write_config(
        temp_dir,
        "duplicate-b.toml",
        make_full_config(find_free_port()),
    )

    yield (
        "config.duplicate_config_option",
        lambda: assert_start_failure(
            server,
            ["--config", str(first), "--config", str(second)],
        ),
    )

    yield (
        "config.missing_config_value",
        lambda: assert_start_failure(
            server,
            ["--config"],
        ),
    )


def semantic_error_cases() -> List[Tuple[str, Dict[str, str]]]:
    """覆盖所有主要配置项的非法值/边界值。"""
    return [
        ("invalid.host_empty", {
            "server.host": toml_string(""),
        }),
        ("invalid.port_zero", {
            "server.port": "0",
        }),
        ("invalid.port_too_large", {
            "server.port": "65536",
        }),
        ("invalid.poller", {
            "reactor.poller": toml_string("not-a-poller"),
        }),
        ("invalid.sub_reactors_too_large", {
            "reactor.sub_reactors": "17",
        }),
        ("invalid.max_read_buffer_too_small", {
            "network.max_read_buffer_bytes": "3",
        }),
        ("invalid.low_watermark_exceeds_high", {
            "network.write_low_watermark_bytes": str(900 * 1024),
            "network.write_high_watermark_bytes": str(800 * 1024),
        }),
        ("invalid.high_watermark_exceeds_hard_limit", {
            "network.write_high_watermark_bytes": str(3 * 1024 * 1024),
            "network.write_hard_limit_bytes": str(2 * 1024 * 1024),
        }),
        ("invalid.max_write_bytes_per_event_zero", {
            "network.max_write_bytes_per_event": "0",
        }),
        ("invalid.max_accepts_per_event_zero", {
            "network.max_accepts_per_event": "0",
        }),
        ("invalid.graceful_timeout_zero", {
            "shutdown.graceful_timeout_ms": "0",
        }),
        ("invalid.sweep_interval_negative", {
            "ttl.sweep_interval_ms": "-1",
        }),
        ("invalid.log_level", {
            "logging.level": toml_string("verbose"),
        }),
    ]


def type_error_cases() -> List[Tuple[str, Dict[str, str]]]:
    """
    每个配置项的 TOML 类型错误。

    如果 Loader 对错误类型使用 value_or(default)，这些用例可能会错误通过，
    从而暴露“错误类型被静默回退”的问题。
    """
    return [
        ("type_error.host", {
            "server.host": "1234",
        }),
        ("type_error.port", {
            "server.port": toml_string("6379"),
        }),
        ("type_error.poller", {
            "reactor.poller": "123",
        }),
        ("type_error.sub_reactors", {
            "reactor.sub_reactors": toml_string("2"),
        }),
        ("type_error.max_read_buffer_bytes", {
            "network.max_read_buffer_bytes": toml_string("1048580"),
        }),
        ("type_error.write_high_watermark_bytes", {
            "network.write_high_watermark_bytes": toml_string("1048576"),
        }),
        ("type_error.write_low_watermark_bytes", {
            "network.write_low_watermark_bytes": toml_string("524288"),
        }),
        ("type_error.write_hard_limit_bytes", {
            "network.write_hard_limit_bytes": toml_string("4194304"),
        }),
        ("type_error.max_write_bytes_per_event", {
            "network.max_write_bytes_per_event": toml_string("65536"),
        }),
        ("type_error.max_accepts_per_event", {
            "network.max_accepts_per_event": toml_string("64"),
        }),
        ("type_error.graceful_timeout_ms", {
            "shutdown.graceful_timeout_ms": toml_string("3000"),
        }),
        ("type_error.sweep_interval_ms", {
            "ttl.sweep_interval_ms": toml_string("1000"),
        }),
        ("type_error.log_level", {
            "logging.level": "123",
        }),
    ]


def cli_error_cases(
    server: Path,
) -> Iterable[Tuple[str, Callable[[], None]]]:
    """CLI parser 的错误路径。"""
    return [
        (
            "cli.unknown_option",
            lambda: assert_start_failure(
                server,
                ["--definitely-not-an-option", "123"],
            ),
        ),
        (
            "cli.port_not_integer",
            lambda: assert_start_failure(
                server,
                ["--port", "6379abc"],
            ),
        ),
        (
            "cli.port_missing_value",
            lambda: assert_start_failure(
                server,
                ["--port"],
            ),
        ),
        (
            "cli.sub_reactors_not_integer",
            lambda: assert_start_failure(
                server,
                ["--sub-reactors", "abc"],
            ),
        ),
        (
            "cli.invalid_poller",
            lambda: assert_start_failure(
                server,
                ["--poller", "invalid"],
            ),
        ),
        (
            "cli.invalid_log_level",
            lambda: assert_start_failure(
                server,
                ["--log-level", "verbose"],
            ),
        ),
    ]


def strict_unknown_field_case(
    server: Path,
    temp_dir: Path,
) -> None:
    """可选：未知 section/key 必须导致启动失败。"""
    port = find_free_port()

    config = write_config(
        temp_dir,
        "unknown-field.toml",
        make_full_config(
            port,
            extra_text=(
                "[unexpected]\n"
                "this_key_should_not_exist = 1"
            ),
        ),
    )

    assert_start_failure(
        server,
        ["--config", str(config)],
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="TinyKVCache 配置加载黑盒集成测试"
    )

    parser.add_argument(
        "--server",
        required=True,
        type=Path,
        help="tinykv_server 可执行文件路径",
    )

    parser.add_argument(
        "--strict-unknown-fields",
        action="store_true",
        help="要求未知 TOML section/key 导致启动失败",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    server = args.server.resolve()

    if not server.exists():
        print(
            f"error: server executable does not exist: {server}",
            file=sys.stderr,
        )
        return 2

    if not server.is_file():
        print(
            f"error: server path is not a file: {server}",
            file=sys.stderr,
        )
        return 2

    if not os.access(server, os.X_OK):
        print(
            f"error: server is not executable: {server}",
            file=sys.stderr,
        )
        return 2

    results: List[TestResult] = []

    with tempfile.TemporaryDirectory(
        prefix="tinykv-config-test-"
    ) as temp:
        temp_dir = Path(temp)

        results.append(
            run_case(
                "valid.full_config_all_fields",
                lambda: valid_full_config_case(
                    server,
                    temp_dir,
                ),
            )
        )

        results.append(
            run_case(
                "valid.partial_config_uses_defaults",
                lambda: partial_config_case(
                    server,
                    temp_dir,
                ),
            )
        )

        results.append(
            run_case(
                "valid.config_option_forms",
                lambda: config_option_forms_case(
                    server,
                    temp_dir,
                ),
            )
        )

        results.append(
            run_case(
                "valid.cli_overrides_config",
                lambda: cli_override_case(
                    server,
                    temp_dir,
                ),
            )
        )

        results.append(
            run_case(
                "valid.no_config_cli_only",
                lambda: no_config_cli_case(server),
            )
        )

        for name, case in config_file_error_cases(
            server,
            temp_dir,
        ):
            results.append(run_case(name, case))

        for name, overrides in semantic_error_cases():
            def make_case(
                case_name: str = name,
                case_overrides: Dict[str, str] = overrides,
            ) -> None:
                config = write_config(
                    temp_dir,
                    f"{case_name.replace('.', '-')}.toml",
                    make_full_config(
                        find_free_port(),
                        overrides=case_overrides,
                    ),
                )

                assert_start_failure(
                    server,
                    ["--config", str(config)],
                )

            results.append(run_case(name, make_case))

        for name, overrides in type_error_cases():
            def make_type_case(
                case_name: str = name,
                case_overrides: Dict[str, str] = overrides,
            ) -> None:
                config = write_config(
                    temp_dir,
                    f"{case_name.replace('.', '-')}.toml",
                    make_full_config(
                        find_free_port(),
                        overrides=case_overrides,
                    ),
                )

                assert_start_failure(
                    server,
                    ["--config", str(config)],
                )

            results.append(run_case(name, make_type_case))

        for name, case in cli_error_cases(server):
            results.append(run_case(name, case))

        if args.strict_unknown_fields:
            results.append(
                run_case(
                    "config.unknown_field_rejected",
                    lambda: strict_unknown_field_case(
                        server,
                        temp_dir,
                    ),
                )
            )

    passed = sum(result.passed for result in results)
    failed = len(results) - passed

    print()
    print("=" * 72)
    print("TinyKVCache 配置加载集成测试结果")
    print("=" * 72)
    print(f"总计: {len(results)}")
    print(f"通过: {passed}")
    print(f"失败: {failed}")

    if failed:
        print()
        print("失败用例：")
        for result in results:
            if not result.passed:
                print(f"  - {result.name}")

        print()
        print(
            "提示：如果 type_error.* 用例失败且服务仍然能够启动，"
            "请检查 ConfigLoader 是否使用 value_or() 将错误 TOML 类型"
            "静默回退成默认值。生产配置建议严格区分“字段不存在”和“字段类型错误”。"
        )
        return 1

    print()
    print("所有配置加载集成测试通过。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
