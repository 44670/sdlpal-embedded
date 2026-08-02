#!/usr/bin/env python3
"""Build the fixed native EVENT.DEF template from a generated pal_full.pak."""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path


PACK_MAGIC = 0x4B504C50
PACK_VERSION = 1
PACK_HEADER_BYTES = 32
PACK_ARCHIVE_ENTRY_BYTES = 12
PACK_CHUNK_ENTRY_BYTES = 16
PACK_SET_ID_OFFSET = 20
PACK_SIZE_OFFSET = 24
PACK_CRC32_OFFSET = 28
PACK_FORMAT_NATIVE = 1
PACK_ARCHIVE_SSS = 15

TEMPLATE_MAGIC = b"PALEVT1\0"
TEMPLATE_VERSION = 1
TEMPLATE_HEADER_BYTES = 512
TEMPLATE_HEADER_CRC32_OFFSET = 508
TEMPLATE_PAYLOAD_OFFSET = TEMPLATE_HEADER_BYTES

EVENT_RECORD_BYTES = 32
EVENT_RECORD_COUNT = 5369
EVENT_BYTES = EVENT_RECORD_BYTES * EVENT_RECORD_COUNT
EVENT_PAGE_COUNT = 42

# The stock PALSteam/PAL_DOS resources contain 5,332 event records and 294
# scene rows.  Keep the established on-card journal geometry stable by
# appending unreachable zero event records and sentinel scene rows when that
# exact source shape is packed.  The source bytes themselves remain unchanged
# in pal_full.pak; normalization happens only in EVENT.DEF.
PAL_DOS_EVENT_RECORD_COUNT = 5332
PAL_DOS_EVENT_BYTES = EVENT_RECORD_BYTES * PAL_DOS_EVENT_RECORD_COUNT
PAL_DOS_SCENE_RECORD_COUNT = 294
PAL_DOS_SCENE_BYTES = 8 * PAL_DOS_SCENE_RECORD_COUNT

SCENE_RECORD_BYTES = 8
SCENE_RECORD_COUNT = 300
SCENE_BYTES = SCENE_RECORD_BYTES * SCENE_RECORD_COUNT
SCENE_PAGE = EVENT_PAGE_COUNT
SCENE_EVENT_OBJECT_CAPACITY = 160

PAGE_BYTES = 4096
PAGE_COUNT = EVENT_PAGE_COUNT + 1
PAYLOAD_BYTES = PAGE_COUNT * PAGE_BYTES
TEMPLATE_BYTES = TEMPLATE_HEADER_BYTES + PAYLOAD_BYTES

HEADER_VERSION_OFFSET = 8
HEADER_BYTES_OFFSET = 10
HEADER_PACK_SET_ID_OFFSET = 12
HEADER_EVENT_RECORD_BYTES_OFFSET = 16
HEADER_EVENT_RECORD_COUNT_OFFSET = 18
HEADER_SCENE_RECORD_BYTES_OFFSET = 20
HEADER_SCENE_RECORD_COUNT_OFFSET = 22
HEADER_PAGE_BYTES_OFFSET = 24
HEADER_EVENT_PAGE_COUNT_OFFSET = 28
HEADER_PAGE_COUNT_OFFSET = 30
HEADER_EVENT_BYTES_OFFSET = 32
HEADER_SCENE_BYTES_OFFSET = 36
HEADER_PAYLOAD_OFFSET_OFFSET = 40
HEADER_PAYLOAD_BYTES_OFFSET = 44
HEADER_PAYLOAD_CRC32_OFFSET = 48
HEADER_EVENT_CRC32_OFFSET = 52
HEADER_SCENE_CRC32_OFFSET = 56
HEADER_EVENT_SHA256_OFFSET = 60
HEADER_SCENE_SHA256_OFFSET = 92
HEADER_RESERVED_OFFSET = 124


@dataclass(frozen=True)
class PackChunk:
    payload: bytes
    fmt: int
    flags: int


@dataclass(frozen=True)
class EventTemplateSummary:
    pack_set_id: int
    payload_crc32: int
    event_crc32: int
    scene_crc32: int
    header_crc32: int
    event_sha256: str
    scene_sha256: str
    total_bytes: int


def checked_range(offset: int, size: int, total: int) -> bool:
    return 0 <= offset <= total and 0 <= size <= total - offset


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def validate_scene_page_spans(scene_data: bytes) -> None:
    """Reject scene tables that cannot be pinned in the three-page runtime."""
    if len(scene_data) != SCENE_BYTES:
        raise ValueError(
            f"scene table is {len(scene_data)} bytes, expected {SCENE_BYTES}"
        )

    boundaries = [
        u16(scene_data, scene_index * SCENE_RECORD_BYTES + 6)
        for scene_index in range(SCENE_RECORD_COUNT)
    ]
    for boundary_index, boundary in enumerate(boundaries):
        if boundary > EVENT_RECORD_COUNT:
            raise ValueError(
                f"scene boundary {boundary_index} is {boundary}, "
                f"beyond event count {EVENT_RECORD_COUNT}"
            )
        if boundary_index != 0 and boundary < boundaries[boundary_index - 1]:
            raise ValueError(
                f"scene boundary {boundary_index} decreases from "
                f"{boundaries[boundary_index - 1]} to {boundary}"
            )

    # Scene N (1..299) occupies [boundary[N - 1], boundary[N]).
    for scene_number in range(1, SCENE_RECORD_COUNT):
        start = boundaries[scene_number - 1]
        end = boundaries[scene_number]
        if end - start > SCENE_EVENT_OBJECT_CAPACITY:
            raise ValueError(
                f"scene {scene_number} has {end - start} event objects, "
                f"beyond runtime capacity {SCENE_EVENT_OBJECT_CAPACITY}"
            )
        if (
            end != start
            and (end - 1) // (PAGE_BYTES // EVENT_RECORD_BYTES)
            - start // (PAGE_BYTES // EVENT_RECORD_BYTES)
            >= 2
        ):
            raise ValueError(
                f"scene {scene_number} event range [{start}, {end}) "
                "spans more than two event pages"
            )


def normalize_event_data(event_data: bytes) -> bytes:
    if len(event_data) == EVENT_BYTES:
        return event_data
    if len(event_data) != PAL_DOS_EVENT_BYTES:
        raise ValueError(
            f"pal_full.pak SSS chunk 0 is {len(event_data)} bytes, "
            f"expected {PAL_DOS_EVENT_BYTES} or {EVENT_BYTES}"
        )
    return event_data + bytes(EVENT_BYTES - len(event_data))


def normalize_scene_data(scene_data: bytes) -> bytes:
    if len(scene_data) == SCENE_BYTES:
        return scene_data
    if len(scene_data) != PAL_DOS_SCENE_BYTES:
        raise ValueError(
            f"pal_full.pak SSS chunk 1 is {len(scene_data)} bytes, "
            f"expected {PAL_DOS_SCENE_BYTES} or {SCENE_BYTES}"
        )

    last_boundary = u16(
        scene_data,
        (PAL_DOS_SCENE_RECORD_COUNT - 1) * SCENE_RECORD_BYTES + 6,
    )
    if last_boundary != PAL_DOS_EVENT_RECORD_COUNT:
        raise ValueError(
            "PAL_DOS scene sentinel does not match its event-record count"
        )
    sentinel = struct.pack("<HHHH", 0, 0, 0, last_boundary)
    return scene_data + sentinel * (
        SCENE_RECORD_COUNT - PAL_DOS_SCENE_RECORD_COUNT
    )


def crc32_with_zeroed_word(data: bytes, offset: int) -> int:
    image = bytearray(data)
    struct.pack_into("<I", image, offset, 0)
    return zlib.crc32(image) & 0xFFFFFFFF


def parse_full_pack(pack: bytes) -> tuple[int, PackChunk, PackChunk]:
    if len(pack) < PACK_HEADER_BYTES:
        raise ValueError("pal_full.pak has a short header")

    (
        magic,
        version,
        header_bytes,
        archive_count,
        reserved,
        archive_table_offset,
        data_offset,
        pack_set_id,
        declared_pack_bytes,
        declared_pack_crc32,
    ) = struct.unpack_from("<IHHHHIIIII", pack, 0)
    if magic != PACK_MAGIC:
        raise ValueError("pal_full.pak has bad magic")
    if version != PACK_VERSION or header_bytes != PACK_HEADER_BYTES:
        raise ValueError("pal_full.pak has an unsupported header")
    if reserved != 0:
        raise ValueError("pal_full.pak header reserved field is nonzero")
    if pack_set_id == 0:
        raise ValueError("pal_full.pak has a zero pack-set ID")
    if declared_pack_bytes != len(pack):
        raise ValueError(
            f"pal_full.pak size field is {declared_pack_bytes}, "
            f"actual {len(pack)}"
        )
    actual_pack_crc32 = crc32_with_zeroed_word(pack, PACK_CRC32_OFFSET)
    if (
        declared_pack_crc32 == 0
        or declared_pack_crc32 != actual_pack_crc32
    ):
        raise ValueError(
            f"pal_full.pak CRC32 is {declared_pack_crc32:#010x}, "
            f"expected {actual_pack_crc32:#010x}"
        )
    if not checked_range(
        archive_table_offset,
        archive_count * PACK_ARCHIVE_ENTRY_BYTES,
        len(pack),
    ):
        raise ValueError("pal_full.pak archive table is out of range")
    if (
        data_offset < PACK_HEADER_BYTES
        or data_offset > len(pack)
        or data_offset % 4 != 0
    ):
        raise ValueError("pal_full.pak data offset is invalid")
    if (
        archive_table_offset < PACK_HEADER_BYTES
        or archive_table_offset
        + archive_count * PACK_ARCHIVE_ENTRY_BYTES
        > data_offset
    ):
        raise ValueError("pal_full.pak archive table overlaps payload data")

    seen_archives: set[int] = set()
    sss_entry: int | None = None
    for archive_index in range(archive_count):
        entry = (
            archive_table_offset
            + archive_index * PACK_ARCHIVE_ENTRY_BYTES
        )
        archive_id, chunk_count = struct.unpack_from("<HH", pack, entry)
        chunk_table_offset = u32(pack, entry + 4)
        archive_reserved = u32(pack, entry + 8)

        if archive_id in seen_archives:
            raise ValueError(
                f"pal_full.pak has duplicate archive ID {archive_id}"
            )
        seen_archives.add(archive_id)
        if archive_reserved != 0:
            raise ValueError(
                f"pal_full.pak archive {archive_id} reserved field is nonzero"
            )
        if not checked_range(
            chunk_table_offset,
            chunk_count * PACK_CHUNK_ENTRY_BYTES,
            data_offset,
        ):
            raise ValueError(
                f"pal_full.pak archive {archive_id} chunk table "
                "is out of the TOC"
            )
        if archive_id == PACK_ARCHIVE_SSS:
            if chunk_count < 2:
                raise ValueError("pal_full.pak SSS archive lacks chunk 0/1")
            sss_entry = entry

    if sss_entry is None:
        raise ValueError("pal_full.pak has no SSS archive")

    sss_chunk_count = u16(pack, sss_entry + 2)
    sss_chunk_table = u32(pack, sss_entry + 4)

    def chunk(chunk_id: int) -> PackChunk:
        if chunk_id >= sss_chunk_count:
            raise ValueError(f"pal_full.pak has no SSS chunk {chunk_id}")
        entry = sss_chunk_table + chunk_id * PACK_CHUNK_ENTRY_BYTES
        payload_offset, payload_size, fmt, flags, chunk_reserved = (
            struct.unpack_from("<IIHHI", pack, entry)
        )
        if fmt != PACK_FORMAT_NATIVE:
            raise ValueError(
                f"pal_full.pak SSS chunk {chunk_id} is not NATIVE"
            )
        if flags != 0:
            raise ValueError(
                f"pal_full.pak SSS chunk {chunk_id} has flags "
                f"{flags:#06x}"
            )
        if chunk_reserved != 0:
            raise ValueError(
                f"pal_full.pak SSS chunk {chunk_id} reserved field "
                "is nonzero"
            )
        if (
            payload_offset < data_offset
            or payload_offset % 4 != 0
            or not checked_range(payload_offset, payload_size, len(pack))
        ):
            raise ValueError(
                f"pal_full.pak SSS chunk {chunk_id} payload is out of range"
            )
        return PackChunk(
            pack[payload_offset : payload_offset + payload_size],
            fmt,
            flags,
        )

    source_event_chunk = chunk(0)
    source_scene_chunk = chunk(1)
    event_chunk = PackChunk(
        normalize_event_data(source_event_chunk.payload),
        source_event_chunk.fmt,
        source_event_chunk.flags,
    )
    scene_chunk = PackChunk(
        normalize_scene_data(source_scene_chunk.payload),
        source_scene_chunk.fmt,
        source_scene_chunk.flags,
    )
    validate_scene_page_spans(scene_chunk.payload)
    return pack_set_id, event_chunk, scene_chunk


def build_event_template(pack: bytes) -> bytes:
    pack_set_id, event_chunk, scene_chunk = parse_full_pack(pack)
    event_data = event_chunk.payload
    scene_data = scene_chunk.payload

    payload = bytearray(PAYLOAD_BYTES)
    payload[:EVENT_BYTES] = event_data
    scene_offset = EVENT_PAGE_COUNT * PAGE_BYTES
    payload[scene_offset : scene_offset + SCENE_BYTES] = scene_data

    payload_crc32 = zlib.crc32(payload) & 0xFFFFFFFF
    event_crc32 = zlib.crc32(event_data) & 0xFFFFFFFF
    scene_crc32 = zlib.crc32(scene_data) & 0xFFFFFFFF
    event_sha256 = hashlib.sha256(event_data).digest()
    scene_sha256 = hashlib.sha256(scene_data).digest()

    header = bytearray(TEMPLATE_HEADER_BYTES)
    header[: len(TEMPLATE_MAGIC)] = TEMPLATE_MAGIC
    struct.pack_into(
        "<HHIHHHHIHHIIIIIII",
        header,
        HEADER_VERSION_OFFSET,
        TEMPLATE_VERSION,
        TEMPLATE_HEADER_BYTES,
        pack_set_id,
        EVENT_RECORD_BYTES,
        EVENT_RECORD_COUNT,
        SCENE_RECORD_BYTES,
        SCENE_RECORD_COUNT,
        PAGE_BYTES,
        EVENT_PAGE_COUNT,
        PAGE_COUNT,
        EVENT_BYTES,
        SCENE_BYTES,
        TEMPLATE_PAYLOAD_OFFSET,
        PAYLOAD_BYTES,
        payload_crc32,
        event_crc32,
        scene_crc32,
    )
    header[
        HEADER_EVENT_SHA256_OFFSET :
        HEADER_EVENT_SHA256_OFFSET + 32
    ] = event_sha256
    header[
        HEADER_SCENE_SHA256_OFFSET :
        HEADER_SCENE_SHA256_OFFSET + 32
    ] = scene_sha256
    header_crc32 = crc32_with_zeroed_word(
        header, TEMPLATE_HEADER_CRC32_OFFSET
    )
    struct.pack_into(
        "<I", header, TEMPLATE_HEADER_CRC32_OFFSET, header_crc32
    )
    image = bytes(header + payload)
    validate_event_template(image, expected_pack_set_id=pack_set_id)
    return image


def validate_event_template(
    image: bytes,
    expected_pack_set_id: int | None = None,
) -> EventTemplateSummary:
    if len(image) != TEMPLATE_BYTES:
        raise ValueError(
            f"EVENT.DEF is {len(image)} bytes, expected {TEMPLATE_BYTES}"
        )
    if image[:8] != TEMPLATE_MAGIC:
        raise ValueError("EVENT.DEF has bad magic")

    version = u16(image, HEADER_VERSION_OFFSET)
    header_bytes = u16(image, HEADER_BYTES_OFFSET)
    pack_set_id = u32(image, HEADER_PACK_SET_ID_OFFSET)
    declared_header_crc32 = u32(
        image, TEMPLATE_HEADER_CRC32_OFFSET
    )
    actual_header_crc32 = crc32_with_zeroed_word(
        image[:TEMPLATE_HEADER_BYTES],
        TEMPLATE_HEADER_CRC32_OFFSET,
    )
    if version != TEMPLATE_VERSION:
        raise ValueError(f"EVENT.DEF has unsupported version {version}")
    if header_bytes != TEMPLATE_HEADER_BYTES:
        raise ValueError("EVENT.DEF has a bad header size")
    if pack_set_id == 0:
        raise ValueError("EVENT.DEF has a zero pack-set ID")
    if (
        expected_pack_set_id is not None
        and pack_set_id != expected_pack_set_id
    ):
        raise ValueError(
            f"EVENT.DEF pack-set ID is {pack_set_id:#010x}, "
            f"expected {expected_pack_set_id:#010x}"
        )
    if (
        declared_header_crc32 == 0
        or declared_header_crc32 != actual_header_crc32
    ):
        raise ValueError(
            f"EVENT.DEF header CRC32 is {declared_header_crc32:#010x}, "
            f"expected {actual_header_crc32:#010x}"
        )

    expected_fields = {
        "event record bytes": (
            u16(image, HEADER_EVENT_RECORD_BYTES_OFFSET),
            EVENT_RECORD_BYTES,
        ),
        "event record count": (
            u16(image, HEADER_EVENT_RECORD_COUNT_OFFSET),
            EVENT_RECORD_COUNT,
        ),
        "scene record bytes": (
            u16(image, HEADER_SCENE_RECORD_BYTES_OFFSET),
            SCENE_RECORD_BYTES,
        ),
        "scene record count": (
            u16(image, HEADER_SCENE_RECORD_COUNT_OFFSET),
            SCENE_RECORD_COUNT,
        ),
        "page bytes": (
            u32(image, HEADER_PAGE_BYTES_OFFSET),
            PAGE_BYTES,
        ),
        "event page count": (
            u16(image, HEADER_EVENT_PAGE_COUNT_OFFSET),
            EVENT_PAGE_COUNT,
        ),
        "page count": (
            u16(image, HEADER_PAGE_COUNT_OFFSET),
            PAGE_COUNT,
        ),
        "event bytes": (
            u32(image, HEADER_EVENT_BYTES_OFFSET),
            EVENT_BYTES,
        ),
        "scene bytes": (
            u32(image, HEADER_SCENE_BYTES_OFFSET),
            SCENE_BYTES,
        ),
        "payload offset": (
            u32(image, HEADER_PAYLOAD_OFFSET_OFFSET),
            TEMPLATE_PAYLOAD_OFFSET,
        ),
        "payload bytes": (
            u32(image, HEADER_PAYLOAD_BYTES_OFFSET),
            PAYLOAD_BYTES,
        ),
    }
    for label, (actual, expected) in expected_fields.items():
        if actual != expected:
            raise ValueError(
                f"EVENT.DEF {label} is {actual}, expected {expected}"
            )
    if any(
        image[HEADER_RESERVED_OFFSET:TEMPLATE_HEADER_CRC32_OFFSET]
    ):
        raise ValueError("EVENT.DEF reserved header bytes are nonzero")

    payload = image[TEMPLATE_PAYLOAD_OFFSET:]
    event_data = payload[:EVENT_BYTES]
    event_padding = payload[
        EVENT_BYTES : EVENT_PAGE_COUNT * PAGE_BYTES
    ]
    scene_offset = EVENT_PAGE_COUNT * PAGE_BYTES
    scene_data = payload[scene_offset : scene_offset + SCENE_BYTES]
    scene_padding = payload[scene_offset + SCENE_BYTES :]
    if any(event_padding):
        raise ValueError("EVENT.DEF event tail padding is nonzero")
    if any(scene_padding):
        raise ValueError("EVENT.DEF scene tail padding is nonzero")
    validate_scene_page_spans(scene_data)

    declared_payload_crc32 = u32(
        image, HEADER_PAYLOAD_CRC32_OFFSET
    )
    declared_event_crc32 = u32(image, HEADER_EVENT_CRC32_OFFSET)
    declared_scene_crc32 = u32(image, HEADER_SCENE_CRC32_OFFSET)
    actual_payload_crc32 = zlib.crc32(payload) & 0xFFFFFFFF
    actual_event_crc32 = zlib.crc32(event_data) & 0xFFFFFFFF
    actual_scene_crc32 = zlib.crc32(scene_data) & 0xFFFFFFFF
    for label, declared, actual in (
        ("payload", declared_payload_crc32, actual_payload_crc32),
        ("event", declared_event_crc32, actual_event_crc32),
        ("scene", declared_scene_crc32, actual_scene_crc32),
    ):
        if declared == 0 or declared != actual:
            raise ValueError(
                f"EVENT.DEF {label} CRC32 is {declared:#010x}, "
                f"expected {actual:#010x}"
            )

    event_sha256 = hashlib.sha256(event_data).digest()
    scene_sha256 = hashlib.sha256(scene_data).digest()
    if (
        image[
            HEADER_EVENT_SHA256_OFFSET :
            HEADER_EVENT_SHA256_OFFSET + 32
        ]
        != event_sha256
    ):
        raise ValueError("EVENT.DEF event SHA-256 differs")
    if (
        image[
            HEADER_SCENE_SHA256_OFFSET :
            HEADER_SCENE_SHA256_OFFSET + 32
        ]
        != scene_sha256
    ):
        raise ValueError("EVENT.DEF scene SHA-256 differs")

    return EventTemplateSummary(
        pack_set_id=pack_set_id,
        payload_crc32=actual_payload_crc32,
        event_crc32=actual_event_crc32,
        scene_crc32=actual_scene_crc32,
        header_crc32=actual_header_crc32,
        event_sha256=event_sha256.hex(),
        scene_sha256=scene_sha256.hex(),
        total_bytes=len(image),
    )


def write_event_template(full_pack_path: Path, out_path: Path) -> EventTemplateSummary:
    pack = full_pack_path.read_bytes()
    pack_set_id, _, _ = parse_full_pack(pack)
    image = build_event_template(pack)
    summary = validate_event_template(
        image, expected_pack_set_id=pack_set_id
    )

    out_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = out_path.with_name(out_path.name + ".tmp")
    temporary_path.write_bytes(image)
    written = temporary_path.read_bytes()
    written_summary = validate_event_template(
        written, expected_pack_set_id=pack_set_id
    )
    if written_summary != summary:
        raise ValueError("EVENT.DEF write-back self-check differs")
    temporary_path.replace(out_path)
    final_summary = validate_event_template(
        out_path.read_bytes(), expected_pack_set_id=pack_set_id
    )
    if final_summary != summary:
        raise ValueError("EVENT.DEF final self-check differs")
    return final_summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--full-pack",
        type=Path,
        required=True,
        help="generated complete native pal_full.pak",
    )
    parser.add_argument(
        "--out",
        type=Path,
        required=True,
        help="output EVENT.DEF path",
    )
    args = parser.parse_args()

    try:
        summary = write_event_template(args.full_pack, args.out)
    except (OSError, ValueError) as exc:
        print(f"pal_event_template_build.py: {exc}", file=sys.stderr)
        return 2

    print(
        f"{args.out}: {summary.total_bytes} bytes "
        f"pack_set_id=0x{summary.pack_set_id:08x} "
        f"events={EVENT_RECORD_COUNT}/{EVENT_BYTES} "
        f"scenes={SCENE_RECORD_COUNT}/{SCENE_BYTES} "
        f"payload_crc32=0x{summary.payload_crc32:08x} "
        f"event_crc32=0x{summary.event_crc32:08x} "
        f"scene_crc32=0x{summary.scene_crc32:08x} "
        f"header_crc32=0x{summary.header_crc32:08x}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
