#!/usr/bin/env python3
"""Audit the Cardputer ADV 8 MiB/no-PSRAM chapter-cache build.

This checker deliberately joins the two halves of the profile contract:

* the generated ESP-IDF image really uses the dedicated 8 MiB partition
  layout and contains the cache implementation; and
* every host-built chapter artifact agrees with its manifest and with the
  CACHE catalog embedded in ``pal_core.pak``.

It does not regenerate packs or claim that the full story route is playable.
In particular, the manifest must retain the current event-object paging
limitation instead of silently presenting the resource closure as a finished
full-game port.
"""

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
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
ESP32S3_DIR = ROOT / "esp32s3"
TOOLS_DIR = ROOT / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import pal_chapter_pack_build as chapter  # noqa: E402
import pal_pack_build as pack  # noqa: E402


FLASH_BYTES = 0x800000
APP_BYTES = 0x0C0000
CORE_OFFSET = 0x0D0000
CORE_SLOT_BYTES = 0x460000
CACHE_OFFSET = 0x530000
CACHE_PARTITION_BYTES = 0x2D0000
CACHE_COMMIT_BYTES = 0x1000
CACHE_PAYLOAD_HARD_BYTES = 0x2CF000
CACHE_PACK_SOFT_BYTES = 0x2BF000
ACTIVE_TF_TOC_BYTES = 2048
BUNDLE_COUNT = 15
GENERATED_UI_HEADER = (
    ESP32S3_DIR / "main" / "generated" / "pal_native_ui_240x135.h"
)
MAX_DRAM_BSS = 212 * 1024
MAX_DRAM_DATA = 16 * 1024
MAX_DIRAM_STATIC = 264 * 1024
MAX_STATIC_STACK = 2048

PARTITION_MAGIC = 0x50AA
PARTITION_MD5_MAGIC = 0xEBEB
PARTITION_ENTRY_BYTES = 32

EXPECTED_PARTITIONS = (
    ("nvs", 0x01, 0x02, 0x009000, 0x006000, 0),
    ("phy_init", 0x01, 0x01, 0x00F000, 0x001000, 0),
    ("factory", 0x00, 0x00, 0x010000, APP_BYTES, 0),
    ("pal_core", 0x01, 0x40, CORE_OFFSET, CORE_SLOT_BYTES, 0),
    (
        "pal_cache",
        0x01,
        0x41,
        CACHE_OFFSET,
        CACHE_PARTITION_BYTES,
        0,
    ),
)

REQUIRED_CACHE_SYMBOLS = {
    "CardputerExtreme_ShowLoading",
    "PalEngineBridge_ClearOverlay",
    "PalEngineBridge_SetOverlayPackConst",
    "PalEngineChapterCache_BuildCommit",
    "PalEngineChapterCache_Decide",
    "PalEngineChapterCache_DescribeScene",
    "PalEngineChapterCache_OpenCatalog",
    "PalEngineChapterCache_PrepareScene",
    "PalEngineChapterCache_SceneNeedsBundle",
    "PalEngineChapterCache_TargetInit",
    "PalFont10_Open",
    "PalNativeUi_Font10IdentityMatches",
    "sha256_finish",
    "sha256_transform",
    "sha256_update",
}

FORBIDDEN_DECODER_SYMBOLS = {
    "Decompress",
    "PAL_MKFCompressedChunkReadUnavailable",
    "PAL_MKFCompressedChunkSizeUnavailable",
    "PAL_MKFDecompressChunk",
    "PAL_MKFGetDecompressedSize",
    "PAL_RuntimeCodecUnavailable",
    "YJ1_Decompress",
    "YJ2_Decompress",
}
FORBIDDEN_PROJECT_CALLS = {
    "malloc",
    "calloc",
    "realloc",
    "free",
    "PAL_MKFDecompressChunk",
    "PAL_MKFGetDecompressedSize",
    "YJ1_Decompress",
    "YJ2_Decompress",
}


@dataclass(frozen=True)
class PackMeta:
    filename: str
    size: int
    sha256: str
    crc32: int
    set_id: int
    toc_bytes: int
    archive_ids: tuple[int, ...]


@dataclass(frozen=True)
class BuildMetrics:
    app_bytes: int = 0
    elf_bytes: int = 0
    project_sources: int = 0
    linked_symbols: int = 0
    dram_bss_bytes: int = 0
    diram_static_bytes: int = 0
    max_stack_bytes: int = 0


@dataclass(frozen=True)
class PackMetrics:
    set_id: int = 0
    core_bytes: int = 0
    tf_bytes: int = 0
    full_bytes: int = 0
    tf_toc_bytes: int = 0
    catalog_bytes: int = 0
    largest_bundle_bytes: int = 0


def read_json(path: Path, errors: list[str], label: str) -> object:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"{label} cannot be read: {exc}")
        return {}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_sdkconfig(path: Path, errors: list[str]) -> dict[str, str]:
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        errors.append(f"{path}: cannot read sdkconfig: {exc}")
        return {}

    result: dict[str, str] = {}
    unset = re.compile(r"^# (CONFIG_[A-Za-z0-9_]+) is not set$")
    for line in lines:
        match = unset.match(line)
        if match:
            result[match.group(1)] = "n"
            continue
        if not line.startswith("CONFIG_") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if len(value) >= 2 and value[0] == value[-1] == '"':
            value = value[1:-1]
        result[key] = value
    return result


def parse_cmake_cache(path: Path, errors: list[str]) -> dict[str, str]:
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        errors.append(f"{path}: cannot read CMake cache: {exc}")
        return {}
    result: dict[str, str] = {}
    for line in lines:
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        left, value = line.split("=", 1)
        key = left.split(":", 1)[0]
        result[key] = value
    return result


def cmake_truth(value: str | None) -> bool:
    return value is not None and value.upper() in {"1", "ON", "TRUE", "YES", "Y"}


def parse_int_token(value: str) -> int:
    return int(value.strip(), 0)


def csv_partition_kind(
    type_token: str,
    subtype_token: str,
) -> tuple[int, int]:
    type_names = {"app": 0x00, "data": 0x01}
    type_value = type_names.get(type_token.lower())
    if type_value is None:
        type_value = parse_int_token(type_token)
    subtype_names = {
        (0x00, "factory"): 0x00,
        (0x01, "phy"): 0x01,
        (0x01, "nvs"): 0x02,
    }
    subtype_value = subtype_names.get((type_value, subtype_token.lower()))
    if subtype_value is None:
        subtype_value = parse_int_token(subtype_token)
    return type_value, subtype_value


def parse_partition_csv(
    path: Path,
    errors: list[str],
) -> list[tuple[str, int, int, int, int, int]]:
    result: list[tuple[str, int, int, int, int, int]] = []
    try:
        stream = path.open("r", encoding="utf-8", newline="")
    except OSError as exc:
        errors.append(f"{path}: cannot read partition CSV: {exc}")
        return result

    with stream:
        for line_number, row in enumerate(csv.reader(stream), 1):
            if not row or row[0].lstrip().startswith("#"):
                continue
            if len(row) < 5:
                errors.append(f"{path}:{line_number}: short partition row")
                continue
            fields = [field.strip() for field in row]
            try:
                type_value, subtype_value = csv_partition_kind(
                    fields[1], fields[2]
                )
                offset = parse_int_token(fields[3])
                size = parse_int_token(fields[4])
                flags = parse_int_token(fields[5]) if len(fields) > 5 and fields[5] else 0
            except ValueError as exc:
                errors.append(
                    f"{path}:{line_number}: invalid partition value: {exc}"
                )
                continue
            result.append(
                (
                    fields[0],
                    type_value,
                    subtype_value,
                    offset,
                    size,
                    flags,
                )
            )
    return result


def parse_partition_binary(
    path: Path,
    errors: list[str],
) -> list[tuple[str, int, int, int, int, int]]:
    try:
        data = path.read_bytes()
    except OSError as exc:
        errors.append(f"{path}: cannot read generated partition table: {exc}")
        return []

    entries: list[tuple[str, int, int, int, int, int]] = []
    md5_seen = False
    cursor = 0
    while cursor + PARTITION_ENTRY_BYTES <= len(data):
        entry = data[cursor : cursor + PARTITION_ENTRY_BYTES]
        magic = struct.unpack_from("<H", entry)[0]
        if magic == PARTITION_MAGIC:
            type_value, subtype_value, offset, size = struct.unpack_from(
                "<BBII", entry, 2
            )
            raw_label = entry[12:28].split(b"\0", 1)[0]
            try:
                label = raw_label.decode("ascii")
            except UnicodeDecodeError:
                errors.append(f"{path}: non-ASCII partition label at {cursor:#x}")
                label = raw_label.decode("ascii", errors="replace")
            flags = struct.unpack_from("<I", entry, 28)[0]
            entries.append(
                (label, type_value, subtype_value, offset, size, flags)
            )
            cursor += PARTITION_ENTRY_BYTES
            continue
        if magic == PARTITION_MD5_MAGIC:
            expected = hashlib.md5(data[:cursor]).digest()
            actual = entry[16:32]
            if entry[2:16] != b"\xff" * 14 or actual != expected:
                errors.append(f"{path}: generated partition-table MD5 mismatch")
            md5_seen = True
            cursor += PARTITION_ENTRY_BYTES
            break
        if entry == b"\xff" * PARTITION_ENTRY_BYTES:
            break
        errors.append(f"{path}: invalid generated partition entry at {cursor:#x}")
        break

    if not md5_seen:
        errors.append(f"{path}: generated partition table has no MD5 record")
    if any(value != 0xFF for value in data[cursor:]):
        errors.append(f"{path}: non-erased bytes follow partition table")
    return entries


def check_partition_layout(
    csv_path: Path,
    generated_path: Path,
    errors: list[str],
) -> None:
    source_entries = parse_partition_csv(csv_path, errors)
    generated_entries = parse_partition_binary(generated_path, errors)
    expected = list(EXPECTED_PARTITIONS)
    if source_entries != expected:
        errors.append(
            f"{csv_path}: partition layout differs from exact cache profile: "
            f"{source_entries!r}"
        )
    if generated_entries != expected:
        errors.append(
            f"{generated_path}: generated partition layout differs from CSV/profile: "
            f"{generated_entries!r}"
        )

    if CACHE_PARTITION_BYTES - CACHE_COMMIT_BYTES != CACHE_PAYLOAD_HARD_BYTES:
        errors.append("internal cache partition/payload constants are inconsistent")
    if CACHE_OFFSET != CORE_OFFSET + CORE_SLOT_BYTES:
        errors.append("core/cache partitions are not adjacent")
    if CACHE_OFFSET + CACHE_PARTITION_BYTES != FLASH_BYTES:
        errors.append("chapter cache partition does not end exactly at 8 MiB")


def require_config(
    values: dict[str, str],
    expected: dict[str, str],
    label: str,
    errors: list[str],
) -> None:
    for key, wanted in expected.items():
        actual = values.get(key)
        if actual != wanted:
            errors.append(f"{label}: {key}={actual!r}, expected {wanted!r}")


def check_configuration(
    defaults_path: Path,
    generated_path: Path,
    errors: list[str],
) -> None:
    expected = {
        "CONFIG_IDF_TARGET": "esp32s3",
        "CONFIG_IDF_TARGET_ESP32S3": "y",
        "CONFIG_ESPTOOLPY_FLASHSIZE": "8MB",
        "CONFIG_ESPTOOLPY_FLASHSIZE_8MB": "y",
        "CONFIG_PARTITION_TABLE_CUSTOM": "y",
        "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME": (
            "partitions_cardputer_extreme_cache.csv"
        ),
        "CONFIG_PARTITION_TABLE_FILENAME": (
            "partitions_cardputer_extreme_cache.csv"
        ),
        "CONFIG_SPIRAM": "n",
        "CONFIG_FATFS_VOLUME_COUNT": "1",
        "CONFIG_FATFS_LFN_NONE": "y",
        "CONFIG_FATFS_LFN_HEAP": "n",
        "CONFIG_FATFS_LFN_STACK": "n",
        "CONFIG_FATFS_SECTOR_512": "y",
        "CONFIG_FATFS_FS_LOCK": "0",
        "CONFIG_FATFS_PER_FILE_CACHE": "n",
        "CONFIG_FATFS_USE_FASTSEEK": "n",
        "CONFIG_FATFS_USE_DYN_BUFFERS": "n",
    }
    defaults = parse_sdkconfig(defaults_path, errors)
    generated = parse_sdkconfig(generated_path, errors)
    require_config(defaults, expected, str(defaults_path), errors)
    require_config(generated, expected, str(generated_path), errors)

    if generated.get("CONFIG_PARTITION_TABLE_OFFSET", "0x8000") != "0x8000":
        errors.append(
            f"{generated_path}: partition table must remain at flash offset 0x8000"
        )


def parse_define_ints(path: Path, errors: list[str]) -> dict[str, int]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        errors.append(f"{path}: cannot read constants: {exc}")
        return {}
    result: dict[str, int] = {}
    pattern = re.compile(
        r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+"
        r"(0[xX][0-9A-Fa-f]+|[0-9]+)[uUlL]*\s*(?:/\*.*)?$",
        re.MULTILINE,
    )
    for match in pattern.finditer(text):
        result[match.group(1)] = int(match.group(2), 0)
    return result


def compiler_sibling(compiler: Path, suffix: str) -> Path:
    if compiler.name.endswith("gcc"):
        return compiler.with_name(compiler.name[:-3] + suffix)
    return compiler.with_name(f"xtensa-esp32s3-elf-{suffix}")


def run_text(argv: list[str], errors: list[str], label: str) -> str:
    try:
        return subprocess.check_output(
            argv,
            text=True,
            stderr=subprocess.STDOUT,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        output = getattr(exc, "output", "")
        errors.append(f"{label} failed: {exc}{': ' + output if output else ''}")
        return ""


def symbol_names(nm_output: str) -> set[str]:
    result: set[str] = set()
    for line in nm_output.splitlines():
        fields = line.split()
        if len(fields) >= 2:
            result.add(fields[-1].split("@", 1)[0])
    return result


def section_sizes(objdump_output: str) -> dict[str, int]:
    result: dict[str, int] = {}
    for line in objdump_output.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0].isdigit():
            try:
                result[fields[1]] = int(fields[2], 16)
            except ValueError:
                pass
    return result


def check_stack_usage(
    build_dir: Path,
    expected_reports: int,
    errors: list[str],
) -> int:
    stack_dir = build_dir / "esp-idf" / "main"
    reports = sorted(stack_dir.rglob("*.su"))
    if len(reports) != expected_reports:
        errors.append(
            f"project stack reports {len(reports)}, expected {expected_reports}"
        )

    maximum = 0
    maximum_name = "none"
    for path in reports:
        try:
            lines = path.read_text(
                encoding="utf-8", errors="replace"
            ).splitlines()
        except OSError as exc:
            errors.append(f"{path}: cannot read stack report: {exc}")
            continue
        for line in lines:
            fields = line.rsplit("\t", 2)
            if len(fields) != 3:
                errors.append(f"{path}: malformed stack row {line!r}")
                continue
            try:
                size = int(fields[1])
            except ValueError:
                errors.append(f"{path}: invalid stack size in {line!r}")
                continue
            if fields[2] != "static":
                errors.append(f"{path}: non-static stack usage in {line!r}")
            if size > maximum:
                maximum = size
                maximum_name = fields[0]

    if maximum > MAX_STATIC_STACK:
        errors.append(
            f"largest static stack frame {maximum} in {maximum_name} "
            f"exceeds {MAX_STATIC_STACK}"
        )
    return maximum


def check_project_object_calls(
    build_dir: Path,
    nm: Path,
    expected_objects: int,
    errors: list[str],
) -> None:
    objects = sorted(
        (build_dir / "esp-idf" / "main").rglob("*.obj")
    )
    if len(objects) != expected_objects:
        errors.append(
            f"project objects {len(objects)}, expected {expected_objects}"
        )
    if not objects:
        return

    undefined = run_text(
        [str(nm), "-u", *(str(path) for path in objects)],
        errors,
        "project object undefined-symbol scan",
    )
    for line in undefined.splitlines():
        fields = line.split()
        if not fields:
            continue
        name = fields[-1].split("@", 1)[0]
        if (
            name in FORBIDDEN_PROJECT_CALLS
            or name.startswith(("YJ1_", "YJ2_", "LZ4_decompress"))
            or name.startswith(("_Zn", "_Zd"))
        ):
            errors.append(
                f"project object calls forbidden heap/decompress symbol {name}"
            )


def is_root_project_source(path: Path, build_dir: Path) -> bool:
    try:
        path.resolve().relative_to(ROOT)
    except ValueError:
        return False
    try:
        path.resolve().relative_to(build_dir.resolve())
        return False
    except ValueError:
        return True


def check_cmake_sources(errors: list[str]) -> None:
    top_path = ESP32S3_DIR / "CMakeLists.txt"
    main_path = ESP32S3_DIR / "main" / "CMakeLists.txt"
    cache_path = (
        ESP32S3_DIR / "engine_bridge" / "pal_engine_chapter_cache.c"
    )
    target_packs_path = (
        ESP32S3_DIR / "engine_bridge" / "pal_engine_target_packs.c"
    )
    try:
        top = top_path.read_text(encoding="utf-8", errors="replace")
        main = main_path.read_text(encoding="utf-8", errors="replace")
        cache = cache_path.read_text(encoding="utf-8", errors="replace")
        target_packs = target_packs_path.read_text(
            encoding="utf-8", errors="replace"
        )
    except OSError as exc:
        errors.append(f"cannot read CMake source: {exc}")
        return

    top_tokens = (
        "CARDPUTER_EXTREME_CHAPTER_CACHE",
        "CARDPUTER_EXTREME_CHAPTER_CACHE requires CARDPUTER_EXTREME_NO_PSRAM=ON",
    )
    main_tokens = (
        "if(CARDPUTER_EXTREME_CHAPTER_CACHE)",
        '"../engine_bridge/pal_engine_chapter_cache.c"',
        "PAL_EXTREME_CHAPTER_CACHE=1",
    )
    for token in top_tokens:
        if token not in top:
            errors.append(f"{top_path}: missing chapter-cache gate {token!r}")
    for token in main_tokens:
        if token not in main:
            errors.append(f"{main_path}: missing chapter-cache gate {token!r}")

    for token in (
        "PalFont10_Open(&core_pack, &font10)",
        "PalNativeUi_Font10IdentityMatches(",
    ):
        if token not in target_packs:
            errors.append(
                f"{target_packs_path}: missing chapter FONT10 gate {token!r}"
            )

    for token in (
        "static FIL pal_chapter_bundle_file;",
        "f_open(",
        "f_read(",
        "f_close(",
    ):
        if token not in cache:
            errors.append(
                f"{cache_path}: missing fixed-storage FatFS shape {token!r}"
            )
    forbidden_calls = re.compile(
        r"\b(?:malloc|calloc|realloc|free|fopen|fread|fseek|ftell|fclose)\s*\("
    )
    for match in forbidden_calls.finditer(cache):
        line = cache.count("\n", 0, match.start()) + 1
        errors.append(
            f"{cache_path}:{line}: forbidden heap/stdio file call "
            f"{match.group(0)!r}"
        )


def check_build(
    build_dir: Path,
    errors: list[str],
) -> BuildMetrics:
    description_path = build_dir / "project_description.json"
    description = read_json(description_path, errors, "project description")
    if not isinstance(description, dict):
        description = {}

    app_name = description.get("app_bin", "sdlpal_cardputer_extreme.bin")
    elf_name = description.get("app_elf", "sdlpal_cardputer_extreme.elf")
    if not isinstance(app_name, str) or not isinstance(elf_name, str):
        errors.append(f"{description_path}: invalid app artifact names")
        app_name = "sdlpal_cardputer_extreme.bin"
        elf_name = "sdlpal_cardputer_extreme.elf"
    app_path = build_dir / app_name
    elf_path = build_dir / elf_name

    required_paths = (
        app_path,
        elf_path,
        build_dir / "sdkconfig",
        build_dir / "CMakeCache.txt",
        build_dir / "compile_commands.json",
        build_dir / "build.ninja",
        build_dir / "partition_table" / "partition-table.bin",
    )
    missing = [path for path in required_paths if not path.is_file()]
    if missing:
        errors.extend(f"missing build artifact: {path}" for path in missing)
        return BuildMetrics()

    if description.get("target") != "esp32s3":
        errors.append(f"{description_path}: target is not esp32s3")

    app_size = app_path.stat().st_size
    if app_size <= 0 or app_size > APP_BYTES:
        errors.append(f"{app_path}: app size {app_size} exceeds {APP_BYTES}")

    cmake_cache = parse_cmake_cache(build_dir / "CMakeCache.txt", errors)
    for key in (
        "PAL_CORES3SE_ENGINE_HOST",
        "CARDPUTER_EXTREME_NO_PSRAM",
        "CARDPUTER_EXTREME_CHAPTER_CACHE",
    ):
        if not cmake_truth(cmake_cache.get(key)):
            errors.append(f"CMake cache did not enable {key}")

    compile_data = read_json(
        build_dir / "compile_commands.json",
        errors,
        "compile commands",
    )
    if not isinstance(compile_data, list):
        errors.append("compile_commands.json is not a list")
        compile_data = []
    project_entries = [
        entry
        for entry in compile_data
        if isinstance(entry, dict)
        and isinstance(entry.get("file"), str)
        and is_root_project_source(Path(entry["file"]), build_dir)
    ]
    inventory = read_json(
        ESP32S3_DIR / "cardputer_extreme_sources.json",
        errors,
        "Cardputer extreme source inventory",
    )
    inventory_sources = (
        inventory.get("sources")
        if isinstance(inventory, dict)
        else None
    )
    if not isinstance(inventory_sources, list) or not all(
        isinstance(item, str) for item in inventory_sources
    ):
        errors.append("Cardputer extreme source inventory is malformed")
        expected_sources: set[str] = set()
    else:
        expected_sources = set(inventory_sources)
        expected_sources.add(
            "esp32s3/engine_bridge/pal_engine_chapter_cache.c"
        )
    actual_sources = {
        Path(str(entry["file"])).resolve().relative_to(ROOT).as_posix()
        for entry in project_entries
    }
    if actual_sources != expected_sources:
        for source in sorted(expected_sources - actual_sources):
            errors.append(f"cache build is missing project source {source}")
        for source in sorted(actual_sources - expected_sources):
            errors.append(f"cache build has unexpected project source {source}")

    cache_entries = [
        entry
        for entry in project_entries
        if Path(str(entry["file"])).name == "pal_engine_chapter_cache.c"
    ]
    if len(cache_entries) != 1:
        errors.append(
            "compile graph must contain exactly one pal_engine_chapter_cache.c "
            f"entry, got {len(cache_entries)}"
        )

    compile_tokens = (
        "-DPAL_CARDPUTER_EXTREME=1",
        "-DPAL_EXTREME_CHAPTER_CACHE=1",
        "-DPAL_NO_RUNTIME_DECOMPRESS=1",
        "-DPAL_NO_RUNTIME_HEAP=1",
        "-DPAL_ENGINE_TF_TOC_BYTES=2048u",
    )
    for entry in project_entries:
        source = str(entry["file"])
        command = entry.get("command")
        if not isinstance(command, str):
            errors.append(f"{source}: missing compile command")
            continue
        try:
            words = shlex.split(command)
        except ValueError as exc:
            errors.append(f"{source}: malformed compile command: {exc}")
            continue
        for token in compile_tokens:
            if token not in words:
                errors.append(f"{source}: compile command is missing {token}")

    ninja = (build_dir / "build.ninja").read_text(
        encoding="utf-8", errors="replace"
    )
    for token in (
        "pal_engine_chapter_cache.c.obj",
        "cardputer_extreme_board.c.obj",
        "cardputer_extreme_memory.c.obj",
    ):
        if token not in ninja:
            errors.append(f"build graph is missing {token}")
    for token in (
        "cores3se_board.c.obj",
        "cores3se_memory.c.obj",
        "pal_engine_psram.lf",
    ):
        if token in ninja:
            errors.append(f"no-PSRAM cache build graph contains {token}")

    compiler_value = description.get("c_compiler")
    if not isinstance(compiler_value, str) or not compiler_value:
        errors.append(f"{description_path}: no C compiler path")
        return BuildMetrics(
            app_size,
            elf_path.stat().st_size,
            len(project_entries),
            0,
        )
    compiler = Path(compiler_value)
    nm = compiler_sibling(compiler, "nm")
    objdump = compiler_sibling(compiler, "objdump")
    if not nm.is_file():
        errors.append(f"target nm is missing: {nm}")
        nm_output = ""
    else:
        nm_output = run_text(
            [str(nm), "-a", str(elf_path)],
            errors,
            "target nm",
        )
        check_project_object_calls(
            build_dir, nm, len(project_entries), errors
        )
    symbols = symbol_names(nm_output)
    for name in sorted(REQUIRED_CACHE_SYMBOLS - symbols):
        errors.append(f"required cache symbol is missing: {name}")
    decoder_hits = sorted(
        name
        for name in symbols
        if name in FORBIDDEN_DECODER_SYMBOLS
        or name.startswith(("YJ1_", "YJ2_", "LZ4_decompress"))
    )
    for name in decoder_hits:
        errors.append(f"forbidden runtime decoder symbol is linked: {name}")
    for name in sorted(symbol for symbol in symbols if symbol.startswith("pal_psram_")):
        errors.append(f"PSRAM storage symbol is linked: {name}")

    sections: dict[str, int] = {}
    if not objdump.is_file():
        errors.append(f"target objdump is missing: {objdump}")
    else:
        sections = section_sizes(
            run_text(
                [str(objdump), "-h", str(elf_path)],
                errors,
                "target objdump",
            )
        )
        if sections.get(".ext_ram.bss", 0) != 0:
            errors.append(
                f".ext_ram.bss is {sections['.ext_ram.bss']} in no-PSRAM build"
            )

    dram_bss = sections.get(".dram0.bss", -1)
    dram_data = sections.get(".dram0.data", -1)
    diram_static = sum(
        max(0, sections.get(name, 0))
        for name in (".dram0.dummy", ".dram0.data", ".noinit", ".dram0.bss")
    )
    if dram_bss < 0 or dram_bss > MAX_DRAM_BSS:
        errors.append(f".dram0.bss {dram_bss} exceeds {MAX_DRAM_BSS}")
    if dram_data < 0 or dram_data > MAX_DRAM_DATA:
        errors.append(f".dram0.data {dram_data} exceeds {MAX_DRAM_DATA}")
    if diram_static > MAX_DIRAM_STATIC:
        errors.append(
            f"DIRAM static use {diram_static} exceeds {MAX_DIRAM_STATIC}"
        )
    max_stack = check_stack_usage(build_dir, len(project_entries), errors)

    return BuildMetrics(
        app_size,
        elf_path.stat().st_size,
        len(project_entries),
        len(symbols),
        max(0, dram_bss),
        diram_static,
        max_stack,
    )


def iter_pack_archives(
    data: bytes,
) -> Iterable[tuple[int, list[tuple[int, int, int, int]]]]:
    archive_count = pack.u16(data, 8)
    archive_table_offset = pack.u32(data, 12)
    for archive_index in range(archive_count):
        entry_offset = (
            archive_table_offset + archive_index * pack.ARCHIVE_ENTRY_SIZE
        )
        archive_id, chunk_count = struct.unpack_from("<HH", data, entry_offset)
        chunk_table_offset = pack.u32(data, entry_offset + 4)
        chunks: list[tuple[int, int, int, int]] = []
        for chunk_id in range(chunk_count):
            chunk_offset = (
                chunk_table_offset + chunk_id * pack.CHUNK_ENTRY_SIZE
            )
            payload_offset, payload_size, fmt, flags = struct.unpack_from(
                "<IIHH", data, chunk_offset
            )
            chunks.append((payload_offset, payload_size, fmt, flags))
        yield archive_id, chunks


def pack_chunk(
    data: bytes,
    archive_id: int,
    chunk_id: int,
) -> tuple[bytes, int] | None:
    for current_id, chunks in iter_pack_archives(data):
        if current_id != archive_id:
            continue
        if not 0 <= chunk_id < len(chunks):
            return None
        offset, size, fmt, _flags = chunks[chunk_id]
        return data[offset : offset + size], fmt
    return None


def parse_generated_font10_identity(
    path: Path,
    errors: list[str],
) -> tuple[int, int, int] | None:
    try:
        source = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        errors.append(f"{path}: generated UI header cannot be read: {exc}")
        return None

    values: list[int] = []
    for name in (
        "PAL_NATIVE_UI_GENERATED_FONT_GLYPH_COUNT",
        "PAL_NATIVE_UI_GENERATED_FONT_IMAGE_BYTES",
        "PAL_NATIVE_UI_GENERATED_FONT_PAYLOAD_CRC32",
    ):
        match = re.search(
            rf"^#define\s+{name}\s+(0[xX][0-9a-fA-F]+|[0-9]+)u$",
            source,
            re.MULTILINE,
        )
        if match is None:
            errors.append(f"{path}: missing generated define {name}")
            return None
        values.append(int(match.group(1), 0))
    return values[0], values[1], values[2]


def check_font10_contract(
    manifest: dict[str, object],
    core_data: bytes,
    full_path: Path,
    errors: list[str],
) -> None:
    identity = parse_generated_font10_identity(GENERATED_UI_HEADER, errors)
    raw_font10 = manifest.get("font10")
    if not isinstance(raw_font10, dict):
        errors.append("chapter manifest is missing mandatory FONT10 metadata")
        return

    summary = raw_font10.get("font10")
    if not isinstance(summary, dict):
        errors.append("chapter manifest FONT10 summary is not an object")
    elif identity is not None and (
        summary.get("glyph_count"),
        summary.get("bytes"),
        summary.get("payload_crc32"),
    ) != identity:
        errors.append("chapter FONT10 identity differs from generated UI header")

    archive = raw_font10.get("archive")
    archive_path = (
        Path(archive["path"])
        if isinstance(archive, dict) and isinstance(archive.get("path"), str)
        else None
    )
    data_dir_value = manifest.get("data_dir")
    data_dir = Path(data_dir_value) if isinstance(data_dir_value, str) else None
    if archive_path is None or data_dir is None:
        errors.append("chapter FONT10 provenance paths are missing")
        return
    try:
        expected_chunk, expected_summary = pack.build_font10_archive_chunk(
            data_dir,
            archive_path,
        )
    except (OSError, ValueError, zipfile.BadZipFile) as exc:
        errors.append(f"cannot deterministically rebuild chapter FONT10: {exc}")
        return
    if raw_font10 != expected_summary:
        errors.append("chapter FONT10 metadata differs from deterministic rebuild")

    try:
        full_data = full_path.read_bytes()
    except OSError as exc:
        errors.append(f"{full_path}: cannot read complete pack for FONT10: {exc}")
        return
    for label, image in (
        ("pal_core.pak", core_data),
        ("pal_full.pak", full_data),
    ):
        actual = pack_chunk(image, pack.ARCHIVE_IDS["FONT"], 1)
        if actual is None:
            errors.append(f"{label} has no mandatory FONT chunk 1")
            continue
        payload, fmt = actual
        if fmt != pack.FORMAT_FONT10:
            errors.append(f"{label} FONT#1 is not FONT10 format")
        if payload != expected_chunk.payload:
            errors.append(
                f"{label} FONT#1 differs from the standard host-generated FONT10"
            )


def inspect_pack(
    path: Path,
    errors: list[str],
) -> tuple[PackMeta | None, bytes | None]:
    try:
        data = path.read_bytes()
    except OSError as exc:
        errors.append(f"{path}: cannot read pack: {exc}")
        return None, None
    try:
        pack.verify_pack(data)
    except ValueError as exc:
        errors.append(f"{path}: invalid pack: {exc}")
        return None, data

    archive_ids: list[int] = []
    for archive_id, chunks in iter_pack_archives(data):
        archive_ids.append(archive_id)
        for chunk_id, (offset, size, _fmt, flags) in enumerate(chunks):
            if flags != 0:
                errors.append(
                    f"{path}: runtime flags {flags:#x} on "
                    f"archive {archive_id} chunk {chunk_id}"
                )
            if size and data[offset : offset + min(size, 4)] == b"YJ_1":
                errors.append(
                    f"{path}: YJ1 payload remains in "
                    f"archive {archive_id} chunk {chunk_id}"
                )

    meta = PackMeta(
        path.name,
        len(data),
        sha256_bytes(data),
        pack.u32(data, pack.PACK_CRC32_OFFSET),
        pack.u32(data, pack.PACK_SET_ID_OFFSET),
        pack.u32(data, 16),
        tuple(archive_ids),
    )
    return meta, data


def check_summary(
    label: str,
    summary: object,
    meta: PackMeta,
    errors: list[str],
) -> None:
    if not isinstance(summary, dict):
        errors.append(f"manifest has no {label} pack summary")
        return
    expected = {
        "filename": meta.filename,
        "size": meta.size,
        "sha256": meta.sha256,
        "crc32": meta.crc32,
        "pack_set_id": meta.set_id,
        "toc_bytes": meta.toc_bytes,
    }
    for key, value in expected.items():
        if summary.get(key) != value:
            errors.append(
                f"manifest {label}.{key}={summary.get(key)!r}, "
                f"artifact has {value!r}"
            )


def check_archive_ids(
    label: str,
    meta: PackMeta,
    expected_names: Iterable[str],
    errors: list[str],
) -> None:
    expected = tuple(
        pack.ARCHIVE_IDS[name]
        for name in sorted(expected_names, key=lambda item: pack.ARCHIVE_IDS[item])
    )
    if meta.archive_ids != expected:
        errors.append(
            f"{label} archive IDs {meta.archive_ids!r}, expected {expected!r}"
        )


def check_catalog(
    catalog: bytes,
    catalog_summary: object,
    set_id: int,
    bundle_meta: list[PackMeta],
    intervals: list[dict[str, object]],
    errors: list[str],
) -> None:
    try:
        chapter.verify_catalog(catalog, set_id)
    except ValueError as exc:
        errors.append(f"CACHE catalog verification failed: {exc}")
        return

    (
        magic,
        version,
        header_size,
        catalog_set_id,
        scene_count,
        bundle_count,
        scene_offset,
        descriptor_offset,
        total_size,
        declared_crc,
    ) = struct.unpack_from("<4sHHIHHIIII", catalog)
    del magic, version, header_size
    if catalog_set_id != set_id:
        errors.append("CACHE catalog set ID differs from pack set")
    if scene_count != chapter.CATALOG_SCENE_COUNT:
        errors.append(f"CACHE catalog has {scene_count} scenes")
    if bundle_count != BUNDLE_COUNT:
        errors.append(f"CACHE catalog has {bundle_count} bundles")
    if total_size != len(catalog):
        errors.append("CACHE catalog total-size field differs from payload")

    if not isinstance(catalog_summary, dict):
        errors.append("manifest has no catalog summary")
    else:
        expected_summary = {
            "archive": "CACHE",
            "archive_id": pack.ARCHIVE_IDS["CACHE"],
            "chunk_id": 0,
            "format": "RAW",
            "size": len(catalog),
            "sha256": sha256_bytes(catalog),
            "crc32": declared_crc,
            "magic": chapter.CATALOG_MAGIC.decode("ascii"),
            "header_size": chapter.CATALOG_HEADER_SIZE,
            "scene_table_offset": chapter.CATALOG_SCENE_TABLE_OFFSET,
            "bundle_table_offset": (
                chapter.CATALOG_SCENE_TABLE_OFFSET
                + chapter.CATALOG_SCENE_COUNT
            ),
            "bundle_descriptor_size": chapter.CATALOG_BUNDLE_DESC_SIZE,
            "bundle_filename_pattern": "b%02u.pak",
            "scene_zero_bundle": 0xFF,
        }
        for key, value in expected_summary.items():
            if catalog_summary.get(key) != value:
                errors.append(
                    f"manifest catalog.{key}={catalog_summary.get(key)!r}, "
                    f"expected {value!r}"
                )

    expected_scene_table = chapter.make_scene_table()
    actual_scene_table = catalog[scene_offset:descriptor_offset]
    if actual_scene_table != expected_scene_table:
        errors.append("CACHE catalog scene table differs from audited intervals")

    expected_intervals = [
        {
            "bundle_id": bundle_id,
            "scene_first": first,
            "scene_last": last,
        }
        for bundle_id, (first, last) in enumerate(chapter.SCENE_INTERVALS)
    ]
    if intervals != expected_intervals:
        errors.append("manifest scene intervals differ from CACHE catalog policy")

    for bundle_id, meta in enumerate(bundle_meta):
        offset = descriptor_offset + bundle_id * chapter.CATALOG_BUNDLE_DESC_SIZE
        descriptor_id = catalog[offset]
        reserved = catalog[offset + 1 : offset + 4]
        descriptor_size = struct.unpack_from("<I", catalog, offset + 4)[0]
        descriptor_sha = catalog[offset + 8 : offset + 40].hex()
        if descriptor_id != bundle_id or reserved != b"\0\0\0":
            errors.append(f"CACHE descriptor {bundle_id} has invalid ID/reserved")
        if descriptor_size != meta.size:
            errors.append(
                f"CACHE descriptor b{bundle_id:02d} size {descriptor_size}, "
                f"artifact has {meta.size}"
            )
        if descriptor_sha != meta.sha256:
            errors.append(
                f"CACHE descriptor b{bundle_id:02d} SHA-256 differs from artifact"
            )


def check_manifest_contract(manifest: dict[str, object], errors: list[str]) -> None:
    if manifest.get("schema") != "sdlpal-embedded-chapter-pack-manifest":
        errors.append("chapter manifest schema mismatch")
    if manifest.get("version") != 1:
        errors.append("chapter manifest version mismatch")
    runtime = manifest.get("runtime")
    if not isinstance(runtime, dict):
        errors.append("chapter manifest has no runtime contract")
    else:
        required_runtime = {
            "heap_required": False,
            "runtime_decompression_required": False,
            "payloads_are_runtime_native": True,
            "overlay_shape": "one replaceable SPI-NOR bundle",
        }
        for key, value in required_runtime.items():
            if runtime.get(key) != value:
                errors.append(f"manifest runtime.{key} must be {value!r}")

    window = manifest.get("core_event_object_window")
    if not isinstance(window, dict):
        errors.append("manifest does not disclose the event-object paging limit")
    else:
        expected = {
            "status": "runtime-compatibility-window-event-pager-not-yet-wired",
            "record_bytes": 32,
            "core_record_count": chapter.CORE_EVENT_OBJECT_COUNT,
            "core_chunk_bytes": chapter.CORE_EVENT_OBJECT_BYTES,
            "current_runtime_scene_window": "scenes 1..22",
            "outside_window_behavior": "fail-fast",
        }
        for key, value in expected.items():
            if window.get(key) != value:
                errors.append(
                    f"manifest core_event_object_window.{key} must be {value!r}"
                )
        full_bytes = window.get("full_source_chunk_bytes")
        if not isinstance(full_bytes, int) or full_bytes <= chapter.CORE_EVENT_OBJECT_BYTES:
            errors.append("manifest full SSS event chunk is not larger than core window")


def check_pack_constants(errors: list[str]) -> None:
    independent = {
        "CORE_SLOT_CAP": CORE_SLOT_BYTES,
        "HARD_OVERLAY_PACK_CAP": CACHE_PAYLOAD_HARD_BYTES,
        "SOFT_OVERLAY_CAP": CACHE_PACK_SOFT_BYTES,
        "CATALOG_SCENE_COUNT": 300,
        "CATALOG_BUNDLE_DESC_SIZE": 40,
    }
    for name, expected in independent.items():
        actual = getattr(chapter, name, None)
        if actual != expected:
            errors.append(
                f"chapter builder {name}={actual!r}, expected {expected:#x}"
            )

    header_path = ESP32S3_DIR / "engine_bridge" / "pal_engine_chapter_cache.h"
    defines = parse_define_ints(header_path, errors)
    expected_defines = {
        "PAL_ENGINE_CACHE_PARTITION_BYTES": CACHE_PARTITION_BYTES,
        "PAL_ENGINE_CACHE_COMMIT_SECTOR_BYTES": CACHE_COMMIT_BYTES,
        "PAL_ENGINE_CACHE_PAYLOAD_HARD_BYTES": CACHE_PAYLOAD_HARD_BYTES,
        "PAL_ENGINE_CACHE_PACK_SOFT_BYTES": CACHE_PACK_SOFT_BYTES,
        "PAL_ENGINE_CACHE_CATALOG_SCENES": 300,
        "PAL_ENGINE_CACHE_DESCRIPTOR_BYTES": 40,
    }
    for name, expected in expected_defines.items():
        actual = defines.get(name)
        if actual != expected:
            errors.append(
                f"{header_path}: {name}={actual!r}, expected {expected:#x}"
            )


def check_packs(
    pack_dir: Path,
    manifest_path: Path,
    errors: list[str],
) -> PackMetrics:
    manifest_value = read_json(manifest_path, errors, "chapter manifest")
    if not isinstance(manifest_value, dict):
        errors.append("chapter manifest root is not an object")
        return PackMetrics()
    manifest: dict[str, object] = manifest_value
    check_manifest_contract(manifest, errors)

    paths = {
        "core": pack_dir / "pal_core.pak",
        "tf": pack_dir / "pal_tf.pak",
        "full": pack_dir / "pal_full.pak",
    }
    bundle_paths = [pack_dir / f"b{bundle_id:02d}.pak" for bundle_id in range(BUNDLE_COUNT)]
    required = [*paths.values(), *bundle_paths]
    missing = [path for path in required if not path.is_file()]
    if missing:
        errors.extend(f"missing chapter artifact: {path}" for path in missing)
        return PackMetrics()
    extra_bundles = sorted(
        path.name
        for path in pack_dir.glob("b*.pak")
        if path.name not in {item.name for item in bundle_paths}
    )
    if extra_bundles:
        errors.append(f"unexpected chapter bundle artifacts: {extra_bundles}")

    metadata: dict[str, PackMeta] = {}
    core_data: bytes | None = None
    for label, path in paths.items():
        meta, data = inspect_pack(path, errors)
        if meta is not None:
            metadata[label] = meta
        if label == "core":
            core_data = data

    bundle_meta: list[PackMeta] = []
    for path in bundle_paths:
        meta, _data = inspect_pack(path, errors)
        if meta is not None:
            bundle_meta.append(meta)
    if set(metadata) != set(paths) or len(bundle_meta) != BUNDLE_COUNT:
        return PackMetrics()

    core_meta = metadata["core"]
    tf_meta = metadata["tf"]
    full_meta = metadata["full"]
    check_archive_ids(
        "pal_core.pak",
        core_meta,
        (*chapter.CORE_FULL_ARCHIVES, "MGO", "CACHE"),
        errors,
    )
    check_archive_ids("pal_tf.pak", tf_meta, chapter.TF_ARCHIVES, errors)
    check_archive_ids(
        "pal_full.pak",
        full_meta,
        chapter.FULL_MIRROR_ARCHIVES,
        errors,
    )
    for bundle_id, meta in enumerate(bundle_meta):
        check_archive_ids(
            f"b{bundle_id:02d}.pak",
            meta,
            chapter.OVERLAY_ARCHIVES,
            errors,
        )

    pack_set = manifest.get("pack_set")
    if not isinstance(pack_set, dict):
        errors.append("manifest has no pack_set object")
        manifest_set_id = 0
    else:
        manifest_set_id = pack_set.get("id")
        if not isinstance(manifest_set_id, int):
            errors.append("manifest pack_set.id is not an integer")
            manifest_set_id = 0
        if pack_set.get("id_hex") != f"0x{manifest_set_id:08x}":
            errors.append("manifest pack_set.id_hex differs from integer ID")
    all_meta = [core_meta, tf_meta, full_meta, *bundle_meta]
    actual_set_ids = {meta.set_id for meta in all_meta}
    if actual_set_ids != {manifest_set_id} or manifest_set_id == 0:
        errors.append(
            f"pack-set IDs are zero/inconsistent: manifest={manifest_set_id:#x}, "
            f"artifacts={sorted(actual_set_ids)!r}"
        )

    packs_summary = manifest.get("packs")
    if not isinstance(packs_summary, dict):
        errors.append("manifest has no packs object")
        packs_summary = {}
    check_summary("core", packs_summary.get("core"), core_meta, errors)
    check_summary("tf", packs_summary.get("tf"), tf_meta, errors)
    check_summary("full", packs_summary.get("full"), full_meta, errors)

    bundle_summaries = packs_summary.get("bundles")
    if not isinstance(bundle_summaries, list) or len(bundle_summaries) != BUNDLE_COUNT:
        errors.append(f"manifest must contain exactly {BUNDLE_COUNT} bundle summaries")
        bundle_summaries = []
    for bundle_id, meta in enumerate(bundle_meta):
        if bundle_id >= len(bundle_summaries):
            break
        summary = bundle_summaries[bundle_id]
        if not isinstance(summary, dict):
            errors.append(f"manifest bundle {bundle_id} summary is not an object")
            continue
        expected = {
            "id": bundle_id,
            "filename": meta.filename,
            "scene_first": chapter.SCENE_INTERVALS[bundle_id][0],
            "scene_last": chapter.SCENE_INTERVALS[bundle_id][1],
            "size": meta.size,
            "sha256": meta.sha256,
            "soft_cap_bytes": CACHE_PACK_SOFT_BYTES,
            "within_soft_cap": True,
            "over_soft_cap_bytes": 0,
        }
        for key, value in expected.items():
            if summary.get(key) != value:
                errors.append(
                    f"manifest bundle {bundle_id}.{key}={summary.get(key)!r}, "
                    f"expected {value!r}"
                )

    if core_meta.size > CORE_SLOT_BYTES:
        errors.append(
            f"pal_core.pak {core_meta.size} exceeds core slot {CORE_SLOT_BYTES}"
        )
    if tf_meta.toc_bytes > ACTIVE_TF_TOC_BYTES:
        errors.append(
            f"pal_tf.pak TOC {tf_meta.toc_bytes} exceeds {ACTIVE_TF_TOC_BYTES}"
        )
    for bundle_id, meta in enumerate(bundle_meta):
        if meta.size > CACHE_PAYLOAD_HARD_BYTES:
            errors.append(
                f"b{bundle_id:02d}.pak {meta.size} exceeds cache payload "
                f"{CACHE_PAYLOAD_HARD_BYTES}"
            )
        if meta.size > CACHE_PACK_SOFT_BYTES:
            errors.append(
                f"b{bundle_id:02d}.pak {meta.size} exceeds conservative cap "
                f"{CACHE_PACK_SOFT_BYTES}"
            )

    soft_cap = manifest.get("overlay_soft_cap")
    largest = max(meta.size for meta in bundle_meta)
    if not isinstance(soft_cap, dict):
        errors.append("manifest has no overlay_soft_cap object")
    else:
        expected = {
            "bytes": CACHE_PACK_SOFT_BYTES,
            "hex": f"0x{CACHE_PACK_SOFT_BYTES:x}",
            "hard_pack_bytes": CACHE_PAYLOAD_HARD_BYTES,
            "hard_pack_hex": f"0x{CACHE_PAYLOAD_HARD_BYTES:x}",
            "largest_bundle_bytes": largest,
            "over_cap_bundle_ids": [],
        }
        for key, value in expected.items():
            if soft_cap.get(key) != value:
                errors.append(
                    f"manifest overlay_soft_cap.{key}={soft_cap.get(key)!r}, "
                    f"expected {value!r}"
                )
    core_slot = manifest.get("core_slot")
    if not isinstance(core_slot, dict):
        errors.append("manifest has no core_slot object")
    else:
        expected = {
            "bytes": CORE_SLOT_BYTES,
            "hex": f"0x{CORE_SLOT_BYTES:x}",
            "pack_bytes": core_meta.size,
            "remaining_bytes": CORE_SLOT_BYTES - core_meta.size,
        }
        for key, value in expected.items():
            if core_slot.get(key) != value:
                errors.append(
                    f"manifest core_slot.{key}={core_slot.get(key)!r}, "
                    f"expected {value!r}"
                )

    if core_data is None:
        errors.append("cannot inspect CACHE catalog in pal_core.pak")
        catalog = b""
    else:
        check_font10_contract(
            manifest,
            core_data,
            paths["full"],
            errors,
        )
        catalog_chunk = pack_chunk(
            core_data,
            pack.ARCHIVE_IDS["CACHE"],
            0,
        )
        if catalog_chunk is None:
            errors.append("pal_core.pak has no CACHE#0 catalog")
            catalog = b""
        else:
            catalog, catalog_format = catalog_chunk
            if catalog_format != pack.FORMAT_RAW:
                errors.append("pal_core.pak CACHE#0 is not RAW")

        window = manifest.get("core_event_object_window")
        if isinstance(window, dict):
            core_sss = pack_chunk(core_data, pack.ARCHIVE_IDS["SSS"], 0)
            full_path = paths["full"]
            try:
                full_data = full_path.read_bytes()
            except OSError:
                full_data = b""
            full_sss = (
                pack_chunk(full_data, pack.ARCHIVE_IDS["SSS"], 0)
                if full_data
                else None
            )
            if core_sss is None or len(core_sss[0]) != window.get("core_chunk_bytes"):
                errors.append("core SSS#0 size differs from event-window manifest")
            if full_sss is None or len(full_sss[0]) != window.get(
                "full_source_chunk_bytes"
            ):
                errors.append("full-mirror SSS#0 size differs from event manifest")

    scene_partition = manifest.get("scene_partition")
    intervals = (
        scene_partition.get("intervals")
        if isinstance(scene_partition, dict)
        and isinstance(scene_partition.get("intervals"), list)
        else []
    )
    if catalog:
        check_catalog(
            catalog,
            manifest.get("catalog"),
            manifest_set_id,
            bundle_meta,
            intervals,
            errors,
        )

    return PackMetrics(
        manifest_set_id,
        core_meta.size,
        tf_meta.size,
        full_meta.size,
        tf_meta.toc_bytes,
        len(catalog),
        largest,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=ROOT / "build-cardputer-extreme-cache",
    )
    parser.add_argument(
        "--pack-dir",
        type=Path,
        default=Path("/tmp/pal_cardputer_extreme_chapters"),
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        help="default: PACK_DIR/chapter_manifest.json",
    )
    parser.add_argument(
        "--partition-csv",
        type=Path,
        default=ESP32S3_DIR / "partitions_cardputer_extreme_cache.csv",
    )
    parser.add_argument(
        "--sdkconfig-defaults",
        type=Path,
        default=ESP32S3_DIR / "sdkconfig.cardputer_extreme_cache.defaults",
    )
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    pack_dir = args.pack_dir.resolve()
    manifest_path = (
        args.manifest.resolve()
        if args.manifest is not None
        else pack_dir / "chapter_manifest.json"
    )
    errors: list[str] = []

    check_pack_constants(errors)
    check_cmake_sources(errors)
    check_partition_layout(
        args.partition_csv.resolve(),
        build_dir / "partition_table" / "partition-table.bin",
        errors,
    )
    check_configuration(
        args.sdkconfig_defaults.resolve(),
        build_dir / "sdkconfig",
        errors,
    )
    build_metrics = check_build(build_dir, errors)
    pack_metrics = check_packs(pack_dir, manifest_path, errors)

    print("Cardputer ADV chapter-cache contract")
    print(
        f"  flash={FLASH_BYTES} bytes, app={build_metrics.app_bytes}/{APP_BYTES}, "
        f"core-slot={pack_metrics.core_bytes}/{CORE_SLOT_BYTES}"
    )
    print(
        f"  cache-partition={CACHE_PARTITION_BYTES}, "
        f"payload={pack_metrics.largest_bundle_bytes}/"
        f"{CACHE_PAYLOAD_HARD_BYTES} "
        f"(soft {CACHE_PACK_SOFT_BYTES})"
    )
    print(
        f"  TF={pack_metrics.tf_bytes}, "
        f"TOC={pack_metrics.tf_toc_bytes}/{ACTIVE_TF_TOC_BYTES}, "
        f"full-mirror={pack_metrics.full_bytes}"
    )
    print(
        f"  pack_set_id=0x{pack_metrics.set_id:08x}, "
        f"bundles={BUNDLE_COUNT}, catalog={pack_metrics.catalog_bytes}, "
        f"project_sources={build_metrics.project_sources}"
    )
    print(
        f"  .dram0.bss={build_metrics.dram_bss_bytes}/{MAX_DRAM_BSS}, "
        f"DIRAM-static={build_metrics.diram_static_bytes}/{MAX_DIRAM_STATIC}, "
        f"max-stack={build_metrics.max_stack_bytes}/{MAX_STATIC_STACK}"
    )
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print(
        "PASS: exact 8MiB/no-PSRAM cache image and all chapter "
        "artifacts/catalog descriptors agree"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
