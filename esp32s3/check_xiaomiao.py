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
CORE_MAX_BYTES = 1536 * 1024
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
MAIN_TASK_STACK_BYTES = 16 * 1024
MIN_LINKER_DRAM_RESERVE = 80 * 1024
MIN_POST_MAIN_STACK_RESERVE = 64 * 1024
MAX_STATIC_STACK_BYTES = 2048
EVENT_TEMPLATE_BYTES = 512 + 43 * 4096

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
CORE_ARCHIVES = {"DATA", "PAT", "RGM", "SSS", "TEXT", "FONT"}
SD_ARCHIVES = {
    "ABC", "BALL", "F", "FBP", "FIRE", "GOP", "MAP", "MGO",
    "MIDI", "MUS", "RNG", "SFX",
}
DIRECT_STAGED_ARCHIVES = {
    "ABC", "BALL", "F", "FIRE", "GOP", "MAP", "MGO",
}

PSRAM_SYMBOLS = {
    "pal_sram_framebuffer": 160 * 128,
    "pal_sram_aux_framebuffer": 160 * 128,
    "pal_mem_level2_core_pack": CORE_MAX_BYTES,
    "pal_mem_level2_tf_toc": TF_TOC_MAX_BYTES,
    "pal_mem_level2_transient_chunk": TRANSIENT_CHUNK_BYTES,
    "pal_mem_level2_scene_arena": SCENE_ARENA_BYTES,
    "pal_mem_level2_player_arena": PLAYER_ARENA_BYTES,
    "pal_mem_level2_battle_arena": BATTLE_ARENA_BYTES,
    "pal_mem_level2_fight_effect": FIGHT_EFFECT_BYTES,
    "pal_mem_level2_fight_summon": FIGHT_SUMMON_BYTES,
    "pal_sram_extreme_res_state": 36,
    "pal_sram_extreme_res_event_sprite_ptrs": 160 * 4,
    "pal_sram_extreme_savegame_static": 14096,
    "pal_sram_extreme_global_magics": 3648,
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


def generated_define(path: Path, name: str, errors: list[str]) -> int | None:
    text = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(
        rf"^#define\s+{re.escape(name)}\s+(0[xX][0-9a-fA-F]+|[0-9]+)u$",
        text,
        re.MULTILINE,
    )
    if match is None:
        errors.append(f"{path}: missing generated define {name}")
        return None
    return int(match.group(1), 0)


def validate_event_template(path: Path, set_id: int, errors: list[str]) -> None:
    data = path.read_bytes()
    if len(data) != EVENT_TEMPLATE_BYTES:
        errors.append(f"{path}: size {len(data)} != {EVENT_TEMPLATE_BYTES}")
        return
    if data[:8] != b"PALEVT1\0" or struct.unpack_from("<HH", data, 8) != (1, 512):
        errors.append(f"{path}: invalid EVENT.DEF header")
    if struct.unpack_from("<I", data, 12)[0] != set_id:
        errors.append(f"{path}: pack-set ID does not match the resource packs")
    header = bytearray(data[:512])
    declared_header_crc = struct.unpack_from("<I", header, 508)[0]
    struct.pack_into("<I", header, 508, 0)
    if declared_header_crc != (zlib.crc32(header) & 0xFFFFFFFF):
        errors.append(f"{path}: header CRC32 mismatch")
    payload = data[512:]
    if struct.unpack_from("<I", data, 48)[0] != (zlib.crc32(payload) & 0xFFFFFFFF):
        errors.append(f"{path}: payload CRC32 mismatch")
    expected = (32, 5369, 8, 300, 4096, 42, 43)
    actual = (
        struct.unpack_from("<H", data, 16)[0],
        struct.unpack_from("<H", data, 18)[0],
        struct.unpack_from("<H", data, 20)[0],
        struct.unpack_from("<H", data, 22)[0],
        struct.unpack_from("<I", data, 24)[0],
        struct.unpack_from("<H", data, 28)[0],
        struct.unpack_from("<H", data, 30)[0],
    )
    if actual != expected:
        errors.append(f"{path}: event/scene geometry {actual} != {expected}")


def aligned_total(sizes: list[int]) -> int:
    used = 0
    for size in sizes:
        used = (used + 3) & ~3
        used += size
    return used


def audit_sprite_arenas(
    core_path: Path,
    sd_path: Path,
    core: Pack,
    sd: Pack,
    errors: list[str],
) -> tuple[int, int, int]:
    """Prove scoped resource arenas against the shipped DOS data and scripts."""
    core_image = core_path.read_bytes()
    mgo = sd.archives.get(ARCHIVE_IDS["MGO"], [])
    maps = sd.archives.get(ARCHIVE_IDS["MAP"], [])
    gop = sd.archives.get(ARCHIVE_IDS["GOP"], [])
    abc = sd.archives.get(ARCHIVE_IDS["ABC"], [])
    player_f = sd.archives.get(ARCHIVE_IDS["F"], [])
    fire = sd.archives.get(ARCHIVE_IDS["FIRE"], [])
    sss = core.archives.get(ARCHIVE_IDS["SSS"], [])
    data = core.archives.get(ARCHIVE_IDS["DATA"], [])

    def payload(chunk: Chunk) -> bytes:
        return core_image[chunk.offset : chunk.offset + chunk.size]

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
    return max_scene, max_player, max_battle


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--core-pack", required=True, type=Path)
    parser.add_argument("--sd-pack", required=True, type=Path)
    parser.add_argument("--event-template", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--expected-compiler", required=True, type=Path)
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    build = args.build_dir.resolve()
    core_path = args.core_pack.resolve()
    sd_path = args.sd_pack.resolve()
    event_path = args.event_template.resolve()
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
    ui_header = root / "esp32s3/main/generated/pal_native_ui_160x128.h"
    board_source = root / "esp32s3/main/xiaomiao_board.c"
    ending_source = root / "ending.c"
    palcommon_source = root / "palcommon.c"
    rngplay_source = root / "rngplay.c"
    pack_provider_source = root / "esp32s3/engine_bridge/pal_engine_pack_provider.c"
    fullscreen_stretch_header = root / "embedded/pal_fullscreen_stretch.h"
    fullscreen_rendering_doc = root / "embedded/FULLSCREEN_ASSET_RENDERING.md"
    responsive_ui_sources = (
        root / "map.c",
        root / "scene.c",
        root / "battle.c",
        root / "uibattle.c",
        root / "ui.c",
        root / "uigame.c",
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
        core_path, sd_path, event_path, manifest_path, ui_header, board_source,
        ending_source, palcommon_source, rngplay_source, pack_provider_source,
        fullscreen_stretch_header, fullscreen_rendering_doc,
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
    ):
        if not config_enabled(config, name):
            errors.append(f"sdkconfig must enable {name}")
    for name in (
        "CONFIG_SPIRAM_USE_CAPS_ALLOC", "CONFIG_SPIRAM_USE_MALLOC",
        "CONFIG_FATFS_USE_DYN_BUFFERS", "CONFIG_FATFS_PER_FILE_CACHE",
        "CONFIG_FATFS_USE_FASTSEEK",
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
        "pal_native_ui_160x128.h",
        "-DPAL_NO_RUNTIME_HEAP=1", "-DPAL_NO_RUNTIME_DECOMPRESS=1",
        "-DPAL_CONTRACT_NO_AUDIO=1",
    ):
        if token not in joined_commands:
            errors.append(f"Xiaomiao compile contract is missing {token}")
    if "-DPAL_EXTREME_CHAPTER_CACHE=1" in joined_commands:
        errors.append("Xiaomiao unexpectedly enables the NOR chapter cache")
    if "-DMEM_LEVEL1=1" in joined_commands:
        errors.append("Xiaomiao unexpectedly enables MEM_LEVEL1")
    if "-DPAL_CARDPUTER_EXTREME=1" in joined_commands:
        errors.append("Xiaomiao still defines retired PAL_CARDPUTER_EXTREME")

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
        "landscape BGR MADCTL": "value = 0x68",
    }
    for label, snippet in board_contract.items():
        if snippet not in board_text:
            errors.append(f"Xiaomiao board source is missing {label}: {snippet}")
    reset_pos = board_text.find("pulse_lcd_reset()")
    bus_pos = board_text.find("spi_bus_initialize(")
    if reset_pos < 0 or bus_pos < 0 or reset_pos > bus_pos:
        errors.append("LCD reset on GPIO19 must finish before SPI initializes it as SD MISO")

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

    symbols = parse_objdump_symbols(run_text([objdump, "-t", elf]))
    for name in (
        "PalEngineBridge_OpenNativeRngFrame",
        "PalEngineBridge_ReadNativeRngFrameRange",
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

    core = parse_pack(core_path, errors)
    sd = parse_pack(sd_path, errors)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if core is not None and sd is not None:
        if core.set_id != sd.set_id:
            errors.append("core and SD packs have different pack-set IDs")
        if core.size > CORE_MAX_BYTES:
            errors.append(f"core pack {core.size} exceeds {CORE_MAX_BYTES}")
        if sd.toc_bytes > TF_TOC_MAX_BYTES:
            errors.append(f"SD pack TOC {sd.toc_bytes} exceeds {TF_TOC_MAX_BYTES}")
        core_ids = {ARCHIVE_IDS[name] for name in CORE_ARCHIVES}
        sd_ids = {ARCHIVE_IDS[name] for name in SD_ARCHIVES}
        if set(core.archives) != core_ids:
            errors.append(f"core archive IDs {set(core.archives)} != {core_ids}")
        if set(sd.archives) != sd_ids:
            errors.append(f"SD archive IDs {set(sd.archives)} != {sd_ids}")
        if set(core.archives) & set(sd.archives):
            errors.append("core and SD packs overlap archive ownership")
        font_chunks = core.archives.get(ARCHIVE_IDS["FONT"], [])
        if len(font_chunks) <= 1 or font_chunks[1].fmt != PACK_FORMAT_FONT10:
            errors.append("core pack is missing FONT10 chunk 1")
        for name in DIRECT_STAGED_ARCHIVES:
            chunks = sd.archives.get(ARCHIVE_IDS[name], [])
            maximum = max((chunk.size for chunk in chunks), default=0)
            if maximum > TRANSIENT_CHUNK_BYTES:
                errors.append(
                    f"{name} chunk {maximum} exceeds the 64 KiB staging buffer"
                )
        for chunk_id, chunk in enumerate(sd.archives.get(ARCHIVE_IDS["FBP"], [])):
            if chunk.size and (
                chunk.size != 320 * 200 or chunk.fmt != PACK_FORMAT_NATIVE
            ):
                errors.append(
                    f"FBP chunk {chunk_id} is not a native 320x200 canvas: "
                    f"size={chunk.size}, format={chunk.fmt}"
                )
        max_rng_frame, rng_frame_count = audit_rng_frames(sd_path, sd, errors)
        validate_event_template(event_path, core.set_id, errors)
        (
            max_event_sprites,
            max_player_sprites,
            max_battle_sprites,
        ) = audit_sprite_arenas(core_path, sd_path, core, sd, errors)

    font_manifest = manifest.get("font10", {}).get("font10", {})
    font_defines = {
        "bytes": "PAL_NATIVE_UI_GENERATED_FONT_IMAGE_BYTES",
        "glyph_count": "PAL_NATIVE_UI_GENERATED_FONT_GLYPH_COUNT",
        "payload_crc32": "PAL_NATIVE_UI_GENERATED_FONT_PAYLOAD_CRC32",
    }
    for manifest_name, define_name in font_defines.items():
        generated = generated_define(ui_header, define_name, errors)
        if generated is not None and font_manifest.get(manifest_name) != generated:
            errors.append(
                f"FONT10 manifest {manifest_name}={font_manifest.get(manifest_name)} "
                f"!= generated header {generated}"
            )
    metrics = font_manifest.get("metrics", {})
    for manifest_name, define_name in (
        ("cell_width", "PAL_NATIVE_UI_GENERATED_FONT_CELL_WIDTH"),
        ("cell_height", "PAL_NATIVE_UI_GENERATED_FONT_CELL_HEIGHT"),
    ):
        generated = generated_define(ui_header, define_name, errors)
        if generated is not None and metrics.get(manifest_name) != generated:
            errors.append(
                f"FONT10 metric {manifest_name}={metrics.get(manifest_name)} "
                f"!= generated header {generated}"
            )
    if core is not None:
        font_chunks = core.archives.get(ARCHIVE_IDS["FONT"], [])
        if len(font_chunks) > 1 and font_chunks[1].size != font_manifest.get("bytes"):
            errors.append("packed FONT10 byte count differs from its manifest")

    if manifest.get("runtime") != {
        "heap_required": False,
        "payloads_are_runtime_native": True,
        "runtime_decompression_required": False,
    }:
        errors.append("manifest runtime contract is not native/no-heap/no-decode")
    for key, path, pack in (("nor", core_path, core), ("tf", sd_path, sd)):
        item = manifest.get("packs", {}).get(key, {})
        if item.get("size") != path.stat().st_size or item.get("sha256") != sha256_file(path):
            errors.append(f"manifest identity mismatch for {key} pack")
        if pack is not None and item.get("toc_bytes") != pack.toc_bytes:
            errors.append(f"manifest TOC size mismatch for {key} pack")
        if pack is not None and (
            item.get("pack_set_id") != pack.set_id or item.get("crc32") != pack.crc32
        ):
            errors.append(f"manifest header identity mismatch for {key} pack")
        for name, selection in item.get("chunk_selection", {}).items():
            if name != "FONT" and selection.get("absent_chunk_count") != 0:
                errors.append(f"{key}/{name} is not a complete archive selection")

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

    print("Xiaomiao SD-only check passed")
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
    print(f"  core pack: {core_path.stat().st_size} / {CORE_MAX_BYTES} bytes")
    print(f"  SD pack: {sd_path.stat().st_size} bytes; TOC {sd.toc_bytes} / {TF_TOC_MAX_BYTES}")
    print(
        f"  RNG frames: max {max_rng_frame} bytes across {rng_frame_count} "
        f"frames; bounded input window {RNG_INPUT_WINDOW_BYTES} bytes"
    )
    print(f"  EVENT.DEF: {event_path.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
