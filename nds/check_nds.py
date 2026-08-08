#!/usr/bin/env python3
"""Measured build gate for the classic Nintendo DS target."""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
import subprocess
from pathlib import Path


MAIN_RAM_LIMIT = 0x02380000
MIN_UNUSED_MAIN_RAM = 256 * 1024
ITCM_START = 0x01FF8000
ITCM_LIMIT = 0x02000000
DTCM_START = 0x02FF0000
DTCM_LIMIT = 0x02FF3E80
MIN_UNUSED_DTCM = 3 * 1024
PACK_ARCHIVE_ENTRY_SIZE = 12
PACK_CHUNK_ENTRY_SIZE = 16
MGO_ARCHIVE_ID = 9
MUS_ARCHIVE_ID = 11
EXPECTED_OWNERS = {
    "pal_sram_framebuffer": 256 * 192,
    "pal_sram_aux_framebuffer": 256 * 192,
    "pal_mem_level2_scene_arena": 256 * 1024,
    "pal_mem_level2_player_arena": 128 * 1024,
    "pal_mem_level2_battle_arena": 512 * 1024,
    "pal_mem_level2_fight_effect": 64 * 1024,
    "pal_mem_level2_fight_summon": 64 * 1024,
    "pal_mem_level2_resident_pack": 1024 * 1024,
    "pal_mem_level2_tf_toc": 40 * 1024,
    "pal_mem_level2_transient_chunk": 64 * 1024,
    "pal_nds_track": 10108,
    "pal_nds_opl_staging": 256 * 2,
    "pal_nds_opl_tick_queue": 32 * (4 + 256 * 2),
    "pal_nds_audio_ring": 16 * 512 * 2,
    "pal_nds_audio_thread_stack": 6 * 1024,
    "pal_nds_opl_queue_overruns": 4,
    "pal_nds_opl_queue_underruns": 4,
    "pal_nds_audio_deadline_misses": 4,
    "pal_nds_present_count": 4,
    "pal_nds_dbopl_render_ticks_total": 4,
    "pal_nds_dbopl_render_ticks_max": 4,
    "pal_nds_dbopl_render_ticks_min": 4,
    "pal_nds_dbopl_render_calls": 4,
    "PalNdsDbOpl2Core::pal_nds_dbopl2_state": 2472,
    "PalNdsDbOpl2Core::pal_nds_dbopl2_reset_state": 2472,
    "PalNdsDbOpl2Core::pal_nds_dbopl2_scratch": 256 * 4,
    "PalNdsDbOpl2Core::EnvelopeBuffer": 2 * 256 * 2,
    "PalNdsDbOpl2Core::WaveTable": 4 * 512 * 2,
    "PalNdsDbOpl2Core::MulTable": 384 * 2,
    "PalNdsDbOpl2Core::KslTable": 128,
    "PalNdsDbOpl2Core::TremoloTable": 52,
    "PalNdsDbOpl2Core::ChanOffsetTable": 32 * 2,
    "PalNdsDbOpl2Core::OpOffsetTable": 64 * 2,
}
EXPECTED_DTCM_OWNERS = (
    "PalNdsDbOpl2Core::pal_nds_dbopl2_state",
    "PalNdsDbOpl2Core::pal_nds_dbopl2_scratch",
    "PalNdsDbOpl2Core::EnvelopeBuffer",
    "PalNdsDbOpl2Core::WaveTable",
    "PalNdsDbOpl2Core::MulTable",
)
EXPECTED_GAME_TITLE = b"SDLPAL\0\0\0\0\0\0"
EXPECTED_GAME_CODE = b"####"
EXPECTED_MAKER_CODE = b"00"
# Ship the TWL-aware image (unit code 0x02, full 0x4000 header with the TWL
# loadlist): the NTR-only image (ndstool -h 0x200, unit 0x00) white-screens
# on the accepted real-hardware boot path (TWiLight Menu / nds-bootstrap),
# verified against a known-good on-device binary.
EXPECTED_UNIT_CODE = 0x02
EXPECTED_HEADER_SIZE = 0x4000
MAX_OPL_WRITES_PER_TICK = 256


def run(*args: str) -> str:
    result = subprocess.run(
        args, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True,
    )
    return result.stdout


def parse_symbols(elf: Path, nm: str) -> dict[str, tuple[int, int, str]]:
    # Demangle C++ names so retired-owner checks see e.g.
    # PalMameOpl2Core::pal_mame_opl2_state instead of the mangled spelling.
    output = run(nm, "-C", "-S", "--defined-only", str(elf))
    symbols: dict[str, tuple[int, int, str]] = {}
    pattern = re.compile(
        r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([A-Za-z])\s+(\S+)$"
    )
    absolute_pattern = re.compile(
        r"^([0-9a-fA-F]+)\s+([A-Za-z])\s+(\S+)$"
    )
    for line in output.splitlines():
        match = pattern.match(line.strip())
        if match:
            symbols[match.group(4)] = (
                int(match.group(1), 16), int(match.group(2), 16), match.group(3)
            )
            continue
        match = absolute_pattern.match(line.strip())
        if match:
            symbols[match.group(3)] = (
                int(match.group(1), 16), 0, match.group(2)
            )
    return symbols


def parse_nitro_listing(nds: Path, ndstool: str) -> tuple[int, int, int]:
    output = run(ndstool, "-l", str(nds))
    files = []
    pattern = re.compile(
        r"^\s*\d+\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)"
        r"\s+(\d+)\s+(/\S+)$"
    )
    for line in output.splitlines():
        match = pattern.match(line)
        if match:
            files.append(
                (int(match.group(1), 16), int(match.group(2), 16),
                 int(match.group(3)), match.group(4))
            )
    if len(files) != 1 or files[0][3] != "/pal_full.pak":
        raise ValueError(f"NitroFS must contain only /pal_full.pak, got {files!r}")
    start, end, size, _name = files[0]
    if end - start != size:
        raise ValueError(
            f"NitroFS extent mismatch: {start:#x}..{end:#x}, size={size}"
        )
    return start, end, size


def hash_extent(path: Path, start: int, size: int) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        source.seek(start)
        remaining = size
        while remaining:
            block = source.read(min(1024 * 1024, remaining))
            if not block:
                raise ValueError(f"short extent in {path}")
            digest.update(block)
            remaining -= len(block)
    return digest.hexdigest()


def profile_rix_writes(profiler: Path, pack: Path) -> tuple[int, int]:
    output = run(str(profiler), str(pack))
    header: list[str] | None = None
    maximum = 0
    maximum_track = 0
    for line in output.splitlines():
        fields = line.split("\t")
        if "max_tick_writes" in fields:
            header = fields
            continue
        if header is None or len(fields) != len(header):
            continue
        try:
            track = int(fields[0])
            writes = int(fields[header.index("max_tick_writes")])
        except ValueError:
            continue
        if writes > maximum:
            maximum = writes
            maximum_track = track
    if header is None:
        raise ValueError("RIX profiler did not return its tabular header")
    return maximum, maximum_track


def check_nds_header(path: Path) -> list[str]:
    with path.open("rb") as source:
        header = source.read(0x240)
    if len(header) != 0x240:
        return ["NDS header is shorter than 0x240 bytes"]

    errors = []
    if header[0x00:0x0C] != EXPECTED_GAME_TITLE:
        errors.append(f"NDS game title is {header[0x00:0x0C]!r}, expected SDLPAL")
    if header[0x0C:0x10] != EXPECTED_GAME_CODE:
        errors.append(
            f"NDS game code is {header[0x0C:0x10]!r}, expected homebrew ####"
        )
    if header[0x10:0x12] != EXPECTED_MAKER_CODE:
        errors.append(f"NDS maker code is {header[0x10:0x12]!r}, expected 00")
    if header[0x12] != EXPECTED_UNIT_CODE:
        errors.append(
            f"NDS unit code is 0x{header[0x12]:02x}, "
            f"expected TWL-aware 0x{EXPECTED_UNIT_CODE:02x}"
        )
    header_size = struct.unpack_from("<I", header, 0x84)[0]
    if header_size != EXPECTED_HEADER_SIZE:
        errors.append(
            f"NDS header size is 0x{header_size:x}, "
            f"expected full 0x{EXPECTED_HEADER_SIZE:x} with the TWL loadlist"
        )
    public_save, private_save = struct.unpack_from("<II", header, 0x238)
    if header_size >= 0x240 and (public_save != 0 or private_save != 0):
        errors.append(
            "DSiWare public/private save sizes must remain zero for the "
            "DLDI FAT save backend"
        )
    return errors


def pack_identity(path: Path) -> tuple[int, int]:
    with path.open("rb") as source:
        header = source.read(32)
    if len(header) != 32 or struct.unpack_from("<I", header, 0)[0] != 0x4B504C50:
        raise ValueError(f"{path} is not a PAL pack")
    if struct.unpack_from("<H", header, 4)[0] != 1:
        raise ValueError(f"{path} has an unsupported PAL pack version")
    declared = struct.unpack_from("<I", header, 24)[0]
    if declared != path.stat().st_size:
        raise ValueError(
            f"PAL pack size field is {declared}, file is {path.stat().st_size}"
        )
    return struct.unpack_from("<I", header, 20)[0], declared


def pack_chunk_sizes(
    path: Path, archive_id: int, chunk_ids: tuple[int, ...] | None
) -> dict[int, int]:
    file_size = path.stat().st_size
    with path.open("rb") as source:
        header = source.read(32)
        archive_count = struct.unpack_from("<H", header, 8)[0]
        archive_table = struct.unpack_from("<I", header, 12)[0]
        if archive_table + archive_count * PACK_ARCHIVE_ENTRY_SIZE > file_size:
            raise ValueError("PAL pack archive table is out of range")
        source.seek(archive_table)
        entries = source.read(archive_count * PACK_ARCHIVE_ENTRY_SIZE)
        for index in range(archive_count):
            entry = index * PACK_ARCHIVE_ENTRY_SIZE
            current_id, chunk_count, chunk_table = struct.unpack_from(
                "<HHI", entries, entry
            )
            if current_id != archive_id:
                continue
            if chunk_ids is not None and (
                not chunk_ids or max(chunk_ids) >= chunk_count
            ):
                raise ValueError(f"PAL pack archive {archive_id} is too short")
            if chunk_table + chunk_count * PACK_CHUNK_ENTRY_SIZE > file_size:
                raise ValueError(
                    f"PAL pack archive {archive_id} chunk table is out of range"
                )
            sizes: dict[int, int] = {}
            selected_ids = range(chunk_count) if chunk_ids is None else chunk_ids
            for chunk_id in selected_ids:
                source.seek(chunk_table + chunk_id * PACK_CHUNK_ENTRY_SIZE)
                chunk = source.read(PACK_CHUNK_ENTRY_SIZE)
                if len(chunk) != PACK_CHUNK_ENTRY_SIZE:
                    raise ValueError("short PAL pack chunk entry")
                offset, size = struct.unpack_from("<II", chunk)
                if offset + size > file_size:
                    raise ValueError(
                        f"PAL pack archive {archive_id} chunk {chunk_id} "
                        "is out of range"
                    )
                sizes[chunk_id] = size
            return sizes
    raise ValueError(f"PAL pack is missing archive {archive_id}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--nds", type=Path, required=True)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--rix-profiler", type=Path, required=True)
    parser.add_argument(
        "--tool-prefix", default="/opt/devkitpro/devkitARM/bin/arm-none-eabi-"
    )
    parser.add_argument("--ndstool", default="/opt/devkitpro/tools/bin/ndstool")
    args = parser.parse_args()

    for path in (args.elf, args.nds, args.pack, args.rix_profiler):
        if not path.is_file():
            raise SystemExit(f"missing required artifact: {path}")

    symbols = parse_symbols(args.elf, args.tool_prefix + "nm")
    errors = check_nds_header(args.nds)
    for name, expected_size in EXPECTED_OWNERS.items():
        actual = symbols.get(name)
        if actual is None:
            errors.append(f"missing fixed owner {name}")
        elif actual[1] != expected_size:
            errors.append(
                f"{name} is {actual[1]} bytes, expected {expected_size}"
            )

    for retired in (
        "pal_nds_voice_waves",
        "pal_nds_rhythm_waves",
        "pal_nds_sine",
        "pal_nds_base_timers",
        "pal_nds_tl_volume",
        "pal_mame_opl2_state",
    ):
        if any(
            name == retired or name.endswith("::" + retired) for name in symbols
        ):
            errors.append(f"retired NDS music owner is still linked: {retired}")

    opl_render = symbols.get("NdsDbOpl2_Render")
    if opl_render is None:
        errors.append("missing NdsDbOpl2_Render")
    elif not (ITCM_START <= opl_render[0] < ITCM_LIMIT):
        errors.append(
            f"NdsDbOpl2_Render is at {opl_render[0]:#010x}, outside ARM9 ITCM"
        )

    for name in EXPECTED_DTCM_OWNERS:
        owner = symbols.get(name)
        if owner is not None and not (
            DTCM_START <= owner[0] and owner[0] + owner[1] <= DTCM_LIMIT
        ):
            errors.append(
                f"{name} is at {owner[0]:#010x}, outside ARM9 DTCM"
            )

    dtcm_bss_end = symbols.get("__dtcm_bss_end")
    if dtcm_bss_end is None:
        errors.append("missing __dtcm_bss_end")
        unused_dtcm = 0
    else:
        unused_dtcm = DTCM_LIMIT - dtcm_bss_end[0]
        if unused_dtcm < MIN_UNUSED_DTCM:
            errors.append(
                f"unused ARM9 DTCM user-stack space is {unused_dtcm}, "
                f"minimum {MIN_UNUSED_DTCM}"
            )

    heap = symbols.get("__heap_start_ntr")
    if heap is None:
        errors.append("missing __heap_start_ntr")
        heap_start = MAIN_RAM_LIMIT
    else:
        heap_start = heap[0]
    unused = MAIN_RAM_LIMIT - heap_start
    if unused < MIN_UNUSED_MAIN_RAM:
        errors.append(
            f"unused ARM9 main RAM is {unused}, minimum {MIN_UNUSED_MAIN_RAM}"
        )

    start, _end, nitro_size = parse_nitro_listing(args.nds, args.ndstool)
    pack_set, pack_size = pack_identity(args.pack)
    cinematic_mgo = pack_chunk_sizes(
        args.pack, MGO_ARCHIVE_ID, (71, 73, 571, 572, 635)
    )
    mus_sizes = pack_chunk_sizes(args.pack, MUS_ARCHIVE_ID, None)
    oversized_mus = [
        (chunk_id, size)
        for chunk_id, size in mus_sizes.items()
        if size > EXPECTED_OWNERS["pal_nds_track"]
    ]
    if oversized_mus:
        chunk_id, size = max(oversized_mus, key=lambda item: item[1])
        errors.append(
            f"MUS chunk {chunk_id} is {size} bytes, exceeds "
            f"pal_nds_track ({EXPECTED_OWNERS['pal_nds_track']} bytes)"
        )
    for chunk_id, owner in (
        (71, "pal_mem_level2_fight_effect"),
        (73, "pal_mem_level2_fight_summon"),
        (571, "pal_mem_level2_fight_effect"),
        (572, "pal_mem_level2_fight_summon"),
        (635, "pal_mem_level2_fight_effect"),
    ):
        if cinematic_mgo[chunk_id] == 0:
            errors.append(f"cinematic MGO {chunk_id} is empty")
        elif cinematic_mgo[chunk_id] > EXPECTED_OWNERS[owner]:
            errors.append(
                f"cinematic MGO {chunk_id} is {cinematic_mgo[chunk_id]} "
                f"bytes, exceeds {owner}"
            )
    if nitro_size != pack_size:
        errors.append(
            f"embedded pal_full.pak is {nitro_size} bytes, source is {pack_size}"
        )
    source_hash = hash_extent(args.pack, 0, pack_size)
    embedded_hash = hash_extent(args.nds, start, nitro_size)
    if source_hash != embedded_hash:
        errors.append("embedded pal_full.pak SHA-256 differs from its source")

    maximum_writes, maximum_write_track = profile_rix_writes(
        args.rix_profiler, args.pack
    )
    if maximum_writes > MAX_OPL_WRITES_PER_TICK:
        errors.append(
            f"RIX track {maximum_write_track} writes {maximum_writes} OPL "
            f"registers in one tick, exceeds {MAX_OPL_WRITES_PER_TICK}"
        )

    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1

    fixed_bytes = sum(EXPECTED_OWNERS.values())
    print("Nintendo DS target check passed")
    print(
        f"  ARM9_end={heap_start:#010x}, limit={MAIN_RAM_LIMIT:#010x}, "
        f"unused={unused} bytes"
    )
    print(f"  fixed_owners={fixed_bytes} bytes, native_screens=2x256x192x8")
    print(f"  unused_DTCM_user_stack={unused_dtcm} bytes")
    print(
        f"  max_opl_writes_per_tick={maximum_writes} "
        f"(track {maximum_write_track}), capacity={MAX_OPL_WRITES_PER_TICK}"
    )
    print(
        f"  ROM={args.nds.stat().st_size} bytes, pal_full.pak={pack_size} bytes, "
        f"pack_set={pack_set:#010x}"
    )
    print(f"  pal_full.pak_sha256={source_hash}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
