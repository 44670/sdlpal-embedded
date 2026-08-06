#!/usr/bin/env python3
"""Drive the opt-in DeSmuME SDL CLI WebSocket harness."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import sys
from typing import Any
from urllib.parse import urlparse
import zlib


WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
SHOT_MAGIC = b"NDSSHOT1"
DS_KEYS = (
    "a", "b", "select", "start", "right", "left",
    "up", "down", "r", "l", "x", "y",
)


class HarnessError(RuntimeError):
    pass


def recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks: list[bytes] = []
    while size:
        chunk = sock.recv(size)
        if not chunk:
            raise HarnessError("WebSocket connection closed")
        chunks.append(chunk)
        size -= len(chunk)
    return b"".join(chunks)


def client_frame(opcode: int, payload: bytes) -> bytes:
    mask = os.urandom(4)
    length = len(payload)
    if length <= 125:
        header = bytes((0x80 | opcode, 0x80 | length))
    elif length <= 0xFFFF:
        header = bytes((0x80 | opcode, 0x80 | 126)) + struct.pack(">H", length)
    else:
        header = bytes((0x80 | opcode, 0x80 | 127)) + struct.pack(">Q", length)
    encoded = bytes(value ^ mask[index & 3] for index, value in enumerate(payload))
    return header + mask + encoded


def receive_frame(sock: socket.socket) -> tuple[int, bytes]:
    first, second = recv_exact(sock, 2)
    if not first & 0x80 or second & 0x80:
        raise HarnessError("invalid server frame")
    length = second & 0x7F
    if length == 126:
        length = struct.unpack(">H", recv_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack(">Q", recv_exact(sock, 8))[0]
    return first & 0x0F, recv_exact(sock, length)


class Client:
    def __init__(self, url: str, timeout: float) -> None:
        parsed = urlparse(url)
        if parsed.scheme != "ws" or not parsed.hostname:
            raise HarnessError("URL must use ws://")
        self.host = parsed.hostname
        self.port = parsed.port or 80
        self.path = parsed.path or "/"
        self.timeout = timeout
        self.sock: socket.socket | None = None

    def __enter__(self) -> "Client":
        sock = socket.create_connection((self.host, self.port), self.timeout)
        sock.settimeout(self.timeout)
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request = (
            f"GET {self.path} HTTP/1.1\r\n"
            f"Host: {self.host}:{self.port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        ).encode("ascii")
        sock.sendall(request)
        response = bytearray()
        while b"\r\n\r\n" not in response:
            response.extend(sock.recv(4096))
            if len(response) > 16384:
                raise HarnessError("oversized handshake")
        header, _, extra = bytes(response).partition(b"\r\n\r\n")
        if extra:
            raise HarnessError("unexpected handshake payload")
        lines = header.decode("ascii", "strict").split("\r\n")
        if not lines or " 101 " not in f" {lines[0]} ":
            raise HarnessError("WebSocket handshake failed")
        headers: dict[str, str] = {}
        for line in lines[1:]:
            name, separator, value = line.partition(":")
            if separator:
                headers[name.lower()] = value.strip()
        expected = base64.b64encode(
            hashlib.sha1((key + WS_GUID).encode("ascii")).digest()
        ).decode("ascii")
        if headers.get("sec-websocket-accept") != expected:
            raise HarnessError("invalid Sec-WebSocket-Accept")
        self.sock = sock
        return self

    def __exit__(self, *_: object) -> None:
        if self.sock is not None:
            try:
                self.sock.sendall(client_frame(8, b""))
            except OSError:
                pass
            self.sock.close()
            self.sock = None

    def command(self, request: dict[str, Any]) -> bytes:
        if self.sock is None:
            raise HarnessError("client is not connected")
        payload = json.dumps(request, separators=(",", ":")).encode("utf-8")
        self.sock.sendall(client_frame(1, payload))
        while True:
            opcode, response = receive_frame(self.sock)
            if opcode == 9:
                self.sock.sendall(client_frame(10, response))
                continue
            if opcode == 8:
                raise HarnessError("server closed the WebSocket")
            if opcode not in (1, 2):
                raise HarnessError(f"unexpected opcode {opcode}")
            return response


def json_response(payload: bytes) -> dict[str, Any]:
    try:
        result = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise HarnessError("invalid JSON response") from exc
    if not isinstance(result, dict):
        raise HarnessError("response is not an object")
    if not result.get("ok"):
        raise HarnessError(str(result.get("error", "command failed")))
    return result


def png_bytes(width: int, height: int, rgb: bytes) -> bytes:
    if len(rgb) != width * height * 3:
        raise HarnessError("screenshot extent mismatch")

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + kind
            + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
        )

    rows = b"".join(
        b"\0" + rgb[row * width * 3 : (row + 1) * width * 3]
        for row in range(height)
    )
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows, 6))
        + chunk(b"IEND", b"")
    )


def save_screenshot(payload: bytes, output: Path) -> tuple[int, int]:
    if len(payload) < 12 or payload[:8] != SHOT_MAGIC:
        json_response(payload)
        raise HarnessError("invalid screenshot response")
    width, height = struct.unpack(">HH", payload[8:12])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(png_bytes(width, height, payload[12:]))
    return width, height


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument(
        "--url",
        default=os.environ.get("DESMUME_WS_URL", "ws://127.0.0.1:12345/"),
    )
    result.add_argument("--timeout", type=float, default=30.0)
    commands = result.add_subparsers(dest="command", required=True)
    commands.add_parser("status")
    run = commands.add_parser("run")
    run.add_argument("frames", type=int)
    for name in ("pause", "resume", "flush-save", "reset", "quit"):
        commands.add_parser(name)
    keys = commands.add_parser("input")
    keys.add_argument("keys", nargs="+", choices=DS_KEYS)
    keys.add_argument("--hold-frames", type=int, default=2)
    keys.add_argument("--gap-frames", type=int, default=2)
    keys.add_argument("--repeat", type=int, default=1)
    shot = commands.add_parser("screenshot")
    shot.add_argument("output", type=Path)
    shot.add_argument("--screen", choices=("main", "touch", "both"), default="both")
    return result


def main() -> int:
    args = parser().parse_args()
    with Client(args.url, args.timeout) as client:
        if args.command == "screenshot":
            width, height = save_screenshot(
                client.command({"cmd": "screenshot", "screen": args.screen}),
                args.output,
            )
            print(f"wrote {args.output} ({width}x{height})")
        elif args.command == "input":
            if (
                args.hold_frames <= 0
                or args.gap_frames < 0
                or not 1 <= args.repeat <= 10000
            ):
                raise HarnessError("frame counts must be nonnegative and hold must be positive")
            result: dict[str, Any] = {}
            for _ in range(args.repeat):
                for key in args.keys:
                    json_response(client.command({
                        "cmd": "input", "key": key, "action": "tap",
                        "frames": args.hold_frames,
                    }))
                    result = json_response(client.command({
                        "cmd": "run", "frames": args.hold_frames + args.gap_frames,
                    }))
            print(json.dumps(result, sort_keys=True))
        else:
            request: dict[str, Any] = {"cmd": args.command}
            if args.command == "run":
                request["frames"] = args.frames
            print(json.dumps(json_response(client.command(request)), sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (HarnessError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
