#!/usr/bin/env python3
"""Check the embedded-runtime memory/resource contract.

This is a host-side audit tool. It does not prove the port is finished, but it
turns the main hard requirements into repeatable checks:

- no project-side heap allocation in selected runtime sources,
- no runtime decompression path in selected runtime sources,
- no forbidden heap/decompress symbols in a native verification binary,
- section sizes visible through size/objdump/nm.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


PACK_MAGIC = 0x4B504C50
PACK_VERSION = 1
PACK_HEADER_SIZE = 32
PACK_ARCHIVE_ENTRY_SIZE = 12
PACK_CHUNK_ENTRY_SIZE = 16
PACK_CHUNK_F_COMPRESSED = 0x0001
PACK_ARCHIVE_IDS = {
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
PACK_ARCHIVE_NAMES = {archive_id: name for name, archive_id in PACK_ARCHIVE_IDS.items()}
PACK_FORMAT_NAMES = {
    0: "RAW",
    1: "NATIVE",
    2: "RNG_FRAMES",
    3: "TEXT_UTF16",
    4: "FONT_GLYPHS",
    5: "SFX_PCM16",
}


DEFAULT_SOURCE_GLOBS = ("*.c", "*.cpp", "*.h")
DEFAULT_EXCLUDE_DIRS = {
    ".git",
    "3ds",
    "3rd",
    "adplug",
    "android",
    "Consult",
    "docs",
    "emscripten",
    "incomplete_ports",
    "ios",
    "libmad",
    "liboggvorbis",
    "libopusfile",
    "libretro",
    "macos",
    "native_midi",
    "overlay",
    "scripts",
    "sdl_compat",
    "shaders",
    "timidity",
    "tools",
    "wii",
    "win32",
    "winrt",
}

HEAP_PATTERNS = (
    r"\bmalloc\s*\(",
    r"\bcalloc\s*\(",
    r"\brealloc\s*\(",
    r"\bfree\s*\(",
    r"\bUTIL_malloc\s*\(",
    r"\bUTIL_calloc\s*\(",
    r"\bSDL_malloc\s*\(",
    r"\bSDL_calloc\s*\(",
    r"\bSDL_realloc\s*\(",
    r"\bSDL_free\s*\(",
)

DECOMPRESS_PATTERNS = (
    r"\bPAL_MKFDecompressChunk\s*\(",
    r"\bPAL_MKFGetDecompressedSize\s*\(",
    r"\bDecompress\s*\(",
    r"\bYJ1_Decompress\s*\(",
    r"\bYJ2_Decompress\s*\(",
    r"\bdecompress\b",
    r"\bDecompress\b",
)

FORBIDDEN_SYMBOL_PATTERNS = (
    r"(^|[^\w])malloc($|[^\w])",
    r"(^|[^\w])calloc($|[^\w])",
    r"(^|[^\w])realloc($|[^\w])",
    r"(^|[^\w])free($|[^\w])",
    r"UTIL_malloc",
    r"UTIL_calloc",
    r"PAL_MKFDecompressChunk",
    r"PAL_MKFGetDecompressedSize",
    r"YJ1_Decompress",
    r"YJ2_Decompress",
    r"Decompress",
)


@dataclass(frozen=True)
class Hit:
    kind: str
    path: Path
    line: int
    text: str


@dataclass(frozen=True)
class Symbol:
    address: int
    size: int
    kind: str
    name: str


@dataclass(frozen=True)
class PackChunk:
    archive_id: int
    chunk_id: int
    offset: int
    size: int
    fmt: int
    flags: int


def iter_sources(root: Path, include_third_party: bool):
    for pattern in DEFAULT_SOURCE_GLOBS:
        for path in root.glob(pattern):
            if path.is_file():
                yield path

    for path in root.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in {".c", ".cpp", ".h", ".hpp"}:
            continue
        rel_parts = path.relative_to(root).parts
        if not include_third_party and rel_parts and rel_parts[0] in DEFAULT_EXCLUDE_DIRS:
            continue
        if len(rel_parts) == 1:
            continue
        yield path


def strip_c_comments(text: str) -> str:
    out: list[str] = []
    i = 0
    in_block = False
    in_string: str | None = None
    escape = False

    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""

        if in_block:
            if ch == "\n":
                out.append("\n")
            elif ch == "*" and nxt == "/":
                in_block = False
                out.append(" ")
                i += 1
            else:
                out.append(" ")
        elif in_string:
            out.append(ch)
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == in_string:
                in_string = None
        elif ch in {'"', "'"}:
            in_string = ch
            out.append(ch)
        elif ch == "/" and nxt == "*":
            in_block = True
            out.append(" ")
            i += 1
        elif ch == "/" and nxt == "/":
            while i < len(text) and text[i] != "\n":
                out.append(" ")
                i += 1
            if i < len(text):
                out.append("\n")
        else:
            out.append(ch)
        i += 1

    return "".join(out)


def scan_sources(root: Path, include_third_party: bool) -> list[Hit]:
    heap_res = [re.compile(p) for p in HEAP_PATTERNS]
    decomp_res = [re.compile(p, re.IGNORECASE) for p in DECOMPRESS_PATTERNS]
    hits: list[Hit] = []

    for path in sorted(set(iter_sources(root, include_third_party))):
        try:
            raw_text = path.read_text(errors="ignore")
        except OSError:
            continue
        original_lines = raw_text.splitlines()
        lines = strip_c_comments(raw_text).splitlines()
        for lineno, line in enumerate(lines, 1):
            if any(expr.search(line) for expr in heap_res):
                hits.append(Hit("heap", path.relative_to(root), lineno, original_lines[lineno - 1].strip()))
            if any(expr.search(line) for expr in decomp_res):
                hits.append(Hit("decompress", path.relative_to(root), lineno, original_lines[lineno - 1].strip()))

    return hits


def run_tool(args: list[str]) -> str:
    try:
        return subprocess.check_output(args, stderr=subprocess.STDOUT, text=True)
    except FileNotFoundError as exc:
        raise RuntimeError(f"missing tool: {args[0]}") from exc
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(f"{' '.join(args)} failed:\n{exc.output}") from exc


def parse_size_output(output: str) -> dict[str, int]:
    lines = [line.split() for line in output.splitlines() if line.strip()]
    if len(lines) < 2:
        return {}
    header = lines[0]
    values = lines[1]
    result = {}
    for key in ("text", "data", "bss", "dec"):
        if key in header:
            result[key] = int(values[header.index(key)], 0)
    return result


def parse_objdump_sections(output: str) -> dict[str, int]:
    sections: dict[str, int] = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0].isdigit() and parts[1].startswith("."):
            try:
                sections[parts[1]] = int(parts[2], 16)
            except ValueError:
                pass
    return sections


def parse_budget(values: list[str]) -> dict[str, int]:
    result = {}
    for item in values:
        name, sep, raw = item.partition("=")
        if not sep or not name:
            raise SystemExit(f"bad --max form: {item}; expected NAME=BYTES")
        result[name] = int(raw, 0)
    return result


def u16(data: bytes, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8)


def u32(data: bytes, offset: int) -> int:
    return (
        data[offset]
        | (data[offset + 1] << 8)
        | (data[offset + 2] << 16)
        | (data[offset + 3] << 24)
    )


def checked_range(offset: int, size: int, total: int) -> bool:
    return 0 <= offset <= total and 0 <= size <= total - offset


def parse_pack_size_budget(values: list[str]) -> dict[Path, int]:
    result: dict[Path, int] = {}
    for item in values:
        raw_path, sep, raw_limit = item.rpartition("=")
        if not sep or not raw_path:
            raise SystemExit(f"bad --max-pack-size form: {item}; expected PATH=BYTES")
        result[Path(raw_path).resolve()] = int(raw_limit, 0)
    return result


def parse_archive_ids(values: list[str]) -> set[int]:
    result: set[int] = set()
    for item in values:
        for raw_name in item.split(","):
            name = raw_name.strip().upper()
            if not name:
                continue
            if name in PACK_ARCHIVE_IDS:
                result.add(PACK_ARCHIVE_IDS[name])
            else:
                try:
                    result.add(int(name, 0))
                except ValueError as exc:
                    raise SystemExit(f"unknown pack archive: {raw_name}") from exc
    return result


def parse_pack_chunks(path: Path) -> tuple[bytes, list[PackChunk]]:
    data = path.read_bytes()
    if len(data) < PACK_HEADER_SIZE:
        raise ValueError("short pack header")
    if u32(data, 0) != PACK_MAGIC:
        raise ValueError("bad pack magic")
    if u16(data, 4) != PACK_VERSION or u16(data, 6) != PACK_HEADER_SIZE:
        raise ValueError("bad pack version/header size")

    archive_count = u16(data, 8)
    archive_table_offset = u32(data, 12)
    pack_size = u32(data, 24)
    if pack_size != len(data):
        raise ValueError(f"pack size field is {pack_size}, actual {len(data)}")
    if not checked_range(archive_table_offset, archive_count * PACK_ARCHIVE_ENTRY_SIZE, len(data)):
        raise ValueError("archive table out of range")

    chunks: list[PackChunk] = []
    seen_archives: set[int] = set()
    for archive_index in range(archive_count):
        archive_offset = archive_table_offset + archive_index * PACK_ARCHIVE_ENTRY_SIZE
        archive_id = u16(data, archive_offset)
        chunk_count = u16(data, archive_offset + 2)
        chunk_table_offset = u32(data, archive_offset + 4)
        if archive_id in seen_archives:
            raise ValueError(f"duplicate archive id {archive_id}")
        seen_archives.add(archive_id)
        if not checked_range(chunk_table_offset, chunk_count * PACK_CHUNK_ENTRY_SIZE, len(data)):
            raise ValueError(f"chunk table out of range for archive {archive_id}")

        for chunk_id in range(chunk_count):
            chunk_offset = chunk_table_offset + chunk_id * PACK_CHUNK_ENTRY_SIZE
            payload_offset = u32(data, chunk_offset)
            payload_size = u32(data, chunk_offset + 4)
            fmt = u16(data, chunk_offset + 8)
            flags = u16(data, chunk_offset + 10)
            if not checked_range(payload_offset, payload_size, len(data)):
                raise ValueError(f"payload out of range: archive {archive_id} chunk {chunk_id}")
            chunks.append(PackChunk(archive_id, chunk_id, payload_offset, payload_size, fmt, flags))
    return data, chunks


def check_pack(path: Path, max_size: int | None, forbidden_archives: set[int]) -> tuple[list[str], str]:
    errors: list[str] = []
    report: list[str] = []

    try:
        data, chunks = parse_pack_chunks(path)
    except OSError as exc:
        return [f"{path}: cannot read pack: {exc}"], ""
    except ValueError as exc:
        return [f"{path}: {exc}"], ""

    if max_size is not None and len(data) > max_size:
        errors.append(f"{path}: pack size {len(data)} bytes, over budget {max_size}")

    archive_payloads: dict[int, int] = {}
    archive_chunks: dict[int, int] = {}
    format_counts: dict[int, int] = {}
    bad_flags: list[PackChunk] = []
    bad_magic: list[PackChunk] = []
    bad_formats: list[PackChunk] = []
    forbidden_present: set[int] = set()

    for chunk in chunks:
        archive_payloads[chunk.archive_id] = archive_payloads.get(chunk.archive_id, 0) + chunk.size
        archive_chunks[chunk.archive_id] = archive_chunks.get(chunk.archive_id, 0) + 1
        format_counts[chunk.fmt] = format_counts.get(chunk.fmt, 0) + 1
        if chunk.flags != 0 or (chunk.flags & PACK_CHUNK_F_COMPRESSED) != 0:
            bad_flags.append(chunk)
        if chunk.fmt not in PACK_FORMAT_NAMES:
            bad_formats.append(chunk)
        if chunk.archive_id in forbidden_archives:
            forbidden_present.add(chunk.archive_id)
        payload = data[chunk.offset : chunk.offset + min(chunk.size, 4)]
        if payload == b"YJ_1":
            bad_magic.append(chunk)

    if bad_flags:
        errors.append(f"{path}: chunks with runtime flags: {len(bad_flags)}")
    if bad_formats:
        errors.append(f"{path}: chunks with unknown formats: {len(bad_formats)}")
    if bad_magic:
        errors.append(f"{path}: chunks still carrying YJ_1 payloads: {len(bad_magic)}")
    if forbidden_present:
        names = ", ".join(PACK_ARCHIVE_NAMES.get(archive_id, str(archive_id)) for archive_id in sorted(forbidden_present))
        errors.append(f"{path}: forbidden archives present: {names}")

    report.append(f"## pack {path}")
    report.append(f"size={len(data)} chunks={len(chunks)} payload={sum(chunk.size for chunk in chunks)}")
    for archive_id in sorted(archive_chunks):
        report.append(
            f"archive {archive_id} chunks={archive_chunks[archive_id]} payload={archive_payloads[archive_id]}"
        )
    report.append("formats " + " ".join(
        f"{PACK_FORMAT_NAMES.get(fmt, str(fmt))}={count}" for fmt, count in sorted(format_counts.items())
    ))
    if max_size is not None:
        report.append(f"max-size={max_size}")
    if forbidden_present:
        report.append("forbidden-archives " + " ".join(
            PACK_ARCHIVE_NAMES.get(archive_id, str(archive_id)) for archive_id in sorted(forbidden_present)
        ))
    for label, bad in (("flagged", bad_flags), ("unknown-format", bad_formats), ("yj1", bad_magic)):
        if bad:
            report.append(label)
            for chunk in bad[:80]:
                report.append(
                    f"  archive={chunk.archive_id} chunk={chunk.chunk_id} size={chunk.size} "
                    f"format={chunk.fmt} flags=0x{chunk.flags:04x}"
                )

    return errors, "\n".join(report)


def check_link_map(path: Path) -> tuple[list[str], str]:
    try:
        size = path.stat().st_size
    except OSError as exc:
        return [f"{path}: cannot stat linker map: {exc}"], ""
    if size <= 0:
        return [f"{path}: empty linker map"], ""
    return [], f"## linker map {path}\nsize={size}"


def parse_sized_symbols(output: str) -> list[Symbol]:
    symbols: list[Symbol] = []
    expr = re.compile(r"^\s*([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+([A-Za-z])\s+(.+)$")
    for line in output.splitlines():
        match = expr.match(line)
        if not match:
            continue
        symbols.append(
            Symbol(
                address=int(match.group(1), 16),
                size=int(match.group(2), 16),
                kind=match.group(3),
                name=match.group(4),
            )
        )
    return symbols


def append_symbol_prefix_report(
    report: list[str],
    errors: list[str],
    symbols: list[Symbol],
    prefixes: list[str],
    budgets: dict[str, int],
) -> None:
    ordered_prefixes: list[str] = []
    for prefix in [*prefixes, *budgets.keys(), "pal_sram_", "pal_psram_"]:
        if prefix and prefix not in ordered_prefixes:
            ordered_prefixes.append(prefix)

    rows: list[tuple[str, int, list[Symbol]]] = []
    for prefix in ordered_prefixes:
        matching = [symbol for symbol in symbols if symbol.name.startswith(prefix)]
        if not matching and prefix not in budgets:
            continue
        total = sum(symbol.size for symbol in matching)
        rows.append((prefix, total, sorted(matching, key=lambda symbol: symbol.size, reverse=True)))
        if prefix in budgets and total > budgets[prefix]:
            errors.append(f"symbols {prefix} total {total} bytes, over budget {budgets[prefix]}")

    if not rows:
        return

    report.append("\n## symbol prefix totals")
    for prefix, total, matching in rows:
        limit = budgets.get(prefix)
        if limit is None:
            report.append(f"{prefix} total={total}")
        else:
            report.append(f"{prefix} total={total} limit={limit}")
        for symbol in matching[:80]:
            report.append(f"  {symbol.size:10d} {symbol.kind} {symbol.name}")


def check_binary(
    binary: Path,
    budgets: dict[str, int],
    symbol_prefixes: list[str],
    symbol_prefix_budgets: dict[str, int],
) -> tuple[list[str], str]:
    errors: list[str] = []
    report: list[str] = []

    size_output = run_tool(["size", str(binary)])
    size_values = parse_size_output(size_output)
    report.append("## size")
    report.append(size_output.rstrip())

    objdump_output = run_tool(["objdump", "-h", str(binary)])
    sections = parse_objdump_sections(objdump_output)
    report.append("\n## objdump -h sections")
    for name, value in sorted(sections.items()):
        report.append(f"{name} {value}")

    nm_output = run_tool(["nm", "-C", str(binary)])
    symbol_hits = []
    symbol_res = [re.compile(pattern) for pattern in FORBIDDEN_SYMBOL_PATTERNS]
    for line in nm_output.splitlines():
        if any(expr.search(line) for expr in symbol_res):
            symbol_hits.append(line)
    if symbol_hits:
        errors.append(f"forbidden symbols present: {len(symbol_hits)}")
        report.append("\n## forbidden symbols")
        report.extend(symbol_hits[:200])

    nm_sized_output = run_tool(["nm", "-S", "--size-sort", "-C", str(binary)])
    symbols = parse_sized_symbols(nm_sized_output)
    append_symbol_prefix_report(report, errors, symbols, symbol_prefixes, symbol_prefix_budgets)

    for name, limit in budgets.items():
        actual = sections.get(name)
        if actual is None and name in size_values:
            actual = size_values[name]
        if actual is None:
            errors.append(f"budget section not found: {name}")
        elif actual > limit:
            errors.append(f"{name} is {actual} bytes, over budget {limit}")

    return errors, "\n".join(report)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--link-map", type=Path, action="append", default=[])
    parser.add_argument("--pack", type=Path, action="append", default=[])
    parser.add_argument(
        "--forbid-pack-archive",
        action="append",
        default=[],
        metavar="NAME[,NAME...]",
        help="fail if any checked pack contains these archive IDs or names",
    )
    parser.add_argument("--include-third-party", action="store_true")
    parser.add_argument("--max", action="append", default=[], metavar="NAME=BYTES")
    parser.add_argument("--max-pack-size", action="append", default=[], metavar="PATH=BYTES")
    parser.add_argument(
        "--symbol-prefix",
        action="append",
        default=[],
        metavar="PREFIX",
        help="print total size for ELF symbols whose names start with PREFIX",
    )
    parser.add_argument(
        "--max-symbol-prefix",
        action="append",
        default=[],
        metavar="PREFIX=BYTES",
        help="fail if ELF symbols whose names start with PREFIX exceed BYTES in total",
    )
    parser.add_argument("--fail-on-source", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    hits = scan_sources(root, args.include_third_party)

    errors: list[str] = []
    print("# Embedded Contract Check")
    print(f"root: {root}")

    by_kind: dict[str, list[Hit]] = {"heap": [], "decompress": []}
    for hit in hits:
        by_kind.setdefault(hit.kind, []).append(hit)

    for kind in ("heap", "decompress"):
        kind_hits = by_kind.get(kind, [])
        print(f"\n## source {kind} hits: {len(kind_hits)}")
        for hit in kind_hits[:200]:
            print(f"{hit.path}:{hit.line}: {hit.text}")
        if args.fail_on_source and kind_hits:
            errors.append(f"source {kind} hits: {len(kind_hits)}")

    if args.binary:
        binary_errors, binary_report = check_binary(
            args.binary.resolve(),
            parse_budget(args.max),
            args.symbol_prefix,
            parse_budget(args.max_symbol_prefix),
        )
        errors.extend(binary_errors)
        print()
        print(binary_report)

    pack_size_budgets = parse_pack_size_budget(args.max_pack_size)
    forbidden_archives = parse_archive_ids(args.forbid_pack_archive)
    for raw_pack_path in args.pack:
        pack_path = raw_pack_path.resolve()
        pack_errors, pack_report = check_pack(pack_path, pack_size_budgets.get(pack_path), forbidden_archives)
        errors.extend(pack_errors)
        if pack_report:
            print()
            print(pack_report)

    for raw_map_path in args.link_map:
        map_path = raw_map_path.resolve()
        map_errors, map_report = check_link_map(map_path)
        errors.extend(map_errors)
        if map_report:
            print()
            print(map_report)

    if errors:
        print("\n## FAIL")
        for error in errors:
            print(f"- {error}")
        return 1

    print("\n## PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
