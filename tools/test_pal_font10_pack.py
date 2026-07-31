#!/usr/bin/env python3
"""Pack-builder and no-heap C-view tests for optional FONT10 chunk 1."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock
import zlib


TOOLS_DIR = Path(__file__).resolve().parent
ROOT = TOOLS_DIR.parent
BUILDER_PATH = TOOLS_DIR / "pal_pack_build.py"
CONTRACT_PATH = TOOLS_DIR / "embedded_contract_check.py"
PAL_DATA_DIR = Path("/mnt/hgfs/deb13/PAL")

sys.path.insert(0, str(TOOLS_DIR))
from pal_ui_layout import font as font_tool  # noqa: E402

spec = importlib.util.spec_from_file_location(
    "pal_font10_pack_builder_under_test",
    BUILDER_PATH,
)
assert spec is not None and spec.loader is not None
builder = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = builder
spec.loader.exec_module(builder)

contract_spec = importlib.util.spec_from_file_location(
    "pal_font10_contract_under_test",
    CONTRACT_PATH,
)
assert contract_spec is not None and contract_spec.loader is not None
contract = importlib.util.module_from_spec(contract_spec)
sys.modules[contract_spec.name] = contract
contract_spec.loader.exec_module(contract)


def build_mkf(chunks: list[bytes]) -> bytes:
    table_bytes = (len(chunks) + 1) * 4
    offsets = [table_bytes]
    cursor = table_bytes
    for chunk in chunks:
        cursor += len(chunk)
        offsets.append(cursor)
    return (
        struct.pack(f"<{len(offsets)}I", *offsets)
        + b"".join(chunks)
    )


def write_minimal_pal_data(root: Path) -> None:
    word_data = "仙".encode("cp950").ljust(10, b" ")
    word_data += b"A".ljust(10, b"\0")
    message_data = "仙 A".encode("cp950")
    first_message_bytes = len("仙".encode("cp950"))
    offsets = struct.pack(
        "<III", 0, first_message_bytes, len(message_data)
    )
    (root / "WORD.DAT").write_bytes(word_data)
    (root / "M.MSG").write_bytes(message_data)
    (root / "SSS.MKF").write_bytes(
        build_mkf([b"", b"", b"", offsets])
    )

    # The legacy FONT chunk only needs one valid CP950 codepoint for these
    # integration tests.  Its bytes are compared before/after FONT10 opt-in.
    (root / "WOR16.ASC").write_bytes("仙".encode("cp950"))
    (root / "WOR16.FON").write_bytes(
        b"\0" * builder.FONT_GLYPH_SOURCE_OFFSET
        + bytes(range(builder.FONT_GLYPH_SOURCE_BYTES))
    )


def synthetic_font(codepoints: set[int]) -> font_tool.BdfFont:
    bbox = font_tool.BdfBoundingBox(10, 10, 0, -1)
    glyphs = {
        codepoint: font_tool.BdfGlyph(
            name=f"U+{codepoint:04X}",
            encoding=codepoint,
            dwidth_x=10,
            dwidth_y=0,
            bbox=bbox,
            rows=(0,) * 10,
        )
        for codepoint in codepoints
    }
    return font_tool.BdfFont(
        version="2.1",
        name="synthetic-font10-pack-test",
        size=(10, 75, 75),
        bbox=bbox,
        ascent=9,
        descent=1,
        properties={"FONT_ASCENT": "9", "FONT_DESCENT": "1"},
        glyphs=glyphs,
    )


def sample_font10() -> bytes:
    records = b"".join(
        (
            font_tool.FONT10_RECORD.pack(
                0x0020, 5, b"\0" * font_tool.FONT10_BITMAP_BYTES
            ),
            font_tool.FONT10_RECORD.pack(
                0x0041,
                5,
                b"\x80" + b"\0" * (font_tool.FONT10_BITMAP_BYTES - 1),
            ),
            font_tool.FONT10_RECORD.pack(
                0x4ED9,
                10,
                b"\xff" * (font_tool.FONT10_BITMAP_BYTES - 1) + b"\xf0",
            ),
        )
    )
    return font_tool.FONT10_HEADER.pack(
        font_tool.FONT10_MAGIC,
        font_tool.FONT10_VERSION,
        font_tool.FONT10_HEADER_BYTES,
        3,
        font_tool.FONT10_RECORD_BYTES,
        font_tool.FONT10_CELL_WIDTH,
        font_tool.FONT10_CELL_HEIGHT,
        9,
        1,
        0,
        zlib.crc32(records) & 0xFFFFFFFF,
        font_tool.FONT10_HEADER_BYTES + len(records),
    ) + records


def oversized_font10() -> bytes:
    record_count = (
        contract.PACK_FONT10_MAX_BYTES
        - font_tool.FONT10_HEADER_BYTES
    ) // font_tool.FONT10_RECORD_BYTES + 1
    records = b"".join(
        font_tool.FONT10_RECORD.pack(
            codepoint,
            5,
            b"\0" * font_tool.FONT10_BITMAP_BYTES,
        )
        for codepoint in range(1, record_count + 1)
    )
    return font_tool.FONT10_HEADER.pack(
        font_tool.FONT10_MAGIC,
        font_tool.FONT10_VERSION,
        font_tool.FONT10_HEADER_BYTES,
        record_count,
        font_tool.FONT10_RECORD_BYTES,
        font_tool.FONT10_CELL_WIDTH,
        font_tool.FONT10_CELL_HEIGHT,
        9,
        1,
        0,
        zlib.crc32(records) & 0xFFFFFFFF,
        font_tool.FONT10_HEADER_BYTES + len(records),
    ) + records


def font_chunk_offsets(pack: bytearray) -> tuple[int, int, int]:
    archive_offset = builder.u32(pack, 12)
    archive_id, chunk_count = struct.unpack_from(
        "<HH", pack, archive_offset
    )
    if archive_id != builder.ARCHIVE_IDS["FONT"] or chunk_count != 2:
        raise AssertionError("test pack does not contain two FONT chunks")
    table_offset = builder.u32(pack, archive_offset + 4)
    chunk_entry = table_offset + builder.CHUNK_ENTRY_SIZE
    payload_offset = builder.u32(pack, chunk_entry)
    payload_size = builder.u32(pack, chunk_entry + 4)
    return chunk_entry, payload_offset, payload_size


def repair_checksums(pack: bytearray) -> None:
    _entry, payload_offset, payload_size = font_chunk_offsets(pack)
    header_bytes = struct.unpack_from("<H", pack, payload_offset + 10)[0]
    payload = pack[
        payload_offset + header_bytes : payload_offset + payload_size
    ]
    struct.pack_into(
        "<I",
        pack,
        payload_offset + 24,
        zlib.crc32(payload) & 0xFFFFFFFF,
    )
    struct.pack_into("<I", pack, builder.PACK_CRC32_OFFSET, 0)
    struct.pack_into(
        "<I",
        pack,
        builder.PACK_CRC32_OFFSET,
        zlib.crc32(pack) & 0xFFFFFFFF,
    )


class Font10PackBuilderTests(unittest.TestCase):
    def test_opt_in_appends_chunk_one_without_changing_legacy_chunk(self) -> None:
        image = sample_font10()
        with tempfile.TemporaryDirectory(prefix="pal-font10-pack-") as tmp:
            data_dir = Path(tmp)
            write_minimal_pal_data(data_dir)
            legacy = builder.load_archive(data_dir, "FONT")
            extended = builder.load_archive(
                data_dir,
                "FONT",
                builder.Chunk(image, builder.FORMAT_FONT10),
            )

        self.assertEqual(len(legacy), 1)
        self.assertEqual(len(extended), 2)
        self.assertEqual(extended[0], legacy[0])
        self.assertEqual(extended[1].payload, image)
        self.assertEqual(extended[1].fmt, builder.FORMAT_FONT10)
        self.assertEqual(builder.FORMAT_NAMES[6], "FONT10")

    def test_official_build_path_records_bounded_reproducible_metadata(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(prefix="pal-font10-build-") as tmp:
            root = Path(tmp)
            write_minimal_pal_data(root)
            release = root / "release.zip"
            release.write_bytes(b"verified-by-test-double")
            codepoints = set(
                font_tool.collect_pal_corpus_characters(
                    root,
                    extra_texts=builder.FONT10_UI_LABELS.values(),
                )
            )
            font = synthetic_font(codepoints)
            with mock.patch.object(
                font_tool,
                "extract_locked_bdf",
                return_value=font,
            ) as verifier:
                chunk_a, summary_a = builder.build_font10_archive_chunk(
                    root, release
                )
                chunk_b, summary_b = builder.build_font10_archive_chunk(
                    root, release
                )

        verifier.assert_called()
        self.assertEqual(chunk_a, chunk_b)
        self.assertEqual(summary_a, summary_b)
        parsed = font_tool.parse_font10(chunk_a.payload)
        self.assertEqual(chunk_a.fmt, builder.FORMAT_FONT10)
        self.assertEqual(len(parsed.glyphs), len(codepoints))
        self.assertEqual(
            summary_a["archive"]["sha256"],
            hashlib.sha256(b"verified-by-test-double").hexdigest(),
        )
        self.assertEqual(summary_a["bdf"]["sha256"], font_tool.BDF_MEMBER_SHA256)
        self.assertEqual(
            summary_a["font10"]["sha256"],
            hashlib.sha256(chunk_a.payload).hexdigest(),
        )
        self.assertEqual(summary_a["font10"]["bytes"], len(chunk_a.payload))
        self.assertEqual(
            summary_a["font10"]["glyph_count"], len(codepoints)
        )
        self.assertEqual(
            summary_a["font10"]["metrics"]["line_height"], 10
        )
        self.assertEqual(
            summary_a["pack_chunk"],
            {
                "archive": "FONT",
                "chunk_id": 1,
                "format": "FONT10",
                "format_id": 6,
            },
        )

    def test_cli_flag_adds_chunk_and_manifest_while_no_flag_is_legacy(
        self,
    ) -> None:
        font10 = sample_font10()
        font10_summary = {
            "schema": "sdlpal-embedded-font10",
            "version": 1,
            "font10": {
                "bytes": len(font10),
                "sha256": hashlib.sha256(font10).hexdigest(),
            },
        }
        with tempfile.TemporaryDirectory(prefix="pal-font10-cli-") as tmp:
            root = Path(tmp)
            data_dir = root / "data"
            data_dir.mkdir()
            write_minimal_pal_data(data_dir)
            layout = root / "layout.json"
            layout.write_text(
                json.dumps(
                    {
                        "schema": "sdlpal-embedded-pack-layout",
                        "version": 1,
                        "packs": {"nor": ["FONT"], "tf": []},
                    }
                )
            )
            release = root / "release.zip"
            release.write_bytes(b"test-double")
            legacy_nor = root / "legacy-nor.pak"
            legacy_tf = root / "legacy-tf.pak"
            with mock.patch.object(
                sys,
                "argv",
                [
                    str(BUILDER_PATH),
                    str(data_dir),
                    "--layout",
                    str(layout),
                    "--out-nor",
                    str(legacy_nor),
                    "--out-tf",
                    str(legacy_tf),
                ],
            ):
                self.assertEqual(builder.main(), 0)

            expected_nor_archives = {
                "FONT": [
                    builder.Chunk(
                        builder.encode_font_pack(data_dir),
                        builder.FORMAT_FONT_GLYPHS,
                    )
                ]
            }
            expected_tf_archives: dict[str, list[builder.Chunk]] = {}
            expected_set_id = builder.compute_pack_set_id(
                expected_nor_archives,
                expected_tf_archives,
            )
            self.assertEqual(
                legacy_nor.read_bytes(),
                builder.build_pack(expected_nor_archives, expected_set_id),
            )
            self.assertEqual(
                legacy_tf.read_bytes(),
                builder.build_pack(expected_tf_archives, expected_set_id),
            )

            extended_nor = root / "extended-nor.pak"
            extended_tf = root / "extended-tf.pak"
            manifest_path = root / "manifest.json"
            with (
                mock.patch.object(
                    builder,
                    "build_font10_archive_chunk",
                    return_value=(
                        builder.Chunk(font10, builder.FORMAT_FONT10),
                        font10_summary,
                    ),
                ) as build_font,
                mock.patch.object(
                    sys,
                    "argv",
                    [
                        str(BUILDER_PATH),
                        str(data_dir),
                        "--layout",
                        str(layout),
                        "--font10-archive",
                        str(release),
                        "--out-nor",
                        str(extended_nor),
                        "--out-tf",
                        str(extended_tf),
                        "--manifest",
                        str(manifest_path),
                    ],
                ),
            ):
                self.assertEqual(builder.main(), 0)

            pack = extended_nor.read_bytes()
            builder.verify_pack(pack)
            archive_offset = builder.u32(pack, 12)
            self.assertEqual(
                struct.unpack_from("<HH", pack, archive_offset),
                (builder.ARCHIVE_IDS["FONT"], 2),
            )
            table_offset = builder.u32(pack, archive_offset + 4)
            self.assertEqual(
                builder.u16(
                    pack,
                    table_offset + builder.CHUNK_ENTRY_SIZE + 8,
                ),
                builder.FORMAT_FONT10,
            )
            manifest = json.loads(manifest_path.read_text())

        build_font.assert_called_once_with(data_dir, release)
        self.assertEqual(manifest["font10"], font10_summary)
        font_archive = manifest["packs"]["nor"]["archive_summaries"][0]
        self.assertEqual(font_archive["chunk_count"], 2)
        self.assertEqual(
            font_archive["format_counts"],
            {"FONT10": 1, "FONT_GLYPHS": 1},
        )

    @unittest.skipUnless(
        PAL_DATA_DIR.is_dir(),
        f"real PAL data is unavailable at {PAL_DATA_DIR}",
    )
    def test_real_pal_corpus_has_no_invented_ui_labels(
        self,
    ) -> None:
        pal = set(font_tool.collect_pal_corpus_characters(PAL_DATA_DIR))
        with_ui = set(
            font_tool.collect_pal_corpus_characters(
                PAL_DATA_DIR,
                extra_texts=builder.FONT10_UI_LABELS.values(),
            )
        )
        self.assertEqual(len(pal), 2631)
        self.assertEqual(builder.FONT10_UI_LABELS, {})
        self.assertEqual(with_ui, pal)
        self.assertEqual(
            font_tool.FONT10_HEADER_BYTES
            + len(with_ui) * font_tool.FONT10_RECORD_BYTES,
            42128,
        )

    @unittest.skipUnless(
        PAL_DATA_DIR.is_dir()
        and Path(
            "/tmp/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip"
        ).is_file(),
        "real PAL data or pinned Fusion Pixel archive unavailable",
    )
    def test_pack_and_native_layout_use_identical_font10_identity(self) -> None:
        import pal_native_ui_layout

        release = Path(
            "/tmp/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip"
        )
        pack_chunk, pack_summary = builder.build_font10_archive_chunk(
            PAL_DATA_DIR,
            release,
        )
        profile = pal_native_ui_layout.build_profile(
            240,
            135,
            pack_summary["font10"],
        )
        self.assertEqual(len(pack_chunk.payload), 42128)
        self.assertEqual(profile.font["image_bytes"], len(pack_chunk.payload))
        self.assertEqual(
            profile.font["payload_crc32"],
            pack_summary["font10"]["payload_crc32"],
        )
        self.assertEqual(
            pack_summary["font10"]["sha256"],
            hashlib.sha256(pack_chunk.payload).hexdigest(),
        )


class Font10RuntimeViewTests(unittest.TestCase):
    def test_contract_checks_format6_placement_crc_and_budget(self) -> None:
        font10 = sample_font10()
        valid_pack = builder.build_pack(
            {
                "FONT": [
                    builder.Chunk(
                        b"legacy", builder.FORMAT_FONT_GLYPHS
                    ),
                    builder.Chunk(font10, builder.FORMAT_FONT10),
                ]
            }
        )
        misplaced_pack = builder.build_pack(
            {
                "DATA": [
                    builder.Chunk(font10, builder.FORMAT_FONT10)
                ]
            }
        )

        oversized_pack = builder.build_pack(
            {
                "FONT": [
                    builder.Chunk(
                        b"legacy", builder.FORMAT_FONT_GLYPHS
                    ),
                    builder.Chunk(
                        oversized_font10(), builder.FORMAT_FONT10
                    ),
                ]
            }
        )

        with tempfile.TemporaryDirectory(
            prefix="pal-font10-contract-"
        ) as tmp:
            root = Path(tmp)
            valid_path = root / "valid.pak"
            valid_path.write_bytes(valid_pack)
            misplaced_path = root / "misplaced.pak"
            misplaced_path.write_bytes(misplaced_pack)
            oversized_path = root / "oversized.pak"
            oversized_path.write_bytes(oversized_pack)

            valid_errors, valid_report = contract.check_pack(
                valid_path, 1024 * 1024, set()
            )
            misplaced_errors, _ = contract.check_pack(
                misplaced_path, None, set()
            )
            oversized_errors, _ = contract.check_pack(
                oversized_path, None, set()
            )

        self.assertEqual(valid_errors, [])
        self.assertIn("FONT10=1", valid_report)
        self.assertIn("font10 bytes=80 glyphs=3", valid_report)
        self.assertTrue(
            any("invalid placement" in error for error in misplaced_errors)
        )
        self.assertTrue(
            any("exceeds 65536" in error for error in oversized_errors)
        )

    @unittest.skipUnless(shutil.which("cc"), "host C compiler unavailable")
    def test_readonly_host_smoke_and_strict_rejections(self) -> None:
        font10 = sample_font10()
        pack = builder.build_pack(
            {
                "FONT": [
                    builder.Chunk(b"legacy", builder.FORMAT_FONT_GLYPHS),
                    builder.Chunk(font10, builder.FORMAT_FONT10),
                ]
            }
        )
        with tempfile.TemporaryDirectory(prefix="pal-font10-c-") as tmp:
            root = Path(tmp)
            smoke = root / "pal_font10_cache_smoke"
            subprocess.run(
                [
                    "cc",
                    "-std=c99",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "embedded"),
                    str(ROOT / "embedded" / "pal_pack.c"),
                    str(ROOT / "embedded" / "pal_font10_cache.c"),
                    str(ROOT / "embedded" / "pal_font10_cache_smoke.c"),
                    "-o",
                    str(smoke),
                ],
                check=True,
                cwd=ROOT,
            )

            def run_image(name: str, image: bytes | bytearray) -> int:
                path = root / name
                path.write_bytes(image)
                return subprocess.run(
                    [str(smoke), str(path)],
                    check=False,
                    cwd=ROOT,
                ).returncode

            self.assertEqual(run_image("valid.pak", pack), 0)

            too_large = builder.build_pack(
                {
                    "FONT": [
                        builder.Chunk(
                            b"legacy", builder.FORMAT_FONT_GLYPHS
                        ),
                        builder.Chunk(
                            oversized_font10(),
                            builder.FORMAT_FONT10,
                        ),
                    ]
                }
            )
            self.assertNotEqual(
                run_image("too-large.pak", too_large), 0
            )

            bad_crc = bytearray(pack)
            _entry, offset, _size = font_chunk_offsets(bad_crc)
            bad_crc[offset + font_tool.FONT10_HEADER_BYTES + 3] ^= 0x80
            self.assertNotEqual(run_image("bad-crc.pak", bad_crc), 0)

            unsorted = bytearray(pack)
            _entry, offset, _size = font_chunk_offsets(unsorted)
            first = offset + font_tool.FONT10_HEADER_BYTES
            second = first + font_tool.FONT10_RECORD_BYTES
            unsorted[first : first + 2], unsorted[second : second + 2] = (
                unsorted[second : second + 2],
                unsorted[first : first + 2],
            )
            repair_checksums(unsorted)
            builder.verify_pack(bytes(unsorted))
            self.assertNotEqual(run_image("unsorted.pak", unsorted), 0)

            bad_padding = bytearray(pack)
            _entry, offset, _size = font_chunk_offsets(bad_padding)
            bad_padding[
                offset
                + font_tool.FONT10_HEADER_BYTES
                + font_tool.FONT10_RECORD_BYTES
                - 1
            ] |= 0x01
            repair_checksums(bad_padding)
            builder.verify_pack(bytes(bad_padding))
            self.assertNotEqual(
                run_image("bad-padding.pak", bad_padding), 0
            )

            bad_advance = bytearray(pack)
            _entry, offset, _size = font_chunk_offsets(bad_advance)
            bad_advance[
                offset + font_tool.FONT10_HEADER_BYTES + 2
            ] = 0
            repair_checksums(bad_advance)
            builder.verify_pack(bytes(bad_advance))
            self.assertNotEqual(
                run_image("bad-advance.pak", bad_advance), 0
            )

            wrong_format = bytearray(pack)
            entry, _offset, _size = font_chunk_offsets(wrong_format)
            struct.pack_into(
                "<H",
                wrong_format,
                entry + 8,
                builder.FORMAT_FONT_GLYPHS,
            )
            struct.pack_into(
                "<I", wrong_format, builder.PACK_CRC32_OFFSET, 0
            )
            struct.pack_into(
                "<I",
                wrong_format,
                builder.PACK_CRC32_OFFSET,
                zlib.crc32(wrong_format) & 0xFFFFFFFF,
            )
            builder.verify_pack(bytes(wrong_format))
            self.assertNotEqual(
                run_image("wrong-format.pak", wrong_format), 0
            )


if __name__ == "__main__":
    unittest.main()
