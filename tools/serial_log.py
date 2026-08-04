#!/usr/bin/env python3
"""Continuously copy a serial port to an append-only log and stdout."""

from __future__ import annotations

import argparse
from datetime import datetime
import os
from pathlib import Path
import signal
import sys
import time

import serial



def _timestamp() -> str:
    return datetime.now().astimezone().isoformat(timespec="milliseconds")


def _marker(message: str) -> bytes:
    return f"\n# serial-log {_timestamp()} {message}\n".encode("utf-8")


def _open_serial(port: Path, baud: int) -> serial.Serial:
    connection = serial.Serial(
        port=None,
        baudrate=baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.5,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
        exclusive=True,
    )
    connection.port = str(port)
    connection.dtr = False
    connection.rts = False
    try:
        connection.open()
    except BaseException:
        connection.close()
        raise
    return connection


def _write_stdout(data: bytes) -> None:
    try:
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
    except BrokenPipeError:
        pass


def capture(
    port: Path,
    baud: int,
    output: Path,
    reconnect_delay: float,
) -> int:
    stopping = False

    def request_stop(_signum: int, _frame: object) -> None:
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("ab", buffering=0) as log:
        started = _marker(
            f"started port={port} baud={baud} pid={os.getpid()} output={output}"
        )
        log.write(started)
        _write_stdout(started)

        connection: serial.Serial | None = None
        unavailable_reason: str | None = None
        while not stopping:
            if connection is None:
                try:
                    connection = _open_serial(port, baud)
                except (OSError, ValueError, serial.SerialException) as exc:
                    reason = str(exc)
                    if reason != unavailable_reason:
                        event = _marker(f"waiting port={port} error={reason}")
                        log.write(event)
                        _write_stdout(event)
                        unavailable_reason = reason
                    time.sleep(reconnect_delay)
                    continue

                unavailable_reason = None
                event = _marker(f"connected port={port} baud={baud}")
                log.write(event)
                _write_stdout(event)

            try:
                data = connection.read(connection.in_waiting or 1)
                if data:
                    log.write(data)
                    _write_stdout(data)
            except (OSError, serial.SerialException) as exc:
                event = _marker(f"disconnected port={port} error={exc}")
                log.write(event)
                _write_stdout(event)
                connection.close()
                connection = None

        if connection is not None:
            connection.close()
        stopped = _marker(f"stopped port={port}")
        log.write(stopped)
        _write_stdout(stopped)
    return 0


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=Path, required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reconnect-delay", type=float, default=1.0)
    args = parser.parse_args(argv)
    if args.reconnect_delay <= 0:
        parser.error("--reconnect-delay must be greater than zero")
    if args.baud <= 0:
        parser.error("--baud must be greater than zero")
    return args


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(sys.argv[1:] if argv is None else argv)
    try:
        return capture(args.port, args.baud, args.output, args.reconnect_delay)
    except (OSError, ValueError, serial.SerialException) as exc:
        print(f"serial-log: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
