#!/usr/bin/env python3
"""Focused unit tests for the Cardputer extreme artifact gate."""

from __future__ import annotations

import json
import struct
import tempfile
import unittest
from pathlib import Path

import check_cardputer_extreme as check


class CardputerExtremeCheckTests(unittest.TestCase):
    def test_generated_partition_binary_is_decoded(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "partition-table.bin"
            data = bytearray(b"\xff" * 0xC00)
            entries = (
                (0x01, 0x02, 0x9000, 0x6000, b"nvs", 0),
                (0x00, 0x00, 0x10000, check.APP_BYTES, b"factory", 0),
                (
                    0x01,
                    0x40,
                    check.NOR_OFFSET,
                    check.NOR_BYTES,
                    b"pal_nor",
                    0,
                ),
            )
            for index, (part_type, subtype, offset, size, label, flags) in enumerate(
                entries
            ):
                struct.pack_into(
                    "<HBBII16sI",
                    data,
                    index * check.PARTITION_ENTRY_BYTES,
                    check.PARTITION_MAGIC,
                    part_type,
                    subtype,
                    offset,
                    size,
                    label,
                    flags,
                )
            struct.pack_into(
                "<H",
                data,
                len(entries) * check.PARTITION_ENTRY_BYTES,
                check.PARTITION_END_MAGIC,
            )
            path.write_bytes(data)
            errors: list[str] = []
            actual = check.parse_partition_binary(path, errors)
            self.assertEqual(errors, [])
            self.assertEqual(
                actual["factory"],
                (0x00, 0x00, 0x10000, check.APP_BYTES, 0),
            )
            self.assertEqual(
                actual["pal_nor"],
                (0x01, 0x40, check.NOR_OFFSET, check.NOR_BYTES, 0),
            )

    def test_generated_partition_binary_rejects_bad_magic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "partition-table.bin"
            path.write_bytes(b"\0" * 0xC00)
            errors: list[str] = []
            self.assertEqual(check.parse_partition_binary(path, errors), {})
            self.assertTrue(any("invalid partition magic" in error for error in errors))

    def test_memory_configuration_parser(self) -> None:
        text = """
Memory Configuration

Name             Origin             Length             Attributes
iram0_0_seg      0x40374000         0x00057700         xr
dram0_0_seg      0x3fc88000         0x00053700         rw
*default*        0x00000000         0xffffffff

Linker script and memory map
"""
        self.assertEqual(
            check.parse_memory_regions(text)["dram0_0_seg"],
            (0x3FC88000, 0x53700),
        )

    def test_symbol_section_parser_keeps_flash_and_dram_placement(self) -> None:
        text = """
3c0b4e80 g     O .flash.rodata  00006100 pal_mame_opl2_fixed_tl_tab
3fc9c510 g     O .dram0.bss     000006a8 pal_mame_opl2_state
00000000         *UND*          00000000 malloc
"""
        self.assertEqual(
            check.parse_symbol_sections(text),
            {
                "pal_mame_opl2_fixed_tl_tab": (0x6100, ".flash.rodata"),
                "pal_mame_opl2_state": (0x6A8, ".dram0.bss"),
            },
        )

    def test_cmake_cache_parser_strips_types_and_comments(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "CMakeCache.txt"
            path.write_text(
                "\n".join(
                    (
                        "// generated cache",
                        "CARDPUTER_EXTREME_MUSIC:BOOL=ON",
                        "CARDPUTER_EXTREME_NO_PSRAM:UNINITIALIZED=ON",
                        "PAL_CORES3SE_ENGINE_HOST:UNINITIALIZED=1",
                        "",
                    )
                ),
                encoding="utf-8",
            )
            self.assertEqual(
                check.parse_cmake_cache(path),
                {
                    "CARDPUTER_EXTREME_MUSIC": "ON",
                    "CARDPUTER_EXTREME_NO_PSRAM": "ON",
                    "PAL_CORES3SE_ENGINE_HOST": "1",
                },
            )

    def test_heap_telemetry_queries_are_allowed(self) -> None:
        for name in (
            "heap_caps_get_free_size",
            "heap_caps_get_minimum_free_size",
            "heap_caps_get_largest_free_block",
        ):
            self.assertFalse(check.forbidden_project_symbol(name))
        for name in (
            "malloc",
            "heap_caps_malloc",
            "heap_caps_aligned_alloc",
            "PAL_RuntimeHeapAllocUnavailable",
            "PAL_RuntimeCodecUnavailable",
            "_Znwj",
            "_ZdlPv",
        ):
            self.assertTrue(check.forbidden_project_symbol(name))

    def test_source_inventory_requires_sorted_unique_entries(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "sources.json"
            path.write_text(
                json.dumps({"version": 1, "sources": ["z.c", "a.c", "a.c"]}),
                encoding="utf-8",
            )
            errors: list[str] = []
            self.assertEqual(
                check.load_source_inventory(path, errors),
                {"a.c", "z.c"},
            )
            self.assertTrue(any("duplicate" in error for error in errors))
            self.assertTrue(any("sorted" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
