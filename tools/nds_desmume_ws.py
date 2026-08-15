#!/usr/bin/env python3
"""Launch and drive the loopback-only DeSmuME SDL WebSocket harness.

This uses only the Python standard library.  It is intentionally small enough
to reuse from performance gates and gameplay capture scripts without desktop
focus injection, sleeps between guessed frames, or third-party WebSocket/PNG
packages.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import secrets
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib
from pathlib import Path
from typing import BinaryIO


DEFAULT_DESMUME = Path(
    "/home/john/tools/desmume/desmume/src/frontend/posix/"
    "build-sdl/cli/desmume-cli"
)
VALID_KEYS = {
    "a", "b", "select", "start", "right", "left",
    "up", "down", "r", "l", "x", "y",
}
MAX_LOG_BYTES = 8 * 1024 * 1024
FATAL_LOG_MARKERS = (
    b"Undefined instruction", b"armcpu_exception!", b"switchMode: WRONG",
)


class HarnessError(RuntimeError):
    pass


class WebSocket:
    def __init__(self, connection: socket.socket):
        self.connection = connection

    @classmethod
    def connect(cls, port: int, timeout: float) -> "WebSocket":
        connection = socket.create_connection(("127.0.0.1", port), timeout)
        connection.settimeout(timeout)
        key = base64.b64encode(secrets.token_bytes(16)).decode("ascii")
        request = (
            "GET / HTTP/1.1\r\n"
            f"Host: 127.0.0.1:{port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        ).encode("ascii")
        connection.sendall(request)
        response = bytearray()
        while b"\r\n\r\n" not in response:
            block = connection.recv(4096)
            if not block:
                raise HarnessError("DeSmuME closed during WebSocket handshake")
            response.extend(block)
            if len(response) > 16384:
                raise HarnessError("oversized WebSocket handshake")
        header = bytes(response).split(b"\r\n\r\n", 1)[0]
        expected = base64.b64encode(hashlib.sha1(
            (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")
        ).digest())
        if not header.startswith(b"HTTP/1.1 101 ") or expected not in header:
            raise HarnessError(f"WebSocket handshake rejected: {header!r}")
        return cls(connection)

    def close(self) -> None:
        try:
            self._send_frame(8, b"")
        except OSError:
            pass
        self.connection.close()

    def _receive_exact(self, size: int) -> bytes:
        output = bytearray()
        while len(output) < size:
            block = self.connection.recv(size - len(output))
            if not block:
                raise HarnessError("DeSmuME closed the WebSocket")
            output.extend(block)
        return bytes(output)

    def _send_frame(self, opcode: int, payload: bytes) -> None:
        mask = secrets.token_bytes(4)
        size = len(payload)
        if size <= 125:
            header = bytes((0x80 | opcode, 0x80 | size))
        elif size <= 65535:
            header = bytes((0x80 | opcode, 0x80 | 126)) + struct.pack(">H", size)
        else:
            header = bytes((0x80 | opcode, 0x80 | 127)) + struct.pack(">Q", size)
        masked = bytes(value ^ mask[index & 3]
                       for index, value in enumerate(payload))
        self.connection.sendall(header + mask + masked)

    def _receive_frame(self) -> tuple[int, bytes]:
        first, second = self._receive_exact(2)
        if first & 0x80 == 0:
            raise HarnessError("fragmented server frame is unsupported")
        size = second & 0x7F
        if size == 126:
            size = struct.unpack(">H", self._receive_exact(2))[0]
        elif size == 127:
            size = struct.unpack(">Q", self._receive_exact(8))[0]
        if second & 0x80:
            mask = self._receive_exact(4)
        else:
            mask = None
        payload = self._receive_exact(size)
        if mask is not None:
            payload = bytes(value ^ mask[index & 3]
                            for index, value in enumerate(payload))
        return first & 0x0F, payload

    def command(self, command: dict[str, object]) -> tuple[int, bytes]:
        self._send_frame(1, json.dumps(command, separators=(",", ":")).encode())
        while True:
            opcode, payload = self._receive_frame()
            if opcode == 9:
                self._send_frame(10, payload)
                continue
            if opcode == 8:
                raise HarnessError("DeSmuME closed the WebSocket")
            return opcode, payload


def find_port() -> int:
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    return int(port)


def png_chunk(name: bytes, payload: bytes) -> bytes:
    body = name + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(
        ">I", zlib.crc32(body) & 0xFFFFFFFF
    )


def write_screenshot(path: Path, payload: bytes) -> None:
    if len(payload) < 12 or payload[:8] != b"NDSSHOT1":
        raise HarnessError("invalid DeSmuME screenshot payload")
    width, height = struct.unpack(">HH", payload[8:12])
    pixels = payload[12:]
    if width == 0 or height == 0 or len(pixels) != width * height * 3:
        raise HarnessError("invalid DeSmuME screenshot dimensions")
    scanlines = b"".join(
        b"\0" + pixels[row * width * 3:(row + 1) * width * 3]
        for row in range(height)
    )
    png = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(scanlines, 9))
        + png_chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def json_reply(opcode: int, payload: bytes) -> dict[str, object]:
    if opcode != 1:
        raise HarnessError(f"expected JSON reply, got opcode {opcode}")
    try:
        reply = json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise HarnessError(f"invalid JSON reply: {payload!r}") from error
    if not isinstance(reply, dict) or not reply.get("ok"):
        raise HarnessError(f"DeSmuME command failed: {reply!r}")
    return reply


def memory_reply(opcode: int, payload: bytes) -> dict[str, object]:
    """Decode current JSON memory replies and the legacy bare-hex form."""
    if opcode == 1:
        try:
            reply = json.loads(payload)
        except (UnicodeDecodeError, json.JSONDecodeError):
            pass
        else:
            words = reply.get("words") if isinstance(reply, dict) else None
            if (reply.get("ok") and isinstance(words, list)
                    and all(isinstance(word, str)
                            and word.startswith("0x")
                            and len(word) == 10 for word in words)):
                return reply
            raise HarnessError(f"invalid memory reply: {payload!r}")

    prefix = b'{"ok":true,"words":['
    if opcode != 1 or not payload.startswith(prefix):
        raise HarnessError(f"invalid memory reply: {payload!r}")
    body = payload[len(prefix):]
    if not body.endswith(b"]}"):
        raise HarnessError(f"invalid memory reply: {payload!r}")
    body = body[:-2]
    words = [] if not body else body.split(b",")
    if any(len(word) != 8 or any(
        byte not in b"0123456789abcdefABCDEF" for byte in word
    ) for word in words):
        raise HarnessError(f"invalid memory reply: {payload!r}")
    return {"ok": True, "words": [f"0x{word.decode()}" for word in words]}


def execute_action(ws: WebSocket, action: str, root: Path) -> dict[str, object]:
    fields = action.split(":")
    name = fields[0]
    started = time.perf_counter()
    if name == "run" and len(fields) == 2:
        reply = json_reply(*ws.command({"cmd": "run", "frames": int(fields[1])}))
    elif name == "tap" and len(fields) in (2, 3):
        key = fields[1]
        if key not in VALID_KEYS:
            raise HarnessError(f"unknown DS key: {key}")
        frames = int(fields[2]) if len(fields) == 3 else 2
        reply = json_reply(*ws.command(
            {"cmd": "input", "key": key, "action": "tap", "frames": frames}
        ))
    elif name in ("down", "up") and len(fields) == 2:
        key = fields[1]
        if key not in VALID_KEYS:
            raise HarnessError(f"unknown DS key: {key}")
        reply = json_reply(*ws.command(
            {"cmd": "input", "key": key, "action": name}
        ))
    elif name in (
        "status", "pause", "resume", "regs", "flush-save", "reset"
    ) and len(fields) == 1:
        reply = json_reply(*ws.command({"cmd": name}))
    elif name in ("mem", "mem7") and len(fields) == 3:
        reply = memory_reply(*ws.command({
            "cmd": "mem",
            "address": int(fields[1], 0),
            "length": int(fields[2], 0),
            "cpu": "arm7" if name == "mem7" else "arm9",
        }))
    elif name == "capture" and len(fields) in (2, 3):
        screen = fields[2] if len(fields) == 3 else "main"
        if screen not in ("main", "touch", "both"):
            raise HarnessError(f"unknown screenshot screen: {screen}")
        opcode, payload = ws.command({"cmd": "screenshot", "screen": screen})
        if opcode != 2:
            raise HarnessError(f"expected screenshot reply, got opcode {opcode}")
        output = root / fields[1]
        write_screenshot(output, payload)
        reply = {"ok": True, "path": str(output), "screen": screen}
    elif name == "record" and len(fields) in (3, 4):
        screen = fields[3] if len(fields) == 4 else "main"
        frames = int(fields[2])
        if screen not in ("main", "touch", "both"):
            raise HarnessError(f"unknown screenshot screen: {screen}")
        if frames <= 0 or frames > 600:
            raise HarnessError("record frame count must be from 1 through 600")
        output = root / fields[1]
        output.mkdir(parents=True, exist_ok=True)
        for frame in range(frames):
            json_reply(*ws.command({"cmd": "run", "frames": 1}))
            opcode, payload = ws.command(
                {"cmd": "screenshot", "screen": screen}
            )
            if opcode != 2:
                raise HarnessError(
                    f"expected screenshot reply, got opcode {opcode}"
                )
            write_screenshot(output / f"{frame:04d}.png", payload)
        reply = {
            "ok": True, "path": str(output), "screen": screen,
            "frames": frames,
        }
    else:
        raise HarnessError(f"invalid action: {action}")
    elapsed = time.perf_counter() - started
    return {"action": action, "wall_seconds": round(elapsed, 6), **reply}


def connect_when_ready(
    process: subprocess.Popen[bytes], port: int, timeout: float, log_path: Path
) -> WebSocket:
    deadline = time.monotonic() + timeout
    last_error: OSError | None = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise HarnessError(
                f"DeSmuME exited with {process.returncode}; see {log_path}"
            )
        try:
            websocket = WebSocket.connect(port, min(1.0, timeout))
            websocket.connection.settimeout(timeout)
            return websocket
        except OSError as error:
            last_error = error
            time.sleep(0.02)
    raise HarnessError(f"DeSmuME WebSocket did not start: {last_error}")


def copy_emulator_log(
    source: BinaryIO,
    destination: BinaryIO,
    process: subprocess.Popen[bytes],
    fatal: threading.Event,
    stop_on_fatal: bool,
) -> None:
    written = 0
    while True:
        block = source.readline()
        if not block:
            return
        if written < MAX_LOG_BYTES:
            amount = min(len(block), MAX_LOG_BYTES - written)
            destination.write(block[:amount])
            destination.flush()
            written += amount
        if any(marker in block for marker in FATAL_LOG_MARKERS):
            fatal.set()
            if stop_on_fatal and process.poll() is None:
                process.kill()
                return


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument("--desmume", type=Path, default=DEFAULT_DESMUME)
    parser.add_argument(
        "--session", type=Path,
        help="artifact/config directory; otherwise an isolated temporary directory",
    )
    parser.add_argument(
        "--action", action="append", default=[],
        help=("ordered action: run:N, tap:KEY[:N], down:KEY, up:KEY, status, "
              "pause, resume, regs, flush-save, reset, mem:ADDRESS:LENGTH, "
              "mem7:ADDRESS:LENGTH, or "
              "capture:FILE[:main|touch|both], or "
              "record:DIR:FRAMES[:main|touch|both]"),
    )
    parser.add_argument(
        "--audio-capture", type=Path,
        help="write DeSmuME's raw SDL audio stream under the session directory",
    )
    parser.add_argument(
        "--desmume-arg", action="append", default=[],
        help="additional DeSmuME argument inserted before the ROM (repeatable)",
    )
    cflash = parser.add_mutually_exclusive_group()
    cflash.add_argument(
        "--cflash-path", type=Path,
        help="mount this host directory as the emulated Slot-2 FAT volume",
    )
    cflash.add_argument(
        "--cflash-image", type=Path,
        help="mount this persistent FAT image as the emulated Slot-2 volume",
    )
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument(
        "--allow-cpu-failure", action="store_true",
        help="retain diagnostic control after an emulated CPU failure",
    )
    args = parser.parse_args()

    if not args.rom.is_file():
        raise SystemExit(f"missing ROM: {args.rom}")
    if not args.desmume.is_file():
        raise SystemExit(f"missing DeSmuME SDL CLI: {args.desmume}")

    temporary: tempfile.TemporaryDirectory[str] | None = None
    if args.session is None:
        temporary = tempfile.TemporaryDirectory(prefix="sdlpal-nds-desmume-")
        session = Path(temporary.name)
    else:
        session = args.session.resolve()
        session.mkdir(parents=True, exist_ok=True)
    config = session / "config"
    config.mkdir(parents=True, exist_ok=True)
    log_path = session / "desmume.log"
    audio_path: Path | None = None
    if args.audio_capture is not None:
        audio_path = (args.audio_capture if args.audio_capture.is_absolute()
                      else session / args.audio_capture)
        audio_path.parent.mkdir(parents=True, exist_ok=True)
    port = find_port()
    environment = os.environ.copy()
    environment.update({
        "DESMUME_WS_PORT": str(port),
        "DESMUME_WS_HEADLESS": "1",
        "SDL_AUDIODRIVER": "disk" if audio_path is not None else "dummy",
        "XDG_CONFIG_HOME": str(config),
    })
    if audio_path is not None:
        environment["SDL_DISKAUDIOFILE"] = str(audio_path)
        # Let SDL's disk backend consume at device speed.  With delay disabled
        # it drains buffers as fast as the host can call the callback, creating
        # minutes of silence around a few seconds of emulated signal and making
        # the recording unsuitable for timing or waveform analysis.
        # DeSmuME requests 2940 stereo frames per callback: 66.7 ms at
        # 44.1 kHz.  Match that cadence so the raw file duration tracks the
        # emulated real-time run instead of accumulating callback-rate silence.
        environment["SDL_DISKAUDIODELAY"] = "67"
        environment["DESMUME_WS_REALTIME"] = "1"
    command = [
        str(args.desmume), "--start-paused",
    ]
    if audio_path is None:
        command.append("--disable-limiter")
    command.append("--nojoy")
    if args.cflash_path is not None:
        if not args.cflash_path.is_dir():
            raise SystemExit(f"missing cflash directory: {args.cflash_path}")
        command.extend(("--cflash-path", str(args.cflash_path.resolve())))
    if args.cflash_image is not None:
        if not args.cflash_image.is_file():
            raise SystemExit(f"missing cflash image: {args.cflash_image}")
        command.extend(("--cflash-image", str(args.cflash_image.resolve())))
    command.extend(args.desmume_arg)
    command.append(str(args.rom.resolve()))
    log: BinaryIO = log_path.open("wb")
    process = subprocess.Popen(command, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, env=environment)
    assert process.stdout is not None
    fatal_log = threading.Event()
    log_thread = threading.Thread(
        target=copy_emulator_log,
        args=(process.stdout, log, process, fatal_log, not args.allow_cpu_failure),
        name="desmume-log",
        daemon=True,
    )
    log_thread.start()
    ws: WebSocket | None = None
    try:
        ws = connect_when_ready(process, port, args.timeout, log_path)
        actions = args.action or ["run:600", "status"]
        for action in actions:
            result = execute_action(ws, action, session)
            print(json.dumps(result, ensure_ascii=False, sort_keys=True))
        json_reply(*ws.command({"cmd": "quit"}))
        process.wait(timeout=10.0)
    except BaseException:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        raise
    finally:
        if ws is not None:
            ws.close()
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        process.stdout.close()
        log_thread.join(timeout=2.0)
        log.close()
        if temporary is not None:
            temporary.cleanup()
    if fatal_log.is_set() and not args.allow_cpu_failure:
        raise HarnessError(f"DeSmuME reported an emulated CPU failure; see {log_path}")
    if audio_path is not None:
        if not audio_path.is_file():
            raise HarnessError("DeSmuME did not create the requested audio capture")
        audio = audio_path.read_bytes()
        print(json.dumps({
            "audio_capture": str(audio_path),
            "bytes": len(audio),
            "nonzero_bytes": sum(value != 0 for value in audio),
        }, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (HarnessError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
