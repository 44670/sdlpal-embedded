#!/usr/bin/env python3
"""Check the CoreS3 SE bring-up artifact against the embedded contract."""

from __future__ import annotations

import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path


PACK_MAGIC = 0x4B504C50
PACK_HEADER_SIZE = 32
PACK_ARCHIVE_ENTRY_SIZE = 12
PACK_CHUNK_ENTRY_SIZE = 16
PACK_CHUNK_F_COMPRESSED = 0x0001
PAL_NOR_PARTITION_BYTES = 0xB00000
SRAM_BUDGET = 300 * 1024
PSRAM_BUDGET = 8 * 1024 * 1024
DRAM_STATIC_BUDGET = 300 * 1024

SOURCE_FILES = (
    "esp32s3/main/app_main.c",
    "esp32s3/main/cores3se_board.c",
    "esp32s3/main/cores3se_board.h",
    "esp32s3/main/cores3se_hw.h",
    "esp32s3/main/cores3se_memory.c",
    "esp32s3/main/pal_save_fatfs.c",
    "esp32s3/main/pal_save_fatfs.h",
    "embedded/pal_dialog_static.c",
    "embedded/pal_dialog_static.h",
    "embedded/pal_font_cache.c",
    "embedded/pal_font_cache.h",
    "embedded/pal_global_cache.c",
    "embedded/pal_global_cache.h",
    "embedded/pal_memory.h",
    "embedded/pal_pack.c",
    "embedded/pal_pack.h",
    "embedded/pal_palette_static.c",
    "embedded/pal_palette_static.h",
    "embedded/pal_scene_cache.c",
    "embedded/pal_scene_cache.h",
    "embedded/pal_text_cache.c",
    "embedded/pal_text_cache.h",
    "embedded/pal_ui_cache.c",
    "embedded/pal_ui_cache.h",
    "embedded/pal_video_static.c",
    "embedded/pal_video_static.h",
)

FORBIDDEN_SOURCE = (
    re.compile(r"\bmalloc\s*\("),
    re.compile(r"\bcalloc\s*\("),
    re.compile(r"\brealloc\s*\("),
    re.compile(r"\bfree\s*\("),
    re.compile(r"\bUTIL_malloc\s*\("),
    re.compile(r"\bUTIL_calloc\s*\("),
    re.compile(r"\bnew\b"),
    re.compile(r"\bdelete\b"),
    re.compile(r"\bPAL_MKFDecompressChunk\s*\("),
    re.compile(r"\bPAL_MKFGetDecompressedSize\s*\("),
    re.compile(r"\bYJ1_Decompress\s*\("),
    re.compile(r"\bYJ2_Decompress\s*\("),
    re.compile(r"\bDecompress\s*\("),
)

FORBIDDEN_LINKED_SYMBOLS = (
    "PAL_MKFDecompressChunk",
    "PAL_MKFGetDecompressedSize",
    "YJ1_Decompress",
    "YJ2_Decompress",
    "Decompress",
)

FORBIDDEN_PROJECT_UNDEFINED_SYMBOLS = FORBIDDEN_LINKED_SYMBOLS + (
    "malloc",
    "calloc",
    "realloc",
    "free",
    "_malloc_r",
    "_calloc_r",
    "_realloc_r",
    "_free_r",
    "__wrap_malloc",
    "__wrap_calloc",
    "__wrap_realloc",
    "__wrap_free",
)

KEY_SECTIONS = (
    ".iram0.text",
    ".dram0.data",
    ".dram0.bss",
    ".flash.text",
    ".flash.rodata",
    ".ext_ram.bss",
)


def run(cmd: list[str]) -> str:
    return subprocess.check_output(cmd, text=True)


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def scan_sources(root: Path) -> list[str]:
    hits: list[str] = []
    for rel in SOURCE_FILES:
        path = root / rel
        text = path.read_text(errors="replace")
        for line_no, line in enumerate(text.splitlines(), 1):
            if any(pattern.search(line) for pattern in FORBIDDEN_SOURCE):
                hits.append(f"{rel}:{line_no}: {line.strip()}")
    return hits


def parse_size(output: str) -> dict[str, int]:
    lines = [line.split() for line in output.splitlines() if line.strip()]
    if len(lines) < 2:
        return {}
    values: dict[str, int] = {}
    for key, value in zip(lines[0], lines[1][: len(lines[0])]):
        base = 16 if key == "hex" else 10
        try:
            values[key] = int(value, base)
        except ValueError:
            pass
    return values


def symbol_prefix_total(nm_output: str, prefix: str) -> tuple[int, list[tuple[int, str]]]:
    total = 0
    rows: list[tuple[int, str]] = []
    for line in nm_output.splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        try:
            size = int(parts[1], 16)
        except ValueError:
            continue
        name = parts[3]
        if name.startswith(prefix):
            total += size
            rows.append((size, name))
    return total, sorted(rows, reverse=True)


def parse_objdump_sections(output: str) -> dict[str, int]:
    sections: dict[str, int] = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        if not parts[0].isdigit():
            continue
        try:
            sections[parts[1]] = int(parts[2], 16)
        except ValueError:
            pass
    return sections


def linked_forbidden_symbols(nm_output: str) -> list[str]:
    hits: list[str] = []
    names = set()
    for line in nm_output.splitlines():
        parts = line.split()
        if parts:
            names.add(parts[-1])
    for name in FORBIDDEN_LINKED_SYMBOLS:
        if name in names:
            hits.append(name)
    return hits


def project_forbidden_undefined_symbols(output: str) -> list[str]:
    hits: list[str] = []
    current_object = ""
    forbidden = set(FORBIDDEN_PROJECT_UNDEFINED_SYMBOLS)

    for line in output.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.endswith(":"):
            current_object = stripped[:-1]
            continue
        parts = stripped.split()
        if len(parts) == 2 and parts[0] == "U" and parts[1] in forbidden:
            hits.append(f"{current_object}: {parts[1]}")
    return hits


def check_pack(path: Path, label: str, max_size: int | None) -> list[str]:
    errors: list[str] = []
    data = path.read_bytes()
    if max_size is not None and len(data) > max_size:
        errors.append(f"{label} pack is {len(data)} bytes, over limit {max_size}")
        return errors
    if len(data) < PACK_HEADER_SIZE or u32(data, 0) != PACK_MAGIC:
        errors.append(f"{label} pack header is invalid")
        return errors

    archive_count = u16(data, 8)
    archive_table = u32(data, 12)
    pack_size = u32(data, 24)
    if pack_size != len(data):
        errors.append(f"{label} pack declares {pack_size} bytes, file has {len(data)}")
        return errors
    if archive_table + archive_count * PACK_ARCHIVE_ENTRY_SIZE > len(data):
        errors.append(f"{label} pack archive table is out of range")
        return errors

    for archive_index in range(archive_count):
        archive = archive_table + archive_index * PACK_ARCHIVE_ENTRY_SIZE
        chunk_count = u16(data, archive + 2)
        chunk_table = u32(data, archive + 4)
        if chunk_table + chunk_count * PACK_CHUNK_ENTRY_SIZE > len(data):
            errors.append(f"{label} pack chunk table {archive_index} is out of range")
            continue
        for chunk_id in range(chunk_count):
            chunk = chunk_table + chunk_id * PACK_CHUNK_ENTRY_SIZE
            offset = u32(data, chunk)
            size = u32(data, chunk + 4)
            flags = u16(data, chunk + 10)
            if flags & PACK_CHUNK_F_COMPRESSED:
                errors.append(f"{label} pack archive {archive_index} chunk {chunk_id} is compressed")
            if offset + size > len(data):
                errors.append(f"{label} pack archive {archive_index} chunk {chunk_id} is out of range")
            if data[offset : offset + 4] == b"YJ_1":
                errors.append(f"{label} pack archive {archive_index} chunk {chunk_id} still contains YJ1")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--nor-pack", type=Path, required=True)
    parser.add_argument("--tf-pack", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    build_dir = (Path.cwd() / args.build_dir).resolve()
    elf = build_dir / "sdlpal_cores3se.elf"
    errors: list[str] = []

    print("# CoreS3 SE Contract Check")
    print(f"elf: {elf}")
    print(f"nor_pack: {args.nor_pack}")
    print(f"tf_pack: {args.tf_pack}")

    source_hits = scan_sources(root)
    print(f"\nsource forbidden hits: {len(source_hits)}")
    if source_hits:
        errors.extend(source_hits)
        for hit in source_hits:
            print(hit)

    errors.extend(check_pack(args.nor_pack, "NOR", PAL_NOR_PARTITION_BYTES))
    errors.extend(check_pack(args.tf_pack, "TF", None))
    print(f"\nNOR pack bytes: {args.nor_pack.stat().st_size} / {PAL_NOR_PARTITION_BYTES}")
    print(f"TF pack bytes: {args.tf_pack.stat().st_size}")

    size_output = run(["xtensa-esp32s3-elf-size", str(elf)])
    size_values = parse_size(size_output)
    print("\n## size")
    print(size_output.rstrip())

    objdump_sections = parse_objdump_sections(run(["xtensa-esp32s3-elf-objdump", "-h", str(elf)]))
    print("\n## sections")
    for section in KEY_SECTIONS:
        print(f"{section:16s} {objdump_sections.get(section, 0):8d}")

    nm_output = run(["xtensa-esp32s3-elf-nm", "-S", "--size-sort", str(elf)])
    project_undef_output = run(["xtensa-esp32s3-elf-nm", "-u", str(build_dir / "esp-idf/main/libmain.a")])
    sram_total, sram_rows = symbol_prefix_total(nm_output, "pal_sram_")
    psram_total, psram_rows = symbol_prefix_total(nm_output, "pal_psram_")
    linked_hits = linked_forbidden_symbols(nm_output)
    project_undef_hits = project_forbidden_undefined_symbols(project_undef_output)
    print(f"\nproject forbidden undefined hits: {len(project_undef_hits)}")
    for hit in project_undef_hits:
        print(hit)
    print(f"\npal_sram_ total={sram_total} / {SRAM_BUDGET}")
    for size, name in sram_rows:
        print(f"{size:8d} {name}")
    print(f"\npal_psram_ total={psram_total} / {PSRAM_BUDGET}")
    for size, name in psram_rows:
        print(f"{size:8d} {name}")

    if sram_total > SRAM_BUDGET:
        errors.append(f"pal_sram_ total {sram_total} exceeds {SRAM_BUDGET}")
    if psram_total > PSRAM_BUDGET:
        errors.append(f"pal_psram_ total {psram_total} exceeds {PSRAM_BUDGET}")
    dram_static = objdump_sections.get(".dram0.data", 0) + objdump_sections.get(".dram0.bss", 0)
    if dram_static > DRAM_STATIC_BUDGET:
        errors.append(f"DRAM static sections {dram_static} exceed {DRAM_STATIC_BUDGET}")
    if objdump_sections.get(".ext_ram.bss", 0) > PSRAM_BUDGET:
        errors.append(f".ext_ram.bss {objdump_sections.get('.ext_ram.bss', 0)} exceeds {PSRAM_BUDGET}")
    if objdump_sections.get(".ext_ram.bss", 0) < psram_total:
        errors.append(f".ext_ram.bss {objdump_sections.get('.ext_ram.bss', 0)} is smaller than pal_psram_ total {psram_total}")
    if linked_hits:
        errors.extend(f"linked forbidden symbol: {name}" for name in linked_hits)
    if project_undef_hits:
        errors.extend(f"project object forbidden undefined symbol: {hit}" for hit in project_undef_hits)
    if size_values.get("bss", 0) > 9000000:
        errors.append(f"bss exceeds broad target limit: {size_values.get('bss')}")

    if errors:
        print("\n## FAIL")
        for error in errors:
            print(error)
        return 1
    print("\n## PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
