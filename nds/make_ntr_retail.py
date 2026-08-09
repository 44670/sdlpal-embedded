#!/usr/bin/env python3
"""Convert ndstool output into the NTR-only retail launch image."""

from __future__ import annotations

import argparse
import struct
import subprocess
import tempfile
from pathlib import Path


HEADER_SIZE = 0x4000
HEADER_CRC_END = 0x15E
TWL_HEADER_START = 0x160
SECURE_AREA_START = 0x4000
SECURE_AREA_END = 0x8000
ROM_ALIGNMENT = 0x200
DECRYPTED_SECURE_MARKER = b"\xFF\xDE\xFF\xE7\xFF\xDE\xFF\xE7"
DLDI_MAGIC = b"\xED\xA5\x8D\xBF Chishm"


def crc16(data: bytes | bytearray, initial: int = 0xFFFF) -> int:
    value = initial
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xA001 if value & 1 else 0)
    return value


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & -alignment


def encrypted_secure_crc(image: bytearray, ndstool: Path) -> int:
    if not ndstool.is_file():
        raise SystemExit(f"ndstool not found: {ndstool}")
    with tempfile.TemporaryDirectory(prefix="sdlpal-ntr-") as temp_dir:
        encrypted_path = Path(temp_dir) / "secure.nds"
        encrypted_path.write_bytes(image)
        result = subprocess.run(
            (str(ndstool), "-se", str(encrypted_path)),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if result.returncode != 0 or "Encrypted." not in result.stdout:
            raise SystemExit(
                "ndstool could not encrypt the temporary secure area:\n"
                + result.stdout
            )
        with encrypted_path.open("rb") as source:
            source.seek(SECURE_AREA_START)
            secure_area = source.read(SECURE_AREA_END - SECURE_AREA_START)
    if len(secure_area) != SECURE_AREA_END - SECURE_AREA_START:
        raise SystemExit("ndstool produced a short encrypted secure area")
    return crc16(secure_area)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--game-code", required=True)
    parser.add_argument(
        "--ndstool", type=Path,
        default=Path("/opt/devkitpro/tools/bin/ndstool"),
    )
    args = parser.parse_args()

    game_code = args.game_code.encode("ascii")
    if len(game_code) != 4 or not game_code.isalnum() or game_code == b"####":
        raise SystemExit("game code must be four ASCII letters/digits, not ####")

    image = bytearray(args.input.read_bytes())
    if len(image) < SECURE_AREA_END:
        raise SystemExit("ndstool image is too short for an NTR secure area")

    arm9_rom, arm9_entry, arm9_ram = struct.unpack_from("<III", image, 0x20)
    ntr_end = struct.unpack_from("<I", image, 0x80)[0]
    header_size = struct.unpack_from("<I", image, 0x84)[0]
    entry_offset = arm9_rom + arm9_entry - arm9_ram
    if header_size != HEADER_SIZE or arm9_rom < HEADER_SIZE:
        raise SystemExit("ndstool did not produce a full 0x4000 NTR header")
    if not arm9_ram <= arm9_entry < arm9_ram + struct.unpack_from(
        "<I", image, 0x2C
    )[0]:
        raise SystemExit("ARM9 entrypoint lies outside the ARM9 binary")
    if entry_offset + 8 > len(image):
        raise SystemExit("ARM9 entrypoint lies outside the ROM")
    first_word, second_word = struct.unpack_from("<II", image, entry_offset)
    if not 0xEA000000 <= first_word < 0xEC000000 or second_word != 0x39444F4D:
        raise SystemExit("unexpected Calico ARM9 boot signature")
    if entry_offset < SECURE_AREA_START + len(DECRYPTED_SECURE_MARKER):
        raise SystemExit("ARM9 entrypoint leaves no decrypted secure-area marker")
    if not SECURE_AREA_END <= ntr_end <= len(image):
        raise SystemExit("invalid NTR application end offset")
    image[0x0C:0x10] = game_code
    image[0x12] = 0x00
    image[TWL_HEADER_START:HEADER_SIZE] = bytes(HEADER_SIZE - TWL_HEADER_START)
    image[
        SECURE_AREA_START : SECURE_AREA_START + len(DECRYPTED_SECURE_MARKER)
    ] = DECRYPTED_SECURE_MARKER

    final_size = align_up(ntr_end, ROM_ALIGNMENT)
    image = image[:final_size]
    if len(image) < final_size:
        image.extend(b"\xFF" * (final_size - len(image)))
    if DLDI_MAGIC in image:
        raise SystemExit("DLDI patch target remains in the NTR image")

    # Retail headers store the CRC of the encrypted secure area even when the
    # distributed dump itself carries the standard decrypted marker. Reuse
    # ndstool's KEY1 implementation on a temporary copy; the final ROM remains
    # decrypted. The linked module parameters and CARD/backup routines provide
    # the retail classifier and patch surfaces; Calico's entry metadata stays
    # intact.
    secure_crc = encrypted_secure_crc(image, args.ndstool)
    struct.pack_into("<H", image, 0x6C, secure_crc)
    header_crc = crc16(image[:HEADER_CRC_END])
    struct.pack_into("<H", image, 0x15E, header_crc)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(
        f"NTR retail image: game={args.game_code}, bytes={len(image)}, "
        f"entry={entry_offset:#x}, secure_crc={secure_crc:#06x}, "
        f"header_crc={header_crc:#06x}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
