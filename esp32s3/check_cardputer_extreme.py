#!/usr/bin/env python3
"""Audit the 8MB/no-PSRAM Cardputer ADV extreme build and chapter packs."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import shlex
import struct
import subprocess
import sys
import zlib
from pathlib import Path


PACK_MAGIC = 0x4B504C50
PACK_VERSION = 1
PACK_HEADER_BYTES = 32
ARCHIVE_ENTRY_BYTES = 12
CHUNK_ENTRY_BYTES = 16
CHUNK_FLAG_COMPRESSED = 0x0001
APP_BYTES = 0x100000
NOR_OFFSET = 0x110000
NOR_BYTES = 0x6F0000
FLASH_BYTES = 8 * 1024 * 1024
TF_TOC_BYTES = 2048
MAX_DRAM_BSS = 210 * 1024
MAX_DRAM_DATA = 16 * 1024
MAX_DIRAM_STATIC = 256 * 1024
MAX_IRAM_STATIC = 64 * 1024
MIN_LINKER_DRAM_RESERVE = 80 * 1024
MAIN_TASK_STACK = 16 * 1024
MIN_POST_MAIN_STACK_RESERVE = 64 * 1024
MAX_STATIC_STACK = 2048
MUSIC_MAX_APP_BYTES = 512 * 1024
MUSIC_MAX_NOR_BYTES = NOR_BYTES * 97 // 100
MUSIC_MAX_DRAM_BSS = 220 * 1024
MUSIC_MAX_DIRAM_STATIC = 264 * 1024
MUSIC_MIN_LINKER_DRAM_RESERVE = 72 * 1024
MUSIC_MIN_POST_MAIN_STACK_RESERVE = 56 * 1024
MUSIC_OPL_STATE_BYTES = 1704
MUSIC_OPL_TABLE_BYTES = 24832
MUSIC_AUDIO_TASK_STACK_BYTES = 4096
MUSIC_TICK_BYTES = 315 * 2
MUSIC_MAX_OWNED_BSS = 12 * 1024
MUSIC_TRACK_IDS = {
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
PARTITION_MAGIC = 0x50AA
PARTITION_END_MAGIC = 0xEBEB
PARTITION_ENTRY_BYTES = 32

FORBIDDEN_PROJECT_SYMBOLS = {
    "malloc",
    "calloc",
    "realloc",
    "free",
    "aligned_alloc",
    "memalign",
    "valloc",
    "posix_memalign",
    "_malloc_r",
    "_calloc_r",
    "_realloc_r",
    "_free_r",
    "pvPortMalloc",
    "vPortFree",
    "heap_caps_malloc",
    "heap_caps_calloc",
    "heap_caps_realloc",
    "heap_caps_free",
    "heap_caps_aligned_alloc",
    "heap_caps_aligned_free",
    "heap_caps_aligned_calloc",
    "heap_caps_malloc_prefer",
    "heap_caps_realloc_prefer",
    "heap_caps_calloc_prefer",
    "heap_caps_malloc_extmem_enable",
    "PAL_RuntimeHeapAllocUnavailable",
    "PAL_RuntimeHeapZeroAllocUnavailable",
    "PAL_MKFDecompressChunk",
    "PAL_MKFGetDecompressedSize",
    "PAL_MKFCompressedChunkSizeUnavailable",
    "PAL_MKFCompressedChunkReadUnavailable",
    "PAL_RuntimeCodecUnavailable",
    "YJ1_Decompress",
    "YJ2_Decompress",
    "Decompress",
}

ARCHIVE = {
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
    "VOC": 16,
    "TEXT": 17,
    "FONT": 18,
    "SFX": 19,
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_text(argv: list[str]) -> str:
    return subprocess.check_output(argv, text=True, stderr=subprocess.STDOUT)


def parse_sections(text: str) -> dict[str, tuple[int, int]]:
    """Return ELF section name -> (size, VMA) from ``objdump -h``."""
    result: dict[str, tuple[int, int]] = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 4 and fields[0].isdigit():
            try:
                result[fields[1]] = (int(fields[2], 16), int(fields[3], 16))
            except ValueError:
                pass
    return result


def parse_symbols(text: str) -> dict[str, tuple[int, str]]:
    result: dict[str, tuple[int, str]] = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 4:
            continue
        try:
            size = int(fields[1], 16)
        except ValueError:
            continue
        result[fields[3]] = (size, fields[2])
    return result


def parse_symbol_sections(text: str) -> dict[str, tuple[int, str]]:
    result: dict[str, tuple[int, str]] = {}
    for line in text.splitlines():
        fields = line.split()
        sections = [field for field in fields if field.startswith(".")]
        if len(fields) < 3 or len(sections) != 1:
            continue
        try:
            size = int(fields[-2], 16)
        except ValueError:
            continue
        result[fields[-1]] = (size, sections[0])
    return result


def parse_memory_regions(text: str) -> dict[str, tuple[int, int]]:
    """Parse the GNU ld ``Memory Configuration`` table."""
    result: dict[str, tuple[int, int]] = {}
    in_table = False
    pattern = re.compile(
        r"^(\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)(?:\s+.*)?$"
    )
    for line in text.splitlines():
        if line.strip() == "Memory Configuration":
            in_table = True
            continue
        if not in_table:
            continue
        if line.strip() == "Linker script and memory map":
            break
        match = pattern.match(line.strip())
        if match and match.group(1) != "Name":
            result[match.group(1)] = (
                int(match.group(2), 16),
                int(match.group(3), 16),
            )
    return result


def forbidden_project_symbol(name: str) -> bool:
    """True for allocation/decompression calls forbidden in project objects.

    Read-only telemetry such as ``heap_caps_get_free_size`` is intentionally
    allowed; ESP-IDF itself necessarily owns and uses its heap implementation.
    """
    base = name.split("@", 1)[0]
    return (
        base in FORBIDDEN_PROJECT_SYMBOLS
        or base.startswith("_Zn")
        or base.startswith("_Zd")
    )


def parse_undefined_symbols(text: str) -> set[str]:
    result: set[str] = set()
    for line in text.splitlines():
        fields = line.split()
        if fields:
            result.add(fields[-1])
    return result


def compiler_sibling(compiler: Path, suffix: str) -> Path:
    name = compiler.name
    if not name.endswith("gcc"):
        return compiler.with_name(f"xtensa-esp32s3-elf-{suffix}")
    return compiler.with_name(name[:-3] + suffix)


def normalized_source(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root).as_posix()
    except ValueError:
        return str(path.resolve())


def load_source_inventory(path: Path, errors: list[str]) -> set[str]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"source inventory cannot be read: {exc}")
        return set()
    sources = data.get("sources")
    if data.get("version") != 1:
        errors.append(f"{path}: source inventory version must be 1")
    if (
        not isinstance(sources, list)
        or not all(isinstance(item, str) and item for item in sources)
    ):
        errors.append(f"{path}: sources must be a list of non-empty strings")
        return set()
    if len(sources) != len(set(sources)):
        errors.append(f"{path}: duplicate source inventory entries")
    if sources != sorted(sources):
        errors.append(f"{path}: source inventory must be sorted")
    return set(sources)


def project_compile_entries(
    compile_commands: Path,
    root: Path,
    errors: list[str],
) -> list[dict[str, str]]:
    try:
        entries = json.loads(compile_commands.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"compile_commands.json cannot be read: {exc}")
        return []
    if not isinstance(entries, list):
        errors.append("compile_commands.json root must be a list")
        return []
    result: list[dict[str, str]] = []
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        output = str(entry.get("output", "")).replace("\\", "/")
        if output.startswith("esp-idf/main/CMakeFiles/__idf_main.dir/"):
            result.append(entry)
    if not result:
        errors.append("compile_commands.json has no main-component entries")
    actual = [normalized_source(Path(str(entry.get("file", ""))), root) for entry in result]
    if len(actual) != len(set(actual)):
        errors.append("main-component compile commands contain duplicate sources")
    return result


def parse_partition_csv(path: Path, errors: list[str]) -> dict[str, tuple[int, int, int, int, int]]:
    type_names = {"app": 0x00, "data": 0x01}
    subtype_names = {
        (0x00, "factory"): 0x00,
        (0x01, "ota"): 0x00,
        (0x01, "phy"): 0x01,
        (0x01, "nvs"): 0x02,
    }
    result: dict[str, tuple[int, int, int, int, int]] = {}
    try:
        with path.open(newline="", encoding="utf-8") as stream:
            rows = csv.reader(
                line for line in stream if not line.lstrip().startswith("#")
            )
            for line_no, row in enumerate(rows, 1):
                if not row or all(not field.strip() for field in row):
                    continue
                if len(row) < 5:
                    errors.append(f"{path}:{line_no}: incomplete partition row")
                    continue
                label = row[0].strip()
                raw_type = row[1].strip().lower()
                raw_subtype = row[2].strip().lower()
                part_type = type_names.get(raw_type)
                if part_type is None:
                    part_type = int(raw_type, 0)
                subtype = subtype_names.get((part_type, raw_subtype))
                if subtype is None:
                    subtype = int(raw_subtype, 0)
                flags = int(row[5].strip(), 0) if len(row) > 5 and row[5].strip() else 0
                if label in result:
                    errors.append(f"{path}:{line_no}: duplicate partition {label}")
                    continue
                result[label] = (
                    part_type,
                    subtype,
                    int(row[3].strip(), 0),
                    int(row[4].strip(), 0),
                    flags,
                )
    except (OSError, ValueError) as exc:
        errors.append(f"{path}: cannot parse partition CSV: {exc}")
    return result


def parse_partition_binary(
    path: Path,
    errors: list[str],
) -> dict[str, tuple[int, int, int, int, int]]:
    result: dict[str, tuple[int, int, int, int, int]] = {}
    try:
        data = path.read_bytes()
    except OSError as exc:
        errors.append(f"{path}: cannot read generated partition table: {exc}")
        return result
    if len(data) != 0xC00:
        errors.append(f"{path}: generated partition table must be 0xc00 bytes")
    for offset in range(0, len(data) - PARTITION_ENTRY_BYTES + 1, PARTITION_ENTRY_BYTES):
        magic = struct.unpack_from("<H", data, offset)[0]
        if magic == PARTITION_END_MAGIC:
            break
        if magic != PARTITION_MAGIC:
            errors.append(f"{path}: invalid partition magic 0x{magic:04x} at 0x{offset:x}")
            break
        (
            _magic,
            part_type,
            subtype,
            part_offset,
            size,
            raw_label,
            flags,
        ) = struct.unpack_from("<HBBII16sI", data, offset)
        label = raw_label.split(b"\0", 1)[0].decode("ascii", errors="replace")
        if not label or label in result:
            errors.append(f"{path}: empty/duplicate partition label at 0x{offset:x}")
            continue
        result[label] = (part_type, subtype, part_offset, size, flags)
    return result


def parse_sdkconfig(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            result[key] = value.strip().strip('"')
        elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
            result[line[2 : -len(" is not set")]] = "n"
    return result


def parse_cmake_cache(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        key = key_and_type.split(":", 1)[0]
        result[key] = value
    return result


def parse_pack(path: Path, errors: list[str]) -> tuple[int, dict[int, dict[int, tuple[int, int]]]]:
    data = path.read_bytes()
    archives: dict[int, dict[int, tuple[int, int]]] = {}
    if len(data) < PACK_HEADER_BYTES:
        errors.append(f"{path}: pack is shorter than its header")
        return 0, archives
    magic = struct.unpack_from("<I", data, 0)[0]
    version, header_size, archive_count = struct.unpack_from("<HHH", data, 4)
    archive_table = struct.unpack_from("<I", data, 12)[0]
    data_offset = struct.unpack_from("<I", data, 16)[0]
    pack_set_id = struct.unpack_from("<I", data, 20)[0]
    declared_size = struct.unpack_from("<I", data, 24)[0]
    declared_crc = struct.unpack_from("<I", data, 28)[0]
    if magic != PACK_MAGIC or version != PACK_VERSION or header_size != PACK_HEADER_BYTES:
        errors.append(f"{path}: invalid pack header")
        return data_offset, archives
    if declared_size != len(data):
        errors.append(f"{path}: declared size {declared_size} != {len(data)}")
    crc_image = bytearray(data)
    struct.pack_into("<I", crc_image, 28, 0)
    actual_crc = zlib.crc32(crc_image) & 0xFFFFFFFF
    if pack_set_id == 0:
        errors.append(f"{path}: pack-set ID is zero")
    if declared_crc == 0 or declared_crc != actual_crc:
        errors.append(
            f"{path}: CRC32 {declared_crc:#010x} != {actual_crc:#010x}"
        )
    if archive_table + archive_count * ARCHIVE_ENTRY_BYTES > len(data):
        errors.append(f"{path}: archive table is out of bounds")
        return data_offset, archives
    for archive_index in range(archive_count):
        entry = archive_table + archive_index * ARCHIVE_ENTRY_BYTES
        archive_id, chunk_count = struct.unpack_from("<HH", data, entry)
        chunk_table = struct.unpack_from("<I", data, entry + 4)[0]
        if archive_id in archives:
            errors.append(f"{path}: duplicate archive id {archive_id}")
            continue
        chunks: dict[int, tuple[int, int]] = {}
        archives[archive_id] = chunks
        if chunk_table + chunk_count * CHUNK_ENTRY_BYTES > len(data):
            errors.append(f"{path}: archive {archive_id} chunk table is out of bounds")
            continue
        for chunk_id in range(chunk_count):
            chunk = chunk_table + chunk_id * CHUNK_ENTRY_BYTES
            offset, size, fmt, flags = struct.unpack_from("<IIHH", data, chunk)
            if offset > len(data) or size > len(data) - offset:
                errors.append(
                    f"{path}: archive {archive_id} chunk {chunk_id} is out of bounds"
                )
            if flags & CHUNK_FLAG_COMPRESSED:
                errors.append(
                    f"{path}: archive {archive_id} chunk {chunk_id} is compressed"
                )
            chunks[chunk_id] = (size, fmt)
    return data_offset, archives


def nonempty(archives: dict[int, dict[int, tuple[int, int]]], archive_id: int) -> set[int]:
    return {
        chunk_id
        for chunk_id, (size, _fmt) in archives.get(archive_id, {}).items()
        if size
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--nor-pack", required=True, type=Path)
    parser.add_argument("--tf-pack", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--expected-compiler", type=Path)
    parser.add_argument("--source-inventory", type=Path)
    parser.add_argument(
        "--firmware-profile",
        choices=("no-audio", "rix-music"),
        default="no-audio",
    )
    args = parser.parse_args()

    music_profile = args.firmware_profile == "rix-music"
    root = args.root.resolve()
    build = args.build_dir.resolve()
    nor_path = args.nor_pack.resolve()
    tf_path = args.tf_pack.resolve()
    manifest_path = args.manifest.resolve()
    inventory_path = (
        args.source_inventory.resolve()
        if args.source_inventory
        else root
        / "esp32s3"
        / (
            "cardputer_extreme_music_sources.json"
            if music_profile
            else "cardputer_extreme_sources.json"
        )
    )
    elf = build / "sdlpal_cardputer_extreme.elf"
    app_bin = build / "sdlpal_cardputer_extreme.bin"
    map_path = build / "sdlpal_cardputer_extreme.map"
    project_description_path = build / "project_description.json"
    compile_commands_path = build / "compile_commands.json"
    flasher_args_path = build / "flasher_args.json"
    partition_bin = build / "partition_table/partition-table.bin"
    main_archive = build / "esp-idf/main/libmain.a"
    ninja_path = build / "build.ninja"
    cache_path = build / "CMakeCache.txt"
    sdkconfig = build / "sdkconfig"
    if not sdkconfig.exists():
        if cache_path.is_file():
            for line in cache_path.read_text(
                encoding="utf-8", errors="replace"
            ).splitlines():
                if line.startswith("SDKCONFIG:") and not line.startswith("SDKCONFIG_DEFAULTS:"):
                    sdkconfig = Path(line.split("=", 1)[1])
                    break
    errors: list[str] = []

    for path in (
        elf,
        app_bin,
        map_path,
        project_description_path,
        compile_commands_path,
        flasher_args_path,
        partition_bin,
        main_archive,
        ninja_path,
        cache_path,
        sdkconfig,
        inventory_path,
        nor_path,
        tf_path,
        manifest_path,
    ):
        if not path.is_file():
            errors.append(f"missing required artifact: {path}")
    if errors:
        print("\n".join(f"ERROR: {item}" for item in errors))
        return 1

    try:
        project_description = json.loads(
            project_description_path.read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"project_description.json cannot be read: {exc}")
        project_description = {}
    compiler = Path(str(project_description.get("c_compiler", "")))
    if not compiler.is_absolute():
        errors.append(f"project compiler is not an absolute path: {compiler}")
    compiler = compiler.resolve()
    cxx_compiler = compiler_sibling(compiler, "g++")
    objdump = compiler_sibling(compiler, "objdump")
    nm = compiler_sibling(compiler, "nm")
    for tool in (compiler, cxx_compiler, objdump, nm):
        if not tool.is_file():
            errors.append(f"recorded build tool is missing: {tool}")
    if compiler.name != "xtensa-esp32s3-elf-gcc":
        errors.append(f"unexpected target compiler: {compiler}")
    if args.expected_compiler and compiler != args.expected_compiler.resolve():
        errors.append(
            f"actual compiler {compiler} != requested {args.expected_compiler.resolve()}"
        )
    if project_description.get("project_name") != "sdlpal_cardputer_extreme":
        errors.append(
            f"unexpected project name: {project_description.get('project_name')!r}"
        )
    if project_description.get("target") != "esp32s3":
        errors.append(f"unexpected IDF target: {project_description.get('target')!r}")
    if Path(str(project_description.get("project_path", ""))).resolve() != (
        root / "esp32s3"
    ):
        errors.append("project_description.json points at a different source tree")

    sections: dict[str, tuple[int, int]] = {}
    symbols: dict[str, tuple[int, str]] = {}
    symbol_sections: dict[str, tuple[int, str]] = {}
    toolchain_version = "unavailable"
    if compiler.is_file() and objdump.is_file() and nm.is_file():
        try:
            sections = parse_sections(run_text([str(objdump), "-h", str(elf)]))
            symbols = parse_symbols(
                run_text([str(nm), "-S", "--size-sort", str(elf)])
            )
            symbol_sections = parse_symbol_sections(
                run_text([str(objdump), "-t", str(elf)])
            )
            toolchain_version = run_text([str(compiler), "--version"]).splitlines()[0]
        except (OSError, subprocess.CalledProcessError) as exc:
            errors.append(f"recorded Xtensa toolchain failed: {exc}")

    section_size = lambda name: sections.get(name, (-1, 0))[0]
    section_vma = lambda name: sections.get(name, (0, -1))[1]
    dram_bss = section_size(".dram0.bss")
    dram_data = section_size(".dram0.data")
    max_dram_bss = MUSIC_MAX_DRAM_BSS if music_profile else MAX_DRAM_BSS
    max_diram_static = (
        MUSIC_MAX_DIRAM_STATIC if music_profile else MAX_DIRAM_STATIC
    )
    min_linker_dram_reserve = (
        MUSIC_MIN_LINKER_DRAM_RESERVE
        if music_profile
        else MIN_LINKER_DRAM_RESERVE
    )
    min_post_main_stack_reserve = (
        MUSIC_MIN_POST_MAIN_STACK_RESERVE
        if music_profile
        else MIN_POST_MAIN_STACK_RESERVE
    )
    if dram_bss < 0 or dram_bss > max_dram_bss:
        errors.append(f".dram0.bss {dram_bss} exceeds {max_dram_bss}")
    if dram_data < 0 or dram_data > MAX_DRAM_DATA:
        errors.append(f".dram0.data {dram_data} exceeds {MAX_DRAM_DATA}")
    if section_size(".ext_ram.bss") not in (-1, 0):
        errors.append(".ext_ram.bss must be absent/zero in the no-PSRAM profile")
    diram_static = sum(
        max(0, section_size(name))
        for name in (".dram0.dummy", ".dram0.data", ".noinit", ".dram0.bss")
    )
    if diram_static > max_diram_static:
        errors.append(
            f"DIRAM static use {diram_static} exceeds {max_diram_static}"
        )
    iram_static = sum(
        max(0, section_size(name))
        for name in (
            ".iram0.vectors",
            ".iram0.text",
            ".iram0.text_end",
            ".iram0.data",
            ".iram0.bss",
        )
    )
    if iram_static > MAX_IRAM_STATIC:
        errors.append(f"IRAM static use {iram_static} exceeds {MAX_IRAM_STATIC}")

    memory_regions = parse_memory_regions(
        map_path.read_text(encoding="utf-8", errors="replace")
    )
    dram_region = memory_regions.get("dram0_0_seg")
    heap_start = section_vma(".dram0.heap_start")
    if dram_region is None or heap_start < 0:
        errors.append("cannot derive linker DRAM reserve from map/ELF")
        linker_dram_reserve = -1
    else:
        dram_origin, dram_length = dram_region
        dram_end = dram_origin + dram_length
        linker_dram_reserve = dram_end - heap_start
        if (
            heap_start < dram_origin
            or linker_dram_reserve < min_linker_dram_reserve
        ):
            errors.append(
                f"linker DRAM reserve {linker_dram_reserve} is below "
                f"{min_linker_dram_reserve}"
            )
    post_main_stack_reserve = (
        linker_dram_reserve - MAIN_TASK_STACK if linker_dram_reserve >= 0 else -1
    )
    if 0 <= post_main_stack_reserve < min_post_main_stack_reserve:
        errors.append(
            f"linker DRAM reserve after main-task stack {post_main_stack_reserve} "
            f"is below {min_post_main_stack_reserve}"
        )

    required_sizes = {
        "pal_sram_framebuffer": 320 * 200,
        "pal_sram_aux_framebuffer": 320 * 200,
        "pal_sram_display_dma": 4096,
        # 423 contiguous scene records plus the explicit sparse event 5334.
        "pal_sram_extreme_global_event_objects": 424 * 32,
        "pal_sram_extreme_engine_tf_toc": TF_TOC_BYTES,
        "g_rgSpriteToDraw": 512 * 12,
        "internal_buffer": 5 * 256,
    }
    if music_profile:
        required_sizes.update(
            {
                "pal_sram_audio_task_stack_bytes": MUSIC_AUDIO_TASK_STACK_BYTES,
                "pal_sram_audio_tick_bytes": MUSIC_TICK_BYTES,
            }
        )
    for name, expected in required_sizes.items():
        actual = symbols.get(name, (-1, ""))[0]
        if actual != expected:
            errors.append(f"{name}: expected {expected} bytes, got {actual}")
    framebuffer_symbols = [
        name for name, (size, _kind) in symbols.items() if size == 320 * 200
    ]
    if sorted(framebuffer_symbols) != [
        "pal_sram_aux_framebuffer",
        "pal_sram_framebuffer",
    ]:
        errors.append(
            "expected exactly two 64,000-byte logical framebuffer symbols, got "
            + ",".join(sorted(framebuffer_symbols))
        )
    forbidden_prefixes = ["pal_psram_", "pal_sfx_"]
    if not music_profile:
        forbidden_prefixes.extend(
            (
                "pal_audio_",
                "pal_sram_audio_",
                "pal_music_",
                "pal_sram_music_",
            )
        )
    for name in symbols:
        if name.startswith(tuple(forbidden_prefixes)):
            errors.append(f"forbidden linked symbol: {name}")
        if music_profile and (
            name.startswith("PalSfx_")
            or name in ("PalAudio_OpenSfx", "PalAudio_MixSfx")
            or name.startswith(("pal_psram_sfx_", "pal_sram_audio_mix_"))
        ):
            errors.append(f"SFX symbol linked into music-only profile: {name}")
    if music_profile:
        opl_state = [
            (name, size)
            for name, (size, _kind) in symbols.items()
            if "pal_mame_opl2_state" in name
        ]
        if len(opl_state) != 1 or opl_state[0][1] != MUSIC_OPL_STATE_BYTES:
            errors.append(
                f"fixed OPL2 state must be one {MUSIC_OPL_STATE_BYTES}-byte "
                f"symbol, got {opl_state!r}"
            )
        elif symbol_sections.get(opl_state[0][0], (0, ""))[1] != ".dram0.bss":
            errors.append("fixed OPL2 state is not placed in .dram0.bss")
        opl_tables = [
            (name, size, section)
            for name, (size, section) in symbol_sections.items()
            if "pal_mame_opl2_fixed_" in name
        ]
        opl_table_bytes = sum(
            size
            for _name, size, section in opl_tables
            if section == ".flash.rodata"
        )
        if (
            len(opl_tables) != 4
            or any(section != ".flash.rodata" for _name, _size, section in opl_tables)
            or opl_table_bytes != MUSIC_OPL_TABLE_BYTES
        ):
            errors.append(
                f"fixed OPL2 tables must be four .flash.rodata symbols totaling "
                f"{MUSIC_OPL_TABLE_BYTES}, got {opl_tables!r}"
            )
        music_owned_bss = sum(
            size
            for name, (size, kind) in symbols.items()
            if kind.lower() == "b"
            and any(
                token in name
                for token in (
                    "pal_audio_",
                    "pal_sram_audio_",
                    "pal_music_",
                    "pal_sram_music_",
                    "pal_mame_opl2_state",
                )
            )
        )
        if music_owned_bss <= 0 or music_owned_bss > MUSIC_MAX_OWNED_BSS:
            errors.append(
                f"named music/audio BSS {music_owned_bss} is outside "
                f"1..{MUSIC_MAX_OWNED_BSS}"
            )
    for name in (
        "CoreS3Se_Begin",
        "PAL_MKFDecompressChunk",
        "PAL_MKFGetDecompressedSize",
        "YJ1_Decompress",
        "YJ2_Decompress",
        "Decompress",
    ):
        if name in symbols:
            errors.append(f"forbidden linked symbol: {name}")
    required_symbols = [
        "CardputerExtreme_Begin",
        "CardputerExtreme_MountTf",
        "CardputerExtreme_PollKey",
        "CardputerExtreme_FlushIndexedFramebuffer",
        "CardputerExtreme_ScaleIndexedStrip",
        "PalEngineBridge_LogRuntimeMemory",
        "PalEngineBridge_ReadNativeRngFrame",
    ]
    if music_profile:
        required_symbols.extend(
            (
                "AUDIO_PlayMusic",
                "AUDIO_PlaySound",
                "CardputerExtremeAudio_Begin",
                "CardputerExtremeAudio_LogTelemetry",
                "PalMameOpl2_Init",
                "PalMameOpl2_Render",
                "PalMusic_MapMus",
            )
        )
    for name in required_symbols:
        if name not in symbols:
            errors.append(f"required linked symbol missing: {name}")

    if app_bin.stat().st_size > APP_BYTES:
        errors.append(f"app binary {app_bin.stat().st_size} exceeds {APP_BYTES}")
    if music_profile and app_bin.stat().st_size > MUSIC_MAX_APP_BYTES:
        errors.append(
            f"music app binary {app_bin.stat().st_size} exceeds soft budget "
            f"{MUSIC_MAX_APP_BYTES}"
        )
    if nor_path.stat().st_size > NOR_BYTES:
        errors.append(f"NOR pack {nor_path.stat().st_size} exceeds {NOR_BYTES}")
    if music_profile and nor_path.stat().st_size > MUSIC_MAX_NOR_BYTES:
        errors.append(
            f"music NOR pack {nor_path.stat().st_size} exceeds 97% soft budget "
            f"{MUSIC_MAX_NOR_BYTES}"
        )
    if tf_path.stat().st_size == 0:
        errors.append("TF pack is empty")

    cmake_cache = parse_cmake_cache(cache_path)
    required_cache = {
        "PAL_CORES3SE_ENGINE_HOST": "1",
        "CARDPUTER_EXTREME_NO_PSRAM": "ON",
        "CARDPUTER_EXTREME_MUSIC": "ON" if music_profile else "OFF",
    }
    for key, expected in required_cache.items():
        if cmake_cache.get(key) != expected:
            errors.append(
                f"CMake cache {key}: expected {expected}, "
                f"got {cmake_cache.get(key)!r}"
            )

    config = parse_sdkconfig(sdkconfig)
    required_config = {
        "CONFIG_IDF_TARGET": "esp32s3",
        "CONFIG_ESPTOOLPY_FLASHSIZE_8MB": "y",
        "CONFIG_ESPTOOLPY_FLASHSIZE": "8MB",
        "CONFIG_PARTITION_TABLE_CUSTOM": "y",
        "CONFIG_PARTITION_TABLE_FILENAME": "partitions_cardputer_extreme.csv",
        "CONFIG_SPIRAM": "n",
        "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240": "y",
        "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ": "240",
        "CONFIG_FREERTOS_UNICORE": "y",
        "CONFIG_FREERTOS_HZ": "1000",
        "CONFIG_FATFS_VOLUME_COUNT": "1",
        "CONFIG_FATFS_LFN_NONE": "y",
        "CONFIG_FATFS_USE_DYN_BUFFERS": "n",
        "CONFIG_HEAP_POISONING_DISABLED": "y",
        "CONFIG_HEAP_TRACING_OFF": "y",
    }
    for key, expected in required_config.items():
        if config.get(key) != expected:
            errors.append(f"{key}: expected {expected}, got {config.get(key)!r}")
    if int(config.get("CONFIG_ESP_MAIN_TASK_STACK_SIZE", "0"), 0) != MAIN_TASK_STACK:
        errors.append(
            f"CONFIG_ESP_MAIN_TASK_STACK_SIZE must be exactly {MAIN_TASK_STACK}"
        )

    partition_path = root / "esp32s3/partitions_cardputer_extreme.csv"
    partitions = parse_partition_csv(partition_path, errors)
    generated_partitions = parse_partition_binary(partition_bin, errors)
    if generated_partitions != partitions:
        errors.append(
            "generated partition-table.bin does not exactly match "
            "partitions_cardputer_extreme.csv"
        )
    factory = partitions.get("factory", (-1, -1, -1, -1, -1))
    pal_nor = partitions.get("pal_nor", (-1, -1, -1, -1, -1))
    if factory[2:4] != (0x10000, APP_BYTES):
        errors.append("factory partition must be 0x10000+0x100000")
    if pal_nor[2:4] != (NOR_OFFSET, NOR_BYTES):
        errors.append("pal_nor partition must be 0x110000+0x6f0000")
    if (
        not partitions
        or max(entry[2] + entry[3] for entry in partitions.values()) != FLASH_BYTES
    ):
        errors.append("partition table does not end exactly at 8MB")
    try:
        flasher_args = json.loads(flasher_args_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"flasher_args.json cannot be read: {exc}")
        flasher_args = {}
    if flasher_args.get("flash_settings") != {
        "flash_mode": "dio",
        "flash_size": "8MB",
        "flash_freq": "80m",
    }:
        errors.append("generated flash settings are not dio/80m/8MB")
    flash_files = flasher_args.get("flash_files", {})
    if flash_files.get("0x8000") != "partition_table/partition-table.bin":
        errors.append("generated flash args do not flash partition-table.bin at 0x8000")
    if flash_files.get("0x10000") != "sdlpal_cardputer_extreme.bin":
        errors.append("generated flash args do not flash the app at 0x10000")

    nor_toc, nor = parse_pack(nor_path, errors)
    tf_toc, tf = parse_pack(tf_path, errors)
    nor_header = nor_path.read_bytes()[:PACK_HEADER_BYTES]
    tf_header = tf_path.read_bytes()[:PACK_HEADER_BYTES]
    nor_set_id = struct.unpack_from("<I", nor_header, 20)[0]
    tf_set_id = struct.unpack_from("<I", tf_header, 20)[0]
    if nor_set_id == 0 or nor_set_id != tf_set_id:
        errors.append(
            f"NOR/TF pack-set IDs differ: {nor_set_id:#010x} != {tf_set_id:#010x}"
        )
    if tf_toc > TF_TOC_BYTES:
        errors.append(f"TF TOC {tf_toc} exceeds SRAM capacity {TF_TOC_BYTES}")
    if nonempty(nor, ARCHIVE["FBP"]) != {0, 1, 60}:
        errors.append("unexpected NOR FBP chapter selection")
    if nonempty(tf, ARCHIVE["FBP"]) != {3, 6, 8, 21}:
        errors.append("unexpected TF FBP chapter selection")
    if nonempty(tf, ARCHIVE["RNG"]) != {1}:
        errors.append("unexpected TF RNG chapter selection")
    if nonempty(nor, ARCHIVE["MAP"]) != nonempty(nor, ARCHIVE["GOP"]):
        errors.append("MAP/GOP chapter selections differ")
    if nor.get(ARCHIVE["SSS"], {}).get(0, (0, 0))[0] != 423 * 32:
        errors.append("SSS event-object prefix is not exactly 423 records")
    for archive_id in set(nor) & set(tf):
        overlap = nonempty(nor, archive_id) & nonempty(tf, archive_id)
        if overlap:
            errors.append(
                f"archive {archive_id} has non-empty chunks in both packs: {sorted(overlap)}"
            )
    if music_profile:
        for name in ("MIDI", "VOC", "SFX"):
            archive_id = ARCHIVE[name]
            if archive_id in nor or archive_id in tf:
                errors.append(
                    f"{name} archive is present in the music-only profile"
                )
        mus = nor.get(ARCHIVE["MUS"], {})
        if len(mus) != 88:
            errors.append(
                f"MUS archive has {len(mus)} slots instead of sparse 88"
            )
        if nonempty(nor, ARCHIVE["MUS"]) != MUSIC_TRACK_IDS:
            errors.append("unexpected MUS chapter track selection")
        if ARCHIVE["MUS"] in tf:
            errors.append("MUS must be mapped from NOR, not TF")
        if any(fmt != 1 for size, fmt in mus.values() if size):
            errors.append("selected MUS chunks are not runtime-native")
    else:
        audio_ids = {
            ARCHIVE[name] for name in ("MIDI", "MUS", "VOC", "SFX")
        }
        if audio_ids & (set(nor) | set(tf)):
            errors.append("audio archives are present in the no-audio profile")

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"manifest cannot be read: {exc}")
        manifest = {}
    if manifest.get("schema") != "sdlpal-embedded-pack-manifest":
        errors.append("unexpected manifest schema")
    manifest_profile = manifest.get("pack_layout", {}).get("profile")
    if music_profile:
        if manifest_profile != "rix-music":
            errors.append(
                f"manifest profile is {manifest_profile!r}, expected 'rix-music'"
            )
    elif manifest_profile is not None:
        errors.append(
            f"no-audio manifest unexpectedly selects profile {manifest_profile!r}"
        )
    runtime = manifest.get("runtime", {})
    if runtime != {
        "heap_required": False,
        "payloads_are_runtime_native": True,
        "runtime_decompression_required": False,
    }:
        errors.append(f"manifest runtime contract mismatch: {runtime!r}")
    for key, path in (("nor", nor_path), ("tf", tf_path)):
        pack_manifest = manifest.get("packs", {}).get(key, {})
        recorded = pack_manifest.get("size")
        if recorded != path.stat().st_size:
            errors.append(f"manifest {key} pack size mismatch")
        header = path.read_bytes()[:PACK_HEADER_BYTES]
        if pack_manifest.get("pack_set_id") != struct.unpack_from("<I", header, 20)[0]:
            errors.append(f"manifest {key} pack-set ID mismatch")
        if pack_manifest.get("crc32") != struct.unpack_from("<I", header, 28)[0]:
            errors.append(f"manifest {key} CRC32 mismatch")
    layout_path = root / "tools/pal_pack_layout_cardputer_extreme.json"
    try:
        layout = json.loads(layout_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"chapter layout cannot be read: {exc}")
        layout = {}
    closure = layout.get("closure_audit", {})
    if closure.get("status") != "candidate-not-route-proven":
        errors.append("chapter layout must explicitly remain candidate-not-route-proven")
    if closure.get("known_unresolved_scene_destinations") != []:
        errors.append("chapter layout has unexpected unresolved scene destinations")
    if closure.get("chapter_complete_scene_destinations") != [21]:
        errors.append("chapter layout must record scene 21 as the completed boundary")
    if closure.get("known_unresolved_event_object_targets") != []:
        errors.append("chapter layout has unexpected unresolved event-object targets")
    if closure.get("supported_sparse_event_object_targets") != [5334]:
        errors.append("chapter layout must record sparse event-object 5334 support")
    if closure.get("known_unmodeled_script_root_classes") != [
        "item-use-equip-throw",
        "magic-use-success",
        "poison-player-dying-friend-death",
    ]:
        errors.append("chapter layout must explicitly record unmodeled script roots")
    if manifest.get("pack_layout", {}).get("sha256") != sha256_file(layout_path):
        errors.append("manifest layout SHA-256 mismatch")
    data_dir = Path(manifest.get("data_dir", ""))
    for source in manifest.get("source_files", []):
        source_path = data_dir / str(source.get("path", ""))
        if (
            not source_path.is_file()
            or source_path.stat().st_size != source.get("size")
            or sha256_file(source_path) != source.get("sha256")
        ):
            errors.append(f"manifest source mismatch: {source_path}")

    expected_sources = load_source_inventory(inventory_path, errors)
    compile_entries = project_compile_entries(compile_commands_path, root, errors)
    actual_sources = {
        normalized_source(Path(str(entry.get("file", ""))), root)
        for entry in compile_entries
    }
    for source in sorted(actual_sources - expected_sources):
        errors.append(f"linked source missing from inventory: {source}")
    for source in sorted(expected_sources - actual_sources):
        errors.append(f"inventory source is not target-linked: {source}")

    project_objects: list[tuple[str, Path]] = []
    required_compile_tokens = (
        "-DPAL_CARDPUTER_EXTREME=1",
        "-DPAL_EXTREME_TWO_SCREENS=1",
        "-DPAL_EXTREME_SPRITES_TO_DRAW=512",
        "-DPAL_GLOBAL_BUFFER_SIZE=256",
        "-DPAL_NO_RUNTIME_HEAP=1",
        "-DPAL_NO_RUNTIME_DECOMPRESS=1",
        "-DPAL_ESP_CORES3SE_NO_SFX=1",
        "-fstack-usage",
    )
    profile_compile_tokens = (
        ("-DPAL_EXTREME_RIX_MUSIC=1", "-DPAL_CONTRACT_NO_SFX=1")
        if music_profile
        else (
            "-DPAL_CONTRACT_NO_AUDIO=1",
            "-DPAL_ESP_CORES3SE_NO_AUDIO=1",
        )
    )
    forbidden_compile_tokens = (
        ("-DPAL_CONTRACT_NO_AUDIO=1", "-DPAL_ESP_CORES3SE_NO_AUDIO=1")
        if music_profile
        else ("-DPAL_EXTREME_RIX_MUSIC=1",)
    )
    for entry in compile_entries:
        source = normalized_source(Path(str(entry.get("file", ""))), root)
        command = str(entry.get("command", ""))
        try:
            command_words = shlex.split(command)
        except ValueError as exc:
            errors.append(f"{source}: malformed compile command: {exc}")
            command_words = []
        expected_source_compiler = (
            cxx_compiler
            if Path(source).suffix.lower() in (".cc", ".cpp", ".cxx")
            else compiler
        )
        compilers = [
            Path(word).resolve()
            for word in command_words
            if Path(word).name
            in ("xtensa-esp32s3-elf-gcc", "xtensa-esp32s3-elf-g++")
        ]
        if compilers != [expected_source_compiler]:
            errors.append(
                f"{source}: compile command did not use expected compiler "
                f"{expected_source_compiler}"
            )
        for token in (*required_compile_tokens, *profile_compile_tokens):
            if token not in command_words:
                errors.append(f"{source}: compile command is missing {token}")
        for token in forbidden_compile_tokens:
            if token in command_words:
                errors.append(
                    f"{source}: compile command unexpectedly contains {token}"
                )
        output = Path(str(entry.get("output", "")))
        if not output.is_absolute():
            output = Path(str(entry.get("directory", build))) / output
        output = output.resolve()
        if not output.is_file():
            errors.append(f"{source}: missing project object {output}")
        else:
            project_objects.append((source, output))

    if nm.is_file():
        for source, obj in project_objects:
            try:
                undefined = parse_undefined_symbols(
                    run_text([str(nm), "-u", str(obj)])
                )
            except (OSError, subprocess.CalledProcessError) as exc:
                errors.append(f"{source}: cannot inspect undefined symbols: {exc}")
                continue
            for name in sorted(undefined):
                if forbidden_project_symbol(name):
                    errors.append(
                        f"{source}: forbidden project object reference: {name}"
                    )

    ninja = ninja_path.read_text(encoding="utf-8", errors="replace")
    for token in (
        "PAL_CARDPUTER_EXTREME=1",
        "PAL_EXTREME_TWO_SCREENS=1",
        "cardputer_extreme_board.c",
        "cardputer_extreme_memory.c",
        "cardputer_extreme_scaler.c",
    ):
        if token not in ninja:
            errors.append(f"build graph is missing {token}")
    if music_profile:
        for token in (
            "PAL_EXTREME_RIX_MUSIC=1",
            "PAL_CONTRACT_NO_SFX=1",
            "cardputer_extreme_audio.c",
            "pal_engine_target_music.cpp",
            "pal_mame_opl2_static.cpp",
            "pal_music_cache.c",
            "adplug/rix.cpp",
        ):
            if token not in ninja:
                errors.append(f"music build graph is missing {token}")
        for token in (
            "contract_noaudio.c.obj",
            "pal_audio_static.c.obj",
            "pal_sfx_cache.c.obj",
        ):
            if token in ninja:
                errors.append(
                    f"music-only build graph contains forbidden object {token}"
                )
    else:
        if "contract_noaudio.c.obj" not in ninja:
            errors.append("no-audio build graph is missing contract_noaudio.c")
        for token in (
            "cardputer_extreme_audio.c.obj",
            "pal_engine_target_music.cpp.obj",
            "pal_mame_opl2_static.cpp.obj",
        ):
            if token in ninja:
                errors.append(
                    f"no-audio build graph contains music object {token}"
                )
    if "cores3se_board.c.obj" in ninja or "cores3se_memory.c.obj" in ninja:
        errors.append("CoreS3 SE board/memory object leaked into Cardputer profile")

    stack_rows: list[tuple[int, str]] = []
    stack_reports = 0
    for source, obj in project_objects:
        path = obj.with_suffix(".su")
        if not path.is_file():
            errors.append(f"{source}: missing -fstack-usage artifact {path}")
            continue
        stack_reports += 1
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            fields = line.rsplit("\t", 2)
            if len(fields) != 3:
                errors.append(f"{path}: malformed stack-usage row: {line!r}")
                continue
            try:
                stack_rows.append((int(fields[1]), fields[0]))
            except ValueError:
                errors.append(f"{path}: invalid stack size: {line!r}")
            if fields[2] != "static":
                errors.append(f"{path}: non-static stack usage: {line!r}")
    if not stack_rows:
        errors.append("no -fstack-usage artifacts found")
        max_stack = -1
        max_stack_name = "none"
    else:
        max_stack, max_stack_name = max(stack_rows)
        if max_stack > MAX_STATIC_STACK:
            errors.append(
                f"largest static stack frame {max_stack} in {max_stack_name} "
                f"exceeds {MAX_STATIC_STACK}"
            )

    print(f"Cardputer ADV extreme contract ({args.firmware_profile})")
    print(f"  compiler={toolchain_version}")
    print(
        f"  app={app_bin.stat().st_size} / {APP_BYTES} bytes, "
        f"NOR={nor_path.stat().st_size} / {NOR_BYTES}, TF={tf_path.stat().st_size}"
    )
    print(
        f"  .dram0.bss={dram_bss} / {max_dram_bss}, "
        f".dram0.data={dram_data} / {MAX_DRAM_DATA}, "
        f"max_stack={max_stack} ({max_stack_name})"
    )
    print(
        f"  DIRAM_static={diram_static}/{max_diram_static}, "
        f"IRAM_static={iram_static}/{MAX_IRAM_STATIC}, "
        f"linker_DRAM_reserve={linker_dram_reserve}, "
        f"after_main_stack={post_main_stack_reserve}"
    )
    print(
        f"  screens=2x64000, TF_TOC={tf_toc}/{TF_TOC_BYTES}, "
        f"MAP/GOP={len(nonempty(nor, ARCHIVE['MAP']))} chunks, "
        f"sources={len(actual_sources)}, stack_reports={stack_reports}"
    )
    if music_profile:
        print(
            f"  music_owned_bss={music_owned_bss}/{MUSIC_MAX_OWNED_BSS}, "
            f"OPL_state={MUSIC_OPL_STATE_BYTES}, "
            f"OPL_tables={opl_table_bytes}/{MUSIC_OPL_TABLE_BYTES}, "
            f"MUS_tracks={len(MUSIC_TRACK_IDS)}"
        )
        print(
            f"  music_soft_flash: app={app_bin.stat().st_size}/"
            f"{MUSIC_MAX_APP_BYTES}, NOR={nor_path.stat().st_size}/"
            f"{MUSIC_MAX_NOR_BYTES}"
        )
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    if music_profile:
        print(
            "PASS: 8MB/no-PSRAM/two-screen RIX music-only contract; "
            "SFX DISABLED; STORY ROUTE NOT PROVEN"
        )
    else:
        print(
            "PASS: 8MB/no-PSRAM/two-screen mechanical candidate contract; "
            "STORY ROUTE NOT PROVEN (indirect roots/full route coverage pending)"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
