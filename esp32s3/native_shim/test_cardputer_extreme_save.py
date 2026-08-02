#!/usr/bin/env python3
"""Integration test for the Cardputer extreme full-event/atomic save path."""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile
import zlib
from dataclasses import dataclass
from typing import Callable


SAVE_MAGIC = b"PALXSAVE"
SAVE_HEADER_BYTES = 64
SAVE_VERSION = 2
SAVE_FORMAT_DOS = 1
SAVE_EVENT_RECORD_BYTES = 32
SAVE_EVENT_COUNT = 5369
SAVE_EVENT_BYTES = SAVE_EVENT_COUNT * SAVE_EVENT_RECORD_BYTES
SAVE_EVENT_STATE_OFFSET = 12
DOS_SOURCE_EVENT_COUNT = 5332

LEGACY_SAVE_VERSION = 1
LEGACY_SAVE_HEADER_BYTES = 48
LEGACY_PROFILE = 0x32435A53  # "SZC2"
LEGACY_EVENT_COUNT = 424  # IDs 1..423 followed by ID 5334
LEGACY_PREFIX_EVENT_COUNT = 423
LEGACY_SPARSE_EVENT_ID = 5334

TEMPLATE_MAGIC = b"PALEVT1\0"
TEMPLATE_VERSION = 1
TEMPLATE_HEADER_BYTES = 512
TEMPLATE_HEADER_CRC_OFFSET = 508
TEMPLATE_EVENT_PAGE_COUNT = 42
TEMPLATE_PAGE_COUNT = 43
TEMPLATE_PAGE_BYTES = 4096
TEMPLATE_SCENE_RECORD_BYTES = 8
TEMPLATE_SCENE_COUNT = 300
TEMPLATE_SCENE_BYTES = TEMPLATE_SCENE_RECORD_BYTES * TEMPLATE_SCENE_COUNT
TEMPLATE_PAYLOAD_BYTES = TEMPLATE_PAGE_COUNT * TEMPLATE_PAGE_BYTES
TEMPLATE_FILE_BYTES = TEMPLATE_HEADER_BYTES + TEMPLATE_PAYLOAD_BYTES

PACK_MAGIC = 0x4B504C50
PACK_VERSION = 1
PACK_HEADER_BYTES = 32
PACK_SET_ID_OFFSET = 20

EVENT_STATE_FILE_BYTES = (
    1024 + TEMPLATE_PAGE_COUNT * 2 * (512 + TEMPLATE_PAGE_BYTES)
)

INIT_RE = re.compile(
    r"\binit\b.*\bscene=(\d+)\b.*\bstate=([0-9a-f]+)\b.*\bslot=5\b"
)


@dataclass(frozen=True)
class TemplateIdentity:
    image: bytes
    pack_set_id: int
    payload_crc32: int
    event_crc32: int
    scene_crc32: int

    @property
    def event_data(self) -> bytes:
        start = TEMPLATE_HEADER_BYTES
        return self.image[start : start + SAVE_EVENT_BYTES]


def u16(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def put_u16(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", data, offset, value)


def put_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value)


def parse_pack_set_id(path: pathlib.Path) -> int:
    with path.open("rb") as stream:
        data = stream.read(PACK_HEADER_BYTES)
    if len(data) != PACK_HEADER_BYTES:
        raise AssertionError(f"{path}: short pack header")
    magic, version, header_bytes = struct.unpack_from("<IHH", data, 0)
    if (
        magic != PACK_MAGIC
        or version != PACK_VERSION
        or header_bytes != PACK_HEADER_BYTES
    ):
        raise AssertionError(f"{path}: incompatible pack header")
    pack_set_id = u32(data, PACK_SET_ID_OFFSET)
    if pack_set_id == 0:
        raise AssertionError(f"{path}: zero pack-set ID")
    return pack_set_id


def parse_event_template(
    path: pathlib.Path,
    nor: pathlib.Path,
    tf: pathlib.Path,
) -> TemplateIdentity:
    image = path.read_bytes()
    if len(image) != TEMPLATE_FILE_BYTES:
        raise AssertionError(
            f"{path}: expected {TEMPLATE_FILE_BYTES} bytes, got {len(image)}"
        )
    header = image[:TEMPLATE_HEADER_BYTES]
    expected_fields = {
        "magic": (header[:8], TEMPLATE_MAGIC),
        "version": (u16(header, 8), TEMPLATE_VERSION),
        "header bytes": (u16(header, 10), TEMPLATE_HEADER_BYTES),
        "event record bytes": (u16(header, 16), SAVE_EVENT_RECORD_BYTES),
        "event records": (u16(header, 18), SAVE_EVENT_COUNT),
        "scene record bytes": (
            u16(header, 20),
            TEMPLATE_SCENE_RECORD_BYTES,
        ),
        "scene records": (u16(header, 22), TEMPLATE_SCENE_COUNT),
        "page bytes": (u32(header, 24), TEMPLATE_PAGE_BYTES),
        "event pages": (u16(header, 28), TEMPLATE_EVENT_PAGE_COUNT),
        "total pages": (u16(header, 30), TEMPLATE_PAGE_COUNT),
        "event bytes": (u32(header, 32), SAVE_EVENT_BYTES),
        "scene bytes": (u32(header, 36), TEMPLATE_SCENE_BYTES),
        "payload offset": (u32(header, 40), TEMPLATE_HEADER_BYTES),
        "payload bytes": (u32(header, 44), TEMPLATE_PAYLOAD_BYTES),
    }
    for label, (actual, expected) in expected_fields.items():
        if actual != expected:
            raise AssertionError(
                f"{path}: {label}: expected {expected!r}, got {actual!r}"
            )
    if any(header[124:TEMPLATE_HEADER_CRC_OFFSET]):
        raise AssertionError(f"{path}: nonzero reserved template header bytes")
    header_for_crc = bytearray(header)
    put_u32(header_for_crc, TEMPLATE_HEADER_CRC_OFFSET, 0)
    expected_header_crc = zlib.crc32(header_for_crc) & 0xFFFFFFFF
    if u32(header, TEMPLATE_HEADER_CRC_OFFSET) != expected_header_crc:
        raise AssertionError(f"{path}: template header CRC mismatch")

    pack_set_id = u32(header, 12)
    nor_pack_set_id = parse_pack_set_id(nor)
    tf_pack_set_id = parse_pack_set_id(tf)
    if not (
        pack_set_id != 0
        and pack_set_id == nor_pack_set_id
        and pack_set_id == tf_pack_set_id
    ):
        raise AssertionError(
            "EVENT.DEF/NOR/TF pack-set IDs differ: "
            f"{pack_set_id:#010x}/{nor_pack_set_id:#010x}/"
            f"{tf_pack_set_id:#010x}"
        )

    payload = image[TEMPLATE_HEADER_BYTES:]
    event_data = payload[:SAVE_EVENT_BYTES]
    event_tail = payload[
        SAVE_EVENT_BYTES : TEMPLATE_EVENT_PAGE_COUNT * TEMPLATE_PAGE_BYTES
    ]
    scene_offset = TEMPLATE_EVENT_PAGE_COUNT * TEMPLATE_PAGE_BYTES
    scene_data = payload[scene_offset : scene_offset + TEMPLATE_SCENE_BYTES]
    scene_tail = payload[scene_offset + TEMPLATE_SCENE_BYTES :]
    if any(event_tail) or any(scene_tail):
        raise AssertionError(f"{path}: nonzero EVENT.DEF page padding")

    payload_crc = zlib.crc32(payload) & 0xFFFFFFFF
    event_crc = zlib.crc32(event_data) & 0xFFFFFFFF
    scene_crc = zlib.crc32(scene_data) & 0xFFFFFFFF
    declared = (u32(header, 48), u32(header, 52), u32(header, 56))
    actual = (payload_crc, event_crc, scene_crc)
    if declared != actual:
        raise AssertionError(
            f"{path}: payload/event/scene CRC mismatch: "
            f"declared={declared!r} actual={actual!r}"
        )
    return TemplateIdentity(
        image=image,
        pack_set_id=pack_set_id,
        payload_crc32=payload_crc,
        event_crc32=event_crc,
        scene_crc32=scene_crc,
    )


def event_record(
    event_data: bytes | bytearray,
    event_id: int,
) -> bytes:
    if not 1 <= event_id <= SAVE_EVENT_COUNT:
        raise AssertionError(f"event ID {event_id} is outside the full profile")
    offset = (event_id - 1) * SAVE_EVENT_RECORD_BYTES
    return bytes(event_data[offset : offset + SAVE_EVENT_RECORD_BYTES])


def event_state(event_data: bytes | bytearray, event_id: int) -> int:
    record = event_record(event_data, event_id)
    return struct.unpack_from("<h", record, SAVE_EVENT_STATE_OFFSET)[0]


def put_event_state(
    event_data: bytearray,
    event_id: int,
    value: int,
) -> None:
    if not -32768 <= value <= 32767:
        raise AssertionError(f"event state {value} is outside int16")
    offset = (
        (event_id - 1) * SAVE_EVENT_RECORD_BYTES + SAVE_EVENT_STATE_OFFSET
    )
    struct.pack_into("<h", event_data, offset, value)


def refresh_v2_checksums(data: bytearray) -> None:
    if len(data) < SAVE_HEADER_BYTES:
        raise AssertionError("cannot refresh a short v2 save")
    fixed_bytes = u32(data, 20)
    event_bytes = u32(data, 28)
    fixed_start = SAVE_HEADER_BYTES
    event_start = fixed_start + fixed_bytes
    end = event_start + event_bytes
    if end != len(data):
        raise AssertionError("cannot refresh inconsistent v2 save lengths")
    fixed = data[fixed_start:event_start]
    events = data[event_start:end]
    put_u32(data, 36, zlib.crc32(data[fixed_start:end]) & 0xFFFFFFFF)
    put_u32(data, 52, zlib.crc32(fixed) & 0xFFFFFFFF)
    put_u32(data, 56, zlib.crc32(events) & 0xFFFFFFFF)
    put_u32(data, 60, zlib.crc32(data[:60]) & 0xFFFFFFFF)


def refresh_v2_header_checksum(data: bytearray) -> None:
    if len(data) < SAVE_HEADER_BYTES:
        raise AssertionError("cannot refresh a short v2 save header")
    put_u32(data, 60, zlib.crc32(data[:60]) & 0xFFFFFFFF)


def refresh_v1_checksums(data: bytearray) -> None:
    if len(data) < LEGACY_SAVE_HEADER_BYTES:
        raise AssertionError("cannot refresh a short v1 save")
    put_u32(
        data,
        36,
        zlib.crc32(data[LEGACY_SAVE_HEADER_BYTES:]) & 0xFFFFFFFF,
    )
    put_u32(data, 44, zlib.crc32(data[:44]) & 0xFFFFFFFF)


def validate_save(
    data: bytes,
    identity: TemplateIdentity,
) -> dict[str, int | bytes]:
    if len(data) < SAVE_HEADER_BYTES or data[:8] != SAVE_MAGIC:
        raise AssertionError("missing Cardputer extreme save magic")
    (
        version,
        header_bytes,
        profile,
        save_format,
        flags,
        fixed_bytes,
        event_count,
        event_bytes,
        total_bytes,
        payload_crc,
        scene,
        party,
        template_crc,
        generation,
        fixed_crc,
        event_crc,
        header_crc,
    ) = struct.unpack_from("<HHIHHIIIIIHHIIIII", data, 8)
    fixed_start = SAVE_HEADER_BYTES
    event_start = fixed_start + fixed_bytes
    expected = {
        "version": SAVE_VERSION,
        "header_bytes": SAVE_HEADER_BYTES,
        "profile": identity.pack_set_id,
        "save_format": SAVE_FORMAT_DOS,
        "flags": 0,
        "event_count": SAVE_EVENT_COUNT,
        "event_bytes": SAVE_EVENT_BYTES,
        "total_bytes": len(data),
        "template_crc": identity.payload_crc32,
        "payload_crc": zlib.crc32(data[fixed_start:]) & 0xFFFFFFFF,
        "fixed_crc": zlib.crc32(data[fixed_start:event_start])
        & 0xFFFFFFFF,
        "event_crc": zlib.crc32(data[event_start:]) & 0xFFFFFFFF,
        "header_crc": zlib.crc32(data[:60]) & 0xFFFFFFFF,
    }
    actual = {
        "version": version,
        "header_bytes": header_bytes,
        "profile": profile,
        "save_format": save_format,
        "flags": flags,
        "event_count": event_count,
        "event_bytes": event_bytes,
        "total_bytes": total_bytes,
        "template_crc": template_crc,
        "payload_crc": payload_crc,
        "fixed_crc": fixed_crc,
        "event_crc": event_crc,
        "header_crc": header_crc,
    }
    for key, value in expected.items():
        if actual[key] != value:
            raise AssertionError(
                f"{key}: expected {value:#x}, got {actual[key]:#x}"
            )
    if event_start + event_bytes != total_bytes:
        raise AssertionError("header lengths do not sum to exact file length")
    if generation == 0:
        raise AssertionError("save has a zero event-state generation")
    if not 1 <= scene < TEMPLATE_SCENE_COUNT:
        raise AssertionError(f"scene {scene} is outside 1..299")
    if party >= 3:
        raise AssertionError(f"party index {party} is outside 0..2")
    return {
        "fixed_bytes": fixed_bytes,
        "scene": scene,
        "party": party,
        "generation": generation,
        "total_bytes": total_bytes,
        "fixed_data": data[fixed_start:event_start],
        "event_data": data[event_start:],
    }


def build_legacy_v1_save(
    current: bytes,
    identity: TemplateIdentity,
    sparse_state: int,
) -> bytes:
    info = validate_save(current, identity)
    fixed = bytes(info["fixed_data"])
    current_events = bytes(info["event_data"])
    events = bytearray(
        current_events[: LEGACY_PREFIX_EVENT_COUNT * SAVE_EVENT_RECORD_BYTES]
    )
    events.extend(event_record(current_events, LEGACY_SPARSE_EVENT_ID))
    put_event_state(events, LEGACY_EVENT_COUNT, sparse_state)

    header = bytearray(LEGACY_SAVE_HEADER_BYTES)
    header[:8] = SAVE_MAGIC
    put_u16(header, 8, LEGACY_SAVE_VERSION)
    put_u16(header, 10, LEGACY_SAVE_HEADER_BYTES)
    put_u32(header, 12, LEGACY_PROFILE)
    put_u16(header, 16, SAVE_FORMAT_DOS)
    put_u16(header, 18, 0)
    put_u32(header, 20, len(fixed))
    put_u32(header, 24, LEGACY_EVENT_COUNT)
    put_u32(header, 28, len(events))
    put_u32(header, 32, len(header) + len(fixed) + len(events))
    put_u16(header, 40, int(info["scene"]))
    put_u16(header, 42, int(info["party"]))
    result = header + fixed + events
    refresh_v1_checksums(result)
    return bytes(result)


def mutate_valid_v2_events(
    source: bytes,
    states: dict[int, int],
) -> bytes:
    data = bytearray(source)
    fixed_bytes = u32(data, 20)
    event_start = SAVE_HEADER_BYTES + fixed_bytes
    events = bytearray(data[event_start:])
    for event_id, state in states.items():
        put_event_state(events, event_id, state)
    data[event_start:] = events
    refresh_v2_checksums(data)
    return bytes(data)


def build_alternate_profile(
    source: pathlib.Path,
    destination: pathlib.Path,
    pack_set_id: int,
) -> None:
    data = bytearray(source.read_bytes())
    if len(data) < PACK_HEADER_BYTES:
        raise AssertionError(f"{source}: short pack")
    put_u32(data, PACK_SET_ID_OFFSET, pack_set_id)
    put_u32(data, 28, 0)
    put_u32(data, 28, zlib.crc32(data) & 0xFFFFFFFF)
    destination.write_bytes(data)


def build_alternate_template(
    identity: TemplateIdentity,
    destination: pathlib.Path,
    pack_set_id: int,
) -> None:
    data = bytearray(identity.image)
    put_u32(data, 12, pack_set_id)
    put_u32(data, TEMPLATE_HEADER_CRC_OFFSET, 0)
    put_u32(
        data,
        TEMPLATE_HEADER_CRC_OFFSET,
        zlib.crc32(data[:TEMPLATE_HEADER_BYTES]) & 0xFFFFFFFF,
    )
    destination.write_bytes(data)


def build_alternate_template_defaults(
    identity: TemplateIdentity,
    destination: pathlib.Path,
    states: dict[int, int],
) -> None:
    data = bytearray(identity.image)
    event_data = bytearray(
        data[TEMPLATE_HEADER_BYTES : TEMPLATE_HEADER_BYTES + SAVE_EVENT_BYTES]
    )
    for event_id, state in states.items():
        put_event_state(event_data, event_id, state)
    data[
        TEMPLATE_HEADER_BYTES : TEMPLATE_HEADER_BYTES + SAVE_EVENT_BYTES
    ] = event_data

    payload = data[TEMPLATE_HEADER_BYTES:]
    scene_offset = (
        TEMPLATE_HEADER_BYTES
        + TEMPLATE_EVENT_PAGE_COUNT * TEMPLATE_PAGE_BYTES
    )
    scene_data = data[
        scene_offset : scene_offset + TEMPLATE_SCENE_BYTES
    ]
    put_u32(data, 48, zlib.crc32(payload) & 0xFFFFFFFF)
    put_u32(data, 52, zlib.crc32(event_data) & 0xFFFFFFFF)
    put_u32(data, 56, zlib.crc32(scene_data) & 0xFFFFFFFF)
    put_u32(data, TEMPLATE_HEADER_CRC_OFFSET, 0)
    put_u32(
        data,
        TEMPLATE_HEADER_CRC_OFFSET,
        zlib.crc32(data[:TEMPLATE_HEADER_BYTES]) & 0xFFFFFFFF,
    )
    destination.write_bytes(data)


class Runner:
    def __init__(
        self,
        binary: pathlib.Path,
        nor: pathlib.Path,
        tf: pathlib.Path,
        event_template: pathlib.Path,
        identity: TemplateIdentity,
        data_dir: pathlib.Path,
        route: pathlib.Path,
    ) -> None:
        self.binary = binary.resolve()
        self.nor = nor.resolve()
        self.tf = tf.resolve()
        self.event_template = event_template.resolve()
        self.identity = identity
        self.data_dir = data_dir.resolve()
        self.route = route.resolve()
        self.sequence = 0
        self.last_event_path: pathlib.Path | None = None
        self.last_output = ""

    def prepare_save_dir(self, save_dir: pathlib.Path) -> None:
        save_dir.mkdir(parents=True, exist_ok=True)
        destination = save_dir / "EVENT.DEF"
        if destination.exists():
            if destination.read_bytes() != self.identity.image:
                raise AssertionError(
                    f"{save_dir}: existing EVENT.DEF differs from --event-template"
                )
        else:
            shutil.copyfile(self.event_template, destination)

    def assert_event_storage(
        self,
        save_dir: pathlib.Path,
        *,
        expect_event_bad: bool = False,
    ) -> None:
        state = save_dir / "EVENT.STA"
        if not state.is_file():
            raise AssertionError(f"{save_dir}: EVENT.STA was not created")
        if state.stat().st_size != EVENT_STATE_FILE_BYTES:
            raise AssertionError(
                f"{save_dir}: EVENT.STA is {state.stat().st_size} bytes, "
                f"expected {EVENT_STATE_FILE_BYTES}"
            )
        expected_event_names = {"EVENT.DEF", "EVENT.STA"}
        if expect_event_bad:
            expected_event_names.add("EVENT.BAD")
        actual_event_names = {
            path.name for path in save_dir.iterdir() if path.name.startswith("EVENT")
        }
        if actual_event_names != expected_event_names:
            raise AssertionError(
                f"{save_dir}: unexpected EVENT artifacts {actual_event_names}"
            )
        if [path for path in save_dir.glob("*.tmp") if path.is_file()]:
            raise AssertionError(f"{save_dir}: save transaction left a .tmp file")

    def run(
        self,
        save_dir: pathlib.Path,
        *,
        save: bool = False,
        save_times: int = 77,
        sparse_state: int | None = None,
        generic_event_id: int | None = None,
        generic_state: int | None = None,
        reload: bool = False,
        failure: str | None = None,
        save_frame: int = 650,
        reload_frame: int = 650,
        max_presents: int | None = None,
        expect_event_bad: bool = False,
    ) -> tuple[int | None, str | None]:
        self.prepare_save_dir(save_dir)
        self.sequence += 1
        label = f"run-{self.sequence:02d}"
        event_path = save_dir / f"{label}.event"
        self.last_event_path = event_path
        env = os.environ.copy()
        env.update(
            {
                "PAL_CORES3SE_NATIVE_NOR_PACK": str(self.nor),
                "PAL_CORES3SE_NATIVE_TF_PACK": str(self.tf),
                "PAL_CORES3SE_NATIVE_SAVE_DIR": str(save_dir),
                "PAL_CORES3SE_NATIVE_SCREENSHOT_FRAME": "999999",
                "PAL_CORES3SE_NATIVE_MAX_PRESENTS": "0",
                "PAL_DETERMINISTIC_REPLAY": str(self.route),
                "PAL_DETERMINISTIC_CHECKPOINTS": str(
                    save_dir / f"{label}.trace"
                ),
                "PAL_DETERMINISTIC_SCRIPT_TRACE": str(
                    save_dir / f"{label}.script"
                ),
                "PAL_DETERMINISTIC_EVENT_TRACE": str(event_path),
                "PAL_DETERMINISTIC_MAX_PRESENTS": str(
                    max_presents
                    if max_presents is not None
                    else max(
                        720,
                        save_frame + 40 if save else 0,
                        reload_frame + 250 if reload else 0,
                    )
                ),
            }
        )
        for key in (
            "PAL_DETERMINISTIC_SAVE_FRAME",
            "PAL_DETERMINISTIC_SAVE_SLOT",
            "PAL_DETERMINISTIC_SAVE_TIMES",
            "PAL_DETERMINISTIC_SPARSE_EVENT_5334_STATE",
            "PAL_DETERMINISTIC_EVENT_ID",
            "PAL_DETERMINISTIC_EVENT_STATE",
            "PAL_DETERMINISTIC_RELOAD_FRAME",
            "PAL_DETERMINISTIC_RELOAD_SLOT",
            "PAL_CARDPUTER_NATIVE_SAVE_FAIL",
        ):
            env.pop(key, None)
        if save:
            env.update(
                {
                    "PAL_DETERMINISTIC_SAVE_FRAME": str(save_frame),
                    "PAL_DETERMINISTIC_SAVE_SLOT": "5",
                    "PAL_DETERMINISTIC_SAVE_TIMES": str(save_times),
                }
            )
            if sparse_state is not None:
                env["PAL_DETERMINISTIC_SPARSE_EVENT_5334_STATE"] = str(
                    sparse_state
                )
        if generic_event_id is not None:
            if not 1 <= generic_event_id <= SAVE_EVENT_COUNT:
                raise AssertionError(
                    f"generic event ID {generic_event_id} is out of range"
                )
            env["PAL_DETERMINISTIC_EVENT_ID"] = str(generic_event_id)
        if generic_state is not None:
            if generic_event_id is None:
                raise AssertionError("generic_state requires generic_event_id")
            if not save:
                raise AssertionError("generic_state mutation requires save=True")
            env["PAL_DETERMINISTIC_EVENT_STATE"] = str(generic_state)
        if reload:
            env.update(
                {
                    "PAL_DETERMINISTIC_RELOAD_FRAME": str(reload_frame),
                    "PAL_DETERMINISTIC_RELOAD_SLOT": "5",
                }
            )
        if failure is not None:
            env["PAL_CARDPUTER_NATIVE_SAVE_FAIL"] = failure

        result = subprocess.run(
            [str(self.binary)],
            cwd=self.data_dir,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=20,
            check=False,
        )
        self.last_output = result.stdout
        if result.returncode not in (0, 1):
            raise AssertionError(
                f"{label}: engine returned {result.returncode}\n{result.stdout}"
            )
        fatal_markers = (
            "TF event state initialization failed",
            "too many open files",
            "Too many open files",
        )
        for marker in fatal_markers:
            if marker in result.stdout:
                raise AssertionError(
                    f"{label}: event/save I/O failed with permanent "
                    f"EVENT.STA open:\n{result.stdout}"
                )
        self.assert_event_storage(
            save_dir, expect_event_bad=expect_event_bad
        )

        if not reload:
            return None, None
        matches = [
            INIT_RE.search(line)
            for line in event_path.read_text(encoding="utf-8").splitlines()
        ]
        matches = [match for match in matches if match is not None]
        if not matches:
            raise AssertionError(f"{label}: no slot-5 init event")
        return int(matches[-1].group(1)), matches[-1].group(2)


def has_event_snapshot(
    trace: str,
    tag: str,
    slot: int,
    sparse_state: int,
    generic_event_id: int,
    generic_state: int,
) -> bool:
    return any(
        f" {tag} " in line
        and f"slot={slot} " in line
        and f"sparse5334={sparse_state} " in line
        and (
            f"generic_id={generic_event_id} "
            f"generic_state={generic_state}"
        )
        in line
        for line in trace.splitlines()
    )


def write_variant(
    directory: pathlib.Path,
    source: bytes,
    mutate: Callable[[bytearray], None],
) -> None:
    directory.mkdir()
    data = bytearray(source)
    mutate(data)
    (directory / "5.rpg").write_bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=pathlib.Path, required=True)
    parser.add_argument("--nor", type=pathlib.Path, required=True)
    parser.add_argument("--tf", type=pathlib.Path, required=True)
    parser.add_argument("--event-template", type=pathlib.Path, required=True)
    parser.add_argument(
        "--data-dir",
        type=pathlib.Path,
        default=pathlib.Path("/mnt/hgfs/deb13/PALSteam/PAL_DOS"),
    )
    parser.add_argument(
        "--route",
        type=pathlib.Path,
        default=pathlib.Path(__file__).parents[2]
        / "tools"
        / "deterministic_routes"
        / "newgame_walk.route",
    )
    args = parser.parse_args()
    identity = parse_event_template(args.event_template, args.nor, args.tf)
    runner = Runner(
        args.binary,
        args.nor,
        args.tf,
        args.event_template,
        identity,
        args.data_dir,
        args.route,
    )

    with tempfile.TemporaryDirectory(
        prefix="pal-extreme-save-test-"
    ) as temp_text:
        temp = pathlib.Path(temp_text)
        generated = temp / "generated"
        out_of_prefix_event = 5000
        out_of_prefix_state = 1234
        runner.run(
            generated,
            save=True,
            sparse_state=9,
            generic_event_id=out_of_prefix_event,
            generic_state=out_of_prefix_state,
        )
        if runner.last_event_path is None:
            raise AssertionError("save run did not produce an event trace path")
        first_save_events = runner.last_event_path.read_text(encoding="utf-8")
        if (
            "saved_times=77 menu_saved_times=77 sparse5334=9"
            not in first_save_events
            or "generic_id=5000 generic_state=1234"
            not in first_save_events
        ):
            raise AssertionError(
                "menu save counter/full-range event harness did not decode v2"
            )
        save_path = generated / "5.rpg"
        original = save_path.read_bytes()
        info = validate_save(original, identity)
        if event_state(bytes(info["event_data"]), LEGACY_SPARSE_EVENT_ID) != 9:
            raise AssertionError("v2 payload did not store event 5334")
        if (
            event_state(bytes(info["event_data"]), out_of_prefix_event)
            != out_of_prefix_state
        ):
            raise AssertionError("v2 payload did not store event 5000")
        if (generated / "5.tmp").exists() or (generated / "5.bak").exists():
            raise AssertionError("successful first save left a transaction sidecar")

        valid_scene, valid_state = runner.run(
            generated,
            reload=True,
            generic_event_id=out_of_prefix_event,
        )
        if valid_scene != info["scene"]:
            raise AssertionError("v2 save did not restore its scene")
        if runner.last_event_path is None:
            raise AssertionError("reload did not produce an event trace")
        reload_events = runner.last_event_path.read_text(encoding="utf-8")
        if (
            "init" not in reload_events
            or "sparse5334=9" not in reload_events
            or "generic_id=5000 generic_state=1234" not in reload_events
        ):
            raise AssertionError(
                "events 5000/5334 did not survive restart/reload"
            )

        #
        # A malformed live journal is never truncated in place.  Provision a
        # verified replacement, quarantine the rejected bytes as EVENT.BAD,
        # then prove that a valid v2 save can still repopulate all event pages.
        #
        state_recovery_cases = {
            "short": b"interrupted-event-state",
            "corrupt": b"\xa5" * EVENT_STATE_FILE_BYTES,
        }
        for recovery_name, rejected_state_image in state_recovery_cases.items():
            directory = temp / f"event-state-{recovery_name}"
            directory.mkdir()
            (directory / "5.rpg").write_bytes(original)
            (directory / "EVENT.STA").write_bytes(rejected_state_image)
            recovered_live_scene, recovered_live_state = runner.run(
                directory,
                reload=True,
                generic_event_id=out_of_prefix_event,
                expect_event_bad=True,
            )
            if (
                (recovered_live_scene, recovered_live_state)
                != (valid_scene, valid_state)
                or (directory / "EVENT.BAD").read_bytes()
                != rejected_state_image
                or (directory / "EVENT.TMP").exists()
                or "op=state_install replaced_bad=1 result=ok"
                not in runner.last_output
            ):
                raise AssertionError(
                    f"{recovery_name} EVENT.STA was not safely quarantined "
                    "and recovered"
                )
            if runner.last_event_path is None:
                raise AssertionError("state recovery produced no event trace")
            recovery_events = runner.last_event_path.read_text(
                encoding="utf-8"
            )
            if (
                "sparse5334=9" not in recovery_events
                or "generic_id=5000 generic_state=1234"
                not in recovery_events
            ):
                raise AssertionError(
                    f"{recovery_name} EVENT.STA recovery lost full events"
                )

        #
        # A short provisioning temporary with no live journal is disposable:
        # remove it, rebuild a complete default journal, and leave no temp.
        #
        residual_temp = temp / "event-state-residual-temp"
        residual_temp.mkdir()
        (residual_temp / "5.rpg").write_bytes(original)
        (residual_temp / "EVENT.TMP").write_bytes(b"short-provisioning-temp")
        residual_scene, residual_state = runner.run(
            residual_temp,
            reload=True,
            generic_event_id=out_of_prefix_event,
        )
        if (
            (residual_scene, residual_state) != (valid_scene, valid_state)
            or (residual_temp / "EVENT.TMP").exists()
            or (residual_temp / "EVENT.BAD").exists()
            or "op=state_provision" not in runner.last_output
            or "op=state_install replaced_bad=0 result=ok"
            not in runner.last_output
        ):
            raise AssertionError(
                "short residual EVENT.TMP was not rebuilt cleanly"
            )

        #
        # Change only the valid pack-set identity A->B->A while retaining one
        # journal and one slot-5 save.  A normal startup explicitly begins a
        # New Game and must therefore reset EVENT.STA to defaults; saved
        # progress is recovered only by the subsequent slot-5 load.  Both
        # switches must still use the atomic identity-rebind path, and the
        # return to A must not resurrect an older A commit.
        #
        alternate_profile_id = identity.pack_set_id ^ 0x01010101
        if alternate_profile_id == 0:
            alternate_profile_id ^= 0x80000000
        alternate_nor = temp / "profile-b-nor.pak"
        alternate_tf = temp / "profile-b-tf.pak"
        alternate_template = temp / "profile-b-EVENT.DEF"
        build_alternate_profile(
            args.nor, alternate_nor, alternate_profile_id
        )
        build_alternate_profile(
            args.tf, alternate_tf, alternate_profile_id
        )
        build_alternate_template(
            identity, alternate_template, alternate_profile_id
        )
        alternate_identity = parse_event_template(
            alternate_template, alternate_nor, alternate_tf
        )
        profile_switch = temp / "profile-roundtrip"
        runner.run(
            profile_switch,
            save=True,
            sparse_state=9,
            generic_event_id=out_of_prefix_event,
            generic_state=out_of_prefix_state,
        )
        shutil.copyfile(
            alternate_template, profile_switch / "EVENT.DEF"
        )
        alternate_runner = Runner(
            args.binary,
            alternate_nor,
            alternate_tf,
            alternate_template,
            alternate_identity,
            args.data_dir,
            args.route,
        )
        alternate_runner.run(
            profile_switch,
            generic_event_id=out_of_prefix_event,
            reload=True,
        )
        expected_a_to_b = (
            f"op=state_rebind old_profile={identity.pack_set_id:08x} "
            f"profile={alternate_profile_id:08x}"
        )
        if expected_a_to_b not in alternate_runner.last_output:
            raise AssertionError("profile A->B did not rebind newest state")
        if alternate_runner.last_event_path is None:
            raise AssertionError("profile A->B produced no event trace")
        alternate_events = alternate_runner.last_event_path.read_text(
            encoding="utf-8"
        )
        default_sparse_state = event_state(
            identity.event_data, LEGACY_SPARSE_EVENT_ID
        )
        default_generic_state = event_state(
            identity.event_data, out_of_prefix_event
        )
        if not has_event_snapshot(
            alternate_events,
            "init",
            0,
            default_sparse_state,
            out_of_prefix_event,
            default_generic_state,
        ):
            raise AssertionError(
                "profile A->B New Game did not reset EVENT.STA defaults"
            )
        if not has_event_snapshot(
            alternate_events,
            "init",
            5,
            9,
            out_of_prefix_event,
            out_of_prefix_state,
        ):
            raise AssertionError(
                "profile A->B slot-5 load did not restore events 5000/5334"
            )
        shutil.copyfile(args.event_template, profile_switch / "EVENT.DEF")
        runner.run(
            profile_switch,
            generic_event_id=out_of_prefix_event,
            reload=True,
        )
        expected_b_to_a = (
            f"op=state_rebind old_profile={alternate_profile_id:08x} "
            f"profile={identity.pack_set_id:08x}"
        )
        if expected_b_to_a not in runner.last_output:
            raise AssertionError(
                "profile B->A selected an older A commit instead of newest B"
            )
        if runner.last_event_path is None:
            raise AssertionError("profile B->A produced no event trace")
        roundtrip_events = runner.last_event_path.read_text(encoding="utf-8")
        if not has_event_snapshot(
            roundtrip_events,
            "init",
            0,
            default_sparse_state,
            out_of_prefix_event,
            default_generic_state,
        ):
            raise AssertionError(
                "profile B->A New Game did not reset EVENT.STA defaults"
            )
        if not has_event_snapshot(
            roundtrip_events,
            "init",
            5,
            9,
            out_of_prefix_event,
            out_of_prefix_state,
        ):
            raise AssertionError(
                "profile B->A slot-5 load did not restore events 5000/5334"
            )

        #
        # A different template payload is not identity-compatible.  It must
        # take the replacement path and expose the new template defaults,
        # never carry live event mutations across the template boundary.
        #
        changed_default_5000 = -2222
        changed_default_5334 = 2222
        changed_template = temp / "changed-defaults-EVENT.DEF"
        build_alternate_template_defaults(
            identity,
            changed_template,
            {
                out_of_prefix_event: changed_default_5000,
                LEGACY_SPARSE_EVENT_ID: changed_default_5334,
            },
        )
        changed_identity = parse_event_template(
            changed_template, args.nor, args.tf
        )
        if changed_identity.payload_crc32 == identity.payload_crc32:
            raise AssertionError("changed template retained the old payload CRC")
        template_switch = temp / "template-switch"
        runner.run(
            template_switch,
            save=True,
            sparse_state=9,
            generic_event_id=out_of_prefix_event,
            generic_state=out_of_prefix_state,
        )
        shutil.copyfile(changed_template, template_switch / "EVENT.DEF")
        changed_runner = Runner(
            args.binary,
            args.nor,
            args.tf,
            changed_template,
            changed_identity,
            args.data_dir,
            args.route,
        )
        changed_runner.run(
            template_switch,
            generic_event_id=out_of_prefix_event,
        )
        expected_template_rebase = (
            f"op=state_rebase old_profile={identity.pack_set_id:08x} "
            f"old_template={identity.payload_crc32:08x} "
            f"profile={identity.pack_set_id:08x} "
            f"template={changed_identity.payload_crc32:08x}"
        )
        if expected_template_rebase not in changed_runner.last_output:
            raise AssertionError(
                "different template CRC did not replace journal defaults"
            )
        if changed_runner.last_event_path is None:
            raise AssertionError("template replacement produced no event trace")
        changed_events = changed_runner.last_event_path.read_text(
            encoding="utf-8"
        )
        if (
            f"sparse5334={changed_default_5334}" not in changed_events
            or (
                f"generic_id={out_of_prefix_event} "
                f"generic_state={changed_default_5000}"
            )
            not in changed_events
        ):
            raise AssertionError(
                "different template CRC retained old live event mutations"
            )

        #
        # Exercise an event that was impossible to represent in SZC2.  The
        # save is first made valid with IDs 5000 and 5334 changed, then loaded
        # and saved again by the engine.  Comparing the second v2 payload
        # tests full-range paging without requiring a special mutation hook.
        #
        full_range = temp / "full-range"
        full_range.mkdir()
        sparse_state = -1234
        injected = mutate_valid_v2_events(
            original,
            {
                out_of_prefix_event: out_of_prefix_state,
                LEGACY_SPARSE_EVENT_ID: sparse_state,
            },
        )
        (full_range / "5.rpg").write_bytes(injected)
        runner.run(
            full_range,
            reload=True,
            reload_frame=500,
            save=True,
            save_frame=780,
            save_times=78,
            max_presents=840,
        )
        full_range_save = (full_range / "5.rpg").read_bytes()
        full_range_info = validate_save(full_range_save, identity)
        full_range_events = bytes(full_range_info["event_data"])
        if (
            event_state(full_range_events, out_of_prefix_event)
            != out_of_prefix_state
            or event_state(full_range_events, LEGACY_SPARSE_EVENT_ID)
            != sparse_state
        ):
            raise AssertionError(
                "full-range events 5000/5334 did not survive load/save: "
                f"{event_state(full_range_events, out_of_prefix_event)}/"
                f"{event_state(full_range_events, LEGACY_SPARSE_EVENT_ID)}"
            )
        if (full_range / "5.bak").read_bytes() != injected:
            raise AssertionError("full-range overwrite did not retain prior v2 save")

        corrupt = temp / "corrupt"
        write_variant(
            corrupt,
            original,
            lambda data: data.__setitem__(12, data[12] ^ 1),
        )
        fallback_scene, fallback_state = runner.run(corrupt, reload=True)
        if fallback_state == valid_state:
            raise AssertionError("corrupt profile was accepted")

        def mutate_u16(
            offset: int,
            value: int,
            *,
            refresh: bool = True,
        ) -> Callable[[bytearray], None]:
            def apply(data: bytearray) -> None:
                put_u16(data, offset, value)
                if refresh:
                    refresh_v2_checksums(data)

            return apply

        def mutate_u32(
            offset: int,
            value: int,
            *,
            refresh: bool = True,
        ) -> Callable[[bytearray], None]:
            def apply(data: bytearray) -> None:
                put_u32(data, offset, value)
                if refresh:
                    refresh_v2_checksums(data)

            return apply

        def mutate_event_bytes(data: bytearray) -> None:
            put_u32(data, 28, SAVE_EVENT_BYTES - SAVE_EVENT_RECORD_BYTES)
            refresh_v2_header_checksum(data)

        def mutate_checksum_field(
            offset: int,
        ) -> Callable[[bytearray], None]:
            def apply(data: bytearray) -> None:
                put_u32(data, offset, u32(data, offset) ^ 1)
                refresh_v2_header_checksum(data)

            return apply

        def mutate_scene(data: bytearray) -> None:
            put_u16(data, 40, TEMPLATE_SCENE_COUNT)
            put_u16(data, SAVE_HEADER_BYTES + 8, TEMPLATE_SCENE_COUNT)
            refresh_v2_checksums(data)

        def mutate_party(data: bytearray) -> None:
            put_u16(data, 42, 3)
            put_u16(data, SAVE_HEADER_BYTES + 6, 3)
            refresh_v2_checksums(data)

        def mutate_role(data: bytearray) -> None:
            put_u16(data, SAVE_HEADER_BYTES + 44, 6)
            refresh_v2_checksums(data)

        def mutate_fixed_payload(data: bytearray) -> None:
            data[SAVE_HEADER_BYTES + 100] ^= 1

        def mutate_event_payload(data: bytearray) -> None:
            event_offset = SAVE_HEADER_BYTES + u32(data, 20)
            data[event_offset + 100] ^= 1

        def mutate_trailing(data: bytearray) -> None:
            data.append(0)

        compatible_profile = temp / "compatible-profile"
        compatible_profile_id = identity.pack_set_id ^ 0x01020304
        if compatible_profile_id == 0:
            compatible_profile_id = 1
        write_variant(
            compatible_profile,
            original,
            mutate_u32(12, compatible_profile_id),
        )
        compatible_scene, compatible_state = runner.run(
            compatible_profile, reload=True
        )
        if (compatible_scene, compatible_state) != (
            valid_scene,
            valid_state,
        ):
            raise AssertionError(
                "same-template save from another resource profile was rejected"
            )

        variants: dict[str, Callable[[bytearray], None]] = {
            "version": mutate_u16(8, SAVE_VERSION + 1),
            "format": mutate_u16(16, 2),
            "template": mutate_u32(44, identity.payload_crc32 ^ 1),
            "event-count": mutate_u32(24, SAVE_EVENT_COUNT - 1),
            "event-bytes": mutate_event_bytes,
            "trailing-byte": mutate_trailing,
            "header-crc": mutate_u32(60, u32(original, 60) ^ 1, refresh=False),
            "payload-crc": mutate_checksum_field(36),
            "fixed-crc": mutate_checksum_field(52),
            "event-crc": mutate_checksum_field(56),
            "fixed-payload": mutate_fixed_payload,
            "event-payload": mutate_event_payload,
            "scene-bound": mutate_scene,
            "party-bound": mutate_party,
            "role-bound": mutate_role,
        }
        for name, mutate in variants.items():
            directory = temp / name
            write_variant(directory, original, mutate)
            _, state = runner.run(directory, reload=True)
            if state != fallback_state:
                raise AssertionError(f"{name} variant was accepted")

        recovery = temp / "recovery"
        recovery.mkdir()
        (recovery / "5.rpg").write_bytes(
            (temp / "corrupt" / "5.rpg").read_bytes()
        )
        (recovery / "5.bak").write_bytes(original)
        recovered_scene, recovered_state = runner.run(recovery, reload=True)
        if (recovered_scene, recovered_state) != (valid_scene, valid_state):
            raise AssertionError("valid backup did not recover a corrupt final")

        recovery_failure = temp / "recovery-save-failure"
        recovery_failure.mkdir()
        (recovery_failure / "5.rpg").write_bytes(
            (temp / "corrupt" / "5.rpg").read_bytes()
        )
        (recovery_failure / "5.bak").write_bytes(original)
        runner.run(
            recovery_failure,
            save=True,
            save_times=88,
            failure="rename_temp_final",
        )
        if not (recovery_failure / "5.bak").is_file():
            raise AssertionError(
                "failed recovery save discarded the only valid backup"
            )
        recovered_scene_2, recovered_state_2 = runner.run(
            recovery_failure, reload=True
        )
        if (recovered_scene_2, recovered_state_2) != (
            valid_scene,
            valid_state,
        ):
            raise AssertionError("failed recovery save lost valid backup state")

        overwrite = temp / "overwrite"
        overwrite.mkdir()
        (overwrite / "5.rpg").write_bytes(original)
        runner.run(overwrite, save=True, save_times=88)
        overwritten = (overwrite / "5.rpg").read_bytes()
        overwrite_info = validate_save(overwritten, identity)
        if struct.unpack_from("<H", overwritten, SAVE_HEADER_BYTES)[0] != 88:
            raise AssertionError(
                "overwrite reused old scratch state instead of new save data"
            )
        if (overwrite / "5.bak").read_bytes() != original:
            raise AssertionError(
                "successful overwrite did not retain the prior valid save"
            )
        if int(overwrite_info["generation"]) == 0:
            raise AssertionError("overwrite lost event-state generation")
        if runner.last_event_path is None or (
            "saved_times=88 menu_saved_times=88"
            not in runner.last_event_path.read_text(encoding="utf-8")
        ):
            raise AssertionError("menu save counter did not observe overwrite")

        for failure in ("rename_final_backup", "rename_temp_final"):
            directory = temp / failure
            directory.mkdir()
            (directory / "5.rpg").write_bytes(original)
            before = (directory / "5.rpg").read_bytes()
            runner.run(directory, save=True, save_times=88, failure=failure)
            if (directory / "5.rpg").read_bytes() != before:
                raise AssertionError(f"{failure} changed the committed save")
            if (directory / "5.tmp").exists():
                raise AssertionError(f"{failure} left a temporary file")

        write_open_failure = temp / "write-open-failure"
        write_open_failure.mkdir()
        (write_open_failure / "5.rpg").write_bytes(original)
        (write_open_failure / "5.tmp").mkdir()
        runner.run(write_open_failure, save=True, save_times=88)
        if (write_open_failure / "5.rpg").read_bytes() != original:
            raise AssertionError(
                "temporary-file open failure changed the committed save"
            )
        shutil.rmtree(write_open_failure / "5.tmp")

        unlink_failure = temp / "unlink-backup"
        unlink_failure.mkdir()
        (unlink_failure / "5.rpg").write_bytes(original)
        (unlink_failure / "5.bak").write_bytes(original)
        runner.run(
            unlink_failure,
            save=True,
            save_times=88,
            failure="unlink_backup",
        )
        if (unlink_failure / "5.rpg").read_bytes() != original:
            raise AssertionError("unlink_backup changed the committed save")
        if (unlink_failure / "5.bak").read_bytes() != original:
            raise AssertionError("unlink_backup discarded the recovery save")
        if (unlink_failure / "5.tmp").exists():
            raise AssertionError("unlink_backup left a temporary file")

        legacy_sparse_state = 2345
        legacy_v1 = build_legacy_v1_save(
            original, identity, legacy_sparse_state
        )
        legacy_tagged = temp / "legacy-szc2"
        legacy_tagged.mkdir()
        (legacy_tagged / "5.rpg").write_bytes(legacy_v1)
        runner.run(
            legacy_tagged,
            reload=True,
            reload_frame=500,
            save=True,
            save_frame=780,
            save_times=89,
            max_presents=840,
        )
        migrated_tagged = (legacy_tagged / "5.rpg").read_bytes()
        migrated_tagged_info = validate_save(migrated_tagged, identity)
        migrated_tagged_events = bytes(migrated_tagged_info["event_data"])
        if (
            event_state(migrated_tagged_events, LEGACY_SPARSE_EVENT_ID)
            != legacy_sparse_state
        ):
            raise AssertionError("SZC2 sparse event 5334 migrated incorrectly")
        if event_record(
            migrated_tagged_events, LEGACY_PREFIX_EVENT_COUNT + 1
        ) != event_record(
            identity.event_data, LEGACY_PREFIX_EVENT_COUNT + 1
        ):
            raise AssertionError(
                "SZC2 migration treated its sparse record as event 424"
            )
        if event_record(
            migrated_tagged_events, out_of_prefix_event
        ) != event_record(identity.event_data, out_of_prefix_event):
            raise AssertionError(
                "SZC2 migration did not preserve full-template defaults"
            )
        if (legacy_tagged / "5.bak").read_bytes() != legacy_v1:
            raise AssertionError("SZC2 migration did not retain v1 backup")

        legacy_results: list[tuple[str, int, int]] = []
        incompatible_legacy: list[tuple[str, int]] = []
        first_legacy: pathlib.Path | None = None
        legacy_sources = sorted(
            (
                path
                for path in args.data_dir.iterdir()
                if path.is_file()
                and path.name.lower() in {"0.rpg", "1.rpg", "2.rpg"}
            ),
            key=lambda path: path.name,
        )
        for legacy_source in legacy_sources:
            legacy_name = legacy_source.name
            source_data = legacy_source.read_bytes()
            fixed_bytes = int(info["fixed_bytes"])
            if (
                len(source_data) < fixed_bytes
                or (len(source_data) - fixed_bytes)
                % SAVE_EVENT_RECORD_BYTES
                != 0
            ):
                incompatible_legacy.append((legacy_name, -1))
                continue
            source_records = (
                len(source_data) - fixed_bytes
            ) // SAVE_EVENT_RECORD_BYTES
            if source_records != DOS_SOURCE_EVENT_COUNT:
                # A save from a modified resource set (for example the local
                # former 5369-record data set) cannot be losslessly imported
                # into the stock PAL_DOS source identity and must not be
                # truncated.
                directory = temp / (
                    "legacy-dos-incompatible-"
                    + legacy_name.replace(".", "-")
                )
                directory.mkdir()
                (directory / "5.rpg").write_bytes(source_data)
                rejected_scene, rejected_state = runner.run(
                    directory, reload=True
                )
                if (
                    rejected_scene != fallback_scene
                    or rejected_state != fallback_state
                    or (directory / "5.rpg").read_bytes() != source_data
                    or (directory / "5.bak").exists()
                ):
                    raise AssertionError(
                        f"incompatible legacy DOS {legacy_name} was "
                        "accepted, truncated, or modified"
                    )
                incompatible_legacy.append((legacy_name, source_records))
                continue
            if first_legacy is None:
                first_legacy = legacy_source
            directory = temp / (
                "legacy-dos-" + legacy_name.replace(".", "-")
            )
            directory.mkdir()
            (directory / "5.rpg").write_bytes(source_data)
            runner.run(
                directory,
                reload=True,
                reload_frame=500,
                save=True,
                save_frame=780,
                save_times=90,
                max_presents=840,
            )
            migrated = (directory / "5.rpg").read_bytes()
            migrated_info = validate_save(migrated, identity)
            migrated_events = bytes(migrated_info["event_data"])
            source_event_bytes = source_data[fixed_bytes:]
            if (
                len(source_event_bytes)
                != DOS_SOURCE_EVENT_COUNT * SAVE_EVENT_RECORD_BYTES
                or event_record(migrated_events, out_of_prefix_event)
                != event_record(source_event_bytes, out_of_prefix_event)
                or any(
                    migrated_events[
                        DOS_SOURCE_EVENT_COUNT * SAVE_EVENT_RECORD_BYTES :
                    ]
                )
            ):
                raise AssertionError(
                    f"legacy DOS {legacy_name} source events or normalized "
                    "journal tail migrated incorrectly"
                )
            if (directory / "5.bak").read_bytes() != source_data:
                raise AssertionError(
                    f"legacy DOS {legacy_name} source was not retained"
                )
            legacy_results.append(
                (
                    legacy_name,
                    int(migrated_info["scene"]),
                    len(source_event_bytes) // SAVE_EVENT_RECORD_BYTES,
                )
            )

        if first_legacy is not None:
            legacy_recovery = temp / "legacy-recovery-save-failure"
            legacy_recovery.mkdir()
            shutil.copyfile(first_legacy, legacy_recovery / "5.rpg")
            (legacy_recovery / "5.bak").write_bytes(original)
            runner.run(
                legacy_recovery,
                save=True,
                save_times=91,
                failure="rename_temp_final",
            )
            if (legacy_recovery / "5.bak").read_bytes() != original:
                raise AssertionError(
                    "legacy conversion replaced checksum-protected backup"
                )
            recovered_scene_3, recovered_state_3 = runner.run(
                legacy_recovery, reload=True
            )
            if (recovered_scene_3, recovered_state_3) != (
                valid_scene,
                valid_state,
            ):
                raise AssertionError(
                    "failed legacy conversion lost tagged recovery save"
                )

        print(
            "PASS cardputer extreme full-event save:",
            f"bytes={info['total_bytes']}",
            f"fixed={info['fixed_bytes']}",
            f"events={SAVE_EVENT_COUNT}",
            f"profile={identity.pack_set_id:08x}",
            f"template={identity.payload_crc32:08x}",
            f"generation={info['generation']}",
            f"scene={valid_scene}",
            f"state={valid_state}",
            f"fallback={fallback_state}",
            f"mutations={len(variants)}",
            "full_range=5000,5334",
            f"state_recovery={len(state_recovery_cases) + 1}",
            "profile_roundtrip=A-B-A",
            "journal_rebind=save-reload",
            "template_change=defaults",
            "save_profile_compat=same-template",
            "atomic_failures=6",
            "legacy_szc2=1",
            f"legacy_dos={legacy_results}",
            f"legacy_incompatible={incompatible_legacy}",
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
