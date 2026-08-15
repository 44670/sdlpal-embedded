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
ARM7_RAW_START = 0x02380000
ARM7_RAW_LIMIT = 0x023F0000
ITCM_START = 0x01FF8000
ITCM_LIMIT = 0x02000000
DTCM_START = 0x02FF0000
DTCM_LIMIT = 0x02FF3E80
MIN_UNUSED_DTCM = 3 * 1024
PACK_ARCHIVE_ENTRY_SIZE = 12
PACK_CHUNK_ENTRY_SIZE = 16
MGO_ARCHIVE_ID = 9
MUS_ARCHIVE_ID = 11
MAP_ARCHIVE_ID = 8
SSS_ARCHIVE_ID = 15
EXPECTED_OWNERS = {
    "pal_sram_framebuffer": 256 * 192,
    "pal_sram_aux_framebuffer": 256 * 192,
    "pal_nds_minimap_topology": (193 * 191 + 7) // 8,
    "pal_nds_minimap_tiles": 1024 * 64,
    "pal_nds_minimap_pattern_tiles": 4096 * 2,
    "pal_nds_minimap_tilemap": 128 * 128 * 2,
    "pal_nds_minimap_marker_tiles": 64,
    "pal_nds_minimap_obstacle_tile": 32,
    "pal_nds_save_verify": 256,
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
    "PalNdsDbOpl2Core::pal_nds_dbopl2_scratch": 128 * 4,
    "PalNdsDbOpl2Core::EnvelopeBuffer": 2 * 128 * 2,
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
EXPECTED_UNIT_CODE = 0x00
EXPECTED_HEADER_SIZE = 0x4000
DLDI_MAGIC = b"\xED\xA5\x8D\xBF Chishm"
DLDI_RESERVED_BYTES = 16 * 1024
DLDI_RUNTIME_ADDRESS = 0x0380B000
DECRYPTED_SECURE_MARKER = b"\xFF\xDE\xFF\xE7" * 2
MAX_OPL_WRITES_PER_TICK = 256
MINIMAP_TILE_CAPACITY = 1024
MINIMAP_OVERLAY_CAPACITY = 128
MINIMAP_LOGICAL_COLUMNS = 193
MINIMAP_LOGICAL_ROWS = 191
MINIMAP_VISIBLE_COLUMNS = 66
MINIMAP_VISIBLE_ROWS = 50
MINIMAP_SCRIPT_SCAN_LIMIT = 512
MINIMAP_SCRIPT_GRAPH_LIMIT = 64
MINIMAP_SCENE_EVENT_CAPACITY = 160
MINIMAP_EVENT_MOVE_OPERATIONS = frozenset(
    (*range(0x000B, 0x000F), 0x0010, 0x0011, 0x0012, 0x0013,
     0x003F, 0x0044, 0x004C, 0x006C, 0x007C, 0x007D,
     0x0082, 0x0084, 0x0097)
)
PINNED_PAL_DOS_PACK_SHA256 = (
    "9c01aec3be2f9a9404551c27ec1bb3c7d580c10accd4b3b8d254d05c9c4cfb71"
)
PINNED_PAL_DOS_MINIMAP_CLOSURES = (
    # scene, MAP, seed, cells, DWORD-zero, exits, structural blockers
    (4, 1, (95, 80), 5741, 1937, 97, 6),
    (3, 10, (132, 114), 469, 0, 62, 2),
    # Yangzhou scene 82 otherwise leaks into the full unused MAP lattice.
    (82, 79, (86, 111), 2200, 0, 62, 3),
)


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


def parse_sections(elf: Path, readelf: str) -> dict[str, tuple[int, int]]:
    output = run(readelf, "-SW", str(elf))
    sections: dict[str, tuple[int, int]] = {}
    pattern = re.compile(
        r"^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+"
        r"([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+"
    )
    for line in output.splitlines():
        match = pattern.match(line)
        if match:
            sections[match.group(1)] = (
                int(match.group(2), 16), int(match.group(3), 16)
            )
    return sections


def crc16(data: bytes, initial: int = 0xFFFF) -> int:
    value = initial
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xA001 if value & 1 else 0)
    return value


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


def profile_rix_writes(
    profiler: Path, pack: Path
) -> tuple[int, int, dict[int, tuple[bool, int, int, int, int]]]:
    output = run(str(profiler), str(pack))
    header: list[str] | None = None
    maximum = 0
    maximum_track = 0
    profiles: dict[int, tuple[bool, int, int, int, int]] = {}
    required = {
        "track", "rhythm_mode", "melodic_note_on", "reserved_note_on",
        "max_held", "max_tick_writes",
    }
    for line in output.splitlines():
        fields = line.split("\t")
        if "max_tick_writes" in fields:
            if not required.issubset(fields):
                raise ValueError(
                    "RIX profiler is missing rhythm-workload columns"
                )
            header = fields
            continue
        if header is None or len(fields) != len(header):
            continue
        try:
            track = int(fields[header.index("track")])
            rhythm_mode = int(fields[header.index("rhythm_mode")]) != 0
            melodic_note_ons = int(fields[header.index("melodic_note_on")])
            reserved_note_ons = int(fields[header.index("reserved_note_on")])
            maximum_held = int(fields[header.index("max_held")])
            writes = int(fields[header.index("max_tick_writes")])
        except ValueError:
            continue
        profiles[track] = (
            rhythm_mode, melodic_note_ons, reserved_note_ons,
            maximum_held, writes,
        )
        if writes > maximum:
            maximum = writes
            maximum_track = track
    if header is None:
        raise ValueError("RIX profiler did not return its tabular header")
    return maximum, maximum_track, profiles


def check_nds_header(path: Path) -> list[str]:
    with path.open("rb") as source:
        header = source.read(0x8000)
    if len(header) != 0x8000:
        return ["NDS image is shorter than the NTR secure area"]

    errors = []
    if header[0x00:0x0C] != EXPECTED_GAME_TITLE:
        errors.append(f"NDS game title is {header[0x00:0x0C]!r}, expected SDLPAL")
    if header[0x0C:0x10] != EXPECTED_GAME_CODE:
        errors.append(
            f"NDS game code is {header[0x0C:0x10]!r}, "
            f"expected homebrew {EXPECTED_GAME_CODE!r}"
        )
    if header[0x10:0x12] != EXPECTED_MAKER_CODE:
        errors.append(f"NDS maker code is {header[0x10:0x12]!r}, expected 00")
    if header[0x12] != EXPECTED_UNIT_CODE:
        errors.append(
            f"NDS unit code is 0x{header[0x12]:02x}, "
            f"expected NTR-only 0x{EXPECTED_UNIT_CODE:02x}"
        )
    header_size = struct.unpack_from("<I", header, 0x84)[0]
    if header_size != EXPECTED_HEADER_SIZE:
        errors.append(
            f"NDS header size is 0x{header_size:x}, "
            f"expected NTR header area 0x{EXPECTED_HEADER_SIZE:x}"
        )
    if any(header[0x160:EXPECTED_HEADER_SIZE]):
        errors.append(
            "TWL/extended header area 0x160..0x3fff must be zero"
        )
    arm9_rom, arm9_entry, arm9_ram, arm9_size = struct.unpack_from(
        "<IIII", header, 0x20
    )
    arm7_rom, arm7_entry, arm7_ram, arm7_size = struct.unpack_from(
        "<IIII", header, 0x30
    )
    arm9_hook, arm7_hook = struct.unpack_from("<II", header, 0x70)
    # Pico Loader's current homebrew classifier accepts two zero autoload
    # hooks. Require that exact header signal so it cannot enter its retail
    # path; ndstool's ASCII "00" maker code is a separate homebrew convention.
    if arm9_hook != 0 or arm7_hook != 0:
        errors.append("Pico Loader homebrew autoload hooks must both be zero")
    if not arm9_ram <= arm9_entry < arm9_ram + arm9_size:
        errors.append("ARM9 entrypoint lies outside the ARM9 binary")
    if arm7_ram != ARM7_RAW_START:
        errors.append(
            f"ARM7 raw image starts at {arm7_ram:#010x}, "
            f"expected {ARM7_RAW_START:#010x}"
        )
    if not arm7_ram <= arm7_entry < arm7_ram + arm7_size:
        errors.append("ARM7 entrypoint lies outside the ARM7 binary")
    if arm7_ram + arm7_size > ARM7_RAW_LIMIT:
        errors.append(
            f"ARM7 raw image ends at {arm7_ram + arm7_size:#010x}, "
            f"overlaps loader space at {ARM7_RAW_LIMIT:#010x}"
        )
    with path.open("rb") as source:
        source.seek(arm9_rom)
        arm9_binary = source.read(arm9_size)
        source.seek(arm7_rom)
        arm7_binary = source.read(arm7_size)
    if len(arm9_binary) != arm9_size or len(arm7_binary) != arm7_size:
        errors.append("ARM9 or ARM7 binary extent lies outside the ROM")
    else:
        if not arm9_binary.startswith(DECRYPTED_SECURE_MARKER):
            errors.append(
                "ARM9 lacks the decrypted homebrew secure marker required "
                "to bypass Pico Loader's Blowfish-key path"
            )
        dldi_offsets = [
            offset for offset in range(len(arm9_binary))
            if arm9_binary.startswith(DLDI_MAGIC, offset)
        ]
        if len(dldi_offsets) != 1:
            errors.append(
                f"ARM9 must contain one DLDI patch target, got {dldi_offsets}"
            )
        else:
            dldi_offset = dldi_offsets[0]
            if dldi_offset + 0x80 > len(arm9_binary):
                errors.append("DLDI header lies outside the ARM9 binary")
            else:
                dldi = arm9_binary[dldi_offset:dldi_offset + 0x80]
                if dldi[0x0C] != 1:
                    errors.append(f"unsupported DLDI version {dldi[0x0C]}")
                reserved = 1 << dldi[0x0F]
                if reserved != DLDI_RESERVED_BYTES:
                    errors.append(
                        f"DLDI patch space is {reserved} bytes, expected "
                        f"{DLDI_RESERVED_BYTES} for Pico Loader/DSpico"
                    )
                driver_start, driver_end = struct.unpack_from(
                    "<II", dldi, 0x40
                )
                if driver_start != DLDI_RUNTIME_ADDRESS or driver_end != (
                    DLDI_RUNTIME_ADDRESS + DLDI_RESERVED_BYTES
                ):
                    errors.append(
                        "DLDI runtime range is "
                        f"{driver_start:#010x}..{driver_end:#010x}, expected "
                        f"{DLDI_RUNTIME_ADDRESS:#010x}.."
                        f"{DLDI_RUNTIME_ADDRESS + DLDI_RESERVED_BYTES:#010x}"
                    )
    secure_crc = struct.unpack_from("<H", header, 0x6C)[0]
    direct_secure_crc = crc16(header[arm9_rom:0x8000])
    if secure_crc == direct_secure_crc:
        errors.append(
            "homebrew secure-area CRC matches the loaded bytes and can "
            "trigger Pico Loader's Blowfish-key path"
        )
    header_crc = struct.unpack_from("<H", header, 0x15E)[0]
    expected_header_crc = crc16(header[:0x15E])
    if header_crc != expected_header_crc:
        errors.append(
            f"header CRC is {header_crc:#06x}, expected {expected_header_crc:#06x}"
        )
    ntr_end = struct.unpack_from("<I", header, 0x80)[0]
    expected_file_size = (ntr_end + 0x1FF) & ~0x1FF
    if path.stat().st_size != expected_file_size:
        errors.append(
            f"NTR ROM is {path.stat().st_size} bytes, expected trimmed "
            f"size {expected_file_size}"
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


def pack_chunk_payload(path: Path, archive_id: int, chunk_id: int) -> bytes:
    """Read one validated native chunk from the complete PAL pack."""
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
            if chunk_id >= chunk_count:
                raise ValueError(
                    f"PAL pack archive {archive_id} is missing chunk {chunk_id}"
                )
            if chunk_table + chunk_count * PACK_CHUNK_ENTRY_SIZE > file_size:
                raise ValueError(
                    f"PAL pack archive {archive_id} chunk table is out of range"
                )
            source.seek(chunk_table + chunk_id * PACK_CHUNK_ENTRY_SIZE)
            chunk = source.read(PACK_CHUNK_ENTRY_SIZE)
            offset, size = struct.unpack_from("<II", chunk)
            if offset + size > file_size:
                raise ValueError(
                    f"PAL pack archive {archive_id} chunk {chunk_id} is out of range"
                )
            source.seek(offset)
            payload = source.read(size)
            if len(payload) != size:
                raise ValueError(
                    f"PAL pack archive {archive_id} chunk {chunk_id} is short"
                )
            return payload
    raise ValueError(f"PAL pack is missing archive {archive_id}")


def pack_rhythm_tracks(path: Path, chunk_count: int) -> list[int]:
    """Return nonempty RIX chunks whose header selects OPL2 rhythm mode."""
    tracks: list[int] = []
    for chunk_id in range(chunk_count):
        payload = pack_chunk_payload(path, MUS_ARCHIVE_ID, chunk_id)
        if len(payload) > 2 and payload[2] != 0:
            tracks.append(chunk_id)
    return tracks


def check_rhythm_playback_policy() -> list[str]:
    """Keep rhythm tracks audible without linking the over-budget drum loop."""
    nds_dir = Path(__file__).resolve().parent
    music = (nds_dir / "source" / "nds_music.cpp").read_text(
        encoding="utf-8"
    )
    dbopl = (nds_dir / "source" / "nds_dbopl2.itcm.cpp").read_text(
        encoding="utf-8"
    )
    errors: list[str] = []
    if re.search(r"pal_nds_track\s*\[\s*2\s*\]\s*[!=]=", music):
        errors.append(
            "NDS music loader must not reject a complete RIX track by its "
            "rhythm header"
        )
    if "#define PAL_DBOPL_DISABLE_PERCUSSION 1" not in dbopl:
        errors.append(
            "NDS DBOPL must keep the measured over-budget percussion loop "
            "disabled"
        )
    return errors


def minimap_world_to_cell(world_x: int, world_y: int) -> tuple[int, int]:
    """Mirror PAL_CheckObstacleWithRange's isometric diamond selection."""
    raw_x, residual_x = divmod(world_x, 32)
    raw_y, residual_y = divmod(world_y, 16)
    half = 0
    if residual_x + residual_y * 2 >= 16:
        if residual_x + residual_y * 2 >= 48:
            raw_x += 1
            raw_y += 1
        elif 32 - residual_x + residual_y * 2 < 16:
            raw_x += 1
        elif 32 - residual_x + residual_y * 2 < 48:
            half = 1
        else:
            raw_y += 1
    return raw_x + raw_y + half, raw_y - raw_x + 63


def minimap_script_changes_scene(
    scripts: list[tuple[int, int, int, int]], script_entry: int
) -> bool:
    """Mirror the bounded runtime classifier for automatic transitions."""
    scanned = 0
    while (
        script_entry != 0
        and script_entry < len(scripts)
        and scanned < MINIMAP_SCRIPT_SCAN_LIMIT
    ):
        operation, operand0, operand1, _operand2 = scripts[script_entry]
        scanned += 1
        if operation == 0x0059:
            return True
        if operation in (0x0000, 0x0001, 0x0002):
            return False
        if operation == 0x0003 and operand1 == 0:
            script_entry = operand0
        else:
            script_entry += 1
    return False


def minimap_auto_script_moves_event(
    scripts: list[tuple[int, int, int, int]], script_entry: int
) -> bool:
    """Mirror the bounded runtime classifier for stationary blockers."""
    pending = [script_entry] if script_entry else []
    visited: set[int] = set()
    while pending:
        script_entry = pending.pop()
        if script_entry == 0 or script_entry in visited:
            continue
        if not 0 < script_entry < len(scripts):
            return True
        if len(visited) >= MINIMAP_SCRIPT_GRAPH_LIMIT:
            return True
        visited.add(script_entry)
        operation, operand0, operand1, _operand2 = scripts[script_entry]
        if operation in MINIMAP_EVENT_MOVE_OPERATIONS:
            return True
        if operation == 0x0000:
            continue
        if operation in (0x0002, 0x0003):
            if operand0:
                pending.append(operand0)
            if operand1:
                pending.append(script_entry + 1)
        elif operation == 0x0004:
            if operand0:
                pending.append(operand0)
            pending.append(script_entry + 1)
        elif operation == 0x0006:
            if operand1:
                pending.append(operand1)
            pending.append(script_entry + 1)
        else:
            pending.append(script_entry + 1)
        if len(pending) > MINIMAP_SCRIPT_GRAPH_LIMIT:
            return True
    return False


def minimap_pattern_key_space() -> int:
    """Prove the dictionary bound for arbitrary selected-component edges."""
    normalized: set[int] = set()
    for key in range(1 << 12):
        relevant = 0x000F
        relevant |= ((1 << 4) | (1 << 6)) if key & (1 << 0) else 0
        relevant |= ((1 << 5) | (1 << 8)) if key & (1 << 1) else 0
        relevant |= ((1 << 7) | (1 << 10)) if key & (1 << 2) else 0
        relevant |= ((1 << 9) | (1 << 11)) if key & (1 << 3) else 0
        normalized.add(key & relevant)
    return len(normalized)


def pack_minimap_overlay_peak(path: Path) -> tuple[int, int]:
    """Conservatively bound every event object in one visible map window."""
    event_data = pack_chunk_payload(path, SSS_ARCHIVE_ID, 0)
    scene_data = pack_chunk_payload(path, SSS_ARCHIVE_ID, 1)
    if len(event_data) == 0 or len(event_data) % 32:
        raise ValueError("SSS event-object chunk is malformed")
    if len(scene_data) < 16 or len(scene_data) % 8:
        raise ValueError("SSS scene chunk is malformed")

    events = list(struct.iter_unpack("<16H", event_data))
    scenes = list(struct.iter_unpack("<4H", scene_data))

    peak = (0, -1)
    for scene_number in range(1, len(scenes)):
        first = scenes[scene_number - 1][3]
        end = scenes[scene_number][3]
        if first > end or end > len(events):
            raise ValueError(f"SSS scene {scene_number} has an invalid event range")
        grid = [0] * (MINIMAP_LOGICAL_COLUMNS * MINIMAP_LOGICAL_ROWS)
        for event in events[first:end]:
            column, row = minimap_world_to_cell(event[1], event[2])
            if (
                0 <= column < MINIMAP_LOGICAL_COLUMNS
                and 0 <= row < MINIMAP_LOGICAL_ROWS
            ):
                grid[row * MINIMAP_LOGICAL_COLUMNS + column] += 1

        prefix_width = MINIMAP_LOGICAL_COLUMNS + 1
        prefix = [0] * (prefix_width * (MINIMAP_LOGICAL_ROWS + 1))
        for row in range(MINIMAP_LOGICAL_ROWS):
            row_sum = 0
            for column in range(MINIMAP_LOGICAL_COLUMNS):
                row_sum += grid[row * MINIMAP_LOGICAL_COLUMNS + column]
                prefix[(row + 1) * prefix_width + column + 1] = (
                    prefix[row * prefix_width + column + 1] + row_sum
                )
        for top in range(MINIMAP_LOGICAL_ROWS):
            bottom = min(top + MINIMAP_VISIBLE_ROWS, MINIMAP_LOGICAL_ROWS)
            for left in range(MINIMAP_LOGICAL_COLUMNS):
                right = min(
                    left + MINIMAP_VISIBLE_COLUMNS,
                    MINIMAP_LOGICAL_COLUMNS,
                )
                count = (
                    prefix[bottom * prefix_width + right]
                    - prefix[top * prefix_width + right]
                    - prefix[bottom * prefix_width + left]
                    + prefix[top * prefix_width + left]
                )
                if count > peak[0]:
                    peak = (count, scene_number)
    return peak


def pack_minimap_scene_event_peak(path: Path) -> tuple[int, int]:
    """Return the largest validated current-scene event-object range."""
    event_data = pack_chunk_payload(path, SSS_ARCHIVE_ID, 0)
    scene_data = pack_chunk_payload(path, SSS_ARCHIVE_ID, 1)
    if len(event_data) == 0 or len(event_data) % 32:
        raise ValueError("SSS event-object chunk is malformed")
    if len(scene_data) < 16 or len(scene_data) % 8:
        raise ValueError("SSS scene chunk is malformed")
    event_count = len(event_data) // 32
    scenes = list(struct.iter_unpack("<4H", scene_data))
    peak = (0, -1)
    for scene_number in range(1, len(scenes)):
        first = scenes[scene_number - 1][3]
        end = scenes[scene_number][3]
        if first > end or end > event_count:
            raise ValueError(f"SSS scene {scene_number} has an invalid event range")
        if end - first > peak[0]:
            peak = (end - first, scene_number)
    return peak


def pack_minimap_forced_exit_closure(
    path: Path,
    scene_number: int,
    seed: tuple[int, int],
) -> tuple[int, int, int, int, int]:
    """Run the runtime closure with transition and structural cells blocked."""
    event_data = pack_chunk_payload(path, SSS_ARCHIVE_ID, 0)
    scene_data = pack_chunk_payload(path, SSS_ARCHIVE_ID, 1)
    script_data = pack_chunk_payload(path, SSS_ARCHIVE_ID, 4)
    if len(event_data) == 0 or len(event_data) % 32:
        raise ValueError("SSS event-object chunk is malformed")
    if len(scene_data) < 16 or len(scene_data) % 8:
        raise ValueError("SSS scene chunk is malformed")
    if len(script_data) == 0 or len(script_data) % 8:
        raise ValueError("SSS script chunk is malformed")

    events = list(struct.iter_unpack("<16H", event_data))
    scenes = list(struct.iter_unpack("<4H", scene_data))
    scripts = list(struct.iter_unpack("<4H", script_data))
    if not 0 < scene_number < len(scenes):
        raise ValueError(f"invalid minimap fixture scene {scene_number}")
    map_number = scenes[scene_number - 1][0]
    first = scenes[scene_number - 1][3]
    end = scenes[scene_number][3]
    if first > end or end > len(events):
        raise ValueError(f"SSS scene {scene_number} has an invalid event range")

    portals = []
    structural_blockers = []
    for event in events[first:end]:
        vanish_time = event[0] - 0x10000 if event[0] & 0x8000 else event[0]
        state = event[6] - 0x10000 if event[6] & 0x8000 else event[6]
        if (
            vanish_time == 0
            and state > 0
            and event[7] >= 4
            and event[4] != 0
            and minimap_script_changes_scene(scripts, event[4])
        ):
            portals.append(event)
        if (
            vanish_time == 0
            and state >= 2
            and event[7] == 0
            and event[4] == 0
            and not minimap_auto_script_moves_event(scripts, event[5])
        ):
            structural_blockers.append(event)

    payload = pack_chunk_payload(path, MAP_ARCHIVE_ID, map_number)
    if len(payload) != 128 * 64 * 2 * 4:
        raise ValueError(f"MAP chunk {map_number} is malformed")
    topology: dict[tuple[int, int], tuple[int, int]] = {}
    zero_tiles: set[tuple[int, int]] = set()
    for record, (tile,) in enumerate(struct.iter_unpack("<I", payload)):
        if tile & 0x2000:
            continue
        raw_y, within_row = divmod(record, 64 * 2)
        raw_x, half = divmod(within_row, 2)
        cell = raw_x + raw_y + half, raw_y - raw_x + 63
        topology[cell] = raw_x * 32 + half * 16, raw_y * 16 + half * 8
        if tile == 0:
            zero_tiles.add(cell)

    forced_exits = {
        cell
        for cell, (world_x, world_y) in topology.items()
        if any(
            abs(event[1] - world_x) + 2 * abs(event[2] - world_y)
            < (event[7] - 4) * 32 + 16
            for event in portals
        )
    }
    structural_cells = {
        cell
        for cell, (world_x, world_y) in topology.items()
        if any(
            abs(event[1] - world_x) + 2 * abs(event[2] - world_y) < 16
            for event in structural_blockers
        )
    }
    closure_blocked = forced_exits | structural_cells
    if seed not in topology or seed in closure_blocked:
        raise ValueError(
            f"scene {scene_number} minimap seed {seed} is not selectable"
        )
    selected = {seed}
    queue = [seed]
    for cell in queue:
        column, row = cell
        for neighbor in (
            (column - 1, row),
            (column + 1, row),
            (column, row - 1),
            (column, row + 1),
        ):
            if (
                neighbor in topology
                and neighbor not in closure_blocked
                and neighbor not in selected
            ):
                selected.add(neighbor)
                queue.append(neighbor)
    if selected & closure_blocked:
        raise ValueError(
            f"scene {scene_number} minimap selected a closure-blocked cell"
        )
    return (
        map_number,
        len(selected),
        len(selected & zero_tiles),
        len(forced_exits),
        len(structural_cells),
    )


def pack_map_pattern_peak(path: Path) -> tuple[int, int]:
    """Measure the deduplicated 8x8 boundary tiles required by each MAP."""
    file_size = path.stat().st_size
    peak = (0, -1)
    with path.open("rb") as source:
        header = source.read(32)
        archive_count = struct.unpack_from("<H", header, 8)[0]
        archive_table = struct.unpack_from("<I", header, 12)[0]
        source.seek(archive_table)
        entries = source.read(archive_count * PACK_ARCHIVE_ENTRY_SIZE)
        for index in range(archive_count):
            entry = index * PACK_ARCHIVE_ENTRY_SIZE
            archive_id, chunk_count, chunk_table = struct.unpack_from(
                "<HHI", entries, entry
            )
            if archive_id == MAP_ARCHIVE_ID:
                break
        else:
            raise ValueError("PAL pack is missing MAP archive")

        for map_id in range(chunk_count):
            source.seek(chunk_table + map_id * PACK_CHUNK_ENTRY_SIZE)
            offset, size = struct.unpack("<II", source.read(8))
            if size == 0:
                continue
            if size != 128 * 64 * 2 * 4 or offset + size > file_size:
                raise ValueError(f"MAP chunk {map_id} is malformed")
            source.seek(offset)
            payload = source.read(size)
            topology: set[tuple[int, int]] = set()
            for record, (tile,) in enumerate(struct.iter_unpack("<I", payload)):
                # Bottom tile index zero is valid terrain. Runtime collision
                # excludes only records carrying the MAP block flag.
                if tile & 0x2000:
                    continue
                raw_y, within_row = divmod(record, 64 * 2)
                raw_x, half = divmod(within_row, 2)
                topology.add((raw_x + raw_y + half, raw_y - raw_x + 63))
            if not topology:
                continue

            min_column = min(point[0] for point in topology)
            max_column = max(point[0] for point in topology)
            min_row = min(point[1] for point in topology)
            max_row = max(point[1] for point in topology)
            tile_columns = (max_column - min_column + 2) // 2
            tile_rows = (max_row - min_row + 2) // 2
            patterns: set[int] = set()
            for tile_y in range(tile_rows):
                for tile_x in range(tile_columns):
                    column = min_column + tile_x * 2
                    row = min_row + tile_y * 2
                    neighbors = (
                        (column, row), (column + 1, row),
                        (column, row + 1), (column + 1, row + 1),
                        (column, row - 1), (column + 1, row - 1),
                        (column - 1, row), (column - 1, row + 1),
                        (column + 2, row), (column + 2, row + 1),
                        (column, row + 2), (column + 1, row + 2),
                    )
                    key = sum(
                        1 << bit
                        for bit, point in enumerate(neighbors)
                        if point in topology
                    )
                    relevant = 0x000F
                    relevant |= ((1 << 4) | (1 << 6)) if key & (1 << 0) else 0
                    relevant |= ((1 << 5) | (1 << 8)) if key & (1 << 1) else 0
                    relevant |= ((1 << 7) | (1 << 10)) if key & (1 << 2) else 0
                    relevant |= ((1 << 9) | (1 << 11)) if key & (1 << 3) else 0
                    patterns.add(key & relevant)
            if len(patterns) > peak[0]:
                peak = (len(patterns), map_id)
    return peak


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
    sections = parse_sections(args.elf, args.tool_prefix + "readelf")
    errors = check_nds_header(args.nds)
    errors.extend(check_rhythm_playback_policy())
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

    for required in (
        "fatInitDefault",
        "nitroFSMount",
        "nitroromOpen",
        "nitroromGetSelf",
        "cardReadEeprom",
        "cardWriteEeprom",
        "cardEepromSectorErase",
        "NdsTarget_SetLaunchPath",
        "NdsTargetSave_SetDldiReady",
        "NdsTargetSave_SetRetailReady",
        "PalTargetSave_ReadSlot",
        "PalTargetSave_WriteSlot",
    ):
        if required not in symbols:
            errors.append(f"missing dual-storage symbol: {required}")
    for symbol in symbols:
        if symbol == "dvmProbeMountDiscIface" or symbol.startswith("NdsRetail"):
            errors.append(f"retired storage shim is linked: {symbol}")
    dldi_section = sections.get(".dldi")
    if dldi_section != (DLDI_RUNTIME_ADDRESS, DLDI_RESERVED_BYTES):
        errors.append(
            "ARM9 .dldi section must occupy "
            f"{DLDI_RUNTIME_ADDRESS:#010x} for {DLDI_RESERVED_BYTES} bytes, "
            f"got {dldi_section}"
        )
    for section in (".twl", ".twl.rw", ".twl.bss"):
        if sections.get(section, (0, 0))[1] != 0:
            errors.append(
                f"ARM9 ELF contains nonempty forbidden section {section}: "
                f"{sections[section][1]} bytes"
            )

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
    source_hash = hash_extent(args.pack, 0, pack_size)
    cinematic_mgo = pack_chunk_sizes(
        args.pack, MGO_ARCHIVE_ID, (71, 73, 571, 572, 635)
    )
    mus_sizes = pack_chunk_sizes(args.pack, MUS_ARCHIVE_ID, None)
    rhythm_tracks = pack_rhythm_tracks(args.pack, len(mus_sizes))
    if not rhythm_tracks:
        errors.append(
            "complete MUS archive has no rhythm track to exercise the "
            "melodic-only playback policy"
        )
    map_sizes = pack_chunk_sizes(args.pack, MAP_ARCHIVE_ID, None)
    malformed_maps = [
        (chunk_id, size)
        for chunk_id, size in map_sizes.items()
        if size not in (0, 128 * 64 * 2 * 4)
    ]
    if malformed_maps:
        errors.append(
            "MAP chunks used by the full-map renderer have malformed native "
            f"sizes: {malformed_maps[:8]}"
        )
        maximum_minimap_tiles, maximum_minimap_map = 0, -1
    else:
        maximum_minimap_tiles, maximum_minimap_map = pack_map_pattern_peak(
            args.pack
        )
        if maximum_minimap_tiles > MINIMAP_TILE_CAPACITY:
            errors.append(
                f"MAP chunk {maximum_minimap_map} requires "
                f"{maximum_minimap_tiles} minimap tiles, exceeds "
                f"{MINIMAP_TILE_CAPACITY}"
            )
    minimap_pattern_keys = minimap_pattern_key_space()
    if minimap_pattern_keys > MINIMAP_TILE_CAPACITY:
        errors.append(
            f"arbitrary connected minimap boundaries can require "
            f"{minimap_pattern_keys} tiles, exceeds {MINIMAP_TILE_CAPACITY}"
        )
    maximum_minimap_overlays, maximum_minimap_overlay_scene = (
        pack_minimap_overlay_peak(args.pack)
    )
    if maximum_minimap_overlays > MINIMAP_OVERLAY_CAPACITY:
        errors.append(
            f"scene {maximum_minimap_overlay_scene} can contain "
            f"{maximum_minimap_overlays} visible minimap overlays, exceeds "
            f"{MINIMAP_OVERLAY_CAPACITY} OBJ slots"
        )
    maximum_scene_events, maximum_scene_event_scene = (
        pack_minimap_scene_event_peak(args.pack)
    )
    if maximum_scene_events > MINIMAP_SCENE_EVENT_CAPACITY:
        errors.append(
            f"scene {maximum_scene_event_scene} contains "
            f"{maximum_scene_events} event objects, exceeds stationary "
            f"classifier capacity {MINIMAP_SCENE_EVENT_CAPACITY}"
        )
    pinned_minimap_closures: list[str] = []
    if source_hash == PINNED_PAL_DOS_PACK_SHA256:
        for (
            scene_number,
            expected_map,
            seed,
            expected_cells,
            expected_zero_cells,
            expected_forced_exits,
            expected_structural_cells,
        ) in PINNED_PAL_DOS_MINIMAP_CLOSURES:
            (
                map_number,
                cells,
                zero_cells,
                forced_exits,
                structural_cells,
            ) = (
                pack_minimap_forced_exit_closure(
                    args.pack, scene_number, seed
                )
            )
            pinned_minimap_closures.append(
                f"scene{scene_number}/map{map_number}="
                f"{cells}cells/{zero_cells}zero/{forced_exits}exits/"
                f"{structural_cells}structural"
            )
            if (
                map_number != expected_map
                or cells != expected_cells
                or zero_cells != expected_zero_cells
                or forced_exits != expected_forced_exits
                or structural_cells != expected_structural_cells
            ):
                errors.append(
                    "pinned PAL_DOS minimap closure mismatch: "
                    f"scene {scene_number}, map {map_number}, seed {seed}, "
                    f"got {cells} cells/{zero_cells} DWORD-zero cells/"
                    f"{forced_exits} blocked exit cells/"
                    f"{structural_cells} structural blocker cells; "
                    f"expected map {expected_map}, {expected_cells}/"
                    f"{expected_zero_cells}/{expected_forced_exits}/"
                    f"{expected_structural_cells}"
                )
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
    embedded_hash = hash_extent(args.nds, start, nitro_size)
    if source_hash != embedded_hash:
        errors.append("embedded pal_full.pak SHA-256 differs from its source")
    if start + nitro_size <= 32 * 1024 * 1024:
        errors.append(
            "embedded pack does not exercise DLDI self-ROM reads beyond 32MiB"
        )

    maximum_writes, maximum_write_track, rix_profiles = profile_rix_writes(
        args.rix_profiler, args.pack
    )
    if maximum_writes > MAX_OPL_WRITES_PER_TICK:
        errors.append(
            f"RIX track {maximum_write_track} writes {maximum_writes} OPL "
            f"registers in one tick, exceeds {MAX_OPL_WRITES_PER_TICK}"
        )
    profiled_rhythm_tracks = sorted(
        track for track, profile in rix_profiles.items() if profile[0]
    )
    if profiled_rhythm_tracks != rhythm_tracks:
        errors.append(
            "RIX profiler rhythm tracks differ from MUS headers: "
            f"profiled={profiled_rhythm_tracks}, headers={rhythm_tracks}"
        )
    silent_rhythm_tracks = [
        track for track in rhythm_tracks
        if track not in rix_profiles or rix_profiles[track][1] == 0
    ]
    if silent_rhythm_tracks:
        errors.append(
            "rhythm tracks have no melodic key-on events after decoder "
            f"replay: {silent_rhythm_tracks}"
        )
    keyed_reserved_channels = [
        track for track in rhythm_tracks
        if track in rix_profiles and rix_profiles[track][2] != 0
    ]
    if keyed_reserved_channels:
        errors.append(
            "rhythm tracks key on channels reserved for percussion: "
            f"{keyed_reserved_channels}"
        )
    excess_rhythm_polyphony = [
        track for track in rhythm_tracks
        if track in rix_profiles and rix_profiles[track][3] > 6
    ]
    if excess_rhythm_polyphony:
        errors.append(
            "rhythm tracks exceed six simultaneous melodic channels: "
            f"{excess_rhythm_polyphony}"
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
    print(f"  DLDI_patch_space={DLDI_RESERVED_BYTES} bytes")
    print(
        f"  max_minimap_tiles={maximum_minimap_tiles} "
        f"(map {maximum_minimap_map}), capacity={MINIMAP_TILE_CAPACITY}"
    )
    print(
        f"  minimap_boundary_key_space={minimap_pattern_keys}, "
        f"capacity={MINIMAP_TILE_CAPACITY}"
    )
    print(
        f"  max_minimap_overlays={maximum_minimap_overlays} "
        f"(scene {maximum_minimap_overlay_scene}), "
        f"capacity={MINIMAP_OVERLAY_CAPACITY}"
    )
    print(
        f"  max_minimap_scene_events={maximum_scene_events} "
        f"(scene {maximum_scene_event_scene}), "
        f"stationary_capacity={MINIMAP_SCENE_EVENT_CAPACITY}"
    )
    if pinned_minimap_closures:
        print(
            "  pinned_PAL_DOS_minimap_closures="
            + ",".join(pinned_minimap_closures)
        )
    print(
        f"  max_opl_writes_per_tick={maximum_writes} "
        f"(track {maximum_write_track}), capacity={MAX_OPL_WRITES_PER_TICK}"
    )
    rhythm_melodic_min = min(
        (rix_profiles[track][1] for track in rhythm_tracks
         if track in rix_profiles),
        default=0,
    )
    rhythm_max_held = max(
        (rix_profiles[track][3] for track in rhythm_tracks
         if track in rix_profiles),
        default=0,
    )
    print(
        f"  rhythm_tracks={len(rhythm_tracks)}, "
        f"min_melodic_note_ons={rhythm_melodic_min}, "
        f"reserved_note_ons=0, max_held={rhythm_max_held}, "
        "policy=melodic-channels-only"
    )
    print(
        f"  ROM={args.nds.stat().st_size} bytes, pal_full.pak={pack_size} bytes, "
        f"pack_set={pack_set:#010x}"
    )
    print("  resource_backends=DLDI-self-ROM,Slot-1-NitroFS")
    print("  save_backends=fat:/sdlpal/N.rpg,SPI-FLASH-1MiB")
    print(f"  pal_full.pak_sha256={source_hash}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
