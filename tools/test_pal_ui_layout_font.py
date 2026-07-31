#!/usr/bin/env python3
"""Unit tests for the host-only Fusion Pixel FONT10 subset builder."""

from __future__ import annotations

import hashlib
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from pal_ui_layout import font as font_tool  # noqa: E402


def synthetic_bdf(*, include_cjk: bool = True) -> bytes:
    glyphs = [
        """STARTCHAR space
ENCODING 32
SWIDTH 500 0
DWIDTH 5 0
BBX 5 10 0 -1
BITMAP
00
00
00
00
00
00
00
00
00
00
ENDCHAR""",
        """STARTCHAR A
ENCODING 65
SWIDTH 500 0
DWIDTH 5 0
BBX 5 7 0 0
BITMAP
70
88
88
F8
88
88
88
ENDCHAR""",
    ]
    if include_cjk:
        glyphs.append(
            """STARTCHAR U+4ED9
ENCODING 20185
SWIDTH 1000 0
DWIDTH 10 0
BBX 10 10 0 -1
BITMAP
1080
1080
2100
2100
4200
8200
0440
0840
1040
2040
ENDCHAR"""
        )
    return (
        """STARTFONT 2.1
FONT -Test-Synthetic-Regular-R-Normal--10-100-75-75-M-100-ISO10646-1
SIZE 10 75 75
FONTBOUNDINGBOX 10 10 0 -1
STARTPROPERTIES 2
FONT_ASCENT 9
FONT_DESCENT 1
ENDPROPERTIES
CHARS %d
%s
ENDFONT
"""
        % (len(glyphs), "\n".join(glyphs))
    ).encode("ascii")


def build_mkf(chunks: list[bytes]) -> bytes:
    table_bytes = (len(chunks) + 1) * 4
    offsets = [table_bytes]
    cursor = table_bytes
    for chunk in chunks:
        cursor += len(chunk)
        offsets.append(cursor)
    return (
        struct.pack(f"<{len(offsets)}I", *offsets) + b"".join(chunks)
    )


class PalUiLayoutFontTests(unittest.TestCase):
    def setUp(self) -> None:
        self.font = font_tool.parse_bdf(synthetic_bdf())

    def test_parse_bdf_metrics_and_bitmap(self) -> None:
        self.assertEqual(self.font.version, "2.1")
        self.assertEqual(self.font.size, (10, 75, 75))
        self.assertEqual(
            self.font.bbox,
            font_tool.BdfBoundingBox(10, 10, 0, -1),
        )
        self.assertEqual((self.font.ascent, self.font.descent), (9, 1))
        self.assertEqual(tuple(sorted(self.font.glyphs)), (32, 65, 20185))

        glyph = self.font.glyphs[65]
        self.assertEqual(glyph.dwidth_x, 5)
        self.assertEqual(glyph.bbox, font_tool.BdfBoundingBox(5, 7, 0, 0))
        self.assertTrue(glyph.pixel(1, 0))
        self.assertFalse(glyph.pixel(0, 0))
        self.assertTrue(glyph.pixel(0, 3))
        self.assertTrue(glyph.pixel(4, 3))

    def test_collect_text_corpus_is_sorted_bmp_and_skips_controls(self) -> None:
        corpus = font_tool.collect_corpus_characters(
            ["仙A\n", " A\0"], extra_text="\t仙"
        )
        self.assertEqual(corpus, (32, 65, 20185))
        self.assertEqual(
            font_tool.collect_corpus_characters(["A😀"]),
            (65, 128512),
        )
        with self.assertRaises(TypeError):
            font_tool.collect_corpus_characters(["A", b"B"])  # type: ignore[list-item]

    def test_collect_real_pal_word_and_message_shape(self) -> None:
        word_data = "仙".encode("cp950").ljust(10, b" ")
        word_data += b"A".ljust(10, b"\0")
        messages = "仙 A".encode("cp950")
        message_offsets = struct.pack(
            "<III", 0, len("仙".encode("cp950")), len(messages)
        )
        sss = build_mkf([b"", b"", b"", message_offsets])

        with tempfile.TemporaryDirectory(prefix="pal-font-corpus-") as tmp:
            root = Path(tmp)
            (root / "WORD.DAT").write_bytes(word_data)
            (root / "M.MSG").write_bytes(messages)
            (root / "SSS.MKF").write_bytes(sss)
            corpus = font_tool.collect_pal_corpus_characters(root)

        self.assertEqual(corpus, (32, 65, 20185))

    def test_coverage_fails_closed_with_stable_diagnostics(self) -> None:
        incomplete = font_tool.parse_bdf(
            synthetic_bdf(include_cjk=False)
        )
        with self.assertRaises(font_tool.FontCoverageError) as caught:
            font_tool.require_coverage(incomplete, (20185, 66))
        self.assertEqual(caught.exception.missing, (66, 20185))
        self.assertIn("U+0042(B)", str(caught.exception))
        self.assertIn("U+4ED9(仙)", str(caught.exception))
        with self.assertRaisesRegex(ValueError, "only supports BMP"):
            font_tool.require_coverage(self.font, (0x1F600,))

    def test_font10_is_compact_deterministic_and_round_trips(self) -> None:
        image_a = font_tool.build_font10(
            self.font, (20185, 32, 65, 65)
        )
        image_b = font_tool.build_font10(
            self.font, {65, 20185, 32}
        )
        self.assertEqual(image_a, image_b)
        self.assertEqual(
            len(image_a),
            font_tool.FONT10_HEADER_BYTES
            + 3 * font_tool.FONT10_RECORD_BYTES,
        )
        self.assertEqual(
            hashlib.sha256(image_a).hexdigest(),
            "c43bd991ebc0bcab960f250b67ef2791353a45c8cae2f712e8b07da8625c9939",
        )

        decoded = font_tool.parse_font10(image_a)
        self.assertEqual((decoded.ascent, decoded.descent), (9, 1))
        self.assertEqual(
            tuple(glyph.codepoint for glyph in decoded.glyphs),
            (32, 65, 20185),
        )
        glyphs = decoded.by_codepoint()
        self.assertEqual(glyphs[32].advance, 5)
        self.assertEqual(glyphs[65].advance, 5)
        self.assertEqual(glyphs[20185].advance, 10)

        # The 7-row A has y=0 and therefore starts two rows below the
        # global top at y=8; BBX normalization preserves that baseline.
        self.assertFalse(glyphs[65].pixel(1, 0))
        self.assertFalse(glyphs[65].pixel(1, 1))
        self.assertTrue(glyphs[65].pixel(1, 2))
        self.assertTrue(glyphs[20185].pixel(3, 0))

    def test_font10_rejects_corruption_and_unsorted_records(self) -> None:
        image = bytearray(font_tool.build_font10(self.font, (32, 65)))
        image[-1] ^= 0x10
        with self.assertRaisesRegex(ValueError, "CRC32"):
            font_tool.parse_font10(bytes(image))

        image = bytearray(font_tool.build_font10(self.font, (32, 65)))
        first = font_tool.FONT10_HEADER_BYTES
        second = first + font_tool.FONT10_RECORD_BYTES
        image[first : first + 2], image[second : second + 2] = (
            image[second : second + 2],
            image[first : first + 2],
        )
        payload = image[font_tool.FONT10_HEADER_BYTES :]
        struct.pack_into(
            "<I", image, 24, zlib.crc32(payload) & 0xFFFFFFFF
        )
        with self.assertRaisesRegex(ValueError, "strictly sorted"):
            font_tool.parse_font10(bytes(image))

    def test_bdf_rejects_bad_padding_and_wrong_character_count(self) -> None:
        bad_padding = synthetic_bdf().replace(
            b"70\n88\n88\n", b"71\n88\n88\n", 1
        )
        with self.assertRaisesRegex(font_tool.BdfError, "padding"):
            font_tool.parse_bdf(bad_padding)

        bad_count = synthetic_bdf().replace(b"CHARS 3", b"CHARS 4")
        with self.assertRaisesRegex(font_tool.BdfError, "CHARS says"):
            font_tool.parse_bdf(bad_count)

    def test_lock_is_fixed_and_archive_verification_fails_before_zip(self) -> None:
        lock = font_tool.load_release_lock()
        self.assertEqual(lock["release"], "2026.07.20")
        self.assertEqual(
            lock["asset"]["sha256"],  # type: ignore[index]
            font_tool.RELEASE_SHA256,
        )
        with self.assertRaisesRegex(ValueError, "bytes, expected"):
            font_tool.verify_release_archive(b"not the release")


if __name__ == "__main__":
    unittest.main()
