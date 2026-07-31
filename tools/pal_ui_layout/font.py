#!/usr/bin/env python3
"""Build a compact, corpus-subsetted 10px bitmap font for PAL UI tools.

The upstream font archive is deliberately not vendored.  Callers download the
release out of band, then :func:`extract_locked_bdf` verifies the pinned archive
before parsing it.  Normal tests use a small synthetic BDF and need no network.
"""

from __future__ import annotations

import hashlib
import io
import json
import struct
import unicodedata
import zlib
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


RELEASE_VERSION = "2026.07.20"
RELEASE_ASSET = (
    "fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip"
)
RELEASE_URL = (
    "https://github.com/TakWolf/fusion-pixel-font/releases/download/"
    f"{RELEASE_VERSION}/{RELEASE_ASSET}"
)
RELEASE_SHA256 = (
    "2e695c27627bf09683df2afe69b086fa3cd3e52795bce39fded9cc509188b5fe"
)
RELEASE_BYTES = 18_220_904
BDF_MEMBER = "fusion-pixel-10px-monospaced-zh_hant.bdf"
BDF_MEMBER_SHA256 = (
    "39ac134948ca94cf5f429a543914fe6a5e85a1c0bc0bde3c304f2d53f35bfc62"
)
BDF_MEMBER_BYTES = 3_643_786
LOCK_PATH = Path(__file__).with_name("fusion_pixel_font.lock.json")

FONT10_MAGIC = b"FONT10\0\0"
FONT10_VERSION = 1
FONT10_HEADER_BYTES = 32
FONT10_CELL_WIDTH = 10
FONT10_CELL_HEIGHT = 10
FONT10_BITMAP_BYTES = (
    FONT10_CELL_WIDTH * FONT10_CELL_HEIGHT + 7
) // 8
FONT10_RECORD_BYTES = 2 + 1 + FONT10_BITMAP_BYTES
FONT10_HEADER = struct.Struct("<8sHHIHBBBBHII")
FONT10_RECORD = struct.Struct(
    f"<HB{FONT10_BITMAP_BYTES}s"
)


class BdfError(ValueError):
    """The BDF source is malformed or unsuitable for FONT10."""


class FontCoverageError(ValueError):
    """The chosen BDF does not cover every requested corpus character."""

    def __init__(self, missing: Iterable[int]) -> None:
        self.missing = tuple(sorted(set(missing)))
        preview = ", ".join(
            _codepoint_label(codepoint) for codepoint in self.missing[:16]
        )
        suffix = "" if len(self.missing) <= 16 else (
            f", ... ({len(self.missing)} missing)"
        )
        super().__init__(f"font coverage missing: {preview}{suffix}")


@dataclass(frozen=True)
class BdfBoundingBox:
    width: int
    height: int
    x_offset: int
    y_offset: int


@dataclass(frozen=True)
class BdfGlyph:
    name: str
    encoding: int
    dwidth_x: int
    dwidth_y: int
    bbox: BdfBoundingBox
    rows: tuple[int, ...]

    def pixel(self, x: int, y: int) -> bool:
        """Return a BBX-local pixel, where y=0 is the top bitmap row."""
        if not (0 <= x < self.bbox.width and 0 <= y < self.bbox.height):
            return False
        return bool(self.rows[y] & (1 << (self.bbox.width - 1 - x)))


@dataclass(frozen=True)
class BdfFont:
    version: str
    name: str
    size: tuple[int, int, int]
    bbox: BdfBoundingBox
    ascent: int
    descent: int
    properties: dict[str, str]
    glyphs: dict[int, BdfGlyph]


@dataclass(frozen=True)
class Font10Glyph:
    codepoint: int
    advance: int
    bitmap: bytes

    def pixel(self, x: int, y: int) -> bool:
        if not (
            0 <= x < FONT10_CELL_WIDTH
            and 0 <= y < FONT10_CELL_HEIGHT
        ):
            return False
        bit = y * FONT10_CELL_WIDTH + x
        return bool(self.bitmap[bit >> 3] & (0x80 >> (bit & 7)))


@dataclass(frozen=True)
class Font10:
    ascent: int
    descent: int
    glyphs: tuple[Font10Glyph, ...]
    payload_crc32: int

    def by_codepoint(self) -> dict[int, Font10Glyph]:
        return {glyph.codepoint: glyph for glyph in self.glyphs}


def _codepoint_label(codepoint: int) -> str:
    if 0 <= codepoint <= 0x10FFFF:
        char = chr(codepoint)
        if char.isprintable() and not char.isspace():
            return f"U+{codepoint:04X}({char})"
    return f"U+{codepoint:04X}"


def _integers(
    value: str, count: int, *, context: str
) -> tuple[int, ...]:
    fields = value.split()
    if len(fields) != count:
        raise BdfError(
            f"{context}: expected {count} integers, got {value!r}"
        )
    try:
        return tuple(int(field, 10) for field in fields)
    except ValueError as exc:
        raise BdfError(f"{context}: invalid integer in {value!r}") from exc


def _keyword(line: str) -> tuple[str, str]:
    key, separator, value = line.partition(" ")
    return key, value.strip() if separator else ""


def _parse_glyph(
    lines: list[str], start: int
) -> tuple[BdfGlyph | None, int]:
    _, name = _keyword(lines[start])
    if not name:
        raise BdfError(f"line {start + 1}: empty STARTCHAR name")

    encoding: int | None = None
    dwidth: tuple[int, int] | None = None
    bbox: BdfBoundingBox | None = None
    bitmap_lines: list[tuple[int, str]] | None = None
    index = start + 1
    while index < len(lines):
        line = lines[index].strip()
        key, value = _keyword(line)
        if key == "ENCODING":
            values = value.split()
            if len(values) not in (1, 2):
                raise BdfError(
                    f"line {index + 1}: malformed ENCODING"
                )
            try:
                primary = int(values[0], 10)
                alternate = (
                    int(values[1], 10) if len(values) == 2 else -1
                )
            except ValueError as exc:
                raise BdfError(
                    f"line {index + 1}: malformed ENCODING"
                ) from exc
            encoding = alternate if primary == -1 else primary
        elif key == "DWIDTH":
            dwidth = _integers(
                value, 2, context=f"line {index + 1} DWIDTH"
            )
        elif key == "BBX":
            values = _integers(
                value, 4, context=f"line {index + 1} BBX"
            )
            bbox = BdfBoundingBox(*values)
            if bbox.width < 0 or bbox.height < 0:
                raise BdfError(
                    f"line {index + 1}: negative glyph BBX"
                )
        elif key == "BITMAP":
            if bitmap_lines is not None:
                raise BdfError(
                    f"line {index + 1}: duplicate BITMAP"
                )
            bitmap_lines = []
        elif key == "ENDCHAR":
            break
        elif key == "STARTCHAR":
            raise BdfError(
                f"line {index + 1}: nested STARTCHAR"
            )
        elif bitmap_lines is not None:
            bitmap_lines.append((index + 1, line))
        index += 1
    else:
        raise BdfError(f"glyph {name!r}: missing ENDCHAR")

    if encoding is None or dwidth is None or bbox is None:
        raise BdfError(f"glyph {name!r}: missing ENCODING/DWIDTH/BBX")
    if bitmap_lines is None:
        raise BdfError(f"glyph {name!r}: missing BITMAP")
    if len(bitmap_lines) != bbox.height:
        raise BdfError(
            f"glyph {name!r}: bitmap has {len(bitmap_lines)} rows, "
            f"BBX says {bbox.height}"
        )

    row_bytes = (bbox.width + 7) // 8
    rows: list[int] = []
    for line_number, row_hex in bitmap_lines:
        if len(row_hex) != row_bytes * 2:
            raise BdfError(
                f"line {line_number}: bitmap row is {len(row_hex)} "
                f"hex digits, expected {row_bytes * 2}"
            )
        try:
            encoded_row = int(row_hex, 16) if row_hex else 0
        except ValueError as exc:
            raise BdfError(
                f"line {line_number}: non-hex bitmap row"
            ) from exc
        padding = row_bytes * 8 - bbox.width
        if padding and encoded_row & ((1 << padding) - 1):
            raise BdfError(
                f"line {line_number}: nonzero BDF row padding"
            )
        rows.append(encoded_row >> padding)

    glyph = BdfGlyph(
        name=name,
        encoding=encoding,
        dwidth_x=dwidth[0],
        dwidth_y=dwidth[1],
        bbox=bbox,
        rows=tuple(rows),
    )
    return (glyph if encoding >= 0 else None), index + 1


def parse_bdf(data: bytes | str) -> BdfFont:
    """Parse the BDF fields needed by the deterministic FONT10 builder."""
    if isinstance(data, bytes):
        try:
            text = data.decode("ascii", errors="strict")
        except UnicodeDecodeError as exc:
            raise BdfError("BDF is not ASCII") from exc
    elif isinstance(data, str):
        text = data
    else:
        raise TypeError("BDF input must be bytes or str")

    lines = text.splitlines()
    if not lines:
        raise BdfError("empty BDF")

    version = ""
    font_name = ""
    size: tuple[int, int, int] | None = None
    font_bbox: BdfBoundingBox | None = None
    properties: dict[str, str] = {}
    declared_chars: int | None = None
    glyphs: dict[int, BdfGlyph] = {}
    parsed_glyph_count = 0
    saw_endfont = False

    index = 0
    while index < len(lines):
        line = lines[index].strip()
        key, value = _keyword(line)
        if key == "STARTFONT":
            if version:
                raise BdfError(f"line {index + 1}: duplicate STARTFONT")
            version = value
        elif key == "FONT":
            font_name = value
        elif key == "SIZE":
            size = _integers(
                value, 3, context=f"line {index + 1} SIZE"
            )
        elif key == "FONTBOUNDINGBOX":
            values = _integers(
                value,
                4,
                context=f"line {index + 1} FONTBOUNDINGBOX",
            )
            font_bbox = BdfBoundingBox(*values)
            if font_bbox.width <= 0 or font_bbox.height <= 0:
                raise BdfError("FONTBOUNDINGBOX must be positive")
        elif key == "CHARS":
            (declared_chars,) = _integers(
                value, 1, context=f"line {index + 1} CHARS"
            )
            if declared_chars < 0:
                raise BdfError("negative CHARS count")
        elif key == "STARTCHAR":
            glyph, index = _parse_glyph(lines, index)
            parsed_glyph_count += 1
            if glyph is not None:
                if glyph.encoding > 0x10FFFF:
                    raise BdfError(
                        f"glyph {glyph.name!r}: encoding out of Unicode range"
                    )
                if glyph.encoding in glyphs:
                    raise BdfError(
                        f"duplicate encoding U+{glyph.encoding:04X}"
                    )
                glyphs[glyph.encoding] = glyph
            continue
        elif key == "ENDFONT":
            saw_endfont = True
        elif key and key not in {
            "STARTPROPERTIES",
            "ENDPROPERTIES",
            "COMMENT",
        }:
            # Property names needed below have integer values.  Keeping all
            # properties also lets callers audit the fixed upstream release.
            if key in {"FONT_ASCENT", "FONT_DESCENT"}:
                properties[key] = value
            elif (
                key.isupper()
                and version
                and declared_chars is None
                and key not in {
                    "SWIDTH",
                    "DWIDTH",
                    "BBX",
                    "BITMAP",
                    "ENDCHAR",
                }
            ):
                properties.setdefault(key, value)
        index += 1

    if not version or not saw_endfont:
        raise BdfError("missing STARTFONT or ENDFONT")
    if not font_name or size is None or font_bbox is None:
        raise BdfError("missing FONT/SIZE/FONTBOUNDINGBOX")
    if declared_chars is None:
        raise BdfError("missing CHARS")
    if declared_chars != parsed_glyph_count:
        raise BdfError(
            f"CHARS says {declared_chars}, parsed {parsed_glyph_count}"
        )
    try:
        ascent = int(properties["FONT_ASCENT"], 10)
        descent = int(properties["FONT_DESCENT"], 10)
    except (KeyError, ValueError) as exc:
        raise BdfError("missing or invalid FONT_ASCENT/FONT_DESCENT") from exc
    if ascent < 0 or descent < 0:
        raise BdfError("negative FONT_ASCENT/FONT_DESCENT")

    return BdfFont(
        version=version,
        name=font_name,
        size=size,
        bbox=font_bbox,
        ascent=ascent,
        descent=descent,
        properties=properties,
        glyphs=glyphs,
    )


def load_bdf(path: str | Path) -> BdfFont:
    return parse_bdf(Path(path).read_bytes())


def load_release_lock(path: str | Path = LOCK_PATH) -> dict[str, object]:
    """Load the human-readable lock while retaining constants as trust roots."""
    lock = json.loads(Path(path).read_text(encoding="utf-8"))
    expected = {
        ("schema",): "sdlpal-fusion-pixel-font-lock",
        ("version",): 1,
        ("release",): RELEASE_VERSION,
        ("asset", "filename"): RELEASE_ASSET,
        ("asset", "url"): RELEASE_URL,
        ("asset", "sha256"): RELEASE_SHA256,
        ("asset", "bytes"): RELEASE_BYTES,
        ("bdf", "member"): BDF_MEMBER,
        ("bdf", "sha256"): BDF_MEMBER_SHA256,
        ("bdf", "bytes"): BDF_MEMBER_BYTES,
    }
    for keys, value in expected.items():
        current: object = lock
        try:
            for key in keys:
                if not isinstance(current, dict):
                    raise KeyError(key)
                current = current[key]
        except KeyError as exc:
            raise ValueError(
                f"font lock is missing {'.'.join(keys)}"
            ) from exc
        if current != value:
            raise ValueError(
                f"font lock {'.'.join(keys)} is {current!r}, "
                f"expected {value!r}"
            )
    return lock


def _archive_bytes(archive: str | Path | bytes) -> bytes:
    if isinstance(archive, bytes):
        return archive
    if isinstance(archive, (str, Path)):
        return Path(archive).read_bytes()
    raise TypeError("archive must be bytes or a filesystem path")


def verify_release_archive(archive: str | Path | bytes) -> bytes:
    """Verify the pinned zip and return the pinned Traditional Chinese BDF."""
    image = _archive_bytes(archive)
    if len(image) != RELEASE_BYTES:
        raise ValueError(
            f"{RELEASE_ASSET} is {len(image)} bytes, expected {RELEASE_BYTES}"
        )
    digest = hashlib.sha256(image).hexdigest()
    if digest != RELEASE_SHA256:
        raise ValueError(
            f"{RELEASE_ASSET} SHA256 is {digest}, expected {RELEASE_SHA256}"
        )

    try:
        with zipfile.ZipFile(io.BytesIO(image), "r") as archive_file:
            names = archive_file.namelist()
            if len(names) != len(set(names)):
                raise ValueError("font archive contains duplicate members")
            if BDF_MEMBER not in names:
                raise ValueError(
                    f"font archive does not contain {BDF_MEMBER}"
                )
            info = archive_file.getinfo(BDF_MEMBER)
            if info.is_dir() or info.file_size != BDF_MEMBER_BYTES:
                raise ValueError(
                    f"{BDF_MEMBER} size is {info.file_size}, "
                    f"expected {BDF_MEMBER_BYTES}"
                )
            bdf = archive_file.read(info)
    except zipfile.BadZipFile as exc:
        raise ValueError("font release is not a valid zip archive") from exc

    bdf_digest = hashlib.sha256(bdf).hexdigest()
    if bdf_digest != BDF_MEMBER_SHA256:
        raise ValueError(
            f"{BDF_MEMBER} SHA256 is {bdf_digest}, "
            f"expected {BDF_MEMBER_SHA256}"
        )
    return bdf


def extract_locked_bdf(archive: str | Path | bytes) -> BdfFont:
    """Verify and parse the fixed zh_hant BDF from the official release."""
    load_release_lock()
    font = parse_bdf(verify_release_archive(archive))
    if (
        font.size[0] != FONT10_CELL_HEIGHT
        or font.bbox != BdfBoundingBox(10, 10, 0, -1)
        or font.ascent != 9
        or font.descent != 1
    ):
        raise BdfError("locked release has unexpected 10px metrics")
    return font


def _is_corpus_character(character: str) -> bool:
    return not unicodedata.category(character).startswith("C")


def collect_corpus_characters(
    texts: Iterable[str], *, extra_text: str = ""
) -> tuple[int, ...]:
    """Return sorted codepoints actually rendered by a text corpus."""
    codepoints: set[int] = set()
    for text in texts:
        if not isinstance(text, str):
            raise TypeError("corpus entries must be str")
        codepoints.update(
            ord(character)
            for character in text
            if _is_corpus_character(character)
        )
    if not isinstance(extra_text, str):
        raise TypeError("extra_text must be str")
    codepoints.update(
        ord(character)
        for character in extra_text
        if _is_corpus_character(character)
    )
    return tuple(sorted(codepoints))


def _find_pal_file(data_dir: Path, name: str) -> Path:
    for candidate in (name, name.lower()):
        path = data_dir / candidate
        if path.is_file():
            return path
    raise FileNotFoundError(data_dir / name)


def _read_mkf_chunk(path: Path, chunk_index: int) -> bytes:
    data = path.read_bytes()
    if len(data) < 4:
        raise ValueError(f"not an MKF file: {path}")
    first_offset = struct.unpack_from("<I", data, 0)[0]
    if (
        first_offset < 4
        or first_offset % 4
        or first_offset > len(data)
    ):
        raise ValueError(f"bad MKF offset table: {path}")
    chunk_count = (first_offset - 4) // 4
    if not 0 <= chunk_index < chunk_count:
        raise ValueError(
            f"{path} has {chunk_count} chunks, needs #{chunk_index}"
        )
    start, end = struct.unpack_from(
        "<II", data, chunk_index * 4
    )
    if start > end or end > len(data):
        raise ValueError(
            f"bad MKF chunk range {path} #{chunk_index}: {start}..{end}"
        )
    return data[start:end]


def collect_pal_corpus_characters(
    data_dir: str | Path,
    *,
    encoding: str = "cp950",
    extra_texts: Iterable[str] = (),
) -> tuple[int, ...]:
    """Collect rendered characters from real WORD.DAT and M.MSG entries.

    Message boundaries come from SSS.MKF chunk 3, matching the host pack
    builder.  Decoding is strict so a wrong dataset/codepage cannot silently
    produce a font with replacement glyphs.
    """
    root = Path(data_dir)
    word_data = _find_pal_file(root, "WORD.DAT").read_bytes()
    message_data = _find_pal_file(root, "M.MSG").read_bytes()
    offsets_data = _read_mkf_chunk(
        _find_pal_file(root, "SSS.MKF"), 3
    )
    if len(offsets_data) < 8 or len(offsets_data) % 4:
        raise ValueError("SSS.MKF message offset chunk is malformed")
    offsets = struct.unpack(
        f"<{len(offsets_data) // 4}I", offsets_data
    )

    texts: list[str] = []
    for offset in range(0, len(word_data), 10):
        raw = word_data[offset : offset + 10].rstrip(b" \0")
        if raw:
            texts.append(raw.decode(encoding, errors="strict"))
    for index, (start, end) in enumerate(zip(offsets, offsets[1:])):
        if start > end or end > len(message_data):
            raise ValueError(
                f"bad M.MSG offset range #{index}: {start}..{end}"
            )
        texts.append(
            message_data[start:end].decode(encoding, errors="strict")
        )
    texts.extend(extra_texts)
    return collect_corpus_characters(texts)


def missing_codepoints(
    font: BdfFont, codepoints: Iterable[int]
) -> tuple[int, ...]:
    return tuple(
        sorted(set(codepoints).difference(font.glyphs))
    )


def require_coverage(
    font: BdfFont, codepoints: Iterable[int]
) -> tuple[int, ...]:
    """Return a canonical subset or fail closed on every missing glyph."""
    requested = tuple(sorted(set(codepoints)))
    for codepoint in requested:
        if not 0 <= codepoint <= 0xFFFF:
            raise ValueError(
                f"FONT10 only supports BMP codepoints, got "
                f"U+{codepoint:04X}"
            )
        if 0xD800 <= codepoint <= 0xDFFF:
            raise ValueError(
                f"FONT10 cannot encode surrogate U+{codepoint:04X}"
            )
    missing = missing_codepoints(font, requested)
    if missing:
        raise FontCoverageError(missing)
    return requested


def _normalized_bitmap(font: BdfFont, glyph: BdfGlyph) -> bytes:
    if font.bbox.width != 10 or font.bbox.height != 10:
        raise BdfError(
            "FONT10 requires a 10x10 FONTBOUNDINGBOX"
        )
    cell_left = font.bbox.x_offset
    cell_top = (
        font.bbox.y_offset + font.bbox.height - 1
    )
    glyph_left = glyph.bbox.x_offset
    glyph_top = (
        glyph.bbox.y_offset + glyph.bbox.height - 1
    )
    destination_x = glyph_left - cell_left
    destination_y = cell_top - glyph_top
    bitmap = bytearray(FONT10_BITMAP_BYTES)
    for source_y in range(glyph.bbox.height):
        for source_x in range(glyph.bbox.width):
            if not glyph.pixel(source_x, source_y):
                continue
            x = destination_x + source_x
            y = destination_y + source_y
            # A few upstream BDF glyphs declare a taller BBX than the global
            # 10px cell, but their out-of-cell rows are blank.  Cropping those
            # declaration-only rows is lossless; a live pixel outside the cell
            # is an error rather than silent clipping.
            if not (
                0 <= x < FONT10_CELL_WIDTH
                and 0 <= y < FONT10_CELL_HEIGHT
            ):
                raise BdfError(
                    f"glyph {_codepoint_label(glyph.encoding)} has a "
                    "set pixel outside the 10x10 font cell"
                )
            bit = y * FONT10_CELL_WIDTH + x
            bitmap[bit >> 3] |= 0x80 >> (bit & 7)
    return bytes(bitmap)


def build_font10(
    font: BdfFont, codepoints: Iterable[int]
) -> bytes:
    """Encode sorted corpus glyphs as fixed 16-byte binary-search records."""
    requested = require_coverage(font, codepoints)
    if font.ascent > 255 or font.descent > 255:
        raise BdfError("FONT10 ascent/descent do not fit uint8")

    payload = bytearray()
    for codepoint in requested:
        glyph = font.glyphs[codepoint]
        if glyph.dwidth_y != 0:
            raise BdfError(
                f"glyph {_codepoint_label(codepoint)} has vertical DWIDTH"
            )
        if not 0 <= glyph.dwidth_x <= 255:
            raise BdfError(
                f"glyph {_codepoint_label(codepoint)} advance does not "
                "fit uint8"
            )
        payload += FONT10_RECORD.pack(
            codepoint,
            glyph.dwidth_x,
            _normalized_bitmap(font, glyph),
        )

    payload_crc32 = zlib.crc32(payload) & 0xFFFFFFFF
    total_bytes = FONT10_HEADER_BYTES + len(payload)
    header = FONT10_HEADER.pack(
        FONT10_MAGIC,
        FONT10_VERSION,
        FONT10_HEADER_BYTES,
        len(requested),
        FONT10_RECORD_BYTES,
        FONT10_CELL_WIDTH,
        FONT10_CELL_HEIGHT,
        font.ascent,
        font.descent,
        0,
        payload_crc32,
        total_bytes,
    )
    if len(header) != FONT10_HEADER_BYTES:
        raise AssertionError("FONT10 header ABI drift")
    return header + payload


def parse_font10(image: bytes) -> Font10:
    """Strictly validate a FONT10 image and expose its fixed records."""
    if len(image) < FONT10_HEADER_BYTES:
        raise ValueError("short FONT10 header")
    (
        magic,
        version,
        header_bytes,
        glyph_count,
        record_bytes,
        cell_width,
        cell_height,
        ascent,
        descent,
        reserved,
        declared_crc32,
        declared_bytes,
    ) = FONT10_HEADER.unpack_from(image)
    if magic != FONT10_MAGIC:
        raise ValueError("bad FONT10 magic")
    if version != FONT10_VERSION:
        raise ValueError(f"unsupported FONT10 version {version}")
    if header_bytes != FONT10_HEADER_BYTES:
        raise ValueError("bad FONT10 header size")
    if record_bytes != FONT10_RECORD_BYTES:
        raise ValueError("bad FONT10 record size")
    if (
        cell_width != FONT10_CELL_WIDTH
        or cell_height != FONT10_CELL_HEIGHT
    ):
        raise ValueError("bad FONT10 cell dimensions")
    if reserved != 0:
        raise ValueError("nonzero FONT10 reserved field")
    expected_bytes = header_bytes + glyph_count * record_bytes
    if declared_bytes != len(image) or len(image) != expected_bytes:
        raise ValueError(
            f"FONT10 size is {len(image)}, expected {expected_bytes}"
        )
    payload = image[header_bytes:]
    actual_crc32 = zlib.crc32(payload) & 0xFFFFFFFF
    if actual_crc32 != declared_crc32:
        raise ValueError(
            f"FONT10 CRC32 is 0x{actual_crc32:08x}, "
            f"expected 0x{declared_crc32:08x}"
        )

    glyphs: list[Font10Glyph] = []
    previous = -1
    for index in range(glyph_count):
        offset = header_bytes + index * record_bytes
        codepoint, advance, bitmap = FONT10_RECORD.unpack_from(
            image, offset
        )
        if codepoint <= previous:
            raise ValueError("FONT10 codepoints are not strictly sorted")
        if bitmap[-1] & 0x0F:
            raise ValueError(
                f"FONT10 U+{codepoint:04X} has nonzero bitmap padding"
            )
        glyphs.append(Font10Glyph(codepoint, advance, bitmap))
        previous = codepoint
    return Font10(
        ascent=ascent,
        descent=descent,
        glyphs=tuple(glyphs),
        payload_crc32=declared_crc32,
    )
