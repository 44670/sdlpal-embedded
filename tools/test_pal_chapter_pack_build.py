#!/usr/bin/env python3
"""Tests for pal_chapter_pack_build.py."""

from __future__ import annotations

import hashlib
import importlib.util
import os
import struct
import sys
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
PAL_DATA_DIR = Path("/mnt/hgfs/deb13/PALSteam/PAL_DOS")
FONT10_ARCHIVE = Path(
    os.environ.get(
        "FONT10_ARCHIVE",
        str(
            TOOLS_DIR.parent
            / "third_party"
            / "fusion-pixel-font"
            / "fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip"
        ),
    )
)
EXPECTED_CORE_BYTES = 4_502_840
EXPECTED_FULL_BYTES = 57_646_550
EXPECTED_LEVEL2_RESIDENT_BYTES = 1_747_784
EXPECTED_BUNDLE_BYTES = (
    2_648_156,
    2_463_416,
    2_821_260,
    2_344_220,
    2_634_644,
    2_505_372,
    2_760_060,
    2_370_712,
    2_776_084,
    2_405_568,
    2_193_804,
    2_277_834,
    2_441_404,
    2_389_280,
    2_386_960,
)
EXPECTED_SCENE_TABLE_SHA256 = (
    "872f58eacc33d4159add454398227093a1660cda56e76133da55174c5117903b"
)
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

spec = importlib.util.spec_from_file_location(
    "pal_chapter_pack_build_under_test",
    TOOLS_DIR / "pal_chapter_pack_build.py",
)
assert spec is not None and spec.loader is not None
chapter = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = chapter
spec.loader.exec_module(chapter)


def packed_chunk(
    image: bytes,
    archive_name: str,
    chunk_id: int,
) -> chapter.pack.Chunk:
    archive_count = chapter.pack.u16(image, 8)
    archive_table = chapter.pack.u32(image, 12)
    wanted_id = chapter.pack.ARCHIVE_IDS[archive_name]
    for archive_index in range(archive_count):
        entry = archive_table + archive_index * chapter.pack.ARCHIVE_ENTRY_SIZE
        archive_id, chunk_count = struct.unpack_from("<HH", image, entry)
        if archive_id != wanted_id:
            continue
        if not 0 <= chunk_id < chunk_count:
            raise AssertionError(f"missing {archive_name}#{chunk_id}")
        chunk_table = chapter.pack.u32(image, entry + 4)
        chunk_entry = chunk_table + chunk_id * chapter.pack.CHUNK_ENTRY_SIZE
        offset, size, fmt, flags = struct.unpack_from(
            "<IIHH", image, chunk_entry
        )
        if flags != 0:
            raise AssertionError(f"{archive_name}#{chunk_id} is absent")
        return chapter.pack.Chunk(image[offset : offset + size], fmt)
    raise AssertionError(f"missing archive {archive_name}")


class ChapterPackUnitTests(unittest.TestCase):
    def test_chapter_build_requires_pinned_font10_archive(self) -> None:
        with self.assertRaisesRegex(ValueError, "FONT10"):
            chapter.build_chapter_packs(Path("/unused"))

    def test_scene_table_covers_exactly_scenes_1_through_299(self) -> None:
        table = chapter.make_scene_table()
        self.assertEqual(len(table), 300)
        self.assertEqual(table[0], 0xFF)
        self.assertEqual(set(table[1:]), set(range(15)))
        self.assertEqual(
            hashlib.sha256(table).hexdigest(),
            EXPECTED_SCENE_TABLE_SHA256,
        )
        for bundle_id, scene_ids in enumerate(chapter.SCENE_BUNDLES):
            for scene_id in scene_ids:
                self.assertEqual(table[scene_id], bundle_id)
        self.assertEqual(table[105], 5)  # Yangzhou east outskirts, not b02.
        self.assertEqual(table[121], 3)  # One-shot post-meal state.
        self.assertEqual(table[125], 6)  # Local capital room states stay local.
        self.assertEqual(table[126], 6)
        self.assertEqual(table[133], 6)
        self.assertEqual(table[144], 9)  # Locking Demon Tower interior.
        self.assertEqual(table[179], 10)  # Mount Ling summit.
        self.assertEqual(table[202], 11)  # Outside Dali.

    def test_catalog_binary_layout_hashes_and_crc(self) -> None:
        scene_table = chapter.make_scene_table()
        images = [bytes([bundle_id]) * (bundle_id + 1) for bundle_id in range(15)]
        catalog = chapter.build_catalog(0x12345678, scene_table, images)
        chapter.verify_catalog(catalog, 0x12345678)

        self.assertEqual(len(catalog), 32 + 300 + 15 * 40)
        self.assertEqual(catalog[:4], b"PLBC")
        self.assertEqual(struct.unpack_from("<I", catalog, 20)[0], 332)
        first_descriptor = 332
        self.assertEqual(catalog[first_descriptor], 0)
        self.assertEqual(struct.unpack_from("<I", catalog, first_descriptor + 4)[0], 1)
        self.assertEqual(
            catalog[first_descriptor + 8 : first_descriptor + 40],
            hashlib.sha256(images[0]).digest(),
        )

        corrupt = bytearray(catalog)
        corrupt[-1] ^= 1
        with self.assertRaises(ValueError):
            chapter.verify_catalog(bytes(corrupt))

    def test_set_file_carries_core_hash_and_external_catalog(self) -> None:
        scene_table = chapter.make_scene_table()
        images = [bytes([bundle_id]) * (bundle_id + 1) for bundle_id in range(15)]
        set_id = 0x12345678
        catalog = chapter.build_catalog(set_id, scene_table, images)
        core = chapter.pack.build_pack(
            {"DATA": [chapter.pack.Chunk(b"core", chapter.pack.FORMAT_NATIVE)]},
            set_id,
        )
        set_file = chapter.build_set_file(set_id, core, catalog)

        chapter.verify_set_file(set_file, core)
        self.assertEqual(set_file[:4], b"PLST")
        self.assertEqual(struct.unpack_from("<I", set_file, 12)[0], set_id)
        self.assertEqual(struct.unpack_from("<I", set_file, 16)[0], len(core))
        self.assertEqual(
            set_file[32:64],
            hashlib.sha256(core).digest(),
        )
        self.assertEqual(set_file[64:], catalog)

        corrupt = bytearray(set_file)
        corrupt[-1] ^= 1
        with self.assertRaises(ValueError):
            chapter.verify_set_file(bytes(corrupt), core)

    def test_cross_scene_006d_target_is_traversed(self) -> None:
        entries = [(0, 0, 0, 0)] * 8
        entries[1] = (0x006D, 299, 4, 6)
        entries[2] = (0, 0, 0, 0)
        entries[4] = (0x0099, 0xFFFF, 164, 0)
        entries[5] = (0, 0, 0, 0)
        entries[6] = (0x00A5, 70, 635, 7)
        entries[7] = (0, 0, 0, 0)
        self.assertEqual(
            chapter.trace_scripts(entries, {1}, 1),
            {1, 2, 4, 5, 6, 7},
        )

    def test_enemy_summon_and_transform_reach_fixed_point(self) -> None:
        entries = [(0, 0, 0, 0)] * 10
        entries[1] = (7, 0, 0, 0)
        entries[2] = (0, 0, 0, 0)
        entries[4] = (0x009E, 2, 1, 0)
        entries[5] = (0, 0, 0, 0)
        entries[6] = (0x009F, 3, 0, 0)
        entries[7] = (0, 0, 0, 0)
        objects = bytearray(4 * 12)
        struct.pack_into("<6H", objects, 12, 11, 0, 4, 0, 0, 0)
        struct.pack_into("<6H", objects, 24, 12, 0, 6, 0, 0, 0)
        struct.pack_into("<6H", objects, 36, 13, 0, 0, 0, 0, 0)
        teams = struct.pack("<5H", 1, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF)
        tables = chapter.GameTables(
            b"\0" * 32,
            b"\0" * 16,
            bytes(objects),
            tuple(entries),
            teams,
            frozenset({1}),
        )
        closure = chapter.resolve_battle_closure(tables, {1})
        self.assertEqual(closure.team_ids, {0})
        self.assertEqual(closure.enemy_object_ids, {1, 2, 3})
        self.assertEqual(closure.dynamic_enemy_object_ids, {2, 3})
        self.assertTrue({1, 4, 6} <= closure.scripts)

    def test_inbound_006d_scripts_close_target_bundle_assets(self) -> None:
        entries = [(0, 0, 0, 0)] * 8
        # This installer is deliberately unreachable from scene 2's own roots.
        entries[1] = (0x006D, 2, 4, 6)
        entries[2] = (0, 0, 0, 0)
        entries[4] = (0x0099, 0xFFFF, 3, 0)
        entries[5] = (0, 0, 0, 0)
        entries[6] = (0x00A5, 70, 10, 7)
        entries[7] = (0, 0, 0, 0)
        # Three rows: scene 1, scene 2, and the scene-3 sentinel for this
        # synthetic table.  All event spans are empty.
        scenes = (
            struct.pack("<4H", 1, 0, 0, 0)
            + struct.pack("<4H", 2, 0, 0, 0)
            + struct.pack("<4H", 0, 0, 0, 0)
        )
        tables = chapter.GameTables(
            b"",
            scenes,
            b"\0" * 12,
            tuple(entries),
            struct.pack("<5H", 0, 0, 0, 0, 0),
            frozenset({2}),
        )
        closure = chapter.close_bundle(
            0,
            {2},
            tables,
            {"ABC": 20, "GOP": 20, "MAP": 20, "MGO": 20},
        )
        self.assertEqual(closure.inbound_cross_scene_scripts, ((1, 2, 4, 6),))
        self.assertTrue({4, 6} <= closure.battle.scripts)
        self.assertIn(3, closure.map_ids)
        self.assertIn(10, closure.mgo_ids)

    def test_pack_set_id_depends_only_on_portable_complete_data(self) -> None:
        core = {
            "DATA": [chapter.pack.Chunk(b"core", chapter.pack.FORMAT_NATIVE)]
        }
        tf = {
            "FBP": [chapter.pack.Chunk(b"tf", chapter.pack.FORMAT_NATIVE)]
        }
        complete = {**core, **tf}
        original = chapter.compute_chapter_pack_set_id(complete)
        changed_cache = chapter.compute_chapter_pack_set_id(complete)
        changed_mapping = chapter.compute_chapter_pack_set_id(complete)
        changed_full_mirror = chapter.compute_chapter_pack_set_id(
            {
                "DATA": [
                    chapter.pack.Chunk(
                        b"changed full", chapter.pack.FORMAT_NATIVE
                    )
                ],
                **tf,
            },
        )
        self.assertEqual(original, changed_cache)
        self.assertEqual(original, changed_mapping)
        self.assertNotEqual(original, changed_full_mirror)
        self.assertEqual(
            original,
            chapter.pack.compute_portable_pack_set_id(complete),
        )


@unittest.skipUnless(
    PAL_DATA_DIR.is_dir() and FONT10_ARCHIVE.is_file(),
    "real PAL data or pinned Fusion Pixel FONT10 archive is unavailable",
)
class ChapterPackRealDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.build = chapter.build_chapter_packs(
            PAL_DATA_DIR,
            font10_archive=FONT10_ARCHIVE,
        )

    def test_real_build_has_complete_fixed_partition(self) -> None:
        self.assertEqual(self.build.manifest["version"], 2)
        self.assertEqual(len(self.build.bundle_packs), 15)
        self.assertEqual(len(self.build.catalog), 932)
        self.assertEqual(len(self.build.set_file), 996)
        self.assertEqual(len(self.build.core_pack), EXPECTED_CORE_BYTES)
        self.assertEqual(len(self.build.full_pack), EXPECTED_FULL_BYTES)
        self.assertEqual(
            len(self.build.level2_resident_image),
            EXPECTED_LEVEL2_RESIDENT_BYTES,
        )
        self.assertEqual(
            tuple(map(len, self.build.bundle_packs)),
            EXPECTED_BUNDLE_BYTES,
        )
        self.assertEqual(
            {bundle_id for bundle_id in self.build.catalog[32:332]},
            set(range(15)) | {0xFF},
        )
        self.assertEqual(
            self.build.manifest["scene_partition"]["source_scene_row_count"],
            294,
        )
        for image in (
            self.build.core_pack,
            self.build.full_pack,
            self.build.level2_resident_image,
            *self.build.bundle_packs,
        ):
            chapter.pack.verify_pack(image)
            self.assertEqual(
                chapter.pack.u32(image, chapter.pack.PACK_SET_ID_OFFSET),
                self.build.manifest["pack_set"]["id"],
            )

    def test_core_full_and_overlay_archive_contract(self) -> None:
        packs = self.build.manifest["packs"]
        self.assertEqual(
            set(packs["core"]["archives"]),
            set(chapter.CORE_FULL_ARCHIVES) | {"MGO", "CACHE"},
        )
        self.assertEqual(
            set(packs["full"]["archives"]),
            set(chapter.FULL_MIRROR_ARCHIVES),
        )
        self.assertTrue(packs["full"]["portable_complete"])
        self.assertTrue(packs["full"]["all_chunks"])
        self.assertTrue(packs["full"]["allow_cache_overlap"])
        self.assertEqual(
            set(packs["level2_resident"]["archives"]),
            {"DATA", "PAT", "RGM", "SSS", "TEXT", "FONT", "MUS"},
        )
        self.assertTrue(
            set(packs["level2_resident"]["archives"])
            <= set(packs["full"]["archives"])
        )
        self.assertEqual(
            self.build.manifest["runtime"]["portable_complete_tf_file"],
            "pal_full.pak",
        )
        self.assertNotIn("level2_core_cache_tf_file", self.build.manifest["runtime"])
        self.assertEqual(
            self.build.manifest["runtime"]["level2_resident_source"],
            "runtime-derived from pal_full.pak",
        )
        self.assertEqual(
            self.build.manifest["runtime"]["tf_access_shape"],
            "NOR-mapped full-pack TOC with direct bounded payload reads",
        )
        self.assertEqual(
            self.build.manifest["pack_set"]["scope"],
            "portable-complete-data",
        )
        self.assertFalse(
            self.build.manifest["pack_set"]["cache_layout_affects_id"]
        )
        self.assertEqual(
            packs["core"]["archives"]["MGO"]["present_chunk_ids"],
            sorted(chapter.CORE_PERSISTENT_MGO),
        )
        self.assertEqual(
            packs["core"]["archives"]["CACHE"]["present_chunk_ids"],
            [0, 1],
        )
        full_toc = self.build.full_pack[
            : chapter.pack.u32(self.build.full_pack, 16)
        ]
        self.assertEqual(
            packed_chunk(self.build.core_pack, "CACHE", 1),
            chapter.pack.Chunk(full_toc, chapter.pack.FORMAT_RAW),
        )
        self.assertEqual(
            self.build.manifest["full_toc_cache"],
            {
                "archive": "CACHE",
                "archive_id": chapter.pack.ARCHIVE_IDS["CACHE"],
                "chunk_id": 1,
                "format": "RAW",
                "size": len(full_toc),
                "sha256": hashlib.sha256(full_toc).hexdigest(),
                "source": "pal_full.pak[0:data_offset]",
                "cardputer_storage": "pal_core.pak mapped from SPI NOR",
                "cardputer_sram_copy_bytes": 0,
            },
        )
        self.assertEqual(
            packs["core"]["archives"]["SSS"]["present_chunk_payload_bytes"][0],
            170_624,
        )
        self.assertEqual(
            packs["full"]["archives"]["SSS"]["present_chunk_payload_bytes"][0],
            170_624,
        )
        self.assertEqual(
            self.build.manifest["event_state"],
            {
                "status": "standard-rpg-only-persistence",
                "record_bytes": 32,
                "record_count": 5332,
                "record_capacity": 5500,
                "core_chunk_bytes": 170_624,
                "durable_state": "standard N.rpg save slots only",
                "level1_runtime": "three-page LRU over session-only EVENT.WRK",
                "level1_work_writes": "initialization and dirty eviction only",
                "level2_runtime": "complete resident PSRAM array",
            },
        )
        self.assertEqual(
            packs["core"]["archives"]["FONT"]["present_chunk_ids"],
            [1],
        )
        self.assertEqual(
            packs["full"]["archives"]["FONT"]["present_chunk_ids"],
            [0, 1],
        )
        for bundle in packs["bundles"]:
            self.assertEqual(set(bundle["selection"]), set(chapter.OVERLAY_ARCHIVES))
            self.assertLessEqual(bundle["size"], chapter.SOFT_OVERLAY_CAP)
        self.assertLessEqual(len(self.build.core_pack), chapter.CORE_SLOT_CAP)
        self.assertEqual(len(chapter.CORE_GLOBAL_MGO), 35)
        self.assertEqual(len(chapter.CORE_STARTUP_MGO), 2)

    def test_core_font10_is_the_standard_host_generated_chunk(self) -> None:
        expected, summary = chapter.pack.build_font10_archive_chunk(
            PAL_DATA_DIR,
            FONT10_ARCHIVE,
        )
        self.assertEqual(
            packed_chunk(self.build.core_pack, "FONT", 1),
            expected,
        )
        self.assertEqual(
            packed_chunk(self.build.full_pack, "FONT", 1),
            expected,
        )
        self.assertEqual(
            packed_chunk(self.build.level2_resident_image, "FONT", 1),
            expected,
        )
        self.assertEqual(self.build.manifest["font10"], summary)

    def test_level2_resident_payloads_derive_exactly_from_full_pack(self) -> None:
        resident_archives = self.build.manifest["packs"]["level2_resident"]["archives"]
        for archive_name, summary in resident_archives.items():
            for chunk_id in summary["present_chunk_ids"]:
                self.assertEqual(
                    packed_chunk(
                        self.build.level2_resident_image,
                        archive_name,
                        chunk_id,
                    ),
                    packed_chunk(self.build.full_pack, archive_name, chunk_id),
                    f"{archive_name}#{chunk_id}",
                )

    def test_real_closure_records_required_dynamic_edges(self) -> None:
        bundles = self.build.manifest["packs"]["bundles"]
        all_dynamic_maps = {
            item["map_id"]
            for bundle in bundles
            for item in bundle["closure"]["dynamic_maps_0099"]
        }
        all_dynamic_enemies = {
            object_id
            for bundle in bundles
            for object_id in bundle["closure"]["dynamic_enemy_object_ids_009e_009f"]
        }
        all_ending = {
            mgo_id
            for bundle in bundles
            for mgo_id in bundle["closure"]["ending_mgo_ids"]
        }
        cross_scene_targets = [
            item
            for bundle in bundles
            for item in bundle["closure"]["cross_scene_006d_targets"]
        ]
        self.assertEqual(all_dynamic_maps, {164, 165})
        self.assertTrue(all_dynamic_enemies)
        self.assertEqual(all_ending, {571, 572, 635})
        self.assertTrue(cross_scene_targets)

    def test_catalog_descriptors_match_generated_bundles(self) -> None:
        catalog = self.build.catalog
        pack_set_id = self.build.manifest["pack_set"]["id"]
        chapter.verify_catalog(catalog, pack_set_id)
        for bundle_id, image in enumerate(self.build.bundle_packs):
            offset = 332 + bundle_id * 40
            self.assertEqual(catalog[offset], bundle_id)
            self.assertEqual(struct.unpack_from("<I", catalog, offset + 4)[0], len(image))
            self.assertEqual(
                catalog[offset + 8 : offset + 40],
                hashlib.sha256(image).digest(),
            )

    def test_external_set_file_matches_core_and_catalog(self) -> None:
        chapter.verify_set_file(self.build.set_file, self.build.core_pack)
        self.assertEqual(self.build.set_file[64:], self.build.catalog)
        summary = self.build.manifest["set_file"]
        self.assertEqual(summary["filename"], "PALSET.BIN")
        self.assertEqual(summary["core_size"], len(self.build.core_pack))
        self.assertEqual(
            summary["core_sha256"],
            hashlib.sha256(self.build.core_pack).hexdigest(),
        )
        self.assertEqual(
            self.build.manifest["runtime"]["data_identity_file"],
            "PALSET.BIN",
        )
        self.assertFalse(
            self.build.manifest["runtime"]["firmware_embeds_data_hashes"]
        )


if __name__ == "__main__":
    unittest.main()
