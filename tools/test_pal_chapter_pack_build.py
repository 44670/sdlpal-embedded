#!/usr/bin/env python3
"""Tests for pal_chapter_pack_build.py."""

from __future__ import annotations

import hashlib
import importlib.util
import struct
import sys
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
PAL_DATA_DIR = Path("/mnt/hgfs/deb13/PAL")
EXPECTED_CORE_BYTES = 4_334_276
EXPECTED_TF_BYTES = 11_917_143
EXPECTED_FULL_BYTES = 57_755_986
EXPECTED_BUNDLE_BYTES = (
    2_648_252,
    2_463_512,
    2_814_652,
    2_851_120,
    2_826_692,
    2_804_272,
    2_302_148,
    2_776_932,
    1_540_512,
    2_798_712,
    2_796_834,
    2_803_884,
    2_788_396,
    2_815_164,
    2_855_144,
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


class ChapterPackUnitTests(unittest.TestCase):
    def test_scene_table_covers_exactly_scenes_1_through_299(self) -> None:
        table = chapter.make_scene_table()
        self.assertEqual(len(table), 300)
        self.assertEqual(table[0], 0xFF)
        self.assertEqual(set(table[1:]), set(range(15)))
        for bundle_id, (first, last) in enumerate(chapter.SCENE_INTERVALS):
            self.assertEqual(table[first : last + 1], bytes([bundle_id]) * (last - first + 1))

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
            2,
            2,
            tables,
            {"ABC": 20, "GOP": 20, "MAP": 20, "MGO": 20},
        )
        self.assertEqual(closure.inbound_cross_scene_scripts, ((1, 2, 4, 6),))
        self.assertTrue({4, 6} <= closure.battle.scripts)
        self.assertIn(3, closure.map_ids)
        self.assertIn(10, closure.mgo_ids)

    def test_pack_set_id_changes_with_resource_or_scene_mapping(self) -> None:
        core = {
            "DATA": [chapter.pack.Chunk(b"core", chapter.pack.FORMAT_NATIVE)]
        }
        tf = {
            "FBP": [chapter.pack.Chunk(b"tf", chapter.pack.FORMAT_NATIVE)]
        }
        overlays = [
            {
                "MGO": [
                    chapter.pack.Chunk(b"overlay", chapter.pack.FORMAT_NATIVE)
                ]
            }
        ]
        scenes = bytes([0xFF, 0])
        original = chapter.compute_chapter_pack_set_id(
            core, tf, {**core, **tf}, overlays, scenes
        )
        changed_resource = chapter.compute_chapter_pack_set_id(
            core,
            tf,
            {**core, **tf},
            [
                {
                    "MGO": [
                        chapter.pack.Chunk(
                            b"OVERLAY", chapter.pack.FORMAT_NATIVE
                        )
                    ]
                }
            ],
            scenes,
        )
        changed_mapping = chapter.compute_chapter_pack_set_id(
            core, tf, {**core, **tf}, overlays, bytes([0xFF, 1])
        )
        changed_full_mirror = chapter.compute_chapter_pack_set_id(
            core,
            tf,
            {
                "DATA": [
                    chapter.pack.Chunk(
                        b"changed full", chapter.pack.FORMAT_NATIVE
                    )
                ],
                **tf,
            },
            overlays,
            scenes,
        )
        self.assertNotEqual(original, changed_resource)
        self.assertNotEqual(original, changed_mapping)
        self.assertNotEqual(original, changed_full_mirror)


@unittest.skipUnless(
    PAL_DATA_DIR.is_dir(),
    f"real PAL data is unavailable at {PAL_DATA_DIR}",
)
class ChapterPackRealDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.build = chapter.build_chapter_packs(PAL_DATA_DIR)

    def test_real_build_has_complete_fixed_partition(self) -> None:
        self.assertEqual(len(self.build.bundle_packs), 15)
        self.assertEqual(len(self.build.catalog), 932)
        self.assertEqual(len(self.build.core_pack), EXPECTED_CORE_BYTES)
        self.assertEqual(len(self.build.tf_pack), EXPECTED_TF_BYTES)
        self.assertEqual(len(self.build.full_pack), EXPECTED_FULL_BYTES)
        self.assertEqual(
            tuple(map(len, self.build.bundle_packs)),
            EXPECTED_BUNDLE_BYTES,
        )
        self.assertEqual(
            {bundle_id for bundle_id in self.build.catalog[32:332]},
            set(range(15)) | {0xFF},
        )
        for image in (
            self.build.core_pack,
            self.build.tf_pack,
            self.build.full_pack,
            *self.build.bundle_packs,
        ):
            chapter.pack.verify_pack(image)
            self.assertEqual(
                chapter.pack.u32(image, chapter.pack.PACK_SET_ID_OFFSET),
                self.build.manifest["pack_set"]["id"],
            )

    def test_core_tf_and_overlay_archive_contract(self) -> None:
        packs = self.build.manifest["packs"]
        self.assertEqual(
            set(packs["core"]["archives"]),
            set(chapter.CORE_FULL_ARCHIVES) | {"MGO", "CACHE"},
        )
        self.assertEqual(set(packs["tf"]["archives"]), {"FBP", "RNG"})
        self.assertEqual(
            set(packs["full"]["archives"]),
            set(chapter.FULL_MIRROR_ARCHIVES),
        )
        self.assertFalse(packs["full"]["runtime_active"])
        self.assertEqual(
            packs["core"]["archives"]["MGO"]["present_chunk_ids"],
            sorted(chapter.CORE_PERSISTENT_MGO),
        )
        self.assertEqual(
            packs["core"]["archives"]["CACHE"]["present_chunk_ids"],
            [0],
        )
        self.assertEqual(
            packs["core"]["archives"]["SSS"]["present_chunk_payload_bytes"][0],
            423 * 32,
        )
        self.assertEqual(
            packs["full"]["archives"]["SSS"]["present_chunk_payload_bytes"][0],
            171_808,
        )
        for bundle in packs["bundles"]:
            self.assertEqual(set(bundle["selection"]), set(chapter.OVERLAY_ARCHIVES))
            self.assertLessEqual(bundle["size"], chapter.SOFT_OVERLAY_CAP)
        self.assertLessEqual(len(self.build.core_pack), chapter.CORE_SLOT_CAP)
        self.assertEqual(len(chapter.CORE_GLOBAL_MGO), 35)
        self.assertEqual(len(chapter.CORE_STARTUP_MGO), 2)

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


if __name__ == "__main__":
    unittest.main()
