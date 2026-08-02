#!/usr/bin/env python3
"""Tests for the fixed EVENT.DEF builder and validator."""

from __future__ import annotations

import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

import pal_event_template_build as event_template  # noqa: E402
import pal_pack_build as pack_builder  # noqa: E402


PACK_SET_ID = 0x7A31C5E9


def make_events() -> bytes:
    return bytes(
        (
            index
            ^ (index >> 8)
            ^ ((index // event_template.EVENT_RECORD_BYTES) * 13)
        )
        & 0xFF
        for index in range(event_template.EVENT_BYTES)
    )


def make_scenes() -> bytes:
    scenes = bytearray(event_template.SCENE_BYTES)
    for scene_index in range(event_template.SCENE_RECORD_COUNT):
        event_boundary = min(
            scene_index * 18, event_template.EVENT_RECORD_COUNT
        )
        struct.pack_into(
            "<HHHH",
            scenes,
            scene_index * event_template.SCENE_RECORD_BYTES,
            scene_index,
            scene_index ^ 0x1234,
            scene_index ^ 0x5678,
            event_boundary,
        )
    return bytes(scenes)


def make_pal_dos_scenes() -> bytes:
    scenes = bytearray(
        make_scenes()[: event_template.PAL_DOS_SCENE_BYTES]
    )
    for scene_index in range(event_template.PAL_DOS_SCENE_RECORD_COUNT):
        boundary = min(
            scene_index * 18,
            event_template.PAL_DOS_EVENT_RECORD_COUNT,
        )
        if scene_index == event_template.PAL_DOS_SCENE_RECORD_COUNT - 1:
            boundary = event_template.PAL_DOS_EVENT_RECORD_COUNT
        struct.pack_into(
            "<H",
            scenes,
            scene_index * event_template.SCENE_RECORD_BYTES + 6,
            boundary,
        )
    return bytes(scenes)


def make_pack(
    *,
    event_format: int = pack_builder.FORMAT_NATIVE,
    event_payload: bytes | None = None,
    scene_payload: bytes | None = None,
) -> bytes:
    events = make_events() if event_payload is None else event_payload
    scenes = make_scenes() if scene_payload is None else scene_payload
    archives = {
        "SSS": [
            pack_builder.Chunk(events, event_format),
            pack_builder.Chunk(scenes, pack_builder.FORMAT_NATIVE),
            pack_builder.Chunk(b"object", pack_builder.FORMAT_NATIVE),
        ]
    }
    pack = pack_builder.build_pack(archives, PACK_SET_ID)
    pack_builder.verify_pack(pack)
    return pack


def refresh_pack_crc(pack: bytearray) -> None:
    struct.pack_into(
        "<I", pack, event_template.PACK_CRC32_OFFSET, 0
    )
    struct.pack_into(
        "<I",
        pack,
        event_template.PACK_CRC32_OFFSET,
        zlib.crc32(pack) & 0xFFFFFFFF,
    )


def refresh_template_header_crc(image: bytearray) -> None:
    struct.pack_into(
        "<I",
        image,
        event_template.TEMPLATE_HEADER_CRC32_OFFSET,
        0,
    )
    struct.pack_into(
        "<I",
        image,
        event_template.TEMPLATE_HEADER_CRC32_OFFSET,
        zlib.crc32(image[: event_template.TEMPLATE_HEADER_BYTES])
        & 0xFFFFFFFF,
    )


class EventTemplateBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.events = make_events()
        cls.scenes = make_scenes()
        cls.pack = make_pack(
            event_payload=cls.events,
            scene_payload=cls.scenes,
        )
        cls.image = event_template.build_event_template(cls.pack)

    def test_fields_payload_hashes_and_zero_tails(self) -> None:
        summary = event_template.validate_event_template(
            self.image, expected_pack_set_id=PACK_SET_ID
        )
        self.assertEqual(len(self.image), event_template.TEMPLATE_BYTES)
        self.assertEqual(self.image[:8], event_template.TEMPLATE_MAGIC)
        self.assertEqual(summary.pack_set_id, PACK_SET_ID)
        self.assertEqual(
            summary.payload_crc32,
            zlib.crc32(
                self.image[event_template.TEMPLATE_PAYLOAD_OFFSET :]
            )
            & 0xFFFFFFFF,
        )
        self.assertEqual(summary.event_crc32, zlib.crc32(self.events) & 0xFFFFFFFF)
        self.assertEqual(summary.scene_crc32, zlib.crc32(self.scenes) & 0xFFFFFFFF)
        self.assertEqual(summary.event_sha256, hashlib.sha256(self.events).hexdigest())
        self.assertEqual(summary.scene_sha256, hashlib.sha256(self.scenes).hexdigest())

        payload = self.image[event_template.TEMPLATE_PAYLOAD_OFFSET :]
        self.assertEqual(payload[: event_template.EVENT_BYTES], self.events)
        event_page_bytes = (
            event_template.EVENT_PAGE_COUNT * event_template.PAGE_BYTES
        )
        self.assertEqual(
            payload[event_template.EVENT_BYTES : event_page_bytes],
            bytes(event_page_bytes - event_template.EVENT_BYTES),
        )
        self.assertEqual(
            payload[
                event_page_bytes :
                event_page_bytes + event_template.SCENE_BYTES
            ],
            self.scenes,
        )
        self.assertEqual(
            payload[event_page_bytes + event_template.SCENE_BYTES :],
            bytes(
                event_template.PAGE_BYTES - event_template.SCENE_BYTES
            ),
        )

        expected_fields = {
            event_template.HEADER_EVENT_RECORD_BYTES_OFFSET:
                event_template.EVENT_RECORD_BYTES,
            event_template.HEADER_EVENT_RECORD_COUNT_OFFSET:
                event_template.EVENT_RECORD_COUNT,
            event_template.HEADER_SCENE_RECORD_BYTES_OFFSET:
                event_template.SCENE_RECORD_BYTES,
            event_template.HEADER_SCENE_RECORD_COUNT_OFFSET:
                event_template.SCENE_RECORD_COUNT,
            event_template.HEADER_EVENT_PAGE_COUNT_OFFSET:
                event_template.EVENT_PAGE_COUNT,
            event_template.HEADER_PAGE_COUNT_OFFSET:
                event_template.PAGE_COUNT,
        }
        for offset, expected in expected_fields.items():
            self.assertEqual(event_template.u16(self.image, offset), expected)
        self.assertEqual(
            event_template.u32(
                self.image, event_template.HEADER_PAGE_BYTES_OFFSET
            ),
            event_template.PAGE_BYTES,
        )
        self.assertEqual(
            event_template.u32(
                self.image, event_template.HEADER_PAYLOAD_BYTES_OFFSET
            ),
            event_template.PAYLOAD_BYTES,
        )

    def test_cli_writes_and_self_checks(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="pal-event-template-"
        ) as temp_dir:
            temp = Path(temp_dir)
            full_pack = temp / "pal_full.pak"
            output = temp / "EVENT.DEF"
            full_pack.write_bytes(self.pack)
            result = subprocess.run(
                [
                    sys.executable,
                    "-B",
                    str(TOOLS_DIR / "pal_event_template_build.py"),
                    "--full-pack",
                    str(full_pack),
                    "--out",
                    str(output),
                ],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("pack_set_id=0x7a31c5e9", result.stdout)
            self.assertIn("events=5369/171808", result.stdout)
            self.assertIn("scenes=300/2400", result.stdout)
            self.assertEqual(output.read_bytes(), self.image)
            self.assertFalse((temp / "EVENT.DEF.tmp").exists())

    def test_rejects_tampered_or_non_native_full_pack(self) -> None:
        corrupt = bytearray(self.pack)
        corrupt[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC32"):
            event_template.build_event_template(bytes(corrupt))

        non_native = make_pack(event_format=pack_builder.FORMAT_RAW)
        with self.assertRaisesRegex(ValueError, "not NATIVE"):
            event_template.build_event_template(non_native)

        short_events = make_pack(event_payload=self.events[:-1])
        with self.assertRaisesRegex(ValueError, "expected 170624 or 171808"):
            event_template.build_event_template(short_events)

        missing_sss = pack_builder.build_pack(
            {
                "DATA": [
                    pack_builder.Chunk(
                        b"data", pack_builder.FORMAT_NATIVE
                    )
                ]
            },
            PACK_SET_ID,
        )
        with self.assertRaisesRegex(ValueError, "no SSS"):
            event_template.build_event_template(missing_sss)

    def test_stock_pal_dos_tables_are_padded_only_in_event_template(self) -> None:
        source_events = self.events[: event_template.PAL_DOS_EVENT_BYTES]
        source_scenes = make_pal_dos_scenes()
        pack = make_pack(
            event_payload=source_events,
            scene_payload=source_scenes,
        )
        image = event_template.build_event_template(pack)
        payload = image[event_template.TEMPLATE_PAYLOAD_OFFSET :]
        event_pages = (
            event_template.EVENT_PAGE_COUNT * event_template.PAGE_BYTES
        )
        self.assertEqual(
            payload[: event_template.PAL_DOS_EVENT_BYTES], source_events
        )
        self.assertEqual(
            payload[
                event_template.PAL_DOS_EVENT_BYTES :
                event_template.EVENT_BYTES
            ],
            bytes(
                event_template.EVENT_BYTES
                - event_template.PAL_DOS_EVENT_BYTES
            ),
        )
        normalized_scenes = payload[
            event_pages : event_pages + event_template.SCENE_BYTES
        ]
        self.assertEqual(
            normalized_scenes[: event_template.PAL_DOS_SCENE_BYTES],
            source_scenes,
        )
        for scene_index in range(
            event_template.PAL_DOS_SCENE_RECORD_COUNT,
            event_template.SCENE_RECORD_COUNT,
        ):
            self.assertEqual(
                struct.unpack_from(
                    "<4H",
                    normalized_scenes,
                    scene_index * event_template.SCENE_RECORD_BYTES,
                ),
                (0, 0, 0, event_template.PAL_DOS_EVENT_RECORD_COUNT),
            )

    def test_rejects_scene_table_outside_two_page_cache_contract(self) -> None:
        three_pages = bytearray(self.scenes)
        struct.pack_into(
            "<H",
            three_pages,
            6,
            127,
        )
        struct.pack_into(
            "<H",
            three_pages,
            event_template.SCENE_RECORD_BYTES + 6,
            287,
        )
        for scene_index in range(2, event_template.SCENE_RECORD_COUNT):
            offset = scene_index * event_template.SCENE_RECORD_BYTES + 6
            boundary = struct.unpack_from("<H", three_pages, offset)[0]
            if boundary < 287:
                struct.pack_into("<H", three_pages, offset, 287)
        with self.assertRaisesRegex(ValueError, "more than two event pages"):
            event_template.build_event_template(
                make_pack(scene_payload=bytes(three_pages))
            )

        decreasing = bytearray(self.scenes)
        struct.pack_into(
            "<H",
            decreasing,
            2 * event_template.SCENE_RECORD_BYTES + 6,
            1,
        )
        with self.assertRaisesRegex(ValueError, "decreases"):
            event_template.build_event_template(
                make_pack(scene_payload=bytes(decreasing))
            )

        too_many = bytearray(self.scenes)
        struct.pack_into(
            "<H",
            too_many,
            event_template.SCENE_RECORD_BYTES + 6,
            event_template.SCENE_EVENT_OBJECT_CAPACITY + 1,
        )
        for scene_index in range(2, event_template.SCENE_RECORD_COUNT):
            offset = scene_index * event_template.SCENE_RECORD_BYTES + 6
            boundary = struct.unpack_from("<H", too_many, offset)[0]
            if boundary <= event_template.SCENE_EVENT_OBJECT_CAPACITY:
                struct.pack_into(
                    "<H",
                    too_many,
                    offset,
                    event_template.SCENE_EVENT_OBJECT_CAPACITY + 1,
                )
        with self.assertRaisesRegex(ValueError, "beyond runtime capacity"):
            event_template.build_event_template(
                make_pack(scene_payload=bytes(too_many))
            )

    def test_validator_rejects_header_payload_and_hash_tampering(self) -> None:
        bad_header = bytearray(self.image)
        bad_header[event_template.HEADER_PAGE_COUNT_OFFSET] ^= 1
        with self.assertRaisesRegex(ValueError, "header CRC32"):
            event_template.validate_event_template(bytes(bad_header))

        bad_payload = bytearray(self.image)
        bad_payload[event_template.TEMPLATE_PAYLOAD_OFFSET + 123] ^= 1
        with self.assertRaisesRegex(ValueError, "payload CRC32"):
            event_template.validate_event_template(bytes(bad_payload))

        bad_hash = bytearray(self.image)
        bad_hash[event_template.HEADER_EVENT_SHA256_OFFSET] ^= 1
        refresh_template_header_crc(bad_hash)
        with self.assertRaisesRegex(ValueError, "event SHA-256"):
            event_template.validate_event_template(bytes(bad_hash))

    def test_validator_rejects_nonzero_tail_even_with_refreshed_crcs(self) -> None:
        event_tail = bytearray(self.image)
        event_tail_offset = (
            event_template.TEMPLATE_PAYLOAD_OFFSET
            + event_template.EVENT_BYTES
        )
        event_tail[event_tail_offset] = 1
        payload = event_tail[event_template.TEMPLATE_PAYLOAD_OFFSET :]
        struct.pack_into(
            "<I",
            event_tail,
            event_template.HEADER_PAYLOAD_CRC32_OFFSET,
            zlib.crc32(payload) & 0xFFFFFFFF,
        )
        refresh_template_header_crc(event_tail)
        with self.assertRaisesRegex(ValueError, "event tail padding"):
            event_template.validate_event_template(bytes(event_tail))

        scene_tail = bytearray(self.image)
        scene_tail_offset = (
            event_template.TEMPLATE_PAYLOAD_OFFSET
            + event_template.EVENT_PAGE_COUNT
            * event_template.PAGE_BYTES
            + event_template.SCENE_BYTES
        )
        scene_tail[scene_tail_offset] = 1
        payload = scene_tail[event_template.TEMPLATE_PAYLOAD_OFFSET :]
        struct.pack_into(
            "<I",
            scene_tail,
            event_template.HEADER_PAYLOAD_CRC32_OFFSET,
            zlib.crc32(payload) & 0xFFFFFFFF,
        )
        refresh_template_header_crc(scene_tail)
        with self.assertRaisesRegex(ValueError, "scene tail padding"):
            event_template.validate_event_template(bytes(scene_tail))

    def test_pack_chunk_bounds_and_format_are_verified_after_pack_crc(self) -> None:
        malformed = bytearray(self.pack)
        archive_offset = event_template.PACK_HEADER_BYTES
        chunk_table = event_template.u32(malformed, archive_offset + 4)
        struct.pack_into(
            "<I",
            malformed,
            chunk_table,
            len(malformed) - 4,
        )
        struct.pack_into(
            "<I",
            malformed,
            chunk_table + 4,
            event_template.EVENT_BYTES,
        )
        refresh_pack_crc(malformed)
        with self.assertRaisesRegex(ValueError, "out of range"):
            event_template.build_event_template(bytes(malformed))


if __name__ == "__main__":
    unittest.main()
