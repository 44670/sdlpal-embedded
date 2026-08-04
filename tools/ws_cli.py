#!/usr/bin/env python3
"""Control a running Unix SDLPal instance through its local WebSocket port."""

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
import time
from typing import Any
from urllib.parse import urlparse
import zlib


WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
SHOT_MAGIC = b"PALSHOT1"
VALID_KEYS = (
    "up",
    "down",
    "left",
    "right",
    "escape",
    "menu",
    "enter",
    "search",
    "pageup",
    "pagedown",
    "home",
    "end",
    "repeat",
    "auto",
    "defend",
    "use",
    "throw",
    "flee",
    "force",
    "status",
)


class WsError(RuntimeError):
    pass


def _recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            raise WsError("WebSocket connection closed")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def encode_client_frame(opcode: int, payload: bytes, mask: bytes | None = None) -> bytes:
    if len(payload) >= 1 << 63:
        raise ValueError("payload is too large")
    mask = os.urandom(4) if mask is None else mask
    if len(mask) != 4:
        raise ValueError("mask must contain four bytes")
    first = bytes((0x80 | opcode,))
    length = len(payload)
    if length <= 125:
        header = first + bytes((0x80 | length,))
    elif length <= 0xFFFF:
        header = first + bytes((0x80 | 126,)) + struct.pack(">H", length)
    else:
        header = first + bytes((0x80 | 127,)) + struct.pack(">Q", length)
    masked = bytes(value ^ mask[index & 3] for index, value in enumerate(payload))
    return header + mask + masked


def recv_server_frame(sock: socket.socket) -> tuple[int, bytes]:
    first, second = _recv_exact(sock, 2)
    if not first & 0x80:
        raise WsError("fragmented server frames are unsupported")
    if second & 0x80:
        raise WsError("server frame must not be masked")
    length = second & 0x7F
    if length == 126:
        length = struct.unpack(">H", _recv_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack(">Q", _recv_exact(sock, 8))[0]
    return first & 0x0F, _recv_exact(sock, length)


class WsClient:
    def __init__(self, url: str, timeout: float) -> None:
        parsed = urlparse(url)
        if parsed.scheme != "ws" or not parsed.hostname:
            raise WsError("URL must use ws://")
        self.host = parsed.hostname
        self.port = parsed.port or 80
        self.path = parsed.path or "/"
        if parsed.query:
            self.path += "?" + parsed.query
        self.timeout = timeout
        self.sock: socket.socket | None = None

    def __enter__(self) -> "WsClient":
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
            chunk = sock.recv(4096)
            if not chunk:
                raise WsError("connection closed during WebSocket handshake")
            response.extend(chunk)
            if len(response) > 16384:
                raise WsError("oversized WebSocket handshake")
        header, _, extra = bytes(response).partition(b"\r\n\r\n")
        if extra:
            raise WsError("unexpected data after WebSocket handshake")
        lines = header.decode("ascii", "strict").split("\r\n")
        if not lines or " 101 " not in f" {lines[0]} ":
            raise WsError(f"WebSocket handshake failed: {lines[0] if lines else ''}")
        headers: dict[str, str] = {}
        for line in lines[1:]:
            name, separator, value = line.partition(":")
            if separator:
                headers[name.lower()] = value.strip()
        expected = base64.b64encode(
            hashlib.sha1((key + WS_GUID).encode("ascii")).digest()
        ).decode("ascii")
        if headers.get("sec-websocket-accept") != expected:
            raise WsError("invalid Sec-WebSocket-Accept")
        self.sock = sock
        return self

    def __exit__(self, *_: object) -> None:
        if self.sock is not None:
            try:
                self.sock.sendall(encode_client_frame(8, b""))
            except OSError:
                pass
            self.sock.close()
            self.sock = None

    def command(self, request: dict[str, Any]) -> bytes:
        if self.sock is None:
            raise WsError("client is not connected")
        payload = json.dumps(request, separators=(",", ":")).encode("utf-8")
        self.sock.sendall(encode_client_frame(1, payload))
        while True:
            opcode, response = recv_server_frame(self.sock)
            if opcode == 9:
                self.sock.sendall(encode_client_frame(10, response))
                continue
            if opcode == 8:
                raise WsError("server closed the WebSocket")
            if opcode not in (1, 2):
                raise WsError(f"unexpected WebSocket opcode {opcode}")
            return response


def parse_json_response(payload: bytes) -> dict[str, Any]:
    try:
        response = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise WsError("server returned invalid JSON") from exc
    if not isinstance(response, dict):
        raise WsError("server returned a non-object JSON response")
    if not response.get("ok"):
        raise WsError(str(response.get("error", "command failed")))
    return response


def png_bytes(width: int, height: int, rgb: bytes) -> bytes:
    if width <= 0 or height <= 0 or len(rgb) != width * height * 3:
        raise ValueError("invalid RGB image dimensions")

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + kind
            + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
        )

    rows = b"".join(
        b"\0" + rgb[y * width * 3 : (y + 1) * width * 3]
        for y in range(height)
    )
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows, 6))
        + chunk(b"IEND", b"")
    )


def save_screenshot(payload: bytes, output: Path) -> tuple[int, int]:
    if len(payload) < 12 or payload[:8] != SHOT_MAGIC:
        try:
            parse_json_response(payload)
        except WsError:
            raise
        raise WsError("server returned an invalid screenshot")
    width, height = struct.unpack(">HH", payload[8:12])
    rgb = payload[12:]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(png_bytes(width, height, rgb))
    return width, height


def tap_key(client: WsClient, key: str, hold_ms: int = 50) -> None:
    parse_json_response(client.command({"cmd": "input", "key": key, "action": "down"}))
    if hold_ms:
        time.sleep(hold_ms / 1000.0)
    parse_json_response(client.command({"cmd": "input", "key": key, "action": "up"}))


def wait_for_state(
    client: WsClient,
    predicate: Any,
    timeout: float,
    description: str,
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout
    while True:
        state = parse_json_response(client.command({"cmd": "status"}))
        if predicate(state):
            return state
        if time.monotonic() >= deadline:
            raise WsError(f"timed out waiting for {description}")
        time.sleep(0.05)


def dismiss_active_dialogue(client: WsClient, timeout: float) -> dict[str, Any]:
    deadline = time.monotonic() + timeout
    quiet_since: float | None = None
    while True:
        state = parse_json_response(client.command({"cmd": "status"}))
        now = time.monotonic()
        if state.get("dialog"):
            tap_key(client, "enter", 20)
            quiet_since = None
        elif quiet_since is None:
            quiet_since = now
        elif now - quiet_since >= 2.0:
            return state
        if now >= deadline:
            raise WsError("could not dismiss the active dialogue")
        time.sleep(0.05)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--url",
        default=os.environ.get("PAL_WS_URL", "ws://127.0.0.1:12345/"),
        help="server URL (default: %(default)s)",
    )
    parser.add_argument("--timeout", type=float, default=5.0)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("status", help="show current gameplay state")

    event = commands.add_parser("event", help="show one live PAL event object")
    event.add_argument("event", type=int)

    screenshot = commands.add_parser(
        "screenshot", help="capture the current game or native-profile surface"
    )
    screenshot.add_argument("output", type=Path)

    record = commands.add_parser(
        "record", help="capture a bounded screenshot sequence with state JSONL"
    )
    record.add_argument("output_dir", type=Path)
    record.add_argument("--count", type=int, default=20)
    record.add_argument("--interval-ms", type=int, default=100)
    record.add_argument(
        "--battle", type=int, help="start this battle on the same connection before capture"
    )
    record.add_argument(
        "--battlefield", type=int, help="use this battle background with --battle"
    )
    record.add_argument(
        "--battle-auto", action="store_true", help="use auto battle with --battle"
    )

    input_command = commands.add_parser("input", help="inject one or more PAL inputs")
    input_command.add_argument("keys", nargs="+", choices=VALID_KEYS)
    input_command.add_argument("--action", choices=("tap", "down", "up"), default="tap")
    input_command.add_argument(
        "--hold-ms",
        type=int,
        default=50,
        help="duration of a tap before its explicit release (default: %(default)s)",
    )
    input_command.add_argument("--delay-ms", type=int, default=50)

    scene = commands.add_parser("scene", help="switch the field gameplay scene")
    scene.add_argument("scene", type=int)
    scene.add_argument("--x", type=int)
    scene.add_argument("--y", type=int)

    load = commands.add_parser("load", help="load an existing PAL save slot")
    load.add_argument("slot", type=int, choices=range(1, 6))

    script = commands.add_parser(
        "script", help="run a real PAL trigger-script entry from field gameplay"
    )
    script.add_argument("entry", type=int)
    script.add_argument("--event", type=int, default=0)

    shop = commands.add_parser("shop", help="open a real buy menu from field gameplay")
    shop.add_argument("store", type=int)

    ui = commands.add_parser("ui", help="open a real gameplay UI from field gameplay")
    ui.add_argument(
        "name",
        choices=("main", "status", "items", "magic", "save", "confirm", "sell", "system"),
    )

    commands.add_parser(
        "review-fixture",
        help="populate a session-only dense UI review fixture",
    )
    commands.add_parser(
        "review-next",
        help="advance the --ui-test review sequence like F1",
    )

    battle = commands.add_parser("battle", help="start a real battle from field gameplay")
    battle.add_argument("team", type=int)
    battle.add_argument(
        "--battlefield", type=int, help="select the battle background resource"
    )
    battle.add_argument(
        "--auto", action="store_true", help="enable auto battle after entering"
    )

    commands.add_parser(
        "battle-items",
        help="open the real usable-item selector in an active battle",
    )

    sop = commands.add_parser(
        "sop-capture",
        help="capture the reachable gameplay UI review SOP from a running field game",
    )
    sop.add_argument("output_dir", type=Path)
    sop.add_argument("--dialog-script", type=int, default=7739)
    sop.add_argument("--dialog-event", type=int, default=113)
    sop.add_argument("--shop", type=int, default=0)
    sop.add_argument("--battle", type=int, default=3)
    sop.add_argument("--battlefield", type=int, default=3)
    sop.add_argument("--settle-ms", type=int, default=150)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        with WsClient(args.url, args.timeout) as client:
            if args.command == "status":
                response = parse_json_response(client.command({"cmd": "status"}))
                print(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True))
            elif args.command == "event":
                response = parse_json_response(
                    client.command({"cmd": "event", "event": args.event})
                )
                print(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True))
            elif args.command == "screenshot":
                width, height = save_screenshot(
                    client.command({"cmd": "screenshot"}), args.output
                )
                print(f"{args.output}: {width}x{height}")
            elif args.command == "record":
                if args.count <= 0 or args.count > 10000:
                    raise WsError("--count must be in the range 1..10000")
                if args.interval_ms < 0:
                    raise WsError("--interval-ms must not be negative")
                if args.battle_auto and args.battle is None:
                    raise WsError("--battle-auto requires --battle")
                if args.battlefield is not None and args.battle is None:
                    raise WsError("--battlefield requires --battle")
                if args.battle is not None:
                    request: dict[str, Any] = {
                        "cmd": "battle",
                        "team": args.battle,
                        "auto": int(args.battle_auto),
                    }
                    if args.battlefield is not None:
                        request["battlefield"] = args.battlefield
                    parse_json_response(
                        client.command(request)
                    )
                args.output_dir.mkdir(parents=True, exist_ok=True)
                state_lines: list[str] = []
                for index in range(args.count):
                    state = parse_json_response(client.command({"cmd": "status"}))
                    state["capture_index"] = index
                    state_lines.append(
                        json.dumps(state, ensure_ascii=False, sort_keys=True)
                    )
                    output = args.output_dir / f"frame_{index:04d}.png"
                    save_screenshot(client.command({"cmd": "screenshot"}), output)
                    if index + 1 < args.count and args.interval_ms:
                        time.sleep(args.interval_ms / 1000.0)
                (args.output_dir / "state.jsonl").write_text(
                    "\n".join(state_lines) + "\n", encoding="utf-8"
                )
                print(f"{args.output_dir}: captured {args.count} frame(s)")
            elif args.command == "input":
                if args.delay_ms < 0 or args.hold_ms < 0:
                    raise WsError("--delay-ms and --hold-ms must not be negative")
                for index, key in enumerate(args.keys):
                    if args.action == "tap":
                        tap_key(client, key, args.hold_ms)
                    else:
                        parse_json_response(
                            client.command(
                                {"cmd": "input", "key": key, "action": args.action}
                            )
                        )
                    if index + 1 < len(args.keys) and args.delay_ms:
                        time.sleep(args.delay_ms / 1000.0)
                print(f"sent {len(args.keys)} input command(s)")
            elif args.command == "scene":
                if (args.x is None) != (args.y is None):
                    raise WsError("--x and --y must be supplied together")
                request: dict[str, Any] = {"cmd": "scene", "scene": args.scene}
                if args.x is not None:
                    request.update(x=args.x, y=args.y)
                response = parse_json_response(client.command(request))
                print(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True))
            elif args.command == "load":
                response = parse_json_response(
                    client.command({"cmd": "load", "slot": args.slot})
                )
                print(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True))
            elif args.command == "script":
                response = parse_json_response(
                    client.command(
                        {"cmd": "script", "entry": args.entry, "event": args.event}
                    )
                )
                print(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True))
            elif args.command == "shop":
                response = parse_json_response(
                    client.command({"cmd": "shop", "store": args.store})
                )
                print(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True))
            elif args.command == "ui":
                response = parse_json_response(
                    client.command({"cmd": "ui", "name": args.name})
                )
                print(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True))
            elif args.command == "review-fixture":
                response = parse_json_response(
                    client.command({"cmd": "review-fixture"})
                )
                print(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True))
            elif args.command == "review-next":
                response = parse_json_response(
                    client.command({"cmd": "review-next"})
                )
                print(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True))
            elif args.command == "battle":
                request = {
                    "cmd": "battle",
                    "team": args.team,
                    "auto": int(args.auto),
                }
                if args.battlefield is not None:
                    request["battlefield"] = args.battlefield
                response = parse_json_response(
                    client.command(request)
                )
                print(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True))
            elif args.command == "battle-items":
                response = parse_json_response(
                    client.command({"cmd": "battle-items"})
                )
                print(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True))
            elif args.command == "sop-capture":
                if args.settle_ms < 0:
                    raise WsError("--settle-ms must not be negative")
                settle = args.settle_ms / 1000.0
                state = dismiss_active_dialogue(client, args.timeout)
                if not state.get("main_game") or state.get("battle") or state.get("ui"):
                    raise WsError("sop-capture requires idle field gameplay")
                args.output_dir.mkdir(parents=True, exist_ok=True)
                captures: list[dict[str, Any]] = []

                def capture(name: str) -> None:
                    output = args.output_dir / name
                    width, height = save_screenshot(
                        client.command({"cmd": "screenshot"}), output
                    )
                    captures.append({"file": name, "width": width, "height": height})

                def capture_ui(name: str, output: str) -> None:
                    parse_json_response(client.command({"cmd": "ui", "name": name}))
                    wait_for_state(
                        client, lambda value: value.get("ui") == name,
                        args.timeout, f"{name} UI",
                    )
                    if settle:
                        time.sleep(settle)
                    capture(output)
                    tap_key(client, "escape")
                    wait_for_state(
                        client, lambda value: value.get("ui") == "",
                        args.timeout, f"{name} UI to close",
                    )

                capture_ui("main", "04_main_menu.png")
                dismiss_active_dialogue(client, args.timeout)
                capture("01_map.png")
                fixture = parse_json_response(
                    client.command({"cmd": "review-fixture"})
                )
                if fixture.get("party_members") != 3:
                    raise WsError("review fixture did not create a three-member party")
                state = parse_json_response(client.command({"cmd": "status"}))
                capture_ui("status", "05_status.png")

                parse_json_response(client.command({"cmd": "ui", "name": "items"}))
                wait_for_state(
                    client, lambda value: value.get("ui") == "items",
                    args.timeout, "items UI",
                )
                if settle:
                    time.sleep(settle)
                capture("06_items.png")
                for _ in range(10):
                    tap_key(client, "down", 20)
                if settle:
                    time.sleep(settle)
                capture("06_items_scrolled.png")
                tap_key(client, "escape")
                wait_for_state(
                    client, lambda value: value.get("ui") == "",
                    args.timeout, "items UI to close",
                )

                parse_json_response(client.command({"cmd": "ui", "name": "magic"}))
                wait_for_state(
                    client, lambda value: value.get("ui") == "magic",
                    args.timeout, "magic UI",
                )
                if settle:
                    time.sleep(settle)
                capture("07_magic_party.png")
                if state.get("party_members", 1) > 1:
                    tap_key(client, "enter")
                    if settle:
                        time.sleep(settle)
                    capture("08_magic_list.png")
                    for _ in range(10):
                        tap_key(client, "down", 20)
                    if settle:
                        time.sleep(settle)
                    capture("08_magic_scrolled.png")
                tap_key(client, "escape")
                if parse_json_response(client.command({"cmd": "status"})).get("ui"):
                    tap_key(client, "escape")
                wait_for_state(
                    client, lambda value: value.get("ui") == "",
                    args.timeout, "magic UI to close",
                )

                capture_ui("save", "09_save_slots.png")

                capture_ui("confirm", "10_yes_no.png")

                parse_json_response(client.command({"cmd": "shop", "store": args.shop}))
                wait_for_state(
                    client, lambda value: value.get("ui") == "shop",
                    args.timeout, "shop UI",
                )
                if settle:
                    time.sleep(settle)
                capture("11_shop.png")
                tap_key(client, "escape")
                wait_for_state(
                    client, lambda value: value.get("ui") == "",
                    args.timeout, "shop UI to close",
                )

                capture_ui("sell", "11_sell.png")

                parse_json_response(
                    client.command(
                        {
                            "cmd": "script",
                            "entry": args.dialog_script,
                            "event": args.dialog_event,
                        }
                    )
                )
                wait_for_state(
                    client, lambda value: value.get("script"),
                    args.timeout, "dialogue script",
                )
                if settle:
                    time.sleep(settle)
                capture("02_dialogue.png")
                for _ in range(8):
                    tap_key(client, "enter", 20)
                    time.sleep(0.03)

                wait_for_state(
                    client,
                    lambda value: value.get("main_game")
                    and not value.get("battle")
                    and not value.get("script")
                    and not value.get("dialog"),
                    args.timeout,
                    "field gameplay after dialogue",
                )
                parse_json_response(
                    client.command(
                        {
                            "cmd": "battle",
                            "team": args.battle,
                            "battlefield": args.battlefield,
                            "auto": 0,
                        }
                    )
                )
                wait_for_state(
                    client,
                    lambda value: value.get("battle")
                    and value.get("battle_ui_state", 0) != 0,
                    args.timeout,
                    "interactive battle UI",
                )
                if settle:
                    time.sleep(settle)
                capture("03_battle.png")
                parse_json_response(client.command({"cmd": "battle-items"}))
                wait_for_state(
                    client,
                    lambda value: value.get("battle")
                    and value.get("battle_menu_state") == 2,
                    args.timeout,
                    "battle item selector",
                )
                if settle:
                    time.sleep(settle)
                capture("12_battle_items.png")

                (args.output_dir / "captures.json").write_text(
                    json.dumps(captures, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8",
                )
                print(f"{args.output_dir}: captured {len(captures)} SOP screen(s)")
            else:
                raise AssertionError(args.command)
    except (OSError, WsError, ValueError) as exc:
        print(f"ws_cli: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
