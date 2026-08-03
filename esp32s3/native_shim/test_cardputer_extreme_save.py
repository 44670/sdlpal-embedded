#!/usr/bin/env python3
"""Exercise LEVEL1 session paging with an ordinary DOS ``N.rpg`` save."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


EVENT_RECORD_BYTES = 32
EVENT_STATE_OFFSET = 12
EVENT_PAGE_BYTES = 4096
DOS_FIXED_BYTES = 12864
TEST_EVENT_STATE = 1234
SAVE_SLOT = 5
SAVE_TIMES = 77
SSS_ARCHIVE_ID = 15


def packed_chunk_size(path: Path, archive_id: int, chunk_id: int) -> int:
    data = path.read_bytes()
    if len(data) < 32:
        raise AssertionError(f"short pack: {path}")
    archive_count = struct.unpack_from("<H", data, 8)[0]
    archive_table = struct.unpack_from("<I", data, 12)[0]
    for archive_index in range(archive_count):
        entry = archive_table + archive_index * 12
        candidate, chunk_count = struct.unpack_from("<HH", data, entry)
        if candidate != archive_id:
            continue
        if chunk_id >= chunk_count:
            break
        chunk_table = struct.unpack_from("<I", data, entry + 4)[0]
        return struct.unpack_from("<I", data, chunk_table + chunk_id * 16 + 4)[0]
    raise AssertionError(f"missing packed chunk {archive_id}/{chunk_id}: {path}")


def event_state(save: bytes, event_id: int) -> int:
    offset = (
        DOS_FIXED_BYTES
        + (event_id - 1) * EVENT_RECORD_BYTES
        + EVENT_STATE_OFFSET
    )
    return struct.unpack_from("<h", save, offset)[0]


def run_engine(
    args: argparse.Namespace,
    save_dir: Path,
    label: str,
    *,
    save: bool,
    event_id: int,
) -> str:
    event_trace = save_dir / f"{label}.event"
    env = os.environ.copy()
    env.update(
        {
            "PAL_CORES3SE_NATIVE_NOR_PACK": str(args.nor),
            "PAL_CORES3SE_NATIVE_TF_PACK": str(args.tf),
            "PAL_CORES3SE_NATIVE_SAVE_DIR": str(save_dir),
            "PAL_CORES3SE_NATIVE_SCREENSHOT_FRAME": "999999",
            "PAL_CORES3SE_NATIVE_MAX_PRESENTS": "0",
            "PAL_CORES3SE_NATIVE_FRAMES": "1",
            "PAL_DETERMINISTIC_REPLAY": str(args.route),
            "PAL_DETERMINISTIC_CHECKPOINTS": str(save_dir / f"{label}.trace"),
            "PAL_DETERMINISTIC_EVENT_TRACE": str(event_trace),
            "PAL_DETERMINISTIC_EVENT_ID": str(event_id),
            "PAL_DETERMINISTIC_RELOAD_FRAME": "1100" if save else "650",
            "PAL_DETERMINISTIC_RELOAD_SLOT": str(SAVE_SLOT),
            "PAL_DETERMINISTIC_MAX_PRESENTS": "1300" if save else "900",
        }
    )
    for name in (
        "PAL_DETERMINISTIC_SAVE_FRAME",
        "PAL_DETERMINISTIC_SAVE_SLOT",
        "PAL_DETERMINISTIC_SAVE_TIMES",
        "PAL_DETERMINISTIC_EVENT_STATE",
    ):
        env.pop(name, None)
    if save:
        env.update(
            {
                "PAL_DETERMINISTIC_SAVE_FRAME": "1000",
                "PAL_DETERMINISTIC_SAVE_SLOT": str(SAVE_SLOT),
                "PAL_DETERMINISTIC_SAVE_TIMES": str(SAVE_TIMES),
                "PAL_DETERMINISTIC_EVENT_STATE": str(TEST_EVENT_STATE),
            }
        )

    result = subprocess.run(
        [str(args.binary)],
        cwd=args.data_dir,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=20,
        check=False,
    )
    if result.returncode != 1:
        raise AssertionError(
            f"{label}: expected native-shim sentinel exit 1, got "
            f"{result.returncode}\n{result.stdout}"
        )
    text = event_trace.read_text(encoding="utf-8")
    if " init " not in text or " scene " not in text:
        raise AssertionError(f"{label}: deterministic run did not initialize gameplay")
    return text


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--nor", required=True, type=Path)
    parser.add_argument("--tf", required=True, type=Path)
    parser.add_argument("--data-dir", required=True, type=Path)
    parser.add_argument("--route", required=True, type=Path)
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    args.nor = args.nor.resolve()
    args.tf = args.tf.resolve()
    args.data_dir = args.data_dir.resolve()
    args.route = args.route.resolve()

    for path in (args.binary, args.nor, args.tf, args.route):
        if not path.is_file():
            raise AssertionError(f"missing test input: {path}")
    if not args.data_dir.is_dir():
        raise AssertionError(f"missing PAL data directory: {args.data_dir}")

    event_bytes = packed_chunk_size(args.nor, SSS_ARCHIVE_ID, 0)
    if event_bytes <= 0 or event_bytes % EVENT_RECORD_BYTES:
        raise AssertionError(f"invalid SSS#0 event bytes: {event_bytes}")
    event_count = event_bytes // EVENT_RECORD_BYTES
    event_id = min(5000, event_count)
    work_bytes = (
        (event_count + EVENT_PAGE_BYTES // EVENT_RECORD_BYTES - 1)
        // (EVENT_PAGE_BYTES // EVENT_RECORD_BYTES)
        * EVENT_PAGE_BYTES
    )
    dos_save_bytes = DOS_FIXED_BYTES + event_bytes

    with tempfile.TemporaryDirectory(prefix="pal-level1-save-") as temporary:
        save_dir = Path(temporary)
        work = save_dir / "EVENT.WRK"
        tail = b"stale-session-tail"

        # Startup must not unlink or truncate stale session scratch. New Game
        # overwrites the complete addressable work image and ignores this tail.
        work.write_bytes(bytes([0xA5]) * work_bytes + tail)
        events = run_engine(
            args, save_dir, "save-reload", save=True, event_id=event_id
        )

        save_path = save_dir / f"{SAVE_SLOT}.rpg"
        save = save_path.read_bytes()
        if len(save) != dos_save_bytes:
            raise AssertionError(
                f"standard DOS save is {len(save)} bytes, expected {dos_save_bytes}"
            )
        if save[:8] == b"PALXSAVE":
            raise AssertionError("save still has the retired private wrapper")
        if struct.unpack_from("<H", save, 0)[0] != SAVE_TIMES:
            raise AssertionError("standard save did not preserve saved-times")
        if event_state(save, event_id) != TEST_EVENT_STATE:
            raise AssertionError("live dirty event was not serialized into N.rpg")
        if (
            f"generic_id={event_id} generic_state={TEST_EVENT_STATE}"
            not in events
        ):
            raise AssertionError("same-process reload did not restore the saved event")
        if work.stat().st_size != work_bytes + len(tail):
            raise AssertionError("session work file was unexpectedly truncated")
        if work.read_bytes()[-len(tail) :] != tail:
            raise AssertionError("bytes outside the flat work image were touched")

        # A later process must recreate the relevant work bytes from New Game
        # and then from N.rpg; arbitrary old work contents cannot affect load.
        work.write_bytes(bytes([0x5A]) * work_bytes + tail)
        events = run_engine(
            args, save_dir, "restart-load", save=False, event_id=event_id
        )
        if (
            f"generic_id={event_id} generic_state={TEST_EVENT_STATE}"
            not in events
        ):
            raise AssertionError("restart load trusted stale EVENT.WRK over N.rpg")
        if work.stat().st_size != work_bytes + len(tail):
            raise AssertionError("restart truncated the ignored work file")

        retired = ("EVENT.DEF", "EVENT.STA", "EVENT.TMP", "EVENT.BAD")
        present = [name for name in retired if (save_dir / name).exists()]
        if present:
            raise AssertionError(f"retired event-state files were created: {present}")
        if list(save_dir.glob("*.tmp")) or list(save_dir.glob("*.bak")):
            raise AssertionError("save path created a private transaction file")

    print(
        "LEVEL1 save: ok "
        f"standard={dos_save_bytes} events={event_count} work={work_bytes}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
