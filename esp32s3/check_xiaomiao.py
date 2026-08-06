#!/usr/bin/env python3
"""Audit the ESP32-WROVER-B Xiaomiao SD-only firmware and data set."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path


PACK_MAGIC = 0x4B504C50
PACK_VERSION = 1
PACK_HEADER_BYTES = 32
ARCHIVE_ENTRY_BYTES = 12
CHUNK_ENTRY_BYTES = 16
PACK_FORMAT_RNG_FRAMES = 2
PACK_FORMAT_NATIVE = 1
PACK_FORMAT_FONT10 = 6
RESIDENT_MAX_BYTES = 2048 * 1024
TF_TOC_MAX_BYTES = 40 * 1024
TRANSIENT_CHUNK_BYTES = 64 * 1024
RNG_INPUT_WINDOW_BYTES = 160 * 128
SCENE_ARENA_BYTES = 256 * 1024
PLAYER_ARENA_BYTES = 128 * 1024
BATTLE_ARENA_BYTES = 512 * 1024
FIGHT_EFFECT_BYTES = 64 * 1024
FIGHT_SUMMON_BYTES = 64 * 1024
FLASH_BYTES = 4 * 1024 * 1024
APP_OFFSET = 0x10000
APP_PARTITION_BYTES = 0x300000
MAPPED_PSRAM_LIMIT = 0x003C0000
ESP32_IRAM_START = 0x40080000
ESP32_IRAM_END = 0x400A0000
MAIN_TASK_STACK_BYTES = 16 * 1024
MIN_LINKER_DRAM_RESERVE = 80 * 1024
MIN_POST_MAIN_STACK_RESERVE = 64 * 1024
MAX_STATIC_STACK_BYTES = 2048

ARCHIVE_IDS = {
    "ABC": 1,
    "BALL": 2,
    "DATA": 3,
    "F": 4,
    "FBP": 5,
    "FIRE": 6,
    "GOP": 7,
    "MAP": 8,
    "MGO": 9,
    "MIDI": 10,
    "MUS": 11,
    "PAT": 12,
    "RGM": 13,
    "RNG": 14,
    "SSS": 15,
    "TEXT": 17,
    "FONT": 18,
    "SFX": 19,
}
RESIDENT_ARCHIVES = {"DATA", "PAT", "RGM", "SSS", "TEXT", "FONT", "MUS"}
FULL_ARCHIVES = set(ARCHIVE_IDS)
DIRECT_STAGED_ARCHIVES = {
    "ABC", "BALL", "F", "FIRE", "GOP", "MAP", "MGO",
}

PSRAM_SYMBOLS = {
    "pal_sram_framebuffer": 160 * 128,
    "pal_sram_aux_framebuffer": 160 * 128,
    "pal_mem_level2_resident_pack": RESIDENT_MAX_BYTES,
    "pal_mem_level2_tf_toc": TF_TOC_MAX_BYTES,
    "pal_mem_level2_transient_chunk": TRANSIENT_CHUNK_BYTES,
    "pal_mem_level2_scene_arena": SCENE_ARENA_BYTES,
    "pal_mem_level2_player_arena": PLAYER_ARENA_BYTES,
    "pal_mem_level2_battle_arena": BATTLE_ARENA_BYTES,
    "pal_mem_level2_fight_effect": FIGHT_EFFECT_BYTES,
    "pal_mem_level2_fight_summon": FIGHT_SUMMON_BYTES,
    "pal_psram_global_event_objects": 176000,
    "pal_psram_global_magics": 3648,
    "pal_psram_res_state": 36,
    "pal_psram_res_event_sprite_ptrs": 22000,
    "pal_psram_savegame_static": 190064,
}
SRAM_SYMBOLS = {
    "pal_sram_display_dma": 4 * 1024,
    "pal_sram_fbp_scanline": 320,
}

FORBIDDEN_UNDEFINED = {
    "malloc", "calloc", "realloc", "free", "aligned_alloc", "memalign",
    "pvPortMalloc", "vPortFree", "heap_caps_malloc", "heap_caps_calloc",
    "heap_caps_realloc", "heap_caps_free", "PAL_MKFDecompressChunk",
    "PAL_MKFGetDecompressedSize", "YJ1_Decompress", "YJ2_Decompress",
    "Decompress",
}


@dataclass(frozen=True)
class Chunk:
    offset: int
    size: int
    fmt: int


@dataclass(frozen=True)
class Pack:
    size: int
    toc_bytes: int
    set_id: int
    crc32: int
    archives: dict[int, list[Chunk]]


def run_text(argv: list[str | Path]) -> str:
    return subprocess.check_output(
        [str(item) for item in argv], text=True, stderr=subprocess.STDOUT
    )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_pack(path: Path, errors: list[str]) -> Pack | None:
    data = path.read_bytes()
    if len(data) < PACK_HEADER_BYTES:
        errors.append(f"{path}: short pack header")
        return None
    (
        magic, version, header_bytes, archive_count, reserved,
        archive_table, data_offset, set_id, declared_size, declared_crc,
    ) = struct.unpack_from("<IHHHHIIIII", data, 0)
    if magic != PACK_MAGIC or version != PACK_VERSION:
        errors.append(f"{path}: unsupported pack magic/version")
    if header_bytes != PACK_HEADER_BYTES or reserved != 0:
        errors.append(f"{path}: invalid pack header geometry")
    if set_id == 0:
        errors.append(f"{path}: zero pack-set ID")
    if declared_size != len(data):
        errors.append(f"{path}: declared size {declared_size} != {len(data)}")
    crc_image = bytearray(data)
    struct.pack_into("<I", crc_image, 28, 0)
    actual_crc = zlib.crc32(crc_image) & 0xFFFFFFFF
    if declared_crc == 0 or declared_crc != actual_crc:
        errors.append(
            f"{path}: CRC32 {declared_crc:#010x} != {actual_crc:#010x}"
        )
    archive_table_end = archive_table + archive_count * ARCHIVE_ENTRY_BYTES
    if (
        archive_table < PACK_HEADER_BYTES
        or archive_table_end > data_offset
        or data_offset > len(data)
        or data_offset % 4
    ):
        errors.append(f"{path}: invalid archive table/data offset")
        return None

    archives: dict[int, list[Chunk]] = {}
    payload_ranges: list[tuple[int, int, int, int]] = []
    for archive_index in range(archive_count):
        entry = archive_table + archive_index * ARCHIVE_ENTRY_BYTES
        archive_id, chunk_count = struct.unpack_from("<HH", data, entry)
        chunk_table = struct.unpack_from("<I", data, entry + 4)[0]
        if archive_id in archives:
            errors.append(f"{path}: duplicate archive ID {archive_id}")
            continue
        if (
            chunk_table < archive_table_end
            or chunk_table + chunk_count * CHUNK_ENTRY_BYTES > data_offset
        ):
            errors.append(f"{path}: archive {archive_id} chunk table is invalid")
            continue
        chunks: list[Chunk] = []
        archives[archive_id] = chunks
        for chunk_id in range(chunk_count):
            pos = chunk_table + chunk_id * CHUNK_ENTRY_BYTES
            offset, size, fmt, flags = struct.unpack_from("<IIHH", data, pos)
            if flags != 0:
                errors.append(
                    f"{path}: archive {archive_id} chunk {chunk_id} "
                    f"has runtime flags {flags:#06x}"
                )
            if size:
                if offset < data_offset or offset + size > len(data):
                    errors.append(
                        f"{path}: archive {archive_id} chunk {chunk_id} out of range"
                    )
                else:
                    payload_ranges.append((offset, offset + size, archive_id, chunk_id))
            chunks.append(Chunk(offset, size, fmt))

    payload_ranges.sort()
    for previous, current in zip(payload_ranges, payload_ranges[1:]):
        if current[0] < previous[1]:
            errors.append(
                f"{path}: payload overlap archive/chunk "
                f"{previous[2]}/{previous[3]} and {current[2]}/{current[3]}"
            )
    return Pack(len(data), data_offset, set_id, declared_crc, archives)


def audit_rng_frames(path: Path, pack: Pack, errors: list[str]) -> tuple[int, int]:
    data = path.read_bytes()
    maximum = 0
    total = 0
    for movie_id, chunk in enumerate(pack.archives.get(ARCHIVE_IDS["RNG"], [])):
        if chunk.size == 0:
            continue
        if chunk.fmt != PACK_FORMAT_RNG_FRAMES or chunk.size < 4:
            errors.append(f"RNG movie {movie_id} has invalid native format")
            continue
        payload = memoryview(data)[chunk.offset:chunk.offset + chunk.size]
        frame_count = struct.unpack_from("<I", payload, 0)[0]
        table_bytes = 4 + (frame_count + 1) * 4
        if table_bytes > len(payload):
            errors.append(f"RNG movie {movie_id} has a truncated frame table")
            continue
        offsets = [
            struct.unpack_from("<I", payload, 4 + index * 4)[0]
            for index in range(frame_count + 1)
        ]
        if offsets[0] < table_bytes or offsets[-1] > len(payload) or any(
            end < start for start, end in zip(offsets, offsets[1:])
        ):
            errors.append(f"RNG movie {movie_id} has invalid frame offsets")
            continue
        for _frame_id, (start, end) in enumerate(zip(offsets, offsets[1:])):
            frame_bytes = end - start
            total += 1
            maximum = max(maximum, frame_bytes)
    return maximum, total


def parse_objdump_symbols(text: str) -> dict[str, tuple[int, str]]:
    result: dict[str, tuple[int, str]] = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 6 or not fields[3].startswith("."):
            continue
        try:
            size = int(fields[4], 16)
        except ValueError:
            continue
        result[fields[5]] = (size, fields[3])
    return result


def parse_objdump_symbol_addresses(text: str) -> dict[str, tuple[int, str]]:
    result: dict[str, tuple[int, str]] = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 6 or not fields[3].startswith("."):
            continue
        try:
            address = int(fields[0], 16)
        except ValueError:
            continue
        result[fields[5]] = (address, fields[3])
    return result


def parse_sections(text: str) -> dict[str, tuple[int, int]]:
    sections: dict[str, tuple[int, int]] = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 4 and fields[0].isdigit():
            try:
                sections[fields[1]] = (int(fields[2], 16), int(fields[3], 16))
            except ValueError:
                pass
    return sections


def parse_memory_regions(text: str) -> dict[str, tuple[int, int]]:
    regions: dict[str, tuple[int, int]] = {}
    active = False
    pattern = re.compile(r"^(\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)")
    for line in text.splitlines():
        if line.strip() == "Memory Configuration":
            active = True
            continue
        if active and line.strip() == "Linker script and memory map":
            break
        if active:
            match = pattern.match(line.strip())
            if match and match.group(1) != "Name":
                regions[match.group(1)] = (
                    int(match.group(2), 16), int(match.group(3), 16)
                )
    return regions


def config_enabled(config: str, name: str) -> bool:
    return re.search(rf"^{re.escape(name)}=y$", config, re.MULTILINE) is not None


def config_disabled(config: str, name: str) -> bool:
    return re.search(
        rf"^# {re.escape(name)} is not set$", config, re.MULTILINE
    ) is not None


def aligned_total(sizes: list[int]) -> int:
    used = 0
    for size in sizes:
        used = (used + 3) & ~3
        used += size
    return used


def resident_pack_bytes(full: Pack, errors: list[str]) -> int:
    """Size of the fixed PSRAM pack reconstructed from pal_full.pak."""
    archive_ids = sorted(ARCHIVE_IDS[name] for name in RESIDENT_ARCHIVES)
    missing = [archive_id for archive_id in archive_ids if archive_id not in full.archives]
    if missing:
        errors.append(f"pal_full.pak lacks resident archives {missing}")
        return 0
    used = PACK_HEADER_BYTES + len(archive_ids) * ARCHIVE_ENTRY_BYTES
    used += sum(
        len(full.archives[archive_id]) * CHUNK_ENTRY_BYTES
        for archive_id in archive_ids
    )
    used = (used + 3) & ~3
    for archive_id in archive_ids:
        for chunk_id, chunk in enumerate(full.archives[archive_id]):
            if archive_id == ARCHIVE_IDS["FONT"] and chunk_id != 1:
                continue
            used = (used + 3) & ~3
            used += chunk.size
    return used


def audit_sprite_arenas(
    full: Pack,
    full_image: bytes,
    errors: list[str],
) -> tuple[int, int, int]:
    """Prove scoped resource arenas against the shipped DOS data and scripts."""
    mgo = full.archives.get(ARCHIVE_IDS["MGO"], [])
    maps = full.archives.get(ARCHIVE_IDS["MAP"], [])
    gop = full.archives.get(ARCHIVE_IDS["GOP"], [])
    abc = full.archives.get(ARCHIVE_IDS["ABC"], [])
    player_f = full.archives.get(ARCHIVE_IDS["F"], [])
    fire = full.archives.get(ARCHIVE_IDS["FIRE"], [])
    sss = full.archives.get(ARCHIVE_IDS["SSS"], [])
    data = full.archives.get(ARCHIVE_IDS["DATA"], [])

    def payload(chunk: Chunk) -> bytes:
        return full_image[chunk.offset : chunk.offset + chunk.size]

    if len(sss) <= 4 or len(data) <= 3 or len(mgo) < 6:
        errors.append("packs are missing sprite-arena audit inputs")
        return 0, 0, 0
    events = payload(sss[0])
    scenes = payload(sss[1])
    scripts = payload(sss[4])
    player_roles = payload(data[3])
    if len(events) % 32 or len(scenes) % 8 or len(scripts) % 8:
        errors.append("SSS sprite-audit chunks have invalid record geometry")
        return 0, 0, 0
    if len(player_roles) < 36:
        errors.append("DATA player-role chunk is too short for sprite IDs")
        return 0, 0, 0

    scene_sprite_sizes: list[list[int]] = []
    event_count = len(events) // 32
    for scene_number in range(1, len(scenes) // 8):
        start = struct.unpack_from("<H", scenes, (scene_number - 1) * 8 + 6)[0]
        end = struct.unpack_from("<H", scenes, scene_number * 8 + 6)[0]
        if start > end or end > event_count:
            errors.append(f"scene {scene_number} has invalid event boundaries")
            continue
        sprite_ids: list[int] = []
        for event_index in range(start, end):
            sprite_id = struct.unpack_from("<H", events, event_index * 32 + 16)[0]
            if sprite_id and sprite_id not in sprite_ids:
                sprite_ids.append(sprite_id)
        if any(sprite_id >= len(mgo) for sprite_id in sprite_ids):
            errors.append(f"scene {scene_number} refers beyond MGO")
            continue
        scene_sprite_sizes.append([mgo[item].size for item in sprite_ids])
        map_id = struct.unpack_from("<H", scenes, (scene_number - 1) * 8)[0]
        if map_id <= 0 or map_id >= len(maps) or map_id >= len(gop):
            errors.append(f"scene {scene_number} refers to invalid map {map_id}")
            continue
        if maps[map_id].size != 128 * 64 * 2 * 4 or gop[map_id].size == 0:
            errors.append(
                f"scene {scene_number} refers to unusable map/GOP {map_id}"
            )

    # Scene.wMapNum is durable state and opcode 0x0099 can replace it.  Do not
    # prove only the initial scene table: conservatively pair every real scene
    # event-sprite set with every complete MAP/GOP pair in the shipped pack.
    # This intentionally permits combinations which the script never reaches.
    map_pairs: list[list[int]] = []
    for map_id in range(1, min(len(maps), len(gop))):
        if maps[map_id].size == 0 and gop[map_id].size == 0:
            continue
        if maps[map_id].size != 128 * 64 * 2 * 4 or gop[map_id].size == 0:
            continue
        map_pairs.append([maps[map_id].size, gop[map_id].size])
    if not map_pairs or not scene_sprite_sizes:
        errors.append("packs contain no complete MAP/GOP or scene-sprite set")
        max_scene = 0
    else:
        max_scene = max(
            aligned_total(map_pair + sprite_sizes)
            for map_pair in map_pairs
            for sprite_sizes in scene_sprite_sizes
        )
    if max_scene > SCENE_ARENA_BYTES:
        errors.append(
            f"scene arena peak {max_scene} exceeds {SCENE_ARENA_BYTES}"
        )

    # PLAYERROLES starts with three six-WORD arrays: avatar, battle sprite,
    # then scene sprite. Opcode 0x0065 is the only script mutation of the
    # scene-sprite array, so include every operand present in the real script.
    possible: list[set[int]] = [set() for _ in range(6)]
    for role in range(6):
        possible[role].add(struct.unpack_from("<H", player_roles, 24 + role * 2)[0])
    follower_possible: list[set[int]] = [set(), set()]
    for offset in range(0, len(scripts), 8):
        operation, operand0, operand1, _ = struct.unpack_from(
            "<HHHH", scripts, offset
        )
        if operation == 0x0065:
            if operand0 >= 6 or operand1 >= len(mgo):
                errors.append(
                    "script opcode 0065 has invalid role/sprite "
                    f"{operand0}/{operand1}"
                )
                continue
            possible[operand0].add(operand1)
        elif operation == 0x0098:
            for slot, sprite_id in enumerate((operand0, operand1)):
                if sprite_id == 0:
                    continue
                if sprite_id >= len(mgo):
                    errors.append(
                        "script opcode 0098 has invalid follower sprite "
                        f"{sprite_id} in slot {slot + 1}"
                    )
                    continue
                follower_possible[slot].add(sprite_id)
        elif operation == 0x0099:
            map_id = operand1
            if (
                map_id <= 0
                or map_id >= len(maps)
                or map_id >= len(gop)
                or maps[map_id].size != 128 * 64 * 2 * 4
                or gop[map_id].size == 0
            ):
                errors.append(
                    f"script opcode 0099 has invalid replacement map {map_id}"
                )
    role_peaks = sorted(
        (max(mgo[item].size for item in choices) for choices in possible),
        reverse=True,
    )
    follower_peaks = [
        max(mgo[item].size for item in choices)
        for choices in follower_possible
        if choices
    ]
    max_player = aligned_total(role_peaks[:3] + follower_peaks)
    if max_player > PLAYER_ARENA_BYTES:
        errors.append(
            f"player/follower sprite peak {max_player} exceeds {PLAYER_ARENA_BYTES}"
        )

    max_f = max((chunk.size for chunk in player_f), default=0)
    max_abc = max((chunk.size for chunk in abc), default=0)
    max_battle = aligned_total([max_f] * 3 + [max_abc] * 5)
    if max_battle > BATTLE_ARENA_BYTES:
        errors.append(
            f"battle sprite upper bound {max_battle} exceeds {BATTLE_ARENA_BYTES}"
        )
    if max((chunk.size for chunk in fire), default=0) > FIGHT_EFFECT_BYTES:
        errors.append("FIRE chunk exceeds the fixed fight-effect buffer")
    if max_f > FIGHT_SUMMON_BYTES:
        errors.append("F chunk exceeds the fixed summon buffer")
    for chunk_id, owner, capacity in (
        (71, "fight-effect", FIGHT_EFFECT_BYTES),
        (73, "fight-summon", FIGHT_SUMMON_BYTES),
        (571, "fight-effect", FIGHT_EFFECT_BYTES),
        (572, "fight-summon", FIGHT_SUMMON_BYTES),
        (635, "fight-effect", FIGHT_EFFECT_BYTES),
    ):
        if chunk_id >= len(mgo) or mgo[chunk_id].size == 0:
            errors.append(f"cinematic MGO {chunk_id} is unavailable")
        elif mgo[chunk_id].size > capacity:
            errors.append(
                f"cinematic MGO {chunk_id} exceeds the fixed {owner} buffer"
            )
    return max_scene, max_player, max_battle


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--full-pack", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--expected-compiler", required=True, type=Path)
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    build = args.build_dir.resolve()
    full_path = args.full_pack.resolve()
    manifest_path = args.manifest.resolve()
    elf = build / "sdlpal_xiaomiao.elf"
    app_bin = build / "sdlpal_xiaomiao.bin"
    map_path = build / "sdlpal_xiaomiao.map"
    sdkconfig_path = build / "sdkconfig"
    project_path = build / "project_description.json"
    compile_commands_path = build / "compile_commands.json"
    flasher_path = build / "flasher_args.json"
    partition_bin = build / "partition_table/partition-table.bin"
    main_archive = build / "esp-idf/main/libmain.a"
    board_source = root / "esp32s3/main/xiaomiao_board.c"
    audio_source = root / "esp32s3/main/xiaomiao_audio.c"
    guru_screen_source = root / "esp32s3/main/pal_guru_screen.c"
    guru_screen_header = root / "esp32s3/main/pal_guru_screen.h"
    guru_bridge_source = root / "esp32s3/engine_bridge/pal_engine_guru.c"
    guru_bridge_header = root / "esp32s3/engine_bridge/pal_engine_guru.h"
    target_memory_header = root / "esp32s3/main/pal_target_memory.h"
    xiaomiao_memory_header = root / "esp32s3/main/xiaomiao_memory.h"
    cardputer_memory_header = (
        root / "esp32s3/main/cardputer_extreme_memory.h"
    )
    target_video_source = (
        root / "esp32s3/engine_bridge/pal_engine_target_video.c"
    )
    util_source = root / "util.c"
    util_header = root / "util.h"
    fatfs_stdio_source = (
        root / "esp32s3/engine_bridge/pal_engine_fatfs_stdio.c"
    )
    ending_source = root / "ending.c"
    palcommon_source = root / "palcommon.c"
    rngplay_source = root / "rngplay.c"
    pack_provider_source = root / "esp32s3/engine_bridge/pal_engine_pack_provider.c"
    target_packs_source = root / "esp32s3/engine_bridge/pal_engine_target_packs.c"
    contract_stubs_source = root / "unix/embedded_contract_stubs.c"
    fullscreen_stretch_header = root / "embedded/pal_fullscreen_stretch.h"
    fullscreen_rendering_doc = root / "embedded/FULLSCREEN_ASSET_RENDERING.md"
    video_source = root / "video.c"
    uigame_source = root / "uigame.c"
    responsive_ui_sources = (
        root / "map.c",
        root / "scene.c",
        root / "battle.c",
        root / "uibattle.c",
        root / "ui.c",
        uigame_source,
        root / "itemmenu.c",
        root / "magicmenu.c",
    )
    errors: list[str] = []
    max_event_sprites = 0
    max_player_sprites = 0
    max_battle_sprites = 0
    max_rng_frame = 0
    rng_frame_count = 0
    max_static_stack = 0
    max_static_stack_function = ""

    required = (
        elf, app_bin, map_path, sdkconfig_path, project_path,
        compile_commands_path, flasher_path, partition_bin, main_archive,
        full_path, manifest_path, board_source,
        audio_source, guru_screen_source, guru_screen_header,
        guru_bridge_source, guru_bridge_header, target_memory_header,
        xiaomiao_memory_header, cardputer_memory_header,
        target_video_source, util_source, util_header, fatfs_stdio_source,
        ending_source, palcommon_source, rngplay_source, pack_provider_source,
        target_packs_source, contract_stubs_source, fullscreen_stretch_header,
        fullscreen_rendering_doc, video_source,
        *responsive_ui_sources,
    )
    for path in required:
        if not path.is_file():
            errors.append(f"missing required artifact: {path}")
    if errors:
        print("\n".join(f"ERROR: {item}" for item in errors))
        return 1

    project = json.loads(project_path.read_text(encoding="utf-8"))
    compiler = Path(project.get("c_compiler", "")).resolve()
    expected_compiler = args.expected_compiler.resolve()
    if project.get("project_name") != "sdlpal_xiaomiao":
        errors.append("build project is not sdlpal_xiaomiao")
    if project.get("target") != "esp32":
        errors.append("build target is not classic ESP32")
    if compiler != expected_compiler:
        errors.append(f"compiler {compiler} != expected {expected_compiler}")
    nm = compiler.with_name("xtensa-esp32-elf-nm")
    objdump = compiler.with_name("xtensa-esp32-elf-objdump")
    for tool in (compiler, nm, objdump):
        if not tool.is_file():
            errors.append(f"missing toolchain executable: {tool}")
    if errors:
        print("\n".join(f"ERROR: {item}" for item in errors))
        return 1

    config = sdkconfig_path.read_text(encoding="utf-8", errors="replace")
    for name in (
        "CONFIG_IDF_TARGET_ESP32", "CONFIG_ESPTOOLPY_FLASHSIZE_4MB",
        "CONFIG_SPIRAM", "CONFIG_SPIRAM_MODE_QUAD",
        "CONFIG_SPIRAM_SPEED_40M", "CONFIG_SPIRAM_USE_MEMMAP",
        "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY",
        "CONFIG_SPIRAM_MEMTEST", "CONFIG_FATFS_LFN_NONE",
        "CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT",
    ):
        if not config_enabled(config, name):
            errors.append(f"sdkconfig must enable {name}")
    for name in (
        "CONFIG_SPIRAM_USE_CAPS_ALLOC", "CONFIG_SPIRAM_USE_MALLOC",
        "CONFIG_SPIRAM_BANKSWITCH_ENABLE",
        "CONFIG_FATFS_USE_DYN_BUFFERS", "CONFIG_FATFS_PER_FILE_CACHE",
        "CONFIG_FATFS_USE_FASTSEEK",
        "CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT",
        "CONFIG_ESP_SYSTEM_PANIC_SILENT_REBOOT",
    ):
        if not config_disabled(config, name):
            errors.append(f"sdkconfig must leave {name} disabled")
    if "CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384" not in config:
        errors.append("main task stack is not the required 16 KiB")

    compile_entries = json.loads(compile_commands_path.read_text(encoding="utf-8"))
    main_commands = [
        entry.get("command", "")
        for entry in compile_entries
        if "/esp32s3/main/" in entry.get("file", "")
    ]
    joined_commands = "\n".join(main_commands)
    for token in (
        "-DMEM_LEVEL2=1", "-DPAL_EXTREME_TWO_SCREENS=1",
        "-DPAL_TARGET_XIAOMIAO=1", "-DPAL_STORAGE_SD_ONLY=1",
        "-DPAL_NO_RUNTIME_HEAP=1", "-DPAL_NO_RUNTIME_DECOMPRESS=1",
        "-DPAL_EXTREME_RIX_MUSIC=1", "-DPAL_CONTRACT_NO_SFX=1",
        "-DPAL_TARGET_GURU_MEDITATION=1",
    ):
        if token not in joined_commands:
            errors.append(f"Xiaomiao compile contract is missing {token}")
    if "-DPAL_EXTREME_CHAPTER_CACHE=1" in joined_commands:
        errors.append("Xiaomiao unexpectedly enables the NOR chapter cache")
    if "-DMEM_LEVEL1=1" in joined_commands:
        errors.append("Xiaomiao unexpectedly enables MEM_LEVEL1")
    if "-DPAL_CARDPUTER_EXTREME=1" in joined_commands:
        errors.append("Xiaomiao still defines retired PAL_CARDPUTER_EXTREME")
    if "-DPAL_CONTRACT_NO_AUDIO=1" in joined_commands:
        errors.append("Xiaomiao unexpectedly compiles the no-audio contract")

    board_text = board_source.read_text(encoding="utf-8", errors="replace")
    board_contract = {
        "LCD/SD SCLK GPIO18": "PIN_LCD_SCLK = GPIO_NUM_18",
        "LCD/SD MOSI GPIO23": "PIN_LCD_MOSI = GPIO_NUM_23",
        "LCD CS GPIO5": "PIN_LCD_CS = GPIO_NUM_5",
        "LCD D/C GPIO4": "PIN_LCD_DC = GPIO_NUM_4",
        "LCD reset/SD MISO GPIO19": "PIN_LCD_RESET_TF_MISO = GPIO_NUM_19",
        "SD CS GPIO22": "PIN_TF_CS = GPIO_NUM_22",
        "ESP-IDF VSPI host": "SHARED_HOST = SPI3_HOST",
        "six-key pin order": (
            "GPIO_NUM_2, GPIO_NUM_13, GPIO_NUM_27,\n"
            "    GPIO_NUM_35, GPIO_NUM_34, GPIO_NUM_12"
        ),
        "native key semantics": "{'i', 'k', 'j', 'l', '\\r', '\\b'}",
        "shared-bus LCD MISO": "bus_cfg.miso_io_num = PIN_LCD_RESET_TF_MISO",
        "shared-bus SD host": "slot.host_id = SHARED_HOST",
        "20 MHz SD limit": "TF_SPI_CLOCK_KHZ = 20000u",
        "landscape RGB MADCTL": "value = 0x60",
    }
    for label, snippet in board_contract.items():
        if snippet not in board_text:
            errors.append(f"Xiaomiao board source is missing {label}: {snippet}")
    for token in (
        "Xiaomiao_GuruMeditation(",
        "PalGuruScreen_BuildText(",
        "PalGuruScreen_RenderRgb565Strip(",
        "esp_app_get_description()",
    ):
        if token not in board_text:
            errors.append(f"Xiaomiao Guru display is missing {token!r}")
    reset_pos = board_text.find("pulse_lcd_reset()")
    bus_pos = board_text.find("spi_bus_initialize(")
    if reset_pos < 0 or bus_pos < 0 or reset_pos > bus_pos:
        errors.append("LCD reset on GPIO19 must finish before SPI initializes it as SD MISO")

    audio_text = audio_source.read_text(encoding="utf-8", errors="replace")
    audio_contract = {
        "11-bit LEDC PWM": "AUDIO_PWM_DUTY_BITS = 11",
        "passive-buzzer 3x output gain": "AUDIO_OUTPUT_GAIN = 3",
        "sample-rate GPTimer": "audio_sample_alarm(",
        "fixed four-tick ring": "AUDIO_RING_TICKS = 4",
        "passive buzzer GPIO14": "PIN_BUZZER_AUDIO = GPIO_NUM_14",
        "PCM midpoint duty": "AUDIO_PWM_DUTY_MIDPOINT",
        "no allocator-backed sample queue": "pal_sram_audio_ring",
    }
    for label, snippet in audio_contract.items():
        if snippet not in audio_text:
            errors.append(f"Xiaomiao audio source is missing {label}: {snippet}")

    fatfs_stdio_text = fatfs_stdio_source.read_text(
        encoding="utf-8", errors="replace"
    )
    for label, snippet in {
        "classic-ESP32 Level2-only workaround": (
            "defined(CONFIG_IDF_TARGET_ESP32) && defined(MEM_LEVEL2)"
        ),
        "bounded save-write chunks": "PAL_ENGINE_FATFS_WRITE_CHUNK_BYTES 4096u",
        "internal DMA save staging": "pal_engine_fatfs_write_chunk",
    }.items():
        if snippet not in fatfs_stdio_text:
            errors.append(
                f"Xiaomiao FatFS stdio source is missing {label}: {snippet}"
            )

    ending_text = ending_source.read_text(encoding="utf-8", errors="replace")
    if "#define pal_psram_ending_fbp_static pal_sram_aux_framebuffer" in ending_text:
        errors.append(
            "ending FBP must not alias a 64KB source image to the native screen"
        )
    if ending_text.count("PAL_FBPBlitChunkToSurface(gpGlobals->f.fpFBP") < 2:
        errors.append(
            "small-screen show/scroll ending paths must stream FBP scanlines"
        )

    palcommon_text = palcommon_source.read_text(encoding="utf-8")
    rngplay_text = rngplay_source.read_text(encoding="utf-8")
    provider_text = pack_provider_source.read_text(encoding="utf-8")
    target_packs_text = target_packs_source.read_text(encoding="utf-8")
    contract_stubs_text = contract_stubs_source.read_text(encoding="utf-8")
    video_text = video_source.read_text(encoding="utf-8")
    uigame_text = uigame_source.read_text(encoding="utf-8")
    for label, text in (("video", video_text), ("uigame", uigame_text)):
        if '#include "pal_target_memory.h"' not in text:
            errors.append(
                f"{label} two-screen storage must include pal_target_memory.h"
            )
        if '#include "esp32s3/main/cardputer_extreme_memory.h"' in text:
            errors.append(
                f"{label} must not hard-code Cardputer framebuffer geometry"
            )
    for path in root.glob("*.c"):
        text = path.read_text(encoding="utf-8", errors="replace")
        for board_header in (
            "cardputer_extreme_memory.h",
            "xiaomiao_memory.h",
            "cores3se_memory.h",
        ):
            if board_header in text:
                errors.append(
                    f"{path}: shared engine source directly includes "
                    f"board-specific {board_header}"
                )
    target_memory_text = target_memory_header.read_text(encoding="utf-8")
    if (
        "PAL_EXTREME_SCREEN_WIDTH != PAL_TARGET_LCD_WIDTH"
        not in target_memory_text
        or "PAL_EXTREME_SCREEN_HEIGHT != PAL_TARGET_LCD_HEIGHT"
        not in target_memory_text
        or "PAL_EXTREME_SCREEN_BYTES !=" not in target_memory_text
    ):
        errors.append(
            "target memory selector lacks framebuffer/LCD geometry guards"
        )
    if "defined(PAL_TARGET_XIAOMIAO)" not in (
        cardputer_memory_header.read_text(encoding="utf-8")
    ):
        errors.append("Cardputer memory header lacks a Xiaomiao mismatch guard")
    if "defined(PAL_TARGET_CARDPUTER_ADV)" not in (
        xiaomiao_memory_header.read_text(encoding="utf-8")
    ):
        errors.append("Xiaomiao memory header lacks a Cardputer mismatch guard")
    if (
        "native_width > PAL_EXTREME_SCREEN_WIDTH" not in video_text
        or "native_height > PAL_EXTREME_SCREEN_HEIGHT" not in video_text
    ):
        errors.append("video startup lacks backing-buffer geometry guards")
    target_video_text = target_video_source.read_text(encoding="utf-8")
    if "native indexed present rejected:" not in target_video_text:
        errors.append("LCD presenter silently drops invalid native geometry")

    guru_screen_text = guru_screen_source.read_text(encoding="utf-8")
    guru_bridge_text = guru_bridge_source.read_text(encoding="utf-8")
    util_text = util_source.read_text(encoding="utf-8")
    util_header_text = util_header.read_text(encoding="utf-8")
    for token in (
        "static const uint8_t pal_guru_font_5x7",
        "PalGuruScreen_RenderRgb565Strip(",
        '"GURU"',
        '"MEDITATION"',
        '"HALTED"',
    ):
        if token not in guru_screen_text:
            errors.append(f"const Guru screen renderer is missing {token!r}")
    if any(token in guru_screen_text for token in ("malloc(", "calloc(", "free(")):
        errors.append("Guru screen renderer must not use a runtime allocator")
    for token in (
        "AUDIO_CloseDevice();",
        "PalTarget_GuruMeditation(file, line, reason);",
        "vTaskSuspend(NULL);",
    ):
        if token not in guru_bridge_text:
            errors.append(f"Guru halt path is missing {token!r}")
    if (
        "PAL_TerminateOnErrorAt(__FILE__, __LINE__, __VA_ARGS__)"
        not in util_header_text
        or "PalEngineBridge_GuruMeditationAt(file, line, string);"
        not in util_text
    ):
        errors.append("TerminateOnError does not preserve source file and line")
    if (
        "#if defined(MEM_LEVEL1) && !defined(PAL_EXTREME_CHAPTER_CACHE)\n"
        "#define PAL_ENGINE_STRICT_PACK_VALIDATION 1"
        not in provider_text
        or "defined(MEM_LEVEL1) || defined(PAL_STORAGE_SD_ONLY)"
        in provider_text
    ):
        errors.append(
            "SD-only packs must not enable whole-pack payload CRC validation"
        )
    for token in (
        '#define PAL_ENGINE_TF_PACK_PATH "0:/pal_full.pak"',
        "PalLevel2ResidentPack_Build(",
        "pal_mem_level2_resident_pack",
    ):
        if token not in target_packs_text:
            errors.append(f"Xiaomiao portable-pack path is missing {token!r}")
    if "font10.cell_width != 10u || font10.cell_height != 10u" not in target_packs_text:
        errors.append(
            "Xiaomiao must accept data-pack FONT10 subsets by geometry"
        )
    if "PalNativeUi_Font10IdentityMatches" in contract_stubs_text:
        errors.append(
            "Xiaomiao font initialization is still bound to a generated data hash"
        )
    for label, text, tokens in (
        (
            "FBP renderer",
            palcommon_text,
            (
                "PalFullScreenStretch_BlitIndexed(",
                "PalFullScreenStretch_BlitIndexedRow(",
            ),
        ),
        (
            "RNG renderer",
            rngplay_text,
            (
                "PalFullScreenStretch_DestinationRange(",
                "PalEngineBridge_OpenNativeRngFrame(",
                "PalEngineBridge_ReadNativeRngFrameRange(",
            ),
        ),
        (
            "RNG pack provider",
            provider_text,
            (
                "PalEngineBridge_OpenNativeRngFrame(",
                "PalEngineBridge_ReadNativeRngFrameRange(",
            ),
        ),
    ):
        for token in tokens:
            if token not in text:
                errors.append(f"{label} is missing {token!r}")
    for path in responsive_ui_sources:
        if "PalFullScreenStretch_" in path.read_text(encoding="utf-8"):
            errors.append(
                f"{path}: full-screen asset transform leaked into responsive map/UI"
            )

    flasher = json.loads(flasher_path.read_text(encoding="utf-8"))
    if flasher.get("flash_settings") != {
        "flash_mode": "dio", "flash_size": "4MB", "flash_freq": "40m"
    }:
        errors.append("flasher settings are not ESP32-WROVER-B 4MB DIO/40MHz")
    flash_files = flasher.get("flash_files", {})
    if flash_files.get("0x10000") != "sdlpal_xiaomiao.bin":
        errors.append("application is not flashed at 0x10000")
    if len(flash_files) != 3:
        errors.append("flash image unexpectedly contains a game-resource image")
    if app_bin.stat().st_size > APP_PARTITION_BYTES:
        errors.append("application binary exceeds the 3 MiB factory partition")

    partition_data = partition_bin.read_bytes()
    partitions: list[tuple[int, int, int, int, str]] = []
    for pos in range(0, len(partition_data) - 31, 32):
        magic = struct.unpack_from("<H", partition_data, pos)[0]
        if magic != 0x50AA:
            break
        _, ptype, subtype, offset, size, label, _flags = struct.unpack_from(
            "<HBBII16sI", partition_data, pos
        )
        partitions.append(
            (ptype, subtype, offset, size, label.rstrip(b"\0").decode("ascii"))
        )
    if [item[4] for item in partitions] != ["nvs", "phy_init", "factory"]:
        errors.append(f"unexpected partition set: {[item[4] for item in partitions]}")
    app_parts = [item for item in partitions if item[0] == 0]
    if len(app_parts) != 1 or app_parts[0][2:4] != (APP_OFFSET, APP_PARTITION_BYTES):
        errors.append("factory application partition geometry is incorrect")
    if any(offset + size > FLASH_BYTES for _, _, offset, size, _ in partitions):
        errors.append("partition table exceeds 4 MiB flash")

    objdump_symbols = run_text([objdump, "-t", elf])
    symbols = parse_objdump_symbols(objdump_symbols)
    symbol_addresses = parse_objdump_symbol_addresses(objdump_symbols)
    forbidden_data_binding_symbols = (
        "PalNativeUi_Font10IdentityMatches",
        "pack_crc32_const",
        "pack_crc32_read_at",
        "pack_crc32_update",
    )
    for forbidden in forbidden_data_binding_symbols:
        if any(
            name == forbidden or name.startswith(forbidden + ".")
            for name in symbol_addresses
        ):
            errors.append(
                f"Xiaomiao app retains data-binding/hash symbol: {forbidden}"
            )
    for prefix in ("PalEngineEventState_", "PalEventPager_", "PalEventJournal_"):
        linked = sorted(name for name in symbols if name.startswith(prefix))
        if linked:
            errors.append(
                f"LEVEL2 app unexpectedly links {prefix} session-pager code: "
                + ", ".join(linked)
            )
    if b"EVENT.WRK" in elf.read_bytes():
        errors.append("LEVEL2 app unexpectedly contains the LEVEL1 work-file path")
    for name in ("memcpy", "memset"):
        actual = symbol_addresses.get(name)
        if actual is None:
            errors.append(f"required cache-off runtime symbol missing: {name}")
            continue
        address, section = actual
        if (
            section != ".iram0.text"
            or not ESP32_IRAM_START <= address < ESP32_IRAM_END
        ):
            errors.append(
                f"{name} must execute from internal RAM during flash init; "
                f"got {address:#010x} in {section}"
            )
    for name in (
        "PalEngineBridge_OpenNativeRngFrame",
        "PalEngineBridge_ReadNativeRngFrameRange",
        "PalTargetAudio_Begin",
        "gptimer_new_timer",
        "ledc_timer_config",
    ):
        if name not in symbols:
            errors.append(f"required linked symbol missing: {name}")
    for expected_section, expected_symbols in (
        (".ext_ram.bss", PSRAM_SYMBOLS), (".dram0.bss", SRAM_SYMBOLS)
    ):
        for name, expected_size in expected_symbols.items():
            actual = symbols.get(name)
            if actual != (expected_size, expected_section):
                errors.append(
                    f"{name}: symbol placement {actual} != "
                    f"({expected_size}, {expected_section})"
                )
    expected_psram = sum(PSRAM_SYMBOLS.values())
    sections = parse_sections(run_text([objdump, "-h", elf]))
    ext_size = sections.get(".ext_ram.bss", (0, 0))[0]
    if ext_size != expected_psram:
        errors.append(
            f"mapped PSRAM BSS {ext_size} != named ownership {expected_psram}"
        )
    if ext_size > MAPPED_PSRAM_LIMIT:
        errors.append("named mapped PSRAM exceeds the conservative 3.75 MiB limit")

    map_text = map_path.read_text(encoding="utf-8", errors="replace")
    regions = parse_memory_regions(map_text)
    dram = regions.get("dram0_0_seg")
    heap_start = sections.get(".dram0.heap_start", (0, 0))[1]
    dram_reserve = 0
    if dram is None or heap_start == 0:
        errors.append("cannot derive linker DRAM reserve")
    else:
        dram_reserve = dram[0] + dram[1] - heap_start
        if dram_reserve < MIN_LINKER_DRAM_RESERVE:
            errors.append(
                f"linker DRAM reserve {dram_reserve} < {MIN_LINKER_DRAM_RESERVE}"
            )
        if dram_reserve - MAIN_TASK_STACK_BYTES < MIN_POST_MAIN_STACK_RESERVE:
            errors.append("post-main-stack DRAM reserve is below 64 KiB")

    undefined = run_text([nm, "-u", main_archive])
    undefined_names = {line.split()[-1].split("@", 1)[0] for line in undefined.splitlines() if line.split()}
    bad_symbols = sorted(
        name for name in undefined_names
        if name in FORBIDDEN_UNDEFINED or name.startswith("_Zn") or
        name.startswith("_Zd") or name.startswith("esp_himem_")
    )
    if bad_symbols:
        errors.append("forbidden project references: " + ", ".join(bad_symbols))

    stack_reports = list((build / "esp-idf/main").rglob("*.su"))
    if not stack_reports:
        errors.append("no project stack-usage reports were generated")
    for report in stack_reports:
        for line in report.read_text(encoding="utf-8", errors="replace").splitlines():
            fields = line.split("\t")
            if len(fields) < 2:
                continue
            try:
                stack_bytes = int(fields[1].split()[0])
            except (ValueError, IndexError):
                continue
            if stack_bytes > max_static_stack:
                max_static_stack = stack_bytes
                max_static_stack_function = fields[0]
    if max_static_stack > MAX_STATIC_STACK_BYTES:
        errors.append(
            f"static stack peak {max_static_stack} exceeds {MAX_STATIC_STACK_BYTES}: "
            f"{max_static_stack_function}"
        )

    full = parse_pack(full_path, errors)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    resident_bytes = 0
    if full is not None:
        if full.toc_bytes > TF_TOC_MAX_BYTES:
            errors.append(
                f"portable pack TOC {full.toc_bytes} exceeds {TF_TOC_MAX_BYTES}"
            )
        full_ids = {ARCHIVE_IDS[name] for name in FULL_ARCHIVES}
        if set(full.archives) != full_ids:
            errors.append(
                f"portable pack archive IDs {set(full.archives)} != {full_ids}"
            )
        full_image = full_path.read_bytes()
        resident_bytes = resident_pack_bytes(full, errors)
        if resident_bytes > RESIDENT_MAX_BYTES:
            errors.append(
                f"runtime resident image {resident_bytes} exceeds "
                f"{RESIDENT_MAX_BYTES}"
            )
        font_chunks = full.archives.get(ARCHIVE_IDS["FONT"], [])
        if len(font_chunks) <= 1 or font_chunks[1].fmt != PACK_FORMAT_FONT10:
            errors.append("pal_full.pak is missing FONT10 chunk 1")
        mus_chunks = full.archives.get(ARCHIVE_IDS["MUS"], [])
        if len(mus_chunks) != 88:
            errors.append(f"pal_full.pak has {len(mus_chunks)} MUS slots, expected 88")
        else:
            for chunk_id, chunk in enumerate(mus_chunks):
                if chunk_id in (0, 29):
                    if chunk.size != 0:
                        errors.append(f"reserved MUS slot {chunk_id} is not empty")
                elif (
                    chunk.fmt != PACK_FORMAT_NATIVE
                    or chunk.size < 16
                    or full_image[chunk.offset:chunk.offset + 2] != b"\xaa\x55"
                ):
                    errors.append(f"MUS slot {chunk_id} is not a native RIX track")
        for name in DIRECT_STAGED_ARCHIVES:
            chunks = full.archives.get(ARCHIVE_IDS[name], [])
            maximum = max((chunk.size for chunk in chunks), default=0)
            if maximum > TRANSIENT_CHUNK_BYTES:
                errors.append(
                    f"{name} chunk {maximum} exceeds the 64 KiB staging buffer"
                )
        for chunk_id, chunk in enumerate(
            full.archives.get(ARCHIVE_IDS["FBP"], [])
        ):
            if chunk.size and (
                chunk.size != 320 * 200 or chunk.fmt != PACK_FORMAT_NATIVE
            ):
                errors.append(
                    f"FBP chunk {chunk_id} is not a native 320x200 canvas: "
                    f"size={chunk.size}, format={chunk.fmt}"
                )
        max_rng_frame, rng_frame_count = audit_rng_frames(
            full_path, full, errors
        )
        (
            max_event_sprites,
            max_player_sprites,
            max_battle_sprites,
        ) = audit_sprite_arenas(full, full_image, errors)

    font_manifest = manifest.get("font10", {}).get("font10", {})
    metrics = font_manifest.get("metrics", {})
    if metrics.get("cell_width") != 10 or metrics.get("cell_height") != 10:
        errors.append("FONT10 manifest cell geometry is not 10x10")
    if full is not None:
        font_chunks = full.archives.get(ARCHIVE_IDS["FONT"], [])
        if len(font_chunks) > 1 and font_chunks[1].size != font_manifest.get("bytes"):
            errors.append("packed FONT10 byte count differs from its manifest")

    runtime = manifest.get("runtime", {})
    required_runtime = {
        "heap_required": False,
        "payloads_are_runtime_native": True,
        "runtime_decompression_required": False,
        "portable_complete_tf_file": "pal_full.pak",
        "level2_resident_source": "runtime-derived from pal_full.pak",
        "tf_directory_contract": (
            "one complete pack plus Cardputer NOR cache sources"
        ),
    }
    if not isinstance(runtime, dict) or any(
        runtime.get(key) != value for key, value in required_runtime.items()
    ):
        errors.append("manifest runtime contract is not the unified Level2 set")
    pack_set = manifest.get("pack_set", {})
    if full is not None and pack_set.get("id") != full.set_id:
        errors.append("manifest data identity differs from pal_full.pak")
    if (
        pack_set.get("scope") != "portable-complete-data"
        or pack_set.get("cache_layout_affects_id") is not False
    ):
        errors.append("pack-set identity is not independent of target caches")
    packs_manifest = manifest.get("packs", {})
    full_item = packs_manifest.get("full", {})
    if (
        full_item.get("size") != full_path.stat().st_size
        or full_item.get("sha256") != sha256_file(full_path)
    ):
        errors.append("manifest identity mismatch for pal_full.pak")
    if full is not None and (
        full_item.get("toc_bytes") != full.toc_bytes
        or full_item.get("pack_set_id") != full.set_id
        or full_item.get("crc32") != full.crc32
    ):
        errors.append("manifest header identity mismatch for pal_full.pak")
    for obsolete in ("tf", "level2_core"):
        if obsolete in packs_manifest:
            errors.append(f"manifest retains obsolete packs.{obsolete}")
    resident_summary = packs_manifest.get("level2_resident", {})
    if (
        resident_summary.get("materialization")
        != "runtime-derived-in-fixed-psram"
        or resident_summary.get("source") != "pal_full.pak"
        or resident_summary.get("size") != resident_bytes
        or resident_summary.get("pack_set_id")
        != (full.set_id if full is not None else None)
    ):
        errors.append("manifest Level2 resident-view summary is invalid")
    resident_archives = resident_summary.get("archives", {})
    if (
        not isinstance(resident_archives, dict)
        or set(resident_archives) != RESIDENT_ARCHIVES
    ):
        errors.append("manifest Level2 resident archive selection is invalid")
    full_summary = manifest.get("packs", {}).get("full", {})
    if (
        full_summary.get("portable_complete") is not True
        or full_summary.get("all_chunks") is not True
        or full_summary.get("allow_cache_overlap") is not True
    ):
        errors.append("pal_full.pak is not declared complete and portable")
    full_archive_summaries = full_summary.get("archives", {})
    if not isinstance(full_archive_summaries, dict) or set(
        full_archive_summaries
    ) != FULL_ARCHIVES:
        errors.append("pal_full.pak manifest does not cover every native archive")
    elif full is not None:
        for name, archive_id in ARCHIVE_IDS.items():
            item = full_archive_summaries.get(name, {})
            chunk_count = len(full.archives.get(archive_id, []))
            if (
                item.get("source_chunk_count") != chunk_count
                or item.get("present_chunk_ids") != list(range(chunk_count))
            ):
                errors.append(f"pal_full.pak manifest is sparse for {name}")
    level2_layout = manifest.get("level2_resident_policy", {})
    layout_path = root / "tools/pal_pack_layout_xiaomiao.json"
    if (
        not isinstance(level2_layout, dict)
        or level2_layout.get("sha256") != sha256_file(layout_path)
        or level2_layout.get("resident_max_bytes") != RESIDENT_MAX_BYTES
        or level2_layout.get("storage") != "SD-only"
    ):
        errors.append("manifest Level2 resident policy identity/limit is invalid")

    data_dir = Path(manifest.get("data_dir", ""))
    source_files = manifest.get("source_files", [])
    if not data_dir.is_dir() or not isinstance(source_files, list):
        errors.append("manifest source-data inventory is unavailable")
    else:
        for source in source_files:
            if not isinstance(source, dict) or not isinstance(source.get("path"), str):
                errors.append("manifest has a malformed source-data entry")
                continue
            source_path = data_dir / source["path"]
            if not source_path.is_file():
                errors.append(f"missing manifest source file: {source_path}")
                continue
            if (
                source_path.stat().st_size != source.get("size")
                or sha256_file(source_path) != source.get("sha256")
            ):
                errors.append(f"source identity mismatch: {source_path}")

    if errors:
        print("Xiaomiao check FAILED")
        for item in errors:
            print(f"ERROR: {item}")
        return 1

    print("Xiaomiao unified-TF SD-only check passed")
    print(f"  app: {app_bin.stat().st_size} / {APP_PARTITION_BYTES} bytes")
    print(f"  linker DRAM reserve: {dram_reserve} bytes")
    print(
        f"  static stack peak: {max_static_stack} / {MAX_STATIC_STACK_BYTES} "
        f"bytes ({max_static_stack_function})"
    )
    print(f"  mapped PSRAM: {ext_size} / {MAPPED_PSRAM_LIMIT} bytes")
    print(
        "  scoped arenas: "
        f"scene {max_event_sprites}/{SCENE_ARENA_BYTES}, "
        f"player {max_player_sprites}/{PLAYER_ARENA_BYTES}, "
        f"battle {max_battle_sprites}/{BATTLE_ARENA_BYTES} bytes"
    )
    print(
        f"  runtime-derived resident view: {resident_bytes} / "
        f"{RESIDENT_MAX_BYTES} bytes"
    )
    print(
        f"  portable full pack: {full_path.stat().st_size} bytes; "
        f"TOC {full.toc_bytes} / {TF_TOC_MAX_BYTES}"
    )
    print(
        f"  RNG frames: max {max_rng_frame} bytes across {rng_frame_count} "
        f"frames; bounded input window {RNG_INPUT_WINDOW_BYTES} bytes"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
