#!/usr/bin/env python3
"""Build decoded/native PAL resource packs for the embedded runtime.

The generated packs are intentionally simple: every runtime payload is already
raw/native. YJ1 decoding happens here, on the host, never in target code.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path


MAGIC = 0x4B504C50
VERSION = 1
HEADER_SIZE = 32
ARCHIVE_ENTRY_SIZE = 12
CHUNK_ENTRY_SIZE = 16
PACK_SET_ID_OFFSET = 20
PACK_CRC32_OFFSET = 28

FORMAT_RAW = 0
FORMAT_NATIVE = 1
FORMAT_RNG_FRAMES = 2
FORMAT_TEXT_UTF16 = 3
FORMAT_FONT_GLYPHS = 4
FORMAT_SFX_PCM16 = 5
FORMAT_FONT10 = 6
FORMAT_NAMES = {
    FORMAT_RAW: "RAW",
    FORMAT_NATIVE: "NATIVE",
    FORMAT_RNG_FRAMES: "RNG_FRAMES",
    FORMAT_TEXT_UTF16: "TEXT_UTF16",
    FORMAT_FONT_GLYPHS: "FONT_GLYPHS",
    FORMAT_SFX_PCM16: "SFX_PCM16",
    FORMAT_FONT10: "FONT10",
}

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
    # Host-generated chapter-cache catalog.  This archive has no loose PAL
    # source file; tools/pal_chapter_pack_build.py injects its binary chunk.
    "CACHE": 20,
}

DEFAULT_LAYOUT_PATH = Path(__file__).with_name("pal_pack_layout_default.json")

# The native small-screen path reuses the original WORD.DAT/M.MSG text and may
# add only strings that the target engine actually renders.  Keep every such
# string explicit so it changes the FONT10 identity and pack manifest visibly.
FONT10_UI_LABELS: dict[str, str] = {
    "chapter_complete": "CHAPTER COMPLETE - SUZHOU NEXT",
    "numeric_glyphs": "0123456789/",
    "cheat": "CHEAT",
    "cheat_money": "MONEY",
    "cheat_invincible": "INVINCIBLE",
    "cheat_always_win": "ALWAYS WIN",
}


@dataclass(frozen=True)
class Chunk:
    payload: bytes
    fmt: int
    present: bool = True


@dataclass(frozen=True)
class ChunkRule:
    """A layout-v2 rule applied without renumbering the source chunks."""

    all_chunks: bool
    chunk_ids: frozenset[int]
    ranges: tuple[tuple[int, int], ...]
    prefix_bytes: tuple[tuple[int, int], ...]


@dataclass(frozen=True)
class PackLayout:
    version: int
    pack_names: dict[str, list[str]]
    chunk_rules: dict[str, dict[str, ChunkRule]]
    max_bytes: dict[str, int | None]
    profile: str | None = None
    tf_complete_mirror: CompleteMirrorLayout | None = None


@dataclass(frozen=True)
class CompleteMirrorLayout:
    """Host-generated full native resource mirror staged beside pal_tf.pak."""

    archives: tuple[str, ...]
    target_filename: str
    index_strategy: str


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


def build_font10_archive_chunk(
    data_dir: Path,
    release_archive: Path,
) -> tuple[Chunk, dict[str, object]]:
    """Build optional FONT chunk 1 from the pinned Fusion Pixel release.

    This path is deliberately opt-in.  The release archive is verified by
    exact size and SHA-256, the selected BDF is verified independently, and
    every PAL/UI corpus codepoint must be present before a payload is emitted.
    """
    tools_dir = str(Path(__file__).resolve().parent)
    if tools_dir not in sys.path:
        sys.path.insert(0, tools_dir)
    from pal_ui_layout import font as font10_tool

    release_archive = release_archive.resolve()
    release_lock = font10_tool.load_release_lock()
    font = font10_tool.extract_locked_bdf(release_archive)
    pal_codepoints = font10_tool.collect_pal_corpus_characters(data_dir)
    codepoints = font10_tool.collect_pal_corpus_characters(
        data_dir,
        extra_texts=FONT10_UI_LABELS.values(),
    )
    payload = font10_tool.build_font10(font, codepoints)
    parsed = font10_tool.parse_font10(payload)
    if len(parsed.glyphs) != len(codepoints):
        raise AssertionError("FONT10 glyph count changed during encoding")

    codepoint_bytes = b"".join(
        struct.pack("<H", codepoint) for codepoint in codepoints
    )
    advances = [glyph.advance for glyph in parsed.glyphs]
    if (
        not advances
        or min(advances) == 0
        or max(advances) > font10_tool.FONT10_CELL_WIDTH
    ):
        raise ValueError("FONT10 glyph advance is outside its fixed cell")
    summary: dict[str, object] = {
        "schema": "sdlpal-embedded-font10",
        "version": 1,
        "upstream": release_lock["upstream"],
        "release": font10_tool.RELEASE_VERSION,
        "license": release_lock["license"],
        "archive": {
            "path": str(release_archive),
            "filename": font10_tool.RELEASE_ASSET,
            "bytes": release_archive.stat().st_size,
            "sha256": hash_file(release_archive),
        },
        "bdf": {
            "member": font10_tool.BDF_MEMBER,
            "bytes": font10_tool.BDF_MEMBER_BYTES,
            "sha256": font10_tool.BDF_MEMBER_SHA256,
        },
        "corpus": {
            "pal_codepoint_count": len(pal_codepoints),
            "ui_added_codepoint_count": len(set(codepoints) - set(pal_codepoints)),
            "codepoint_count": len(codepoints),
            "codepoints_sha256": hashlib.sha256(codepoint_bytes).hexdigest(),
            "ui_labels": dict(sorted(FONT10_UI_LABELS.items())),
        },
        "pack_chunk": {
            "archive": "FONT",
            "chunk_id": 1,
            "format": "FONT10",
            "format_id": FORMAT_FONT10,
        },
        "font10": {
            "bytes": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
            "glyph_count": len(parsed.glyphs),
            "payload_crc32": parsed.payload_crc32,
            "metrics": {
                "cell_width": font10_tool.FONT10_CELL_WIDTH,
                "cell_height": font10_tool.FONT10_CELL_HEIGHT,
                "ascent": parsed.ascent,
                "descent": parsed.descent,
                "line_height": parsed.ascent + parsed.descent,
                "record_bytes": font10_tool.FONT10_RECORD_BYTES,
                "bitmap_bytes": font10_tool.FONT10_BITMAP_BYTES,
                "advance_min": min(advances, default=0),
                "advance_max": max(advances, default=0),
            },
        },
    }
    return Chunk(payload, FORMAT_FONT10), summary


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


def load_archive(
    data_dir: Path,
    name: str,
    font10_chunk: Chunk | None = None,
) -> list[Chunk]:
    if name == "TEXT":
        return [Chunk(encode_text_pack(data_dir), FORMAT_TEXT_UTF16)]
    if name == "FONT":
        chunks = [Chunk(encode_font_pack(data_dir), FORMAT_FONT_GLYPHS)]
        if font10_chunk is not None:
            if font10_chunk.fmt != FORMAT_FONT10 or not font10_chunk.present:
                raise ValueError("invalid optional FONT10 chunk")
            chunks.append(font10_chunk)
        return chunks
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


def source_paths_for_archive(data_dir: Path, name: str) -> list[Path]:
    if name == "TEXT":
        return [
            find_data_file(data_dir, "WORD.DAT"),
            find_data_file(data_dir, "M.MSG"),
            find_data_file(data_dir, "SSS.MKF"),
        ]
    if name == "FONT":
        return [
            find_data_file(data_dir, "WOR16.ASC"),
            find_data_file(data_dir, "WOR16.FON"),
        ]
    if name == "SFX":
        return [find_data_file(data_dir, "VOC.MKF")]
    return [find_data_file(data_dir, f"{name}.MKF")]


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fp:
        while True:
            data = fp.read(1024 * 1024)
            if not data:
                break
            digest.update(data)
    return digest.hexdigest()


def source_file_manifest(data_dir: Path, names: list[str]) -> list[dict[str, object]]:
    paths: dict[str, Path] = {}
    for name in names:
        for path in source_paths_for_archive(data_dir, name):
            rel = path.relative_to(data_dir).as_posix()
            paths[rel] = path

    result = []
    for rel, path in sorted(paths.items()):
        result.append(
            {
                "path": rel,
                "size": path.stat().st_size,
                "sha256": hash_file(path),
            }
        )
    return result


def summarize_archives(
    archives: dict[str, list[Chunk]],
    include_chunk_hashes: bool = False,
) -> list[dict[str, object]]:
    result = []
    for name in sorted(archives, key=lambda item: ARCHIVE_IDS[item]):
        chunks = archives[name]
        format_counts: dict[str, int] = {}
        chunk_entries = []
        for index, chunk in enumerate(chunks):
            format_name = FORMAT_NAMES.get(chunk.fmt, str(chunk.fmt))
            format_counts[format_name] = format_counts.get(format_name, 0) + 1
            chunk_entry = {
                "id": index,
                "format": format_name,
                "payload_bytes": len(chunk.payload),
            }
            if include_chunk_hashes:
                chunk_entry["sha256"] = hashlib.sha256(chunk.payload).hexdigest()
            chunk_entries.append(chunk_entry)
        result.append(
            {
                "name": name,
                "id": ARCHIVE_IDS[name],
                "chunk_count": len(chunks),
                "payload_bytes": sum(len(chunk.payload) for chunk in chunks),
                "max_payload_bytes": max((len(chunk.payload) for chunk in chunks), default=0),
                "format_counts": dict(sorted(format_counts.items())),
                "chunks": chunk_entries,
            }
        )
    return result


def ranges_from_ids(chunk_ids: list[int]) -> list[list[int]]:
    if not chunk_ids:
        return []

    result: list[list[int]] = []
    first = previous = chunk_ids[0]
    for chunk_id in chunk_ids[1:]:
        if chunk_id == previous + 1:
            previous = chunk_id
            continue
        result.append([first, previous])
        first = previous = chunk_id
    result.append([first, previous])
    return result


def summarize_selection(archives: dict[str, list[Chunk]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for name in sorted(archives, key=lambda item: ARCHIVE_IDS[item]):
        chunks = archives[name]
        present = [index for index, chunk in enumerate(chunks) if chunk.present]
        absent = [index for index, chunk in enumerate(chunks) if not chunk.present]
        result[name] = {
            "source_chunk_count": len(chunks),
            "present_chunk_count": len(present),
            "absent_chunk_count": len(absent),
            "present_chunk_ids": present,
            "absent_chunk_ranges": ranges_from_ids(absent),
            "absent_payload_bytes_are_zero": True,
        }
    return result


def summarize_pack(
    path: Path,
    names: list[str],
    pack: bytes,
    archives: dict[str, list[Chunk]],
    sparse: bool,
    include_chunk_hashes: bool = False,
) -> dict[str, object]:
    archive_entries = summarize_archives(archives, include_chunk_hashes)
    result = {
        "path": str(path.resolve()),
        "size": len(pack),
        "sha256": hashlib.sha256(pack).hexdigest(),
        "archives": names,
        "archive_count": len(archive_entries),
        "pack_set_id": u32(pack, PACK_SET_ID_OFFSET),
        "crc32": u32(pack, PACK_CRC32_OFFSET),
        "toc_bytes": u32(pack, 16),
        "chunk_count": sum(int(archive["chunk_count"]) for archive in archive_entries),
        "payload_bytes": sum(int(archive["payload_bytes"]) for archive in archive_entries),
        "max_payload_bytes": max((int(archive["max_payload_bytes"]) for archive in archive_entries), default=0),
        "archive_summaries": archive_entries,
    }
    if sparse:
        # Keep archive_summaries byte-for-byte compatible with manifest v1.
        # Selection metadata lives alongside it so older checkers can ignore it.
        result["chunk_selection"] = summarize_selection(archives)
    return result


def write_manifest(
    data_dir: Path,
    manifest_path: Path,
    layout_path: Path,
    layout_profile: str | None,
    layout_overrides: dict[str, bool],
    nor_summary: dict[str, object],
    tf_summary: dict[str, object],
    nor_names: list[str],
    tf_names: list[str],
    tf_complete_summary: dict[str, object] | None,
    tf_complete_layout: CompleteMirrorLayout | None,
    font10_summary: dict[str, object] | None = None,
) -> None:
    source_names = [*nor_names, *tf_names]
    if tf_complete_layout is not None:
        source_names.extend(tf_complete_layout.archives)

    manifest = {
        "schema": "sdlpal-embedded-pack-manifest",
        "version": 1,
        "data_dir": str(data_dir.resolve()),
        "runtime": {
            "heap_required": False,
            "runtime_decompression_required": False,
            "payloads_are_runtime_native": True,
        },
        "pack_layout": {
            "path": str(layout_path.resolve()),
            "sha256": hash_file(layout_path),
            "overrides": layout_overrides,
            "packs": {
                "nor": nor_names,
                "tf": tf_names,
            },
        },
        "source_files": source_file_manifest(data_dir, source_names),
        "packs": {
            "nor": nor_summary,
            "tf": tf_summary,
        },
    }
    if layout_profile is not None:
        manifest["pack_layout"]["profile"] = layout_profile
    if font10_summary is not None:
        manifest["font10"] = font10_summary
    if tf_complete_layout is not None and tf_complete_summary is not None:
        manifest["pack_layout"]["tf_complete_mirror"] = {
            "archives": list(tf_complete_layout.archives),
            "target_filename": tf_complete_layout.target_filename,
            "runtime_active": False,
            "all_chunks": True,
            "allow_overlap": True,
            "index_strategy": tf_complete_layout.index_strategy,
        }
        manifest["packs"]["tf_complete"] = tf_complete_summary
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(f"{manifest_path}: manifest source_files={len(manifest['source_files'])}")


def align4(value: int) -> int:
    return (value + 3) & ~3


def build_pack(
    archives: dict[str, list[Chunk]], pack_set_id: int = 0
) -> bytes:
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
        pack_set_id,
        cursor,
        0,
    )
    pack = bytearray(header + archive_entries + chunk_entries + payload)
    struct.pack_into("<I", pack, PACK_CRC32_OFFSET, zlib.crc32(pack) & 0xFFFFFFFF)
    return bytes(pack)


def checked_range(offset: int, size: int, total: int) -> bool:
    return 0 <= offset <= total and 0 <= size <= total - offset


def verify_pack(pack: bytes) -> None:
    if len(pack) < HEADER_SIZE:
        raise ValueError("short pack header")
    magic, version, header_size, archive_count, _reserved0 = struct.unpack_from("<IHHHH", pack, 0)
    archive_table_offset = u32(pack, 12)
    pack_size = u32(pack, 24)
    declared_crc = u32(pack, PACK_CRC32_OFFSET)

    if magic != MAGIC:
        raise ValueError("bad pack magic")
    if version != VERSION:
        raise ValueError(f"bad pack version: {version}")
    if header_size != HEADER_SIZE:
        raise ValueError(f"bad pack header size: {header_size}")
    if pack_size != len(pack):
        raise ValueError(f"pack size field is {pack_size}, actual {len(pack)}")
    crc_image = bytearray(pack)
    struct.pack_into("<I", crc_image, PACK_CRC32_OFFSET, 0)
    actual_crc = zlib.crc32(crc_image) & 0xFFFFFFFF
    if declared_crc == 0 or declared_crc != actual_crc:
        raise ValueError(
            f"pack CRC32 is {declared_crc:#010x}, expected {actual_crc:#010x}"
        )
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


def validate_names(items: list[object], label: str) -> list[str]:
    names: list[str] = []
    seen: set[str] = set()
    for item in items:
        if not isinstance(item, str):
            raise SystemExit(f"{label} contains non-string archive name: {item!r}")
        name = item.strip().upper()
        if not name:
            continue
        if name in seen:
            raise SystemExit(f"{label} contains duplicate archive name: {name}")
        names.append(name)
        seen.add(name)

    unknown = [name for name in names if name not in ARCHIVE_IDS]
    if unknown:
        raise SystemExit(f"unknown archive names: {', '.join(unknown)}")
    return names


def parse_names(raw: str | None, default: list[str]) -> list[str]:
    if raw is None:
        return list(default)
    return validate_names(raw.split(","), "archive list")


def parse_nonnegative_int(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise SystemExit(f"{label} must be a non-negative integer")
    return value


def parse_chunk_rule(value: object, label: str) -> ChunkRule:
    if not isinstance(value, dict):
        raise SystemExit(f"{label} must be an object")

    all_chunks = value.get("all", False)
    if not isinstance(all_chunks, bool):
        raise SystemExit(f"{label}.all must be true or false")

    raw_chunks = value.get("chunks", [])
    if not isinstance(raw_chunks, list):
        raise SystemExit(f"{label}.chunks must be an array")
    chunk_ids = frozenset(parse_nonnegative_int(item, f"{label}.chunks item") for item in raw_chunks)
    if len(chunk_ids) != len(raw_chunks):
        raise SystemExit(f"{label}.chunks contains duplicate chunk IDs")

    raw_ranges = value.get("ranges", [])
    if not isinstance(raw_ranges, list):
        raise SystemExit(f"{label}.ranges must be an array")
    ranges: list[tuple[int, int]] = []
    for index, item in enumerate(raw_ranges):
        if not isinstance(item, list) or len(item) != 2:
            raise SystemExit(f"{label}.ranges[{index}] must be [first, last]")
        first = parse_nonnegative_int(item[0], f"{label}.ranges[{index}][0]")
        last = parse_nonnegative_int(item[1], f"{label}.ranges[{index}][1]")
        if first > last:
            raise SystemExit(f"{label}.ranges[{index}] has first > last")
        ranges.append((first, last))

    if all_chunks and (chunk_ids or ranges):
        raise SystemExit(f"{label} cannot combine all=true with chunks/ranges")
    if not all_chunks and not chunk_ids and not ranges:
        raise SystemExit(f"{label} must select all=true, chunks, or ranges")

    raw_transforms = value.get("transforms", {})
    if not isinstance(raw_transforms, dict):
        raise SystemExit(f"{label}.transforms must be an object")
    prefix_bytes: list[tuple[int, int]] = []
    for raw_chunk_id, raw_transform in raw_transforms.items():
        try:
            chunk_id = int(raw_chunk_id, 10)
        except (TypeError, ValueError):
            raise SystemExit(f"{label}.transforms has invalid chunk ID: {raw_chunk_id!r}") from None
        if str(chunk_id) != raw_chunk_id or chunk_id < 0:
            raise SystemExit(f"{label}.transforms has invalid chunk ID: {raw_chunk_id!r}")
        if not isinstance(raw_transform, dict) or set(raw_transform) != {"prefix_bytes"}:
            raise SystemExit(f"{label}.transforms[{raw_chunk_id}] only supports prefix_bytes")
        prefix_bytes.append(
            (
                chunk_id,
                parse_nonnegative_int(
                    raw_transform["prefix_bytes"],
                    f"{label}.transforms[{raw_chunk_id}].prefix_bytes",
                ),
            )
        )

    return ChunkRule(all_chunks, chunk_ids, tuple(ranges), tuple(sorted(prefix_bytes)))


def parse_chunk_rules(
    data: dict[str, object],
    pack_names: dict[str, list[str]],
) -> dict[str, dict[str, ChunkRule]]:
    raw_selection = data.get("chunk_selection")
    if not isinstance(raw_selection, dict):
        raise SystemExit("pack layout v2 must define a chunk_selection object")

    result: dict[str, dict[str, ChunkRule]] = {"nor": {}, "tf": {}}
    for label in ("nor", "tf"):
        raw_pack_rules = raw_selection.get(label)
        if not isinstance(raw_pack_rules, dict):
            raise SystemExit(f"pack layout v2 must define chunk_selection.{label}")

        normalized: dict[str, object] = {}
        for raw_name, raw_rule in raw_pack_rules.items():
            if not isinstance(raw_name, str):
                raise SystemExit(f"chunk_selection.{label} contains a non-string archive name")
            name = raw_name.strip().upper()
            if name in normalized:
                raise SystemExit(f"chunk_selection.{label} contains duplicate archive name: {name}")
            normalized[name] = raw_rule

        missing = sorted(set(pack_names[label]) - set(normalized))
        extra = sorted(set(normalized) - set(pack_names[label]))
        if missing:
            raise SystemExit(
                f"chunk_selection.{label} has no rule for: {', '.join(missing)}"
            )
        if extra:
            raise SystemExit(
                f"chunk_selection.{label} has rules for archives not in packs.{label}: "
                f"{', '.join(extra)}"
            )
        for name in pack_names[label]:
            result[label][name] = parse_chunk_rule(
                normalized[name],
                f"chunk_selection.{label}.{name}",
            )
    return result


def parse_pack_limits(data: dict[str, object]) -> dict[str, int | None]:
    raw_limits = data.get("pack_limits", {})
    if not isinstance(raw_limits, dict):
        raise SystemExit("pack_limits must be an object")

    result: dict[str, int | None] = {"nor": None, "tf": None}
    for label, value in raw_limits.items():
        if label not in result:
            raise SystemExit(f"pack_limits has unknown pack: {label}")
        if not isinstance(value, dict) or set(value) != {"max_bytes"}:
            raise SystemExit(f"pack_limits.{label} must contain only max_bytes")
        result[label] = parse_nonnegative_int(value["max_bytes"], f"pack_limits.{label}.max_bytes")
    return result


def apply_pack_profile(
    data: dict[str, object],
    profile: str | None,
    pack_names: dict[str, list[str]],
) -> tuple[dict[str, list[str]], dict[str, object]]:
    """Apply an additive sparse-pack profile without changing the base layout.

    Profiles are deliberately additive: the base Cardputer extreme layout
    remains the no-audio contract, while an explicitly selected profile can
    add an archive and its sparse chunk rule.  This prevents an audio-capable
    pack build from silently changing the established no-audio artifacts.
    """
    if profile is None:
        return pack_names, data

    raw_profiles = data.get("profiles")
    if not isinstance(raw_profiles, dict):
        raise SystemExit(f"pack layout has no profiles object: {profile}")
    raw_profile = raw_profiles.get(profile)
    if not isinstance(raw_profile, dict):
        raise SystemExit(f"pack layout has no profile: {profile}")
    raw_additions = raw_profile.get("pack_additions")
    if not isinstance(raw_additions, dict):
        raise SystemExit(f"pack profile {profile} has no pack_additions object")

    result_names = {label: list(names) for label, names in pack_names.items()}
    raw_selection = data.get("chunk_selection")
    if not isinstance(raw_selection, dict):
        raise SystemExit("pack layout profile requires a chunk_selection object")
    merged_selection: dict[str, dict[str, object]] = {}

    for label in ("nor", "tf"):
        raw_base_rules = raw_selection.get(label)
        if not isinstance(raw_base_rules, dict):
            raise SystemExit(f"pack layout must define chunk_selection.{label}")
        merged_rules = dict(raw_base_rules)
        raw_pack_additions = raw_additions.get(label, {})
        if not isinstance(raw_pack_additions, dict):
            raise SystemExit(
                f"pack profile {profile} pack_additions.{label} must be an object"
            )
        for raw_name, raw_rule in raw_pack_additions.items():
            if not isinstance(raw_name, str):
                raise SystemExit(
                    f"pack profile {profile} pack_additions.{label} "
                    "contains a non-string archive name"
                )
            name = raw_name.strip().upper()
            if name not in ARCHIVE_IDS:
                raise SystemExit(
                    f"pack profile {profile} adds unknown archive: {name}"
                )
            if name in result_names[label] or name in merged_rules:
                raise SystemExit(
                    f"pack profile {profile} adds duplicate {label} archive: {name}"
                )
            result_names[label].append(name)
            merged_rules[name] = raw_rule
        merged_selection[label] = merged_rules

    merged_data = dict(data)
    merged_data["chunk_selection"] = merged_selection
    return result_names, merged_data


def parse_complete_tf_mirror(
    data: dict[str, object],
) -> CompleteMirrorLayout | None:
    raw = data.get("tf_complete_mirror")
    if raw is None:
        return None
    if not isinstance(raw, dict):
        raise SystemExit("tf_complete_mirror must be an object")

    required_keys = {
        "archives",
        "target_filename",
        "runtime_active",
        "all_chunks",
        "allow_overlap",
        "index_strategy",
    }
    if set(raw) != required_keys:
        missing = sorted(required_keys - set(raw))
        extra = sorted(set(raw) - required_keys)
        detail = []
        if missing:
            detail.append(f"missing {','.join(missing)}")
        if extra:
            detail.append(f"unknown {','.join(extra)}")
        raise SystemExit(
            "tf_complete_mirror has invalid fields: " + "; ".join(detail)
        )

    archives = raw["archives"]
    if not isinstance(archives, list):
        raise SystemExit("tf_complete_mirror.archives must be an array")
    names = validate_names(archives, "tf_complete_mirror.archives")
    if not names:
        raise SystemExit("tf_complete_mirror.archives must not be empty")
    if raw["runtime_active"] is not False:
        raise SystemExit("tf_complete_mirror.runtime_active must be false")
    if raw["all_chunks"] is not True:
        raise SystemExit("tf_complete_mirror.all_chunks must be true")
    if raw["allow_overlap"] is not True:
        raise SystemExit("tf_complete_mirror.allow_overlap must be true")

    target_filename = raw["target_filename"]
    if not isinstance(target_filename, str):
        raise SystemExit("tf_complete_mirror.target_filename must be a string")
    target_path = Path(target_filename)
    if (
        "/" in target_filename
        or "\\" in target_filename
        or target_path.name != target_filename
        or len(target_path.stem) > 8
        or target_path.suffix.lower() != ".pak"
    ):
        raise SystemExit(
            "tf_complete_mirror.target_filename must be a short 8.3 .pak name"
        )

    index_strategy = raw["index_strategy"]
    if (
        not isinstance(index_strategy, str)
        or index_strategy != "not-indexed-by-this-profile"
    ):
        raise SystemExit(
            "tf_complete_mirror.index_strategy must be "
            "not-indexed-by-this-profile"
        )
    return CompleteMirrorLayout(
        tuple(names),
        target_filename,
        index_strategy,
    )


def load_pack_layout(path: Path, profile: str | None = None) -> PackLayout:
    data = json.loads(path.read_text(errors="replace"))
    version = data.get("version")
    if data.get("schema") != "sdlpal-embedded-pack-layout" or version not in (1, 2):
        raise SystemExit(f"unknown pack layout schema/version: {path}")
    if profile is not None and version != 2:
        raise SystemExit("pack profiles require a version-2 sparse layout")
    packs = data.get("packs")
    if not isinstance(packs, dict):
        raise SystemExit(f"pack layout has no packs object: {path}")
    nor = packs.get("nor")
    tf = packs.get("tf")
    if not isinstance(nor, list) or not isinstance(tf, list):
        raise SystemExit(f"pack layout must define packs.nor and packs.tf arrays: {path}")
    pack_names = {
        "nor": validate_names(nor, "layout packs.nor"),
        "tf": validate_names(tf, "layout packs.tf"),
    }
    pack_names, effective_data = apply_pack_profile(data, profile, pack_names)
    if version == 1:
        return PackLayout(
            1,
            pack_names,
            {"nor": {}, "tf": {}},
            {"nor": None, "tf": None},
            profile,
            parse_complete_tf_mirror(data),
        )
    return PackLayout(
        2,
        pack_names,
        parse_chunk_rules(effective_data, pack_names),
        parse_pack_limits(data),
        profile,
        parse_complete_tf_mirror(data),
    )


def selected_chunk_ids(rule: ChunkRule, chunk_count: int, label: str) -> frozenset[int]:
    if rule.all_chunks:
        selected = set(range(chunk_count))
    else:
        selected = set(rule.chunk_ids)
        for first, last in rule.ranges:
            selected.update(range(first, last + 1))

    out_of_range = sorted(chunk_id for chunk_id in selected if chunk_id >= chunk_count)
    if out_of_range:
        raise SystemExit(
            f"{label} selects out-of-range chunks (source count {chunk_count}): "
            f"{', '.join(str(item) for item in out_of_range)}"
        )
    return frozenset(selected)


def apply_chunk_rule(chunks: list[Chunk], rule: ChunkRule, label: str) -> list[Chunk]:
    selected = selected_chunk_ids(rule, len(chunks), label)
    transforms = dict(rule.prefix_bytes)
    unselected_transforms = sorted(set(transforms) - set(selected))
    if unselected_transforms:
        raise SystemExit(
            f"{label} transforms unselected chunks: "
            f"{', '.join(str(item) for item in unselected_transforms)}"
        )

    result: list[Chunk] = []
    for chunk_id, chunk in enumerate(chunks):
        if chunk_id not in selected:
            result.append(Chunk(b"", chunk.fmt, False))
            continue
        payload = chunk.payload
        if chunk_id in transforms:
            prefix_size = transforms[chunk_id]
            if prefix_size > len(payload):
                raise SystemExit(
                    f"{label} chunk {chunk_id} prefix_bytes={prefix_size} "
                    f"exceeds decoded payload {len(payload)}"
                )
            payload = payload[:prefix_size]
        result.append(Chunk(payload, chunk.fmt, True))
    return result


def load_selected_archives(
    data_dir: Path,
    names: list[str],
    rules: dict[str, ChunkRule] | None,
    cache: dict[str, list[Chunk]] | None = None,
    font10_chunk: Chunk | None = None,
) -> dict[str, list[Chunk]]:
    archives: dict[str, list[Chunk]] = {}
    for name in names:
        if cache is not None and name in cache:
            chunks = cache[name]
        else:
            chunks = load_archive(data_dir, name, font10_chunk)
            if cache is not None:
                cache[name] = chunks
        if rules is not None:
            chunks = apply_chunk_rule(chunks, rules[name], name)
        archives[name] = chunks
    return archives


def validate_disjoint_pack_chunks(
    nor_archives: dict[str, list[Chunk]],
    tf_archives: dict[str, list[Chunk]],
) -> None:
    for name in sorted(set(nor_archives) & set(tf_archives)):
        nor_chunks = nor_archives[name]
        tf_chunks = tf_archives[name]
        if len(nor_chunks) != len(tf_chunks):
            raise SystemExit(f"split archive has inconsistent chunk count: {name}")
        overlap = [
            index
            for index, (nor_chunk, tf_chunk) in enumerate(zip(nor_chunks, tf_chunks))
            if nor_chunk.present and tf_chunk.present
        ]
        if overlap:
            raise SystemExit(
                f"archive chunks listed in both packs: {name} "
                f"{','.join(str(index) for index in overlap)}"
            )


def write_pack(
   out_path: Path,
   names: list[str],
   archives: dict[str, list[Chunk]],
   sparse: bool,
   max_bytes: int | None,
   pack_set_id: int,
   include_chunk_hashes: bool = False,
) -> dict[str, object]:
    pack = build_pack(archives, pack_set_id)
    verify_pack(pack)
    if max_bytes is not None and len(pack) > max_bytes:
        raise SystemExit(f"{out_path}: {len(pack)} bytes exceeds layout limit {max_bytes}")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(pack)
    print(f"{out_path}: {len(pack)} bytes, archives={','.join(names)}")
    return summarize_pack(
        out_path,
        names,
        pack,
        archives,
        sparse,
        include_chunk_hashes,
    )


def canonical_pack_identity_image(
    archives: dict[str, list[Chunk]],
) -> bytes:
    """Build bytes suitable for identifying exact pack contents.

    The two self-referential header words are zeroed, so the resulting digest
    changes with any TOC, format, selection, or payload change but can itself
    be stored in both final pack headers.
    """
    image = bytearray(build_pack(archives, 0))
    struct.pack_into("<I", image, PACK_SET_ID_OFFSET, 0)
    struct.pack_into("<I", image, PACK_CRC32_OFFSET, 0)
    return bytes(image)


def compute_portable_pack_set_id(
    complete_archives: dict[str, list[Chunk]],
) -> int:
    """Identify canonical data independently of target cache placement."""

    digest = hashlib.sha256()
    digest.update(b"sdlpal-portable-pack-set-v1\0")
    digest.update(canonical_pack_identity_image(complete_archives))
    value = int.from_bytes(digest.digest()[:4], "little")
    return value if value != 0 else 1


def compute_pack_set_id(
    nor_archives: dict[str, list[Chunk]],
    tf_archives: dict[str, list[Chunk]],
    tf_complete_archives: dict[str, list[Chunk]] | None = None,
) -> int:
    if tf_complete_archives is not None:
        return compute_portable_pack_set_id(tf_complete_archives)

    digest = hashlib.sha256()
    digest.update(b"sdlpal-pack-set-v1\0")
    digest.update(b"NOR\0")
    digest.update(canonical_pack_identity_image(nor_archives))
    digest.update(b"TF\0")
    digest.update(canonical_pack_identity_image(tf_archives))
    value = int.from_bytes(digest.digest()[:4], "little")
    return value if value != 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data_dir", type=Path)
    parser.add_argument("--out-nor", type=Path, required=True)
    parser.add_argument("--out-tf", type=Path, required=True)
    parser.add_argument(
        "--out-tf-complete",
        type=Path,
        help="write the layout-declared complete decoded TF mirror",
    )
    parser.add_argument("--nor", help="comma-separated archives for NOR pack")
    parser.add_argument("--tf", help="comma-separated archives for TF pack")
    parser.add_argument("--layout", type=Path, default=DEFAULT_LAYOUT_PATH, help="JSON archive-to-pack layout policy")
    parser.add_argument(
        "--profile",
        help="explicit additive profile declared by a version-2 pack layout",
    )
    parser.add_argument(
        "--font10-archive",
        type=Path,
        help=(
            "verified Fusion Pixel Font 10px monospaced BDF release zip; "
            "adds corpus-subsetted FONT chunk 1"
        ),
    )
    parser.add_argument("--manifest", type=Path, help="write a source-hash and decoded-size manifest")
    args = parser.parse_args()

    data_dir = args.data_dir
    font10_chunk = None
    font10_summary = None
    if args.font10_archive is not None:
        font10_chunk, font10_summary = build_font10_archive_chunk(
            data_dir,
            args.font10_archive,
        )
    layout = load_pack_layout(args.layout, args.profile)
    nor_names = parse_names(args.nor, layout.pack_names["nor"])
    tf_names = parse_names(args.tf, layout.pack_names["tf"])
    if layout.tf_complete_mirror is not None and args.out_tf_complete is None:
        raise SystemExit(
            "layout requires --out-tf-complete for its complete TF mirror"
        )
    if layout.tf_complete_mirror is None and args.out_tf_complete is not None:
        raise SystemExit(
            "--out-tf-complete requires a layout tf_complete_mirror policy"
        )

    nor_uses_layout_rules = layout.version == 2 and args.nor is None
    tf_uses_layout_rules = layout.version == 2 and args.tf is None
    nor_rules = layout.chunk_rules["nor"] if nor_uses_layout_rules else None
    tf_rules = layout.chunk_rules["tf"] if tf_uses_layout_rules else None

    if layout.version == 1 or args.nor is not None or args.tf is not None:
        overlap = sorted(set(nor_names) & set(tf_names))
        if overlap:
            raise SystemExit(f"archives listed in both packs: {', '.join(overlap)}")

    archive_cache: dict[str, list[Chunk]] = {}
    nor_archives = load_selected_archives(
        data_dir,
        nor_names,
        nor_rules,
        archive_cache,
        font10_chunk,
    )
    tf_archives = load_selected_archives(
        data_dir,
        tf_names,
        tf_rules,
        archive_cache,
        font10_chunk,
    )
    tf_complete_archives = None
    if layout.tf_complete_mirror is not None:
        tf_complete_archives = load_selected_archives(
            data_dir,
            list(layout.tf_complete_mirror.archives),
            None,
            archive_cache,
            font10_chunk,
        )
    if font10_chunk is not None:
        nor_font = nor_archives.get("FONT")
        if (
            nor_font is None
            or len(nor_font) <= 1
            or not nor_font[1].present
        ):
            raise SystemExit(
                "--font10-archive requires FONT chunk 1 in the NOR pack"
            )
    validate_disjoint_pack_chunks(nor_archives, tf_archives)
    pack_set_id = compute_pack_set_id(
        nor_archives,
        tf_archives,
        tf_complete_archives,
    )

    nor_summary = write_pack(
        args.out_nor,
        nor_names,
        nor_archives,
        nor_uses_layout_rules,
        layout.max_bytes["nor"] if nor_uses_layout_rules else None,
        pack_set_id,
    )
    tf_summary = write_pack(
        args.out_tf,
        tf_names,
        tf_archives,
        tf_uses_layout_rules,
        layout.max_bytes["tf"] if tf_uses_layout_rules else None,
        pack_set_id,
    )
    tf_complete_summary = None
    if (
        layout.tf_complete_mirror is not None
        and tf_complete_archives is not None
        and args.out_tf_complete is not None
    ):
        tf_complete_summary = write_pack(
            args.out_tf_complete,
            list(layout.tf_complete_mirror.archives),
            tf_complete_archives,
            False,
            None,
            pack_set_id,
            True,
        )
    if args.manifest:
        write_manifest(
            data_dir,
            args.manifest,
            args.layout,
            layout.profile,
            {"nor": args.nor is not None, "tf": args.tf is not None},
            nor_summary,
            tf_summary,
            nor_names,
            tf_names,
            tf_complete_summary,
            layout.tf_complete_mirror,
            font10_summary,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
