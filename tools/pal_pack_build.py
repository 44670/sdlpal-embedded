#!/usr/bin/env python3
"""Build decoded/native PAL resource packs for the embedded runtime.

The generated packs are intentionally simple: every runtime payload is already
raw/native. YJ1 decoding happens here, on the host, never in target code.
"""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path


MAGIC = 0x4B504C50
VERSION = 1
HEADER_SIZE = 32
ARCHIVE_ENTRY_SIZE = 12
CHUNK_ENTRY_SIZE = 16

FORMAT_RAW = 0
FORMAT_NATIVE = 1
FORMAT_RNG_FRAMES = 2
FORMAT_TEXT_UTF16 = 3
FORMAT_FONT_GLYPHS = 4
FORMAT_SFX_PCM16 = 5

TEXT_MAGIC = 0x54585450
TEXT_VERSION = 1
TEXT_HEADER_SIZE = 32
FONT_MAGIC = 0x544E4650
FONT_VERSION = 1
FONT_HEADER_SIZE = 32
FONT_GLYPH_SOURCE_OFFSET = 0x682
FONT_GLYPH_SOURCE_BYTES = 30
FONT_GLYPH_BYTES = 32
SFX_MAGIC = 0x58465350
SFX_VERSION = 1
SFX_HEADER_SIZE = 24
SFX_TARGET_SAMPLE_RATE = 22050

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
    "VOC": 16,
    "TEXT": 17,
    "FONT": 18,
    "SFX": 19,
}

DEFAULT_NOR = ["ABC", "BALL", "DATA", "F", "FIRE", "MGO", "MIDI", "MUS", "PAT", "RGM", "SSS", "TEXT", "FONT"]
DEFAULT_TF = ["FBP", "GOP", "MAP", "RNG", "VOC", "SFX"]


@dataclass(frozen=True)
class Chunk:
    payload: bytes
    fmt: int


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def read_mkf(path: Path) -> list[bytes]:
    data = path.read_bytes()
    if len(data) < 4:
        raise ValueError(f"not an MKF file: {path}")
    first = u32(data, 0)
    if first < 4 or first % 4 or first > len(data):
        raise ValueError(f"bad MKF offset table: {path}")
    count = (first - 4) // 4
    offsets = [u32(data, i * 4) for i in range(count + 1)]
    chunks = []
    for index in range(count):
        start, end = offsets[index], offsets[index + 1]
        if start > end or end > len(data):
            raise ValueError(f"bad chunk range {path} #{index}: {start}..{end}")
        chunks.append(data[start:end])
    return chunks


def yj1_get_bits(data: bytes, bitptr_ref: list[int], count: int) -> int:
    bitptr = bitptr_ref[0]
    byte_offset = (bitptr >> 4) << 1
    bptr = bitptr & 0xF
    bitptr_ref[0] += count
    word0 = data[byte_offset] | (data[byte_offset + 1] << 8)
    if count > 16 - bptr:
        count = count + bptr - 16
        mask = 0xFFFF >> bptr
        word1 = data[byte_offset + 2] | (data[byte_offset + 3] << 8)
        return ((word0 & mask) << count) | (word1 >> (16 - count))
    return ((word0 << bptr) & 0xFFFF) >> (16 - count)


def yj1_get_loop(data: bytes, bitptr_ref: list[int], header: bytes) -> int:
    if yj1_get_bits(data, bitptr_ref, 1):
        return header[22]
    temp = yj1_get_bits(data, bitptr_ref, 2)
    if temp:
        return yj1_get_bits(data, bitptr_ref, header[18 + temp])
    return header[23]


def yj1_get_count(data: bytes, bitptr_ref: list[int], header: bytes) -> int:
    temp = yj1_get_bits(data, bitptr_ref, 2)
    if temp:
        if yj1_get_bits(data, bitptr_ref, 1):
            return yj1_get_bits(data, bitptr_ref, header[15 + temp])
        return u16(header, 4 + temp * 2)
    return u16(header, 4)


def yj1_decode(data: bytes) -> bytes:
    if len(data) < 16 or data[:4] != b"YJ_1":
        raise ValueError("not a YJ1 chunk")
    out_len = u32(data, 4)
    block_count = u16(data, 12)
    tree_len = data[15] * 2
    flag = data[16 + tree_len :]
    bitptr = [0]
    leaf = [False] * (tree_len + 1)
    value = [0] * (tree_len + 1)
    left = [-1] * (tree_len + 1)
    right = [-1] * (tree_len + 1)
    leaf[0] = False
    left[0] = 1
    right[0] = 2
    for i in range(1, tree_len + 1):
        leaf[i] = not bool(yj1_get_bits(flag, bitptr, 1))
        value[i] = data[15 + i]
        if not leaf[i]:
            left[i] = (value[i] << 1) + 1
            right[i] = left[i] + 1

    src = 16 + tree_len + (((tree_len >> 4) + (1 if tree_len & 0xF else 0)) << 1)
    dest = bytearray()

    for _ in range(block_count):
        header_start = src
        header = data[header_start : header_start + 24]
        src += 4
        comp_len = u16(header, 2)
        if comp_len == 0:
            raw_len = u16(header, 0)
            dest.extend(data[src : src + raw_len])
            src += raw_len
            continue

        src += 20
        block_data = data[src:]
        bitptr = [0]
        while True:
            loop = yj1_get_loop(block_data, bitptr, header)
            if loop == 0:
                break
            for _ in range(loop):
                node = 0
                while not leaf[node]:
                    node = right[node] if yj1_get_bits(block_data, bitptr, 1) else left[node]
                dest.append(value[node])

            loop = yj1_get_loop(block_data, bitptr, header)
            if loop == 0:
                break
            for _ in range(loop):
                count = yj1_get_count(block_data, bitptr, header)
                pos_index = yj1_get_bits(block_data, bitptr, 2)
                pos = yj1_get_bits(block_data, bitptr, header[12 + pos_index])
                for _ in range(count):
                    dest.append(dest[-pos])

        src = header_start + comp_len

    if len(dest) != out_len:
        raise ValueError(f"YJ1 decoded size mismatch: got {len(dest)}, expected {out_len}")
    return bytes(dest)


def decode_if_needed(chunk: bytes) -> bytes:
    if len(chunk) >= 8 and chunk[:4] == b"YJ_1":
        return yj1_decode(chunk)
    return chunk


def decode_rng_movie(chunk: bytes) -> bytes:
    if not chunk:
        return chunk
    if len(chunk) < 4:
        raise ValueError("short RNG movie")
    frame_count = (u32(chunk, 0) - 4) // 4
    offsets = [u32(chunk, i * 4) for i in range(frame_count + 1)]
    frames = []
    for index in range(frame_count):
        frame = chunk[offsets[index] : offsets[index + 1]]
        frames.append(decode_if_needed(frame))

    table_size = 4 + (frame_count + 1) * 4
    out = bytearray()
    out += struct.pack("<I", frame_count)
    cursor = table_size
    for frame in frames:
        out += struct.pack("<I", cursor)
        cursor += len(frame)
    out += struct.pack("<I", cursor)
    for frame in frames:
        out += frame
    return bytes(out)


def encode_text_pack(data_dir: Path) -> bytes:
    word_path = data_dir / "WORD.DAT"
    msg_path = data_dir / "M.MSG"
    sss_path = data_dir / "SSS.MKF"
    if not word_path.exists():
        word_path = data_dir / "word.dat"
    if not msg_path.exists():
        msg_path = data_dir / "m.msg"
    if not sss_path.exists():
        sss_path = data_dir / "sss.mkf"
    if not word_path.exists() or not msg_path.exists() or not sss_path.exists():
        raise FileNotFoundError("WORD.DAT/M.MSG/SSS.MKF")

    sss = read_mkf(sss_path)
    msg_offsets_data = sss[3]
    msg_offsets = [u32(msg_offsets_data, i * 4) for i in range(len(msg_offsets_data) // 4)]
    word_data = word_path.read_bytes()
    msg_data = msg_path.read_bytes()

    words = []
    for offset in range(0, len(word_data), 10):
        raw = word_data[offset : offset + 10].rstrip(b" \0")
        words.append(raw.decode("cp950", errors="strict").encode("utf-16le"))

    messages = []
    for index in range(len(msg_offsets) - 1):
        start, end = msg_offsets[index], msg_offsets[index + 1]
        if start > end or end > len(msg_data):
            raise ValueError(f"bad M.MSG offset range: {start}..{end}")
        messages.append(msg_data[start:end].decode("cp950", errors="strict").encode("utf-16le"))

    word_table_offset = TEXT_HEADER_SIZE
    message_table_offset = word_table_offset + (len(words) + 1) * 4
    text_offset = message_table_offset + (len(messages) + 1) * 4
    text = bytearray()

    word_offsets = []
    for item in words:
        word_offsets.append(len(text))
        text += item
    word_offsets.append(len(text))

    message_offsets = []
    for item in messages:
        message_offsets.append(len(text))
        text += item
    message_offsets.append(len(text))

    out = bytearray(
        struct.pack(
            "<IHHHHIIIII",
            TEXT_MAGIC,
            TEXT_VERSION,
            TEXT_HEADER_SIZE,
            len(words),
            len(messages),
            word_table_offset,
            message_table_offset,
            text_offset,
            len(text),
            0,
        )
    )
    out += b"".join(struct.pack("<I", offset) for offset in word_offsets)
    out += b"".join(struct.pack("<I", offset) for offset in message_offsets)
    out += text
    return bytes(out)


def find_data_file(data_dir: Path, name: str) -> Path:
    path = data_dir / name
    if path.exists():
        return path
    lower = data_dir / name.lower()
    if lower.exists():
        return lower
    raise FileNotFoundError(path)


def encode_font_pack(data_dir: Path) -> bytes:
    asc_path = find_data_file(data_dir, "WOR16.ASC")
    fon_path = find_data_file(data_dir, "WOR16.FON")
    asc = asc_path.read_bytes().rstrip(b"\xff")
    fon = fon_path.read_bytes()

    if len(asc) % 2 != 0:
        raise ValueError(f"odd WOR16.ASC byte count after terminator trim: {asc_path}")
    if len(fon) < FONT_GLYPH_SOURCE_OFFSET:
        raise ValueError(f"short WOR16.FON: {fon_path}")

    chars = asc.decode("cp950", errors="strict")
    source_count = min(len(chars), (len(fon) - FONT_GLYPH_SOURCE_OFFSET) // FONT_GLYPH_SOURCE_BYTES)
    glyphs: dict[int, bytes] = {}

    for index, ch in enumerate(chars[:source_count]):
        codepoint = ord(ch)
        if codepoint > 0xffff or codepoint in glyphs:
            continue
        start = FONT_GLYPH_SOURCE_OFFSET + index * FONT_GLYPH_SOURCE_BYTES
        glyphs[codepoint] = fon[start : start + FONT_GLYPH_SOURCE_BYTES] + b"\0\0"

    ordered_codepoints = sorted(glyphs)
    codepoint_table_offset = FONT_HEADER_SIZE
    glyph_data_offset = align4(codepoint_table_offset + len(ordered_codepoints) * 2)
    glyph_data_size = len(ordered_codepoints) * FONT_GLYPH_BYTES

    out = bytearray(
        struct.pack(
            "<IHHHHIIIII",
            FONT_MAGIC,
            FONT_VERSION,
            FONT_HEADER_SIZE,
            len(ordered_codepoints),
            FONT_GLYPH_BYTES,
            codepoint_table_offset,
            glyph_data_offset,
            glyph_data_size,
            source_count,
            0,
        )
    )
    out += b"".join(struct.pack("<H", codepoint) for codepoint in ordered_codepoints)
    out += b"\0" * (glyph_data_offset - len(out))
    out += b"".join(glyphs[codepoint] for codepoint in ordered_codepoints)
    return bytes(out)


def encode_sfx_payload(pcm: bytes, source_rate: int) -> bytes:
    if source_rate <= 0:
        raise ValueError(f"bad VOC source rate: {source_rate}")

    if not pcm:
        out_pcm = b""
    else:
        out_samples = (len(pcm) * SFX_TARGET_SAMPLE_RATE + source_rate // 2) // source_rate
        out = bytearray(out_samples * 2)
        for sample_index in range(out_samples):
            pos = sample_index * source_rate
            src_index = pos // SFX_TARGET_SAMPLE_RATE
            frac = pos % SFX_TARGET_SAMPLE_RATE
            if src_index >= len(pcm) - 1:
                sample = (pcm[-1] - 128) << 8
            else:
                s0 = (pcm[src_index] - 128) << 8
                s1 = (pcm[src_index + 1] - 128) << 8
                sample = (s0 * (SFX_TARGET_SAMPLE_RATE - frac) + s1 * frac) // SFX_TARGET_SAMPLE_RATE
            struct.pack_into("<h", out, sample_index * 2, max(-32768, min(32767, sample)))
        out_pcm = bytes(out)

    return struct.pack(
        "<IHHIIII",
        SFX_MAGIC,
        SFX_VERSION,
        SFX_HEADER_SIZE,
        SFX_TARGET_SAMPLE_RATE,
        len(out_pcm) // 2,
        SFX_HEADER_SIZE,
        len(out_pcm),
    ) + out_pcm


def encode_voc_sfx_chunk(raw: bytes) -> bytes:
    if not raw:
        return encode_sfx_payload(b"", SFX_TARGET_SAMPLE_RATE)
    if len(raw) < 26 or raw[:20] != b"Creative Voice File\x1a":
        raise ValueError("not a VOC chunk")

    data_offset = u16(raw, 20)
    if data_offset >= len(raw):
        raise ValueError("bad VOC data offset")

    cursor = data_offset
    while cursor < len(raw) and raw[cursor] != 0:
        if cursor + 4 > len(raw):
            raise ValueError("short VOC block")
        block_type = raw[cursor]
        block_size = raw[cursor + 1] | (raw[cursor + 2] << 8) | (raw[cursor + 3] << 16)
        block_start = cursor + 4
        block_end = block_start + block_size
        if block_end > len(raw):
            raise ValueError("VOC block out of range")
        if block_type == 1:
            if block_size < 2:
                raise ValueError("short VOC sound block")
            time_constant = raw[block_start]
            codec = raw[block_start + 1]
            if codec != 0:
                raise ValueError(f"unsupported VOC codec: {codec}")
            source_rate = ((1000000 // (256 - time_constant) + 99) // 100) * 100
            return encode_sfx_payload(raw[block_start + 2 : block_end], source_rate)
        cursor = block_end

    return encode_sfx_payload(b"", SFX_TARGET_SAMPLE_RATE)


def encode_sfx_pack(data_dir: Path) -> list[Chunk]:
    voc_path = find_data_file(data_dir, "VOC.MKF")
    return [Chunk(encode_voc_sfx_chunk(raw), FORMAT_SFX_PCM16) for raw in read_mkf(voc_path)]


def load_archive(data_dir: Path, name: str) -> list[Chunk]:
    if name == "TEXT":
        return [Chunk(encode_text_pack(data_dir), FORMAT_TEXT_UTF16)]
    if name == "FONT":
        return [Chunk(encode_font_pack(data_dir), FORMAT_FONT_GLYPHS)]
    if name == "SFX":
        return encode_sfx_pack(data_dir)

    path = data_dir / f"{name}.MKF"
    if not path.exists():
        path = data_dir / f"{name.lower()}.mkf"
    if not path.exists():
        raise FileNotFoundError(path)

    chunks = []
    for raw in read_mkf(path):
        if name == "RNG":
            chunks.append(Chunk(decode_rng_movie(raw), FORMAT_RNG_FRAMES))
        else:
            chunks.append(Chunk(decode_if_needed(raw), FORMAT_NATIVE))
    return chunks


def align4(value: int) -> int:
    return (value + 3) & ~3


def build_pack(archives: dict[str, list[Chunk]]) -> bytes:
    names = sorted(archives, key=lambda item: ARCHIVE_IDS[item])
    archive_table_offset = HEADER_SIZE
    chunk_table_offset = archive_table_offset + len(names) * ARCHIVE_ENTRY_SIZE
    chunk_table_size = sum(len(archives[name]) * CHUNK_ENTRY_SIZE for name in names)
    data_offset = align4(chunk_table_offset + chunk_table_size)

    archive_entries = bytearray()
    chunk_entries = bytearray()
    payload = bytearray(b"\0" * (data_offset - (chunk_table_offset + chunk_table_size)))
    cursor = data_offset

    for name in names:
        chunks = archives[name]
        archive_entries += struct.pack("<HHII", ARCHIVE_IDS[name], len(chunks), chunk_table_offset + len(chunk_entries), 0)
        for chunk in chunks:
            if cursor != align4(cursor):
                pad = align4(cursor) - cursor
                payload += b"\0" * pad
                cursor += pad
            chunk_entries += struct.pack("<IIHHI", cursor, len(chunk.payload), chunk.fmt, 0, 0)
            payload += chunk.payload
            cursor += len(chunk.payload)

    header = struct.pack(
        "<IHHHHIIIII",
        MAGIC,
        VERSION,
        HEADER_SIZE,
        len(names),
        0,
        archive_table_offset,
        data_offset,
        0,
        cursor,
        0,
    )
    return header + archive_entries + chunk_entries + payload


def checked_range(offset: int, size: int, total: int) -> bool:
    return 0 <= offset <= total and 0 <= size <= total - offset


def verify_pack(pack: bytes) -> None:
    if len(pack) < HEADER_SIZE:
        raise ValueError("short pack header")
    magic, version, header_size, archive_count, _reserved0 = struct.unpack_from("<IHHHH", pack, 0)
    archive_table_offset = u32(pack, 12)
    pack_size = u32(pack, 24)

    if magic != MAGIC:
        raise ValueError("bad pack magic")
    if version != VERSION:
        raise ValueError(f"bad pack version: {version}")
    if header_size != HEADER_SIZE:
        raise ValueError(f"bad pack header size: {header_size}")
    if pack_size != len(pack):
        raise ValueError(f"pack size field is {pack_size}, actual {len(pack)}")
    if not checked_range(archive_table_offset, archive_count * ARCHIVE_ENTRY_SIZE, len(pack)):
        raise ValueError("archive table out of range")

    seen_archives: set[int] = set()
    for archive_index in range(archive_count):
        archive_entry_offset = archive_table_offset + archive_index * ARCHIVE_ENTRY_SIZE
        archive_id, chunk_count = struct.unpack_from("<HH", pack, archive_entry_offset)
        chunk_table_offset = u32(pack, archive_entry_offset + 4)
        if archive_id in seen_archives:
            raise ValueError(f"duplicate archive id: {archive_id}")
        seen_archives.add(archive_id)
        if not checked_range(chunk_table_offset, chunk_count * CHUNK_ENTRY_SIZE, len(pack)):
            raise ValueError(f"chunk table out of range for archive {archive_id}")

        for chunk_index in range(chunk_count):
            chunk_entry_offset = chunk_table_offset + chunk_index * CHUNK_ENTRY_SIZE
            payload_offset = u32(pack, chunk_entry_offset)
            payload_size = u32(pack, chunk_entry_offset + 4)
            flags = u16(pack, chunk_entry_offset + 10)
            if flags != 0:
                raise ValueError(f"runtime chunk has flags 0x{flags:04x}: archive {archive_id} chunk {chunk_index}")
            if not checked_range(payload_offset, payload_size, len(pack)):
                raise ValueError(f"payload out of range: archive {archive_id} chunk {chunk_index}")


def parse_names(raw: str | None, default: list[str]) -> list[str]:
    if raw is None:
        return default
    names = [item.strip().upper() for item in raw.split(",") if item.strip()]
    unknown = [name for name in names if name not in ARCHIVE_IDS]
    if unknown:
        raise SystemExit(f"unknown archive names: {', '.join(unknown)}")
    return names


def write_pack(data_dir: Path, out_path: Path, names: list[str]) -> None:
    archives = {name: load_archive(data_dir, name) for name in names}
    pack = build_pack(archives)
    verify_pack(pack)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(pack)
    print(f"{out_path}: {len(pack)} bytes, archives={','.join(names)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data_dir", type=Path)
    parser.add_argument("--out-nor", type=Path, required=True)
    parser.add_argument("--out-tf", type=Path, required=True)
    parser.add_argument("--nor", help="comma-separated archives for NOR pack")
    parser.add_argument("--tf", help="comma-separated archives for TF pack")
    args = parser.parse_args()

    data_dir = args.data_dir
    nor_names = parse_names(args.nor, DEFAULT_NOR)
    tf_names = parse_names(args.tf, DEFAULT_TF)
    overlap = sorted(set(nor_names) & set(tf_names))
    if overlap:
        raise SystemExit(f"archives listed in both packs: {', '.join(overlap)}")

    write_pack(data_dir, args.out_nor, nor_names)
    write_pack(data_dir, args.out_tf, tf_names)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
