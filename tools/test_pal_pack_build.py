#!/usr/bin/env python3
"""Unit and real-dataset closure checks for pal_pack_build.py."""

from __future__ import annotations

import collections
import hashlib
import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
BUILDER_PATH = TOOLS_DIR / "pal_pack_build.py"
DEFAULT_LAYOUT_PATH = TOOLS_DIR / "pal_pack_layout_default.json"
EXTREME_LAYOUT_PATH = TOOLS_DIR / "pal_pack_layout_cardputer_extreme.json"
EXTREME_MUSIC_PROFILE = "rix-music"
EXTREME_MUSIC_TRACKS = {
    1,
    2,
    3,
    4,
    8,
    11,
    12,
    24,
    30,
    31,
    33,
    34,
    36,
    37,
    38,
    49,
    61,
    65,
    70,
    71,
    75,
    76,
    77,
    86,
    87,
}
PAL_DATA_DIR = Path("/mnt/hgfs/deb13/PAL")

spec = importlib.util.spec_from_file_location("pal_pack_build_under_test", BUILDER_PATH)
assert spec is not None and spec.loader is not None
builder = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = builder
spec.loader.exec_module(builder)


def source_chunks(name: str) -> list[bytes]:
    return [
        builder.decode_if_needed(chunk)
        for chunk in builder.read_mkf(builder.find_data_file(PAL_DATA_DIR, f"{name}.MKF"))
    ]


def selected_ids(rule: builder.ChunkRule, chunk_count: int) -> set[int]:
    return set(builder.selected_chunk_ids(rule, chunk_count, "test"))


def traverse_scripts(
    entries: list[tuple[int, int, int, int]],
    roots: set[int],
    event_limit: int = 423,
    scene_ids: set[int] | None = None,
) -> set[int]:
    # PAL_InterpretInstruction normally advances to the next entry. These are
    # its conditional branch operands; exploring both sides is conservative.
    branch_operand = {
        0x001E: 1,
        0x0020: 2,
        0x002E: 2,
        0x0033: 0,
        0x0034: 0,
        0x0038: 0,
        0x003A: 0,
        0x0058: 2,
        0x005D: 1,
        0x005E: 1,
        0x0061: 0,
        0x0064: 1,
        0x0068: 0,
        0x0074: 0,
        0x0079: 1,
        0x0081: 2,
        0x0083: 2,
        0x0084: 2,
        0x0086: 2,
        0x0091: 0,
        0x0094: 2,
        0x0095: 1,
        0x009C: 1,
        0x009E: 2,
    }
    queued = collections.deque(roots)
    seen: set[int] = set()
    while queued:
        entry_id = queued.popleft()
        if entry_id in seen or not 0 < entry_id < len(entries):
            continue
        seen.add(entry_id)
        operation, operand0, operand1, operand2 = entries[entry_id]
        operands = (operand0, operand1, operand2)
        if operation == 0:
            continue
        if operation == 1:
            queued.append(entry_id + 1)
            continue

        if operation in (2, 3):
            # With a zero idle-count these are terminal transfers.  A positive
            # count can eventually expire and fall through on a later call.
            queued.append(operand0)
            if operand1:
                queued.append(entry_id + 1)
        else:
            queued.append(entry_id + 1)
        if operation == 4:
            queued.append(operand0)
        elif operation == 6:
            queued.append(operand1)
        elif operation == 7:
            queued.extend((operand1, operand2))
        elif operation == 0x000A:
            queued.append(operand0)
        elif operation in branch_operand:
            queued.append(operands[branch_operand[operation]])
        elif operation == 0x00A2:
            queued.extend(
                range(entry_id + 1, min(len(entries), entry_id + max(1, operand0) + 1))
            )

        # Scripts can install later trigger/auto scripts into chapter event
        # objects, or replace enter/teleport scripts for a chapter scene.
        # Those installed entry points are part of the closure even though
        # control does not jump to them immediately.
        if (
            operation in (0x0024, 0x0025)
            and operand1
            and (operand0 == 0xFFFF or 1 <= operand0 <= event_limit)
        ):
            queued.append(operand1)
        elif operation == 0x006D and scene_ids is not None and operand0 in scene_ids:
            queued.extend(item for item in (operand1, operand2) if item)
    return seen


class PackBuilderUnitTests(unittest.TestCase):
    def test_default_layout_v1_is_unchanged(self) -> None:
        layout = builder.load_pack_layout(DEFAULT_LAYOUT_PATH)
        self.assertEqual(layout.version, 1)
        self.assertIn("MGO", layout.pack_names["nor"])
        self.assertIn("MAP", layout.pack_names["tf"])
        self.assertEqual(layout.chunk_rules, {"nor": {}, "tf": {}})
        self.assertIsNone(layout.tf_complete_mirror)

    def test_v2_sparse_pack_keeps_chunk_numbers_and_zeroes_absent_payloads(self) -> None:
        chunks = [
            builder.Chunk(b"A", builder.FORMAT_NATIVE),
            builder.Chunk(b"BB", builder.FORMAT_NATIVE),
            builder.Chunk(b"CCC", builder.FORMAT_NATIVE),
            builder.Chunk(b"DDDD", builder.FORMAT_NATIVE),
        ]
        rule = builder.ChunkRule(False, frozenset({0}), ((2, 2),), ())
        selected = builder.apply_chunk_rule(chunks, rule, "MGO")
        pack = builder.build_pack({"MGO": selected})
        builder.verify_pack(pack)

        archive_offset = builder.HEADER_SIZE
        self.assertEqual(struct.unpack_from("<H", pack, archive_offset + 2)[0], 4)
        chunk_table_offset = builder.u32(pack, archive_offset + 4)
        sizes = [
            builder.u32(pack, chunk_table_offset + index * builder.CHUNK_ENTRY_SIZE + 4)
            for index in range(4)
        ]
        self.assertEqual(sizes, [1, 0, 3, 0])
        self.assertEqual([chunk.present for chunk in selected], [True, False, True, False])

    def test_v2_prefix_transform_is_bounded_and_recorded_as_present(self) -> None:
        chunks = [builder.Chunk(b"0123456789", builder.FORMAT_NATIVE)]
        rule = builder.ChunkRule(True, frozenset(), (), ((0, 4),))
        selected = builder.apply_chunk_rule(chunks, rule, "SSS")
        self.assertEqual(selected[0].payload, b"0123")
        self.assertTrue(selected[0].present)

        too_long = builder.ChunkRule(True, frozenset(), (), ((0, 11),))
        with self.assertRaises(SystemExit):
            builder.apply_chunk_rule(chunks, too_long, "SSS")

    def test_v2_layout_allows_disjoint_halves_of_one_archive(self) -> None:
        layout_data = {
            "schema": "sdlpal-embedded-pack-layout",
            "version": 2,
            "packs": {"nor": ["MGO"], "tf": ["MGO"]},
            "chunk_selection": {
                "nor": {"MGO": {"ranges": [[0, 1]]}},
                "tf": {"MGO": {"chunks": [2, 3]}},
            },
        }
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "layout.json"
            path.write_text(json.dumps(layout_data))
            layout = builder.load_pack_layout(path)

        source = [builder.Chunk(bytes([index]), builder.FORMAT_NATIVE) for index in range(4)]
        nor = {
            "MGO": builder.apply_chunk_rule(
                source, layout.chunk_rules["nor"]["MGO"], "nor.MGO"
            )
        }
        tf = {
            "MGO": builder.apply_chunk_rule(
                source, layout.chunk_rules["tf"]["MGO"], "tf.MGO"
            )
        }
        builder.validate_disjoint_pack_chunks(nor, tf)
        self.assertEqual([chunk.present for chunk in nor["MGO"]], [True, True, False, False])
        self.assertEqual([chunk.present for chunk in tf["MGO"]], [False, False, True, True])

    def test_v2_additive_profile_does_not_change_base_layout(self) -> None:
        layout_data = {
            "schema": "sdlpal-embedded-pack-layout",
            "version": 2,
            "packs": {"nor": ["MGO"], "tf": ["FBP"]},
            "profiles": {
                "music": {
                    "pack_additions": {
                        "nor": {"MUS": {"chunks": [1, 3]}},
                        "tf": {},
                    }
                }
            },
            "chunk_selection": {
                "nor": {"MGO": {"all": True}},
                "tf": {"FBP": {"all": True}},
            },
        }
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "layout.json"
            path.write_text(json.dumps(layout_data))
            base = builder.load_pack_layout(path)
            music = builder.load_pack_layout(path, "music")

        self.assertNotIn("MUS", base.pack_names["nor"])
        self.assertNotIn("MUS", base.chunk_rules["nor"])
        self.assertEqual(base.profile, None)
        self.assertEqual(music.pack_names["nor"], ["MGO", "MUS"])
        self.assertEqual(music.chunk_rules["nor"]["MUS"].chunk_ids, {1, 3})
        self.assertEqual(music.profile, "music")

    def test_complete_tf_mirror_policy_is_strict_and_profile_independent(self) -> None:
        mirror = {
            "archives": ["MGO", "MUS", "SFX"],
            "target_filename": "pal_full.pak",
            "runtime_active": False,
            "all_chunks": True,
            "allow_overlap": True,
            "index_strategy": "offline-mirror-not-runtime-indexed",
        }
        layout_data = {
            "schema": "sdlpal-embedded-pack-layout",
            "version": 2,
            "packs": {"nor": ["MGO"], "tf": ["FBP"]},
            "profiles": {
                "music": {
                    "pack_additions": {
                        "nor": {"MUS": {"all": True}},
                        "tf": {},
                    }
                }
            },
            "tf_complete_mirror": mirror,
            "chunk_selection": {
                "nor": {"MGO": {"all": True}},
                "tf": {"FBP": {"all": True}},
            },
        }
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "layout.json"
            path.write_text(json.dumps(layout_data))
            base = builder.load_pack_layout(path)
            music = builder.load_pack_layout(path, "music")

            self.assertEqual(
                base.tf_complete_mirror.archives,
                ("MGO", "MUS", "SFX"),
            )
            self.assertEqual(
                base.tf_complete_mirror,
                music.tf_complete_mirror,
            )

            for key, invalid_value in (
                ("runtime_active", True),
                ("all_chunks", False),
                ("allow_overlap", False),
                ("target_filename", "too_long_name.pak"),
                ("target_filename", "bad\\name.pak"),
                ("index_strategy", "runtime"),
            ):
                invalid = json.loads(json.dumps(layout_data))
                invalid["tf_complete_mirror"][key] = invalid_value
                path.write_text(json.dumps(invalid))
                with self.assertRaises(SystemExit):
                    builder.load_pack_layout(path)

    def test_v2_profile_rejects_unknown_or_duplicate_additions(self) -> None:
        layout_data = {
            "schema": "sdlpal-embedded-pack-layout",
            "version": 2,
            "packs": {"nor": ["MUS"], "tf": []},
            "profiles": {
                "duplicate": {
                    "pack_additions": {
                        "nor": {"MUS": {"chunks": [1]}},
                    }
                }
            },
            "chunk_selection": {
                "nor": {"MUS": {"chunks": [1]}},
                "tf": {},
            },
        }
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "layout.json"
            path.write_text(json.dumps(layout_data))
            with self.assertRaises(SystemExit):
                builder.load_pack_layout(path, "missing")
            with self.assertRaises(SystemExit):
                builder.load_pack_layout(path, "duplicate")

    def test_pack_set_id_covers_exact_contents_of_both_packs(self) -> None:
        nor = {"MGO": [builder.Chunk(b"nor", builder.FORMAT_NATIVE)]}
        tf = {"FBP": [builder.Chunk(b"tf", builder.FORMAT_NATIVE)]}
        pack_set_id = builder.compute_pack_set_id(nor, tf)

        self.assertNotEqual(pack_set_id, 0)
        self.assertEqual(pack_set_id, builder.compute_pack_set_id(nor, tf))
        self.assertNotEqual(
            pack_set_id,
            builder.compute_pack_set_id(
                {"MGO": [builder.Chunk(b"NOR", builder.FORMAT_NATIVE)]},
                tf,
            ),
        )
        self.assertNotEqual(
            pack_set_id,
            builder.compute_pack_set_id(
                nor,
                {"FBP": [builder.Chunk(b"TF", builder.FORMAT_NATIVE)]},
            ),
        )

        nor_pack = builder.build_pack(nor, pack_set_id)
        tf_pack = builder.build_pack(tf, pack_set_id)
        builder.verify_pack(nor_pack)
        builder.verify_pack(tf_pack)
        self.assertEqual(
            builder.u32(nor_pack, builder.PACK_SET_ID_OFFSET),
            builder.u32(tf_pack, builder.PACK_SET_ID_OFFSET),
        )

        corrupt = bytearray(nor_pack)
        corrupt[-1] ^= 1
        with self.assertRaises(ValueError):
            builder.verify_pack(bytes(corrupt))

        full = {
            "MGO": [builder.Chunk(b"nor", builder.FORMAT_NATIVE)],
            "FBP": [builder.Chunk(b"tf", builder.FORMAT_NATIVE)],
        }
        full_set_id = builder.compute_pack_set_id(nor, tf, full)
        self.assertNotEqual(full_set_id, pack_set_id)
        self.assertNotEqual(
            full_set_id,
            builder.compute_pack_set_id(
                nor,
                tf,
                {
                    "MGO": [builder.Chunk(b"NOR", builder.FORMAT_NATIVE)],
                    "FBP": [builder.Chunk(b"tf", builder.FORMAT_NATIVE)],
                },
            ),
        )

    def test_complete_pack_manifest_records_chunk_hashes(self) -> None:
        archives = {
            "MGO": [
                builder.Chunk(b"first", builder.FORMAT_NATIVE),
                builder.Chunk(b"second", builder.FORMAT_NATIVE),
            ]
        }
        pack = builder.build_pack(archives, 123)
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "pal_full.pak"
            path.write_bytes(pack)
            summary = builder.summarize_pack(
                path,
                ["MGO"],
                pack,
                archives,
                False,
                True,
            )

        self.assertEqual(summary["toc_bytes"], builder.u32(pack, 16))
        self.assertEqual(summary["sha256"], hashlib.sha256(pack).hexdigest())
        chunks = summary["archive_summaries"][0]["chunks"]
        self.assertEqual(
            [item["sha256"] for item in chunks],
            [
                hashlib.sha256(b"first").hexdigest(),
                hashlib.sha256(b"second").hexdigest(),
            ],
        )


@unittest.skipUnless(PAL_DATA_DIR.is_dir(), f"real PAL data is unavailable at {PAL_DATA_DIR}")
class CardputerExtremeClosureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.layout = builder.load_pack_layout(EXTREME_LAYOUT_PATH)
        cls.layout_json = json.loads(EXTREME_LAYOUT_PATH.read_text())
        cls.sss = source_chunks("SSS")
        cls.data = source_chunks("DATA")
        cls.events = cls.sss[0]
        cls.scenes = cls.sss[1]
        cls.objects = cls.sss[2]
        cls.entries = [
            struct.unpack_from("<4H", cls.sss[4], offset)
            for offset in range(0, len(cls.sss[4]), 8)
        ]
        cls.scene_ids = set(range(1, 21)) | {22}

        roots: set[int] = set()
        cls.map_ids: set[int] = set()
        cls.event_sprite_ids: set[int] = set()
        cls.max_event_end = 0
        for scene_id in cls.scene_ids:
            scene_offset = (scene_id - 1) * 8
            map_id, on_enter, on_teleport, event_start = struct.unpack_from(
                "<4H", cls.scenes, scene_offset
            )
            event_end = struct.unpack_from("<H", cls.scenes, scene_offset + 8 + 6)[0]
            cls.map_ids.add(map_id)
            cls.max_event_end = max(cls.max_event_end, event_end)
            roots.update(item for item in (on_enter, on_teleport) if item)
            for event_index in range(event_start, event_end):
                trigger, automatic = struct.unpack_from("<HH", cls.events, event_index * 32 + 8)
                sprite_id = struct.unpack_from("<H", cls.events, event_index * 32 + 16)[0]
                roots.update(item for item in (trigger, automatic) if item)
                if sprite_id:
                    cls.event_sprite_ids.add(sprite_id)

        seen = traverse_scripts(cls.entries, roots, cls.max_event_end, cls.scene_ids)
        cls.team_ids = {cls.entries[index][1] for index in seen if cls.entries[index][0] == 7}
        cls.enemy_object_ids: set[int] = set()
        for team_id in cls.team_ids:
            for object_id in struct.unpack_from("<5H", cls.data[2], team_id * 10):
                if object_id not in (0, 0xFFFF):
                    cls.enemy_object_ids.add(object_id)
        for object_id in cls.enemy_object_ids:
            _enemy_id, _resistance, on_turn, on_end, on_ready = struct.unpack_from(
                "<5H", cls.objects, object_id * 12
            )
            roots.update(item for item in (on_turn, on_end, on_ready) if item)
        cls.seen = traverse_scripts(cls.entries, roots, cls.max_event_end, cls.scene_ids)
        cls.enemy_sprite_ids = {
            struct.unpack_from("<H", cls.objects, object_id * 12)[0]
            for object_id in cls.enemy_object_ids
        }

    def test_scene_map_and_event_sprite_closure(self) -> None:
        map_count = len(builder.read_mkf(builder.find_data_file(PAL_DATA_DIR, "MAP.MKF")))
        gop_count = len(builder.read_mkf(builder.find_data_file(PAL_DATA_DIR, "GOP.MKF")))
        mgo_count = len(builder.read_mkf(builder.find_data_file(PAL_DATA_DIR, "MGO.MKF")))
        map_selected = selected_ids(self.layout.chunk_rules["nor"]["MAP"], map_count)
        gop_selected = selected_ids(self.layout.chunk_rules["nor"]["GOP"], gop_count)
        mgo_selected = selected_ids(self.layout.chunk_rules["nor"]["MGO"], mgo_count)

        self.assertEqual(self.max_event_end, 423)
        self.assertEqual(self.map_ids, map_selected)
        self.assertEqual(self.map_ids, gop_selected)
        self.assertTrue(self.event_sprite_ids <= mgo_selected)

        player_scene_sprites = set(struct.unpack_from("<6H", self.data[3], 24))
        scripted_scene_sprites = {
            self.entries[index][2]
            for index in self.seen
            if self.entries[index][0] == 0x0065
        }
        self.assertTrue(player_scene_sprites <= mgo_selected)
        self.assertTrue(scripted_scene_sprites <= mgo_selected)
        self.assertTrue({71, 73}.isdisjoint(mgo_selected))

    def test_event_prefix_matches_scene_closure(self) -> None:
        transforms = dict(self.layout.chunk_rules["nor"]["SSS"].prefix_bytes)
        self.assertEqual(transforms, {0: 423 * 32})
        self.assertLess(transforms[0], len(self.events))

        scene_spans = []
        for scene_id in self.scene_ids:
            offset = (scene_id - 1) * 8 + 6
            start = struct.unpack_from("<H", self.scenes, offset)[0]
            end = struct.unpack_from("<H", self.scenes, offset + 8)[0]
            scene_spans.append(end - start)
        self.assertEqual(max(scene_spans), 74)
        self.assertLessEqual(max(scene_spans), 128)

    def test_candidate_boundary_records_unresolved_script_targets(self) -> None:
        audit = self.layout_json["closure_audit"]
        self.assertEqual(audit["status"], "candidate-not-route-proven")

        scene_destinations = {
            self.entries[index][1]
            for index in self.seen
            if self.entries[index][0] == 0x0059
        }
        chapter_complete = set(audit["chapter_complete_scene_destinations"])
        self.assertEqual(chapter_complete, {21})
        self.assertEqual(
            scene_destinations - self.scene_ids - chapter_complete,
            set(audit["known_unresolved_scene_destinations"]),
        )

        explicit_event_target_operations = {
            0x0012,
            0x0013,
            0x0016,
            0x0024,
            0x0025,
            0x0040,
            0x0049,
            0x006C,
            0x006F,
            0x007D,
            0x007E,
            0x0081,
            0x0083,
            0x0084,
            0x0094,
        }
        unresolved_event_targets = {
            self.entries[index][1]
            for index in self.seen
            if self.entries[index][0] in explicit_event_target_operations
            and self.entries[index][1] not in (0, 0xFFFF)
            and self.entries[index][1] > self.max_event_end
        }
        supported_sparse = set(audit["supported_sparse_event_object_targets"])
        self.assertEqual(supported_sparse, {5334})
        self.assertEqual(
            unresolved_event_targets - supported_sparse,
            set(audit["known_unresolved_event_object_targets"]),
        )

        for index in self.seen:
            if self.entries[index][0] != 0x009A:
                continue
            first, last = self.entries[index][1:3]
            self.assertGreater(first, 0)
            self.assertLessEqual(first, last)
            self.assertLessEqual(last, self.max_event_end)

    def test_battle_team_enemy_and_sequential_payload_closure(self) -> None:
        expected_teams = set(
            self.layout_json["closure_audit"]["battles"]["reachable_team_ids"]
        )
        self.assertEqual(self.team_ids, expected_teams)

        abc_count = len(builder.read_mkf(builder.find_data_file(PAL_DATA_DIR, "ABC.MKF")))
        abc_selected = selected_ids(self.layout.chunk_rules["nor"]["ABC"], abc_count)
        self.assertEqual(self.enemy_sprite_ids, abc_selected)

        f_count = len(builder.read_mkf(builder.find_data_file(PAL_DATA_DIR, "F.MKF")))
        fire_count = len(builder.read_mkf(builder.find_data_file(PAL_DATA_DIR, "FIRE.MKF")))
        self.assertEqual(
            selected_ids(self.layout.chunk_rules["nor"]["F"], f_count),
            set(range(f_count)),
        )
        self.assertEqual(
            selected_ids(self.layout.chunk_rules["nor"]["FIRE"], fire_count),
            set(range(fire_count)),
        )

        fbp_count = len(builder.read_mkf(builder.find_data_file(PAL_DATA_DIR, "FBP.MKF")))
        nor_fbp = selected_ids(self.layout.chunk_rules["nor"]["FBP"], fbp_count)
        tf_fbp = selected_ids(self.layout.chunk_rules["tf"]["FBP"], fbp_count)
        battlefield_ids = {
            self.entries[index][1]
            for index in self.seen
            if self.entries[index][0] == 0x004A
        }
        scripted_fbp_ids = {
            self.entries[index][1]
            for index in self.seen
            if self.entries[index][0] in (0x0076, 0x00A4, 0x00A5)
            and self.entries[index][1] != 0xFFFF
        }
        self.assertTrue(({0} | battlefield_ids | scripted_fbp_ids) <= (nor_fbp | tf_fbp))
        self.assertNotIn(37, nor_fbp | tf_fbp)
        self.assertTrue({38, 39}.isdisjoint(nor_fbp | tf_fbp))

        effect_mgo_ids = {
            self.entries[index][2]
            for index in self.seen
            if self.entries[index][0] == 0x00A5
            and self.entries[index][2] not in (0, 0xFFFF)
        }
        mgo_count = len(builder.read_mkf(builder.find_data_file(PAL_DATA_DIR, "MGO.MKF")))
        mgo_selected = selected_ids(self.layout.chunk_rules["nor"]["MGO"], mgo_count)
        self.assertTrue(effect_mgo_ids <= mgo_selected)

        # These opcodes can introduce assets or enemy objects dynamically.
        # The current enumerated graph has none; keep that fact executable
        # instead of silently relying on it.
        self.assertFalse(
            {
                index
                for index in self.seen
                if self.entries[index][0] in (0x0098, 0x0099, 0x009E, 0x009F)
            }
        )

        rng_count = len(builder.read_mkf(builder.find_data_file(PAL_DATA_DIR, "RNG.MKF")))
        tf_rng = selected_ids(self.layout.chunk_rules["tf"]["RNG"], rng_count)
        scripted_rng_ids = {
            self.entries[index][1]
            for index in self.seen
            if self.entries[index][0] == 0x0036
        }
        self.assertEqual(scripted_rng_ids, {1})
        self.assertEqual(tf_rng, {1})

    def test_audio_is_not_in_either_pack(self) -> None:
        packed = set(self.layout.pack_names["nor"]) | set(self.layout.pack_names["tf"])
        self.assertTrue({"MIDI", "MUS", "VOC", "SFX"}.isdisjoint(packed))
        self.assertEqual(self.layout.max_bytes["nor"], 0x6F0000)

    def test_complete_tf_mirror_covers_every_runtime_native_archive(self) -> None:
        mirror = self.layout.tf_complete_mirror
        self.assertIsNotNone(mirror)
        self.assertEqual(mirror.target_filename, "pal_full.pak")
        self.assertEqual(
            set(mirror.archives),
            set(builder.ARCHIVE_IDS) - {"VOC"},
        )
        self.assertIn("SFX", mirror.archives)
        self.assertNotIn("VOC", mirror.archives)
        self.assertTrue(
            set(self.layout.pack_names["nor"]) <= set(mirror.archives)
        )
        self.assertTrue(
            set(self.layout.pack_names["tf"]) <= set(mirror.archives)
        )

    def test_rix_music_profile_is_sparse_and_keeps_the_noaudio_base(self) -> None:
        music = builder.load_pack_layout(
            EXTREME_LAYOUT_PATH, EXTREME_MUSIC_PROFILE
        )
        self.assertEqual(music.profile, EXTREME_MUSIC_PROFILE)
        self.assertIn("MUS", music.pack_names["nor"])
        self.assertNotIn("MUS", music.pack_names["tf"])
        self.assertEqual(
            selected_ids(music.chunk_rules["nor"]["MUS"], 88),
            EXTREME_MUSIC_TRACKS,
        )
        packed = set(music.pack_names["nor"]) | set(music.pack_names["tf"])
        self.assertTrue({"MIDI", "VOC", "SFX"}.isdisjoint(packed))

        profile = self.layout_json["profiles"][EXTREME_MUSIC_PROFILE]
        audit = profile["music_audit"]
        self.assertEqual(audit["source_chunk_count"], 88)
        self.assertEqual(
            set(audit["selected_track_ids"]),
            EXTREME_MUSIC_TRACKS,
        )

        scripted_tracks = {
            self.entries[index][1]
            for index in self.seen
            if self.entries[index][0] in (0x0043, 0x0045)
            and self.entries[index][1] != 0
        }
        scripted_tracks.update(
            self.entries[index][2]
            for index in self.seen
            if self.entries[index][0] == 0x00A3
            and self.entries[index][2] != 0
        )

        # Object 273 is the usable, apply-to-all item whose use script selects
        # music 36. It is an explicit dynamic root outside the scene traversal.
        item_273 = struct.unpack_from("<6H", self.objects, 273 * 12)
        self.assertEqual(item_273[5] & (1 | 16), 1 | 16)
        item_seen = traverse_scripts(
            self.entries,
            {entry for entry in item_273[2:5] if entry},
            self.max_event_end,
            self.scene_ids,
        )
        item_tracks = {
            self.entries[index][1]
            for index in item_seen
            if self.entries[index][0] in (0x0043, 0x0045)
            and self.entries[index][1] != 0
        }
        self.assertEqual(item_tracks, {36})

        # Opening menu 4 and battle victory 2/3 are direct engine call sites.
        self.assertEqual(
            scripted_tracks | item_tracks | {2, 3, 4},
            EXTREME_MUSIC_TRACKS,
        )

        mus = source_chunks("MUS")
        self.assertEqual(len(mus), 88)
        selected_payloads = [mus[track_id] for track_id in EXTREME_MUSIC_TRACKS]
        self.assertTrue(all(payload[:2] == b"\xaa\x55" for payload in selected_payloads))
        self.assertEqual(sum(map(len, selected_payloads)), 75636)
        self.assertEqual(max(map(len, selected_payloads)), 8998)


if __name__ == "__main__":
    unittest.main()
