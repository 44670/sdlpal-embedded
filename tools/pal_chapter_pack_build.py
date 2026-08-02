#!/usr/bin/env python3
"""Build and audit full-game PAL chapter-cache packs.

The output separates immutable, always-mapped resources from one replaceable
chapter overlay:

* ``pal_core.pak`` contains complete small/global archives, the pinned
  host-generated FONT10 chunk, the complete RIX music archive, complete
  F/FIRE battle assets, 35 audited global MGO chunks, two startup MGO chunks,
  and a compact binary CACHE catalog.
* ``pal_tf.pak`` contains every host-decoded FBP and RNG chunk.
* ``b00.pak`` through ``b14.pak`` contain sparse ABC/GOP/MAP/MGO archives.
* ``pal_full.pak`` is a complete decoded/native TF mirror for recovery and
  future profiles.  It overlaps the runtime packs and is not runtime-indexed.
* ``PALSET.BIN`` is the bounded boot record containing the pack-set ID, core
  SHA-256, and complete chapter catalog.  Firmware contains none of those
  data-specific values.

Every source chunk keeps its original ID.  YJ1 and RNG decoding is delegated
to pal_pack_build.py and therefore happens only on the host.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

import pal_pack_build as pack


CATALOG_MAGIC = b"PLBC"
CATALOG_VERSION = 1
CATALOG_HEADER_SIZE = 32
CATALOG_SCENE_COUNT = 300
PAL_DOS_SOURCE_SCENE_COUNT = 294
CATALOG_SCENE_TABLE_OFFSET = CATALOG_HEADER_SIZE
CATALOG_BUNDLE_DESC_SIZE = 40
CATALOG_CRC32_OFFSET = 28

SET_MAGIC = b"PLST"
SET_VERSION = 1
SET_HEADER_SIZE = 64
SET_CRC32_OFFSET = 28
SET_CORE_SHA256_OFFSET = 32
SET_FILENAME = "PALSET.BIN"

SOFT_OVERLAY_CAP = 0x2BF000
HARD_OVERLAY_PACK_CAP = 0x2CF000
CORE_SLOT_CAP = 0x460000
CORE_EVENT_OBJECT_COUNT = 423
CORE_EVENT_OBJECT_BYTES = CORE_EVENT_OBJECT_COUNT * 32

# Scene 300 is the sentinel row that terminates scene 299's event span.
SCENE_INTERVALS: tuple[tuple[int, int], ...] = (
    (1, 20),
    (21, 38),
    (39, 57),
    (58, 76),
    (77, 99),
    (100, 119),
    (120, 143),
    (144, 164),
    (165, 174),
    (175, 195),
    (196, 214),
    (215, 234),
    (235, 257),
    (258, 279),
    (280, 299),
)

CORE_FULL_ARCHIVES: tuple[str, ...] = (
    "DATA",
    "SSS",
    "TEXT",
    "FONT",
    "PAT",
    "MUS",
    "BALL",
    "RGM",
    "F",
    "FIRE",
)
# Global player sprites, script-selected sprites shared by many chapters, and
# one direct ending effect.  Keeping this audited set in core prevents every
# overlay from paying for the same payloads.  MGO #71/#73 are the startup
# crane/title sprites and are persistent for a separate direct call site.
CORE_GLOBAL_MGO = frozenset(
    {
        2,
        3,
        5,
        7,
        26,
        193,
        208,
        228,
        232,
        236,
        242,
        245,
        259,
        273,
        361,
        379,
        385,
        394,
        511,
        521,
        525,
        531,
        532,
        533,
        534,
        538,
        541,
        550,
        563,
        576,
        607,
        627,
        630,
        631,
        632,
    }
)
CORE_STARTUP_MGO = frozenset({71, 73})
CORE_PERSISTENT_MGO = CORE_GLOBAL_MGO | CORE_STARTUP_MGO
OVERLAY_ARCHIVES: tuple[str, ...] = ("ABC", "GOP", "MAP", "MGO")
TF_ARCHIVES: tuple[str, ...] = ("FBP", "RNG")
FULL_MIRROR_ARCHIVES: tuple[str, ...] = tuple(
    name
    for name, _archive_id in sorted(
        pack.ARCHIVE_IDS.items(), key=lambda item: item[1]
    )
    if name not in {"VOC", "CACHE"}
)
ENDING_MGO = frozenset({571, 572, 635})

# PAL_InterpretInstruction normally advances to the next instruction.  These
# operations can branch through the listed operand; following both sides is
# deliberately conservative.
BRANCH_OPERAND = {
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


@dataclass(frozen=True)
class GameTables:
    events: bytes
    scenes: bytes
    objects: bytes
    scripts: tuple[tuple[int, int, int, int], ...]
    enemy_teams: bytes
    player_scene_sprites: frozenset[int]
    source_scene_count: int | None = None

    @property
    def event_count(self) -> int:
        return len(self.events) // 32

    @property
    def scene_count(self) -> int:
        return len(self.scenes) // 8

    @property
    def source_scene_row_count(self) -> int:
        return (
            self.scene_count
            if self.source_scene_count is None
            else self.source_scene_count
        )

    @property
    def object_count(self) -> int:
        return len(self.objects) // 12

    @property
    def enemy_team_count(self) -> int:
        return len(self.enemy_teams) // 10


@dataclass(frozen=True)
class BattleClosure:
    roots: frozenset[int]
    scripts: frozenset[int]
    team_ids: frozenset[int]
    enemy_object_ids: frozenset[int]
    dynamic_enemy_object_ids: frozenset[int]


@dataclass(frozen=True)
class BundleClosure:
    bundle_id: int
    scene_first: int
    scene_last: int
    initial_script_roots: frozenset[int]
    battle: BattleClosure
    map_ids: frozenset[int]
    base_map_ids: frozenset[int]
    dynamic_maps: tuple[tuple[int, int], ...]
    abc_ids: frozenset[int]
    mgo_ids: frozenset[int]
    event_mgo_ids: frozenset[int]
    player_mgo_ids: frozenset[int]
    scripted_mgo_ids: frozenset[int]
    ending_mgo_ids: frozenset[int]
    inbound_cross_scene_scripts: tuple[tuple[int, int, int, int], ...]
    cross_scene_script_targets: tuple[tuple[int, int, int], ...]

    def archive_ids(self, name: str) -> frozenset[int]:
        if name in ("GOP", "MAP"):
            return self.map_ids
        if name == "ABC":
            return self.abc_ids
        if name == "MGO":
            return self.mgo_ids
        raise KeyError(name)


@dataclass(frozen=True)
class ChapterBuild:
    core_pack: bytes
    tf_pack: bytes
    full_pack: bytes
    bundle_packs: tuple[bytes, ...]
    catalog: bytes
    set_file: bytes
    manifest: dict[str, object]


def parse_game_tables(
    data_archives: dict[str, list[pack.Chunk]],
) -> GameTables:
    sss = data_archives["SSS"]
    data = data_archives["DATA"]
    if len(sss) < 5 or len(data) < 4:
        raise ValueError("DATA/SSS archive is missing required chunks")

    events = sss[0].payload
    source_scenes = sss[1].payload
    objects = sss[2].payload
    script_data = sss[4].payload
    enemy_teams = data[2].payload
    player_roles = data[3].payload

    for label, blob, record_size in (
        ("SSS event objects", events, 32),
        ("SSS scenes", source_scenes, 8),
        ("SSS objects", objects, 12),
        ("SSS scripts", script_data, 8),
        ("DATA enemy teams", enemy_teams, 10),
    ):
        if len(blob) % record_size:
            raise ValueError(f"{label} size {len(blob)} is not a multiple of {record_size}")
    if len(player_roles) < 36:
        raise ValueError("short DATA player-role chunk")

    source_scene_count = len(source_scenes) // 8
    scenes = source_scenes
    if source_scene_count == PAL_DOS_SOURCE_SCENE_COUNT:
        last_boundary = struct.unpack_from(
            "<H", source_scenes, (source_scene_count - 1) * 8 + 6
        )[0]
        if last_boundary != len(events) // 32:
            raise ValueError(
                "stock PAL_DOS scene sentinel does not match event count"
            )
        scenes += struct.pack("<HHHH", 0, 0, 0, last_boundary) * (
            CATALOG_SCENE_COUNT - source_scene_count
        )

    scripts = tuple(
        struct.unpack_from("<4H", script_data, offset)
        for offset in range(0, len(script_data), 8)
    )
    player_scene_sprites = frozenset(struct.unpack_from("<6H", player_roles, 24))
    if 0 in player_scene_sprites or 0xFFFF in player_scene_sprites:
        raise ValueError("invalid player scene-sprite number")

    return GameTables(
        events,
        scenes,
        objects,
        scripts,
        enemy_teams,
        player_scene_sprites,
        source_scene_count,
    )


def trace_scripts(
    entries: Sequence[tuple[int, int, int, int]],
    roots: Iterable[int],
    event_limit: int,
) -> set[int]:
    """Return a conservative control-flow and installed-script closure."""

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
        elif operation in BRANCH_OPERAND:
            queued.append(operands[BRANCH_OPERAND[operation]])
        elif operation == 0x00A2:
            queued.extend(
                range(
                    entry_id + 1,
                    min(len(entries), entry_id + max(1, operand0) + 1),
                )
            )

        # Event-object scripts installed for later execution are resources
        # reachable from the same chapter state even without an immediate jump.
        if (
            operation in (0x0024, 0x0025)
            and operand1
            and (operand0 == 0xFFFF or 1 <= operand0 <= event_limit)
        ):
            queued.append(operand1)
        # A chapter can prepare another scene before transitioning to it.  Do
        # not limit this edge to scenes in the current bundle.
        elif operation == 0x006D:
            queued.extend(item for item in (operand1, operand2) if item)
        # Object scripts are mutable too.  Following the installed entry costs
        # no target RAM and prevents an untracked asset edge.
        elif operation == 0x0090 and operand1:
            queued.append(operand1)

    return seen


def object_words(tables: GameTables, object_id: int) -> tuple[int, ...]:
    if not 0 <= object_id < tables.object_count:
        raise ValueError(
            f"enemy object {object_id} is outside SSS object count {tables.object_count}"
        )
    return struct.unpack_from("<6H", tables.objects, object_id * 12)


def resolve_battle_closure(
    tables: GameTables,
    initial_roots: Iterable[int],
) -> BattleClosure:
    """Close battle teams, enemy scripts, summons, and transformations."""

    roots = set(initial_roots)
    previous_scripts: set[int] = set()
    enemy_objects: set[int] = set()
    dynamic_enemy_objects: set[int] = set()
    team_ids: set[int] = set()

    while True:
        scripts = trace_scripts(tables.scripts, roots, tables.event_count)
        next_team_ids = {
            tables.scripts[index][1]
            for index in scripts
            if tables.scripts[index][0] == 7
        }
        invalid_teams = sorted(
            team_id
            for team_id in next_team_ids
            if team_id >= tables.enemy_team_count
        )
        if invalid_teams:
            raise ValueError(
                "script references invalid enemy teams: "
                + ",".join(map(str, invalid_teams))
            )

        next_enemy_objects = set(enemy_objects)
        for team_id in next_team_ids:
            next_enemy_objects.update(
                object_id
                for object_id in struct.unpack_from(
                    "<5H", tables.enemy_teams, team_id * 10
                )
                if object_id not in (0, 0xFFFF)
            )

        next_dynamic = set(dynamic_enemy_objects)
        for index in scripts:
            operation, operand0, _operand1, _operand2 = tables.scripts[index]
            if operation in (0x009E, 0x009F) and operand0 not in (0, 0xFFFF):
                next_enemy_objects.add(operand0)
                next_dynamic.add(operand0)

        next_roots = set(roots)
        for object_id in next_enemy_objects:
            words = object_words(tables, object_id)
            next_roots.update(item for item in words[2:5] if item)

        if (
            scripts == previous_scripts
            and next_team_ids == team_ids
            and next_enemy_objects == enemy_objects
            and next_dynamic == dynamic_enemy_objects
            and next_roots == roots
        ):
            return BattleClosure(
                frozenset(roots),
                frozenset(scripts),
                frozenset(team_ids),
                frozenset(enemy_objects),
                frozenset(dynamic_enemy_objects),
            )

        previous_scripts = scripts
        team_ids = next_team_ids
        enemy_objects = next_enemy_objects
        dynamic_enemy_objects = next_dynamic
        roots = next_roots


def scene_initial_closure(
    tables: GameTables,
    scene_first: int,
    scene_last: int,
) -> tuple[set[int], set[int], set[int]]:
    if not 1 <= scene_first <= scene_last < tables.scene_count:
        raise ValueError(
            f"bad scene interval {scene_first}..{scene_last}; "
            f"scene count including sentinel is {tables.scene_count}"
        )

    roots: set[int] = set()
    map_ids: set[int] = set()
    event_mgo_ids: set[int] = set()
    for scene_id in range(scene_first, scene_last + 1):
        scene_offset = (scene_id - 1) * 8
        map_id, on_enter, on_teleport, event_start = struct.unpack_from(
            "<4H", tables.scenes, scene_offset
        )
        event_end = struct.unpack_from("<H", tables.scenes, scene_offset + 14)[0]
        if event_start > event_end or event_end > tables.event_count:
            raise ValueError(
                f"scene {scene_id} has invalid event span "
                f"{event_start}..{event_end}/{tables.event_count}"
            )

        map_ids.add(map_id)
        roots.update(item for item in (on_enter, on_teleport) if item)
        for event_index in range(event_start, event_end):
            event_offset = event_index * 32
            trigger, automatic = struct.unpack_from(
                "<HH", tables.events, event_offset + 8
            )
            sprite_id = struct.unpack_from("<H", tables.events, event_offset + 16)[0]
            roots.update(item for item in (trigger, automatic) if item)
            if sprite_id:
                event_mgo_ids.add(sprite_id)

    return roots, map_ids, event_mgo_ids


def incoming_scene_script_roots(
    tables: GameTables,
    scene_first: int,
    scene_last: int,
) -> tuple[set[int], tuple[tuple[int, int, int, int], ...]]:
    """Find scripts installed into this interval by any SSS opcode 006d.

    The installing script may belong to a different chapter and does not need
    to be reachable from this interval's default roots.  A save can persist
    that mutation and later enter the target scene, so the target overlay must
    close over every statically named replacement entry point.
    """

    roots: set[int] = set()
    installs: list[tuple[int, int, int, int]] = []
    for entry_id, entry in enumerate(tables.scripts):
        operation, target_scene, on_enter, on_teleport = entry
        if operation != 0x006D or not scene_first <= target_scene <= scene_last:
            continue
        roots.update(item for item in (on_enter, on_teleport) if item)
        installs.append((entry_id, target_scene, on_enter, on_teleport))
    return roots, tuple(installs)


def validate_chunk_ids(
    archive_name: str,
    chunk_ids: Iterable[int],
    chunk_count: int,
) -> None:
    invalid = sorted(
        chunk_id
        for chunk_id in chunk_ids
        if not 0 <= chunk_id < chunk_count
    )
    if invalid:
        raise ValueError(
            f"{archive_name} references outside {chunk_count} chunks: "
            + ",".join(map(str, invalid))
        )


def close_bundle(
    bundle_id: int,
    scene_first: int,
    scene_last: int,
    tables: GameTables,
    archive_counts: dict[str, int],
) -> BundleClosure:
    roots, base_maps, event_mgo = scene_initial_closure(
        tables, scene_first, scene_last
    )
    incoming_roots, incoming_installs = incoming_scene_script_roots(
        tables, scene_first, scene_last
    )
    roots.update(incoming_roots)
    battle = resolve_battle_closure(tables, roots)

    map_ids = set(base_maps)
    dynamic_maps: set[tuple[int, int]] = set()
    scripted_mgo: set[int] = set()
    ending_mgo: set[int] = set()
    cross_scene_targets: set[tuple[int, int, int]] = set()

    for index in battle.scripts:
        operation, operand0, operand1, operand2 = tables.scripts[index]
        if operation == 0x0065 and operand1 not in (0, 0xFFFF):
            scripted_mgo.add(operand1)
        elif operation == 0x006D:
            cross_scene_targets.add((operand0, operand1, operand2))
        elif operation == 0x0099 and operand1 not in (0, 0xFFFF):
            map_ids.add(operand1)
            dynamic_maps.add((operand0, operand1))
        elif operation == 0x00A5 and operand1 not in (0, 0xFFFF):
            scripted_mgo.add(operand1)
        elif operation == 0x0096:
            ending_mgo.update(ENDING_MGO)

    abc_ids = {
        object_words(tables, object_id)[0]
        for object_id in battle.enemy_object_ids
    }
    player_mgo = set(tables.player_scene_sprites)
    mgo_ids = event_mgo | player_mgo | scripted_mgo | ending_mgo
    # These chunks are mapped permanently from pal_core.pak.
    mgo_ids.difference_update(CORE_PERSISTENT_MGO)

    validate_chunk_ids("ABC", abc_ids, archive_counts["ABC"])
    validate_chunk_ids("GOP", map_ids, archive_counts["GOP"])
    validate_chunk_ids("MAP", map_ids, archive_counts["MAP"])
    validate_chunk_ids("MGO", mgo_ids, archive_counts["MGO"])

    return BundleClosure(
        bundle_id,
        scene_first,
        scene_last,
        frozenset(roots),
        battle,
        frozenset(map_ids),
        frozenset(base_maps),
        tuple(sorted(dynamic_maps)),
        frozenset(abc_ids),
        frozenset(mgo_ids),
        frozenset(event_mgo),
        frozenset(player_mgo),
        frozenset(scripted_mgo),
        frozenset(ending_mgo),
        incoming_installs,
        tuple(sorted(cross_scene_targets)),
    )


def sparse_chunks(
    source: Sequence[pack.Chunk],
    selected_ids: Iterable[int],
    archive_name: str,
) -> list[pack.Chunk]:
    selected = frozenset(selected_ids)
    validate_chunk_ids(archive_name, selected, len(source))
    return [
        pack.Chunk(chunk.payload, chunk.fmt, True)
        if chunk_id in selected
        else pack.Chunk(b"", chunk.fmt, False)
        for chunk_id, chunk in enumerate(source)
    ]


def build_overlay_archives(
    source: dict[str, list[pack.Chunk]],
    closure: BundleClosure,
) -> dict[str, list[pack.Chunk]]:
    return {
        name: sparse_chunks(source[name], closure.archive_ids(name), name)
        for name in OVERLAY_ARCHIVES
    }


def make_scene_table(
    intervals: Sequence[tuple[int, int]] = SCENE_INTERVALS,
) -> bytes:
    scene_table = bytearray(b"\xFF" * CATALOG_SCENE_COUNT)
    for bundle_id, (scene_first, scene_last) in enumerate(intervals):
        if bundle_id > 0xFE:
            raise ValueError("too many chapter bundles")
        for scene_id in range(scene_first, scene_last + 1):
            if not 1 <= scene_id < CATALOG_SCENE_COUNT:
                raise ValueError(f"invalid catalog scene {scene_id}")
            if scene_table[scene_id] != 0xFF:
                raise ValueError(f"scene {scene_id} appears in multiple bundles")
            scene_table[scene_id] = bundle_id
    missing = [
        scene_id
        for scene_id in range(1, CATALOG_SCENE_COUNT)
        if scene_table[scene_id] == 0xFF
    ]
    if missing:
        raise ValueError("catalog does not assign scenes: " + ",".join(map(str, missing)))
    return bytes(scene_table)


def compute_chapter_pack_set_id(
    core_base_archives: dict[str, list[pack.Chunk]],
    tf_archives: dict[str, list[pack.Chunk]],
    full_archives: dict[str, list[pack.Chunk]],
    overlay_archives: Sequence[dict[str, list[pack.Chunk]]],
    scene_table: bytes,
) -> int:
    """Identify the logical pack set without a catalog/hash circularity."""

    digest = hashlib.sha256()
    digest.update(b"sdlpal-chapter-pack-set-v1\0")
    digest.update(b"SCENES\0")
    digest.update(scene_table)
    digest.update(b"CORE-BASE\0")
    digest.update(pack.canonical_pack_identity_image(core_base_archives))
    digest.update(b"TF\0")
    digest.update(pack.canonical_pack_identity_image(tf_archives))
    digest.update(b"TF-FULL-MIRROR\0")
    digest.update(pack.canonical_pack_identity_image(full_archives))
    for bundle_id, archives in enumerate(overlay_archives):
        digest.update(f"B{bundle_id:02d}\0".encode("ascii"))
        digest.update(pack.canonical_pack_identity_image(archives))
    value = int.from_bytes(digest.digest()[:4], "little")
    return value if value else 1


def build_catalog(
    pack_set_id: int,
    scene_table: bytes,
    bundle_packs: Sequence[bytes],
) -> bytes:
    if len(scene_table) != CATALOG_SCENE_COUNT:
        raise ValueError(
            f"scene table is {len(scene_table)} bytes, expected {CATALOG_SCENE_COUNT}"
        )
    if not 0 < len(bundle_packs) <= 0xFF:
        raise ValueError("catalog bundle count must be 1..255")

    bundle_table_offset = CATALOG_SCENE_TABLE_OFFSET + len(scene_table)
    total_size = bundle_table_offset + len(bundle_packs) * CATALOG_BUNDLE_DESC_SIZE
    catalog = bytearray(
        struct.pack(
            "<4sHHIHHIIII",
            CATALOG_MAGIC,
            CATALOG_VERSION,
            CATALOG_HEADER_SIZE,
            pack_set_id,
            CATALOG_SCENE_COUNT,
            len(bundle_packs),
            CATALOG_SCENE_TABLE_OFFSET,
            bundle_table_offset,
            total_size,
            0,
        )
    )
    catalog += scene_table
    for bundle_id, bundle_image in enumerate(bundle_packs):
        catalog += struct.pack(
            "<B3xI32s",
            bundle_id,
            len(bundle_image),
            hashlib.sha256(bundle_image).digest(),
        )
    if len(catalog) != total_size:
        raise AssertionError("catalog size accounting error")
    struct.pack_into(
        "<I",
        catalog,
        CATALOG_CRC32_OFFSET,
        zlib.crc32(catalog) & 0xFFFFFFFF,
    )
    verify_catalog(bytes(catalog), pack_set_id)
    return bytes(catalog)


def verify_catalog(catalog: bytes, expected_pack_set_id: int | None = None) -> None:
    if len(catalog) < CATALOG_HEADER_SIZE:
        raise ValueError("short chapter catalog")
    (
        magic,
        version,
        header_size,
        pack_set_id,
        scene_count,
        bundle_count,
        scene_table_offset,
        bundle_table_offset,
        total_size,
        declared_crc,
    ) = struct.unpack_from("<4sHHIHHIIII", catalog)
    if magic != CATALOG_MAGIC or version != CATALOG_VERSION:
        raise ValueError("bad chapter catalog magic/version")
    if header_size != CATALOG_HEADER_SIZE:
        raise ValueError(f"bad chapter catalog header size {header_size}")
    if expected_pack_set_id is not None and pack_set_id != expected_pack_set_id:
        raise ValueError("chapter catalog pack-set ID mismatch")
    if scene_count != CATALOG_SCENE_COUNT:
        raise ValueError(f"bad chapter catalog scene count {scene_count}")
    if scene_table_offset != CATALOG_SCENE_TABLE_OFFSET:
        raise ValueError("bad chapter catalog scene-table offset")
    if bundle_table_offset != scene_table_offset + scene_count:
        raise ValueError("bad chapter catalog bundle-table offset")
    if total_size != len(catalog):
        raise ValueError(
            f"chapter catalog size field is {total_size}, actual {len(catalog)}"
        )
    if total_size != bundle_table_offset + bundle_count * CATALOG_BUNDLE_DESC_SIZE:
        raise ValueError("bad chapter catalog descriptor span")

    crc_image = bytearray(catalog)
    struct.pack_into("<I", crc_image, CATALOG_CRC32_OFFSET, 0)
    actual_crc = zlib.crc32(crc_image) & 0xFFFFFFFF
    if declared_crc == 0 or declared_crc != actual_crc:
        raise ValueError(
            f"chapter catalog CRC32 is {declared_crc:#010x}, "
            f"expected {actual_crc:#010x}"
        )

    scene_table = catalog[scene_table_offset:bundle_table_offset]
    if scene_table[0] != 0xFF:
        raise ValueError("chapter catalog scene 0 must be unassigned")
    valid_ids = set(range(bundle_count))
    invalid_scenes = [
        scene_id
        for scene_id, bundle_id in enumerate(scene_table[1:], 1)
        if bundle_id not in valid_ids
    ]
    if invalid_scenes:
        raise ValueError(
            "chapter catalog has invalid scene mappings: "
            + ",".join(map(str, invalid_scenes))
        )

    for descriptor_index in range(bundle_count):
        offset = bundle_table_offset + descriptor_index * CATALOG_BUNDLE_DESC_SIZE
        bundle_id = catalog[offset]
        reserved = catalog[offset + 1 : offset + 4]
        pack_size = struct.unpack_from("<I", catalog, offset + 4)[0]
        digest = catalog[offset + 8 : offset + 40]
        if bundle_id != descriptor_index or reserved != b"\0\0\0":
            raise ValueError(f"bad chapter bundle descriptor {descriptor_index}")
        if pack_size == 0 or digest == b"\0" * 32:
            raise ValueError(f"empty chapter bundle descriptor {descriptor_index}")


def build_set_file(
    pack_set_id: int,
    core_pack: bytes,
    catalog: bytes,
) -> bytes:
    """Build the small TF bootstrap record consumed before NOR is mapped."""

    verify_catalog(catalog, pack_set_id)
    if (
        not core_pack
        or len(core_pack) > CORE_SLOT_CAP
        or len(core_pack) % 4 != 0
    ):
        raise ValueError("core pack does not fit the Cardputer core slot")
    if pack.u32(core_pack, pack.PACK_SET_ID_OFFSET) != pack_set_id:
        raise ValueError("core pack and chapter catalog set IDs differ")

    total_size = SET_HEADER_SIZE + len(catalog)
    image = bytearray(SET_HEADER_SIZE)
    struct.pack_into(
        "<4sHHIIIIII",
        image,
        0,
        SET_MAGIC,
        SET_VERSION,
        SET_HEADER_SIZE,
        total_size,
        pack_set_id,
        len(core_pack),
        SET_HEADER_SIZE,
        len(catalog),
        0,
    )
    image[SET_CORE_SHA256_OFFSET:SET_HEADER_SIZE] = hashlib.sha256(
        core_pack
    ).digest()
    image += catalog
    struct.pack_into(
        "<I",
        image,
        SET_CRC32_OFFSET,
        zlib.crc32(image) & 0xFFFFFFFF,
    )
    verify_set_file(bytes(image), core_pack)
    return bytes(image)


def verify_set_file(
    image: bytes,
    core_pack: bytes | None = None,
) -> None:
    if len(image) < SET_HEADER_SIZE:
        raise ValueError("short Cardputer data-set record")
    (
        magic,
        version,
        header_size,
        total_size,
        pack_set_id,
        core_size,
        catalog_offset,
        catalog_size,
        declared_crc,
    ) = struct.unpack_from("<4sHHIIIIII", image)
    if magic != SET_MAGIC or version != SET_VERSION:
        raise ValueError("bad Cardputer data-set magic/version")
    if header_size != SET_HEADER_SIZE or total_size != len(image):
        raise ValueError("bad Cardputer data-set header/total size")
    if (
        pack_set_id == 0
        or core_size == 0
        or core_size > CORE_SLOT_CAP
        or core_size % 4 != 0
    ):
        raise ValueError("bad Cardputer data-set core descriptor")
    if catalog_offset != SET_HEADER_SIZE or (
        catalog_size != total_size - catalog_offset
    ):
        raise ValueError("bad Cardputer data-set catalog span")
    core_sha256 = image[SET_CORE_SHA256_OFFSET:SET_HEADER_SIZE]
    if core_sha256 == b"\0" * 32:
        raise ValueError("empty Cardputer data-set core SHA-256")
    crc_image = bytearray(image)
    struct.pack_into("<I", crc_image, SET_CRC32_OFFSET, 0)
    actual_crc = zlib.crc32(crc_image) & 0xFFFFFFFF
    if declared_crc == 0 or declared_crc != actual_crc:
        raise ValueError("bad Cardputer data-set CRC32")
    verify_catalog(
        image[catalog_offset : catalog_offset + catalog_size],
        pack_set_id,
    )
    if core_pack is not None:
        if len(core_pack) != core_size:
            raise ValueError("Cardputer data-set core size mismatch")
        if hashlib.sha256(core_pack).digest() != core_sha256:
            raise ValueError("Cardputer data-set core SHA-256 mismatch")
        if pack.u32(core_pack, pack.PACK_SET_ID_OFFSET) != pack_set_id:
            raise ValueError("Cardputer data-set core pack ID mismatch")


def compact_pack_summary(
    filename: str,
    image: bytes,
    archives: dict[str, list[pack.Chunk]],
) -> dict[str, object]:
    return {
        "filename": filename,
        "size": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
        "crc32": pack.u32(image, pack.PACK_CRC32_OFFSET),
        "pack_set_id": pack.u32(image, pack.PACK_SET_ID_OFFSET),
        "toc_bytes": pack.u32(image, 16),
        "archives": {
            name: {
                "source_chunk_count": len(chunks),
                "present_chunk_count": sum(chunk.present for chunk in chunks),
                "present_payload_bytes": sum(
                    len(chunk.payload) for chunk in chunks if chunk.present
                ),
                "present_chunk_ids": [
                    index for index, chunk in enumerate(chunks) if chunk.present
                ],
                "present_chunk_payload_bytes": [
                    len(chunk.payload) for chunk in chunks if chunk.present
                ],
            }
            for name, chunks in sorted(
                archives.items(), key=lambda item: pack.ARCHIVE_IDS[item[0]]
            )
        },
    }


def bundle_audit(
    closure: BundleClosure,
    summary: dict[str, object],
    soft_cap: int,
) -> dict[str, object]:
    size = int(summary["size"])
    return {
        "id": closure.bundle_id,
        "filename": summary["filename"],
        "scene_first": closure.scene_first,
        "scene_last": closure.scene_last,
        "size": size,
        "sha256": summary["sha256"],
        "soft_cap_bytes": soft_cap,
        "within_soft_cap": size <= soft_cap,
        "over_soft_cap_bytes": max(0, size - soft_cap),
        "selection": {
            name: {
                "chunk_ids": sorted(closure.archive_ids(name)),
                "chunk_ranges": pack.ranges_from_ids(
                    sorted(closure.archive_ids(name))
                ),
            }
            for name in OVERLAY_ARCHIVES
        },
        "closure": {
            "initial_script_root_count": len(closure.initial_script_roots),
            "fixed_point_script_root_count": len(closure.battle.roots),
            "reachable_script_entry_count": len(closure.battle.scripts),
            "battle_team_ids": sorted(closure.battle.team_ids),
            "enemy_object_ids": sorted(closure.battle.enemy_object_ids),
            "dynamic_enemy_object_ids_009e_009f": sorted(
                closure.battle.dynamic_enemy_object_ids
            ),
            "base_map_ids": sorted(closure.base_map_ids),
            "dynamic_maps_0099": [
                {"target_scene": scene_id, "map_id": map_id}
                for scene_id, map_id in closure.dynamic_maps
            ],
            "event_mgo_ids": sorted(closure.event_mgo_ids),
            "player_mgo_ids": sorted(closure.player_mgo_ids),
            "scripted_mgo_ids_0065_00a5": sorted(closure.scripted_mgo_ids),
            "ending_mgo_ids": sorted(closure.ending_mgo_ids),
            "inbound_cross_scene_006d_scripts": [
                {
                    "source_script_entry": entry_id,
                    "target_scene": scene_id,
                    "on_enter": on_enter,
                    "on_teleport": on_teleport,
                }
                for entry_id, scene_id, on_enter, on_teleport
                in closure.inbound_cross_scene_scripts
            ],
            "cross_scene_006d_targets": [
                {
                    "target_scene": scene_id,
                    "on_enter": on_enter,
                    "on_teleport": on_teleport,
                }
                for scene_id, on_enter, on_teleport
                in closure.cross_scene_script_targets
            ],
        },
    }


def build_chapter_packs(
    data_dir: Path,
    soft_cap: int = SOFT_OVERLAY_CAP,
    font10_archive: Path | None = None,
) -> ChapterBuild:
    if soft_cap <= 0:
        raise ValueError("soft overlay cap must be positive")
    if font10_archive is None:
        raise ValueError("pinned FONT10 release archive is required")
    font10_chunk, font10_summary = pack.build_font10_archive_chunk(
        data_dir,
        font10_archive,
    )
    source_names = (
        *FULL_MIRROR_ARCHIVES,
    )
    source: dict[str, list[pack.Chunk]] = {}
    for name in source_names:
        if name not in source:
            source[name] = pack.load_archive(
                data_dir,
                name,
                font10_chunk if name == "FONT" else None,
            )

    font_chunks = source.get("FONT")
    if (
        font_chunks is None
        or len(font_chunks) != 2
        or font_chunks[1] != font10_chunk
    ):
        raise AssertionError("FONT10 must be FONT chunk 1")

    tables = parse_game_tables(source)
    if tables.scene_count != CATALOG_SCENE_COUNT:
        raise ValueError(
            f"real scene table has {tables.scene_count} rows, "
            f"catalog requires {CATALOG_SCENE_COUNT}"
        )
    archive_counts = {name: len(source[name]) for name in OVERLAY_ARCHIVES}
    closures = tuple(
        close_bundle(bundle_id, scene_first, scene_last, tables, archive_counts)
        for bundle_id, (scene_first, scene_last) in enumerate(SCENE_INTERVALS)
    )
    source_006d_entries = {
        entry_id
        for entry_id, entry in enumerate(tables.scripts)
        if entry[0] == 0x006D and 1 <= entry[1] <= 299
    }
    assigned_006d_entries = {
        entry_id
        for closure in closures
        for entry_id, _scene_id, _on_enter, _on_teleport
        in closure.inbound_cross_scene_scripts
    }
    if assigned_006d_entries != source_006d_entries:
        raise AssertionError(
            "opcode 006d target-bundle assignment is incomplete: "
            f"source={len(source_006d_entries)} "
            f"assigned={len(assigned_006d_entries)}"
        )

    core_base_archives = {
        name: list(source[name])
        for name in CORE_FULL_ARCHIVES
    }
    core_base_archives["FONT"] = sparse_chunks(
        source["FONT"], {1}, "FONT"
    )
    # Keep the historic 423-record core SSS prefix for the always-mapped early
    # chapter, while EVENT.DEF/EVENT.STA provide the complete paged mutable
    # table. The full SSS source remains in pal_full.pak for closure analysis.
    core_sss = list(core_base_archives["SSS"])
    if len(core_sss[0].payload) < CORE_EVENT_OBJECT_BYTES:
        raise ValueError(
            f"SSS event-object chunk is {len(core_sss[0].payload)} bytes, "
            f"needs at least {CORE_EVENT_OBJECT_BYTES}"
        )
    core_sss[0] = pack.Chunk(
        core_sss[0].payload[:CORE_EVENT_OBJECT_BYTES],
        core_sss[0].fmt,
    )
    core_base_archives["SSS"] = core_sss
    core_base_archives["MGO"] = sparse_chunks(
        source["MGO"], CORE_PERSISTENT_MGO, "MGO"
    )
    tf_archives = {name: list(source[name]) for name in TF_ARCHIVES}
    full_archives = {
        name: list(source[name])
        for name in FULL_MIRROR_ARCHIVES
    }
    overlay_sets = tuple(
        build_overlay_archives(source, closure)
        for closure in closures
    )
    scene_table = make_scene_table()
    pack_set_id = compute_chapter_pack_set_id(
        core_base_archives,
        tf_archives,
        full_archives,
        overlay_sets,
        scene_table,
    )

    bundle_images = tuple(
        pack.build_pack(archives, pack_set_id)
        for archives in overlay_sets
    )
    tf_image = pack.build_pack(tf_archives, pack_set_id)
    full_image = pack.build_pack(full_archives, pack_set_id)
    for image in (*bundle_images, tf_image, full_image):
        pack.verify_pack(image)

    catalog = build_catalog(pack_set_id, scene_table, bundle_images)
    core_archives = dict(core_base_archives)
    core_archives["CACHE"] = [pack.Chunk(catalog, pack.FORMAT_RAW)]
    core_image = pack.build_pack(core_archives, pack_set_id)
    pack.verify_pack(core_image)
    set_file = build_set_file(pack_set_id, core_image, catalog)
    oversized = [
        (bundle_id, len(image))
        for bundle_id, image in enumerate(bundle_images)
        if len(image) > soft_cap
    ]
    if oversized:
        raise ValueError(
            f"overlay pack exceeds conservative cap {soft_cap:#x}: "
            + ", ".join(
                f"b{bundle_id:02d}={size:#x}"
                for bundle_id, size in oversized
            )
        )
    if any(len(image) > HARD_OVERLAY_PACK_CAP for image in bundle_images):
        raise AssertionError("overlay exceeds 0x2cf000 cache payload hard limit")
    if len(core_image) > CORE_SLOT_CAP:
        raise ValueError(
            f"pal_core.pak is {len(core_image):#x}, exceeds core slot "
            f"{CORE_SLOT_CAP:#x}"
        )

    core_summary = compact_pack_summary("pal_core.pak", core_image, core_archives)
    tf_summary = compact_pack_summary("pal_tf.pak", tf_image, tf_archives)
    full_summary = compact_pack_summary(
        "pal_full.pak", full_image, full_archives
    )
    bundle_summaries = [
        compact_pack_summary(f"b{bundle_id:02d}.pak", image, archives)
        for bundle_id, (image, archives) in enumerate(
            zip(bundle_images, overlay_sets)
        )
    ]
    bundles = [
        bundle_audit(closure, summary, soft_cap)
        for closure, summary in zip(closures, bundle_summaries)
    ]
    manifest: dict[str, object] = {
        "schema": "sdlpal-embedded-chapter-pack-manifest",
        "version": 1,
        "data_dir": str(data_dir.resolve()),
        "runtime": {
            "heap_required": False,
            "runtime_decompression_required": False,
            "payloads_are_runtime_native": True,
            "tf_access_shape": "sequential decoded/native chunk reads",
            "overlay_shape": "one replaceable SPI-NOR bundle",
            "data_identity_file": SET_FILENAME,
            "core_tf_file": "pal_core.pak",
            "firmware_embeds_data_hashes": False,
        },
        "pack_set": {
            "id": pack_set_id,
            "id_hex": f"0x{pack_set_id:08x}",
            "identity": (
                "SHA-256 low 32 bits over scene table and canonical "
                "CORE-BASE/TF/TF-FULL-MIRROR/B00..B14 pack images with "
                "header pack_set_id and CRC32 zeroed; zero is remapped to one"
            ),
        },
        "scene_partition": {
            "catalog_scene_table_entries": CATALOG_SCENE_COUNT,
            "catalog_scene_zero_is_reserved": True,
            "playable_scene_first": 1,
            "playable_scene_last": 299,
            "source_scene_row_count": tables.source_scene_row_count,
            "source_scene_row_300_is_sentinel": True,
            "intervals": [
                {
                    "bundle_id": bundle_id,
                    "scene_first": scene_first,
                    "scene_last": scene_last,
                }
                for bundle_id, (scene_first, scene_last)
                in enumerate(SCENE_INTERVALS)
            ],
        },
        "closure_audit": {
            "source_opcode_006d_count": len(source_006d_entries),
            "assigned_inbound_opcode_006d_count": len(assigned_006d_entries),
            "all_opcode_006d_target_scripts_assigned": True,
            "battle_enemy_fixed_point": True,
            "dynamic_enemy_opcodes": ["009e", "009f"],
            "dynamic_map_opcode": "0099",
            "script_mgo_opcodes": ["0065", "00a5"],
            "direct_ending_mgo_ids": sorted(ENDING_MGO),
            "core_global_mgo_ids": sorted(CORE_GLOBAL_MGO),
            "core_startup_mgo_ids": sorted(CORE_STARTUP_MGO),
        },
        "core_event_object_window": {
            "status": "full-event-pager-active",
            "record_bytes": 32,
            "core_record_count": CORE_EVENT_OBJECT_COUNT,
            "core_chunk_bytes": CORE_EVENT_OBJECT_BYTES,
            "full_source_record_count": tables.event_count,
            "full_source_chunk_bytes": len(source["SSS"][0].payload),
            "current_runtime_scene_window": "scenes 1..22",
            "outside_window_behavior": (
                "bundle cache plus EVENT.DEF/EVENT.STA paging"
            ),
        },
        "catalog": {
            "archive": "CACHE",
            "archive_id": pack.ARCHIVE_IDS["CACHE"],
            "chunk_id": 0,
            "format": "RAW",
            "size": len(catalog),
            "sha256": hashlib.sha256(catalog).hexdigest(),
            "crc32": struct.unpack_from("<I", catalog, CATALOG_CRC32_OFFSET)[0],
            "magic": CATALOG_MAGIC.decode("ascii"),
            "header_size": CATALOG_HEADER_SIZE,
            "scene_table_offset": CATALOG_SCENE_TABLE_OFFSET,
            "bundle_table_offset": CATALOG_SCENE_TABLE_OFFSET
            + CATALOG_SCENE_COUNT,
            "bundle_descriptor_size": CATALOG_BUNDLE_DESC_SIZE,
            "bundle_filename_pattern": "b%02u.pak",
            "scene_zero_bundle": 0xFF,
        },
        "set_file": {
            "filename": SET_FILENAME,
            "magic": SET_MAGIC.decode("ascii"),
            "version": SET_VERSION,
            "header_size": SET_HEADER_SIZE,
            "size": len(set_file),
            "sha256": hashlib.sha256(set_file).hexdigest(),
            "crc32": struct.unpack_from("<I", set_file, SET_CRC32_OFFSET)[0],
            "core_size": len(core_image),
            "core_sha256": hashlib.sha256(core_image).hexdigest(),
            "catalog_offset": SET_HEADER_SIZE,
            "catalog_size": len(catalog),
        },
        "source_files": pack.source_file_manifest(
            data_dir,
            list(dict.fromkeys(source_names)),
        ),
        "packs": {
            "core": core_summary,
            "tf": tf_summary,
            "full": {
                **full_summary,
                "runtime_active": False,
                "all_chunks": True,
                "allow_overlap": True,
                "index_strategy": "offline-mirror-not-runtime-indexed",
            },
            "bundles": bundles,
        },
        "overlay_soft_cap": {
            "bytes": soft_cap,
            "hex": f"0x{soft_cap:x}",
            "hard_pack_bytes": HARD_OVERLAY_PACK_CAP,
            "hard_pack_hex": f"0x{HARD_OVERLAY_PACK_CAP:x}",
            "largest_bundle_bytes": max(map(len, bundle_images)),
            "over_cap_bundle_ids": [
                bundle_id
                for bundle_id, image in enumerate(bundle_images)
                if len(image) > soft_cap
            ],
            "semantics": "strict whole-pack limit; no source chunk is dropped",
        },
        "core_slot": {
            "bytes": CORE_SLOT_CAP,
            "hex": f"0x{CORE_SLOT_CAP:x}",
            "pack_bytes": len(core_image),
            "remaining_bytes": CORE_SLOT_CAP - len(core_image),
        },
    }
    manifest["font10"] = font10_summary
    return ChapterBuild(
        core_image,
        tf_image,
        full_image,
        bundle_images,
        catalog,
        set_file,
        manifest,
    )


def write_chapter_build(
    build: ChapterBuild,
    out_dir: Path,
    manifest_path: Path | None = None,
) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "pal_core.pak").write_bytes(build.core_pack)
    (out_dir / "pal_tf.pak").write_bytes(build.tf_pack)
    (out_dir / "pal_full.pak").write_bytes(build.full_pack)
    (out_dir / SET_FILENAME).write_bytes(build.set_file)
    for bundle_id, image in enumerate(build.bundle_packs):
        (out_dir / f"b{bundle_id:02d}.pak").write_bytes(image)

    if manifest_path is None:
        manifest_path = out_dir / "chapter_manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(build.manifest, indent=2, sort_keys=True) + "\n"
    )
    return manifest_path


def print_audit(build: ChapterBuild) -> None:
    manifest = build.manifest
    pack_set = manifest["pack_set"]
    packs = manifest["packs"]
    print(
        f"pack_set_id={pack_set['id_hex']} "
        f"core={len(build.core_pack)} tf={len(build.tf_pack)} "
        f"full={len(build.full_pack)} "
        f"catalog={len(build.catalog)}"
    )
    for bundle in packs["bundles"]:
        marker = "ok" if bundle["within_soft_cap"] else (
            f"+{bundle['over_soft_cap_bytes']}"
        )
        print(
            f"b{bundle['id']:02d}.pak "
            f"scenes={bundle['scene_first']:03d}-{bundle['scene_last']:03d} "
            f"bytes={bundle['size']} cap={marker} "
            f"sha256={bundle['sha256']}"
        )


def int_auto(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data_dir", type=Path)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument(
        "--manifest",
        type=Path,
        help="manifest path (default: OUT_DIR/chapter_manifest.json)",
    )
    parser.add_argument(
        "--font10-archive",
        type=Path,
        required=True,
        help=(
            "verified Fusion Pixel Font 10px monospaced BDF release zip; "
            "adds corpus-subsetted FONT chunk 1 to pal_core.pak"
        ),
    )
    parser.add_argument(
        "--soft-cap",
        type=int_auto,
        default=SOFT_OVERLAY_CAP,
        help="overlay audit target in bytes (default: 0x2bf000)",
    )
    parser.add_argument(
        "--audit-only",
        action="store_true",
        help="build and verify in memory without writing artifacts",
    )
    args = parser.parse_args()

    build = build_chapter_packs(
        args.data_dir,
        args.soft_cap,
        args.font10_archive,
    )
    print_audit(build)
    if not args.audit_only:
        manifest_path = write_chapter_build(build, args.out_dir, args.manifest)
        print(
            f"wrote {len(build.bundle_packs) + 3} packs, {SET_FILENAME}, "
            f"and {manifest_path}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
