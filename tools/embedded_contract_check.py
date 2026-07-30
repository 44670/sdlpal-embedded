#!/usr/bin/env python3
"""Check the embedded-runtime memory/resource contract.

This is a host-side audit tool. It does not prove the port is finished, but it
turns the main hard requirements into repeatable checks:

- no project-side C/C++ heap allocation in selected runtime sources,
- no runtime decompression path in selected runtime sources,
- no PAL_LARGE local scratch buffers in selected runtime sources,
- no typed static pal_sram_/pal_psram_ storage declarations in selected runtime sources,
- no active loose original PAL data filename references in selected runtime sources,
- no writable/shared mmap pack views in selected runtime sources,
- no forbidden heap/decompress undefined symbols or relocations in selected object files,
- no forbidden heap/decompress symbols in a native verification binary,
- no call sites to heap/decompress trap targets in that binary,
- section sizes and symbols visible through size/objdump/nm.
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
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
    r"\bnew\b",
    r"\bdelete\b",
    r"\boperator\s+new\b",
    r"\boperator\s+delete\b",
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

SCRATCH_PATTERNS = (
    r"\bPAL_LARGE\b",
)

STORAGE_PATTERNS = (
    r"\bstatic\s+(?!uint8_t\b)[^;\n]*\bpal_(?:sram|psram)_",
)

LOOSE_RESOURCE_PATTERNS = (
    r'"[^"\n]*(?:abc|ball|data|f|fbp|fire|gop|map|mgo|pat|rgm|rng|sss|voc|sounds|midi|mus)\.mkf"',
    r'"[^"\n]*(?:word\.dat|m\.msg|desc\.dat|wor16\.asc|wor16\.fon)"',
)

MMAP_CALL_PATTERN = re.compile(r"\bmmap\s*\([^;]*;", re.S)
MMAP_REQUIRED_PATTERNS = (
    re.compile(r"\bPROT_READ\b"),
    re.compile(r"\bMAP_PRIVATE\b"),
)
MMAP_FORBIDDEN_PATTERNS = (
    re.compile(r"\bPROT_WRITE\b"),
    re.compile(r"\bMAP_SHARED\b"),
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
    r"operator new",
    r"operator delete",
    r"(^|[^\w])_Zn[aw]m($|[^\w])",
    r"(^|[^\w])_Zd[al]Pv(m)?($|[^\w])",
)

FORBIDDEN_CALL_TARGETS = {
    "__wrap_malloc",
    "__wrap_calloc",
    "__wrap_realloc",
    "__wrap_free",
    "malloc",
    "calloc",
    "realloc",
    "free",
    "PAL_RuntimeHeapAllocUnavailable",
    "PAL_RuntimeHeapZeroAllocUnavailable",
    "PAL_MKFCompressedChunkSizeUnavailable",
    "PAL_MKFCompressedChunkReadUnavailable",
    "PAL_RuntimeCodecUnavailable",
    "_Znwm",
    "_Znam",
    "_ZdlPv",
    "_ZdaPv",
    "_ZdlPvm",
    "_ZdaPvm",
}

FORBIDDEN_OBJECT_SYMBOL_PATTERNS = tuple(FORBIDDEN_SYMBOL_PATTERNS) + (
    r"SDL_malloc",
    r"SDL_calloc",
    r"SDL_realloc",
    r"SDL_free",
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


def source_label(path: Path, root: Path) -> str:
    try:
        return str(path.relative_to(root))
    except ValueError:
        return str(path)


def normalized_source_path(path: Path, root: Path) -> str:
    source = path if path.is_absolute() else (Path.cwd() / path)
    return source_label(source.resolve(), root).replace("\\", "/")


def check_source_manifest(root: Path, manifest_path: Path, source_files: list[Path]) -> tuple[list[str], str]:
    errors: list[str] = []
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except OSError as exc:
        return [f"{manifest_path}: cannot read source manifest: {exc}"], ""
    except json.JSONDecodeError as exc:
        return [f"{manifest_path}: invalid source manifest JSON: {exc}"], ""

    raw_sources = manifest.get("sources")
    if not isinstance(raw_sources, list) or not all(isinstance(item, str) for item in raw_sources):
        return [f"{manifest_path}: sources must be a list of strings"], ""

    expected = set(raw_sources)
    actual = {normalized_source_path(path, root) for path in source_files}
    for rel in sorted(actual - expected):
        errors.append(f"{manifest_path}: linked source missing from manifest: {rel}")
    for rel in sorted(expected - actual):
        errors.append(f"{manifest_path}: manifest source not linked in this contract profile: {rel}")

    report = "\n".join(
        [
            f"## source inventory {manifest_path}",
            f"sources={len(actual)} expected={len(expected)}",
        ]
    )
    return errors, report


def source_is_excluded(path: Path, root: Path, patterns: list[str]) -> bool:
    label = source_label(path, root)
    name = path.name
    return any(fnmatch.fnmatch(label, pattern) or fnmatch.fnmatch(name, pattern) for pattern in patterns)


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


SOURCE_IF_TOKEN_RE = re.compile(r"\s*(defined|[A-Za-z_]\w*|0[xX][0-9A-Fa-f]+|\d+|&&|\|\||==|!=|<=|>=|[!()<>,-])")


def parse_source_defines(raw_defines: list[str]) -> dict[str, int]:
    defines: dict[str, int] = {}
    for raw in raw_defines:
        if "=" in raw:
            name, value = raw.split("=", 1)
            name = name.strip()
            value = value.strip()
            if not name:
                continue
            try:
                defines[name] = int(value, 0)
            except ValueError:
                defines[name] = 1 if value else 0
        else:
            name = raw.strip()
            if name:
                defines[name] = 1
    return defines


def source_if_tokens(expr: str) -> list[str] | None:
    tokens: list[str] = []
    pos = 0
    while pos < len(expr):
        if expr[pos:].strip() == "":
            break
        match = SOURCE_IF_TOKEN_RE.match(expr, pos)
        if match is None:
            return None
        token = match.group(1)
        tokens.append(token)
        pos = match.end()
    return tokens


class SourceIfParser:
    def __init__(self, tokens: list[str], defines: dict[str, int]):
        self.tokens = tokens
        self.defines = defines
        self.pos = 0

    def peek(self) -> str | None:
        if self.pos >= len(self.tokens):
            return None
        return self.tokens[self.pos]

    def take(self, token: str | None = None) -> str | None:
        cur = self.peek()
        if cur is None or (token is not None and cur != token):
            return None
        self.pos += 1
        return cur

    def parse(self) -> int | None:
        value = self.parse_or()
        if self.peek() is not None:
            return None
        return value

    def parse_or(self) -> int | None:
        value = self.parse_and()
        while self.take("||") is not None:
            rhs = self.parse_and()
            if value is not None and value != 0:
                value = 1
            elif rhs is not None and rhs != 0:
                value = 1
            elif value is None or rhs is None:
                value = None
            else:
                value = 0
        return value

    def parse_and(self) -> int | None:
        value = self.parse_compare()
        while self.take("&&") is not None:
            rhs = self.parse_compare()
            if value is not None and value == 0:
                value = 0
            elif rhs is not None and rhs == 0:
                value = 0
            elif value is None or rhs is None:
                value = None
            else:
                value = 1
        return value

    def parse_compare(self) -> int | None:
        value = self.parse_unary()
        op = self.peek()
        if op not in {"==", "!=", "<", "<=", ">", ">="}:
            return value
        self.take()
        rhs = self.parse_unary()
        if value is None or rhs is None:
            return None
        if op == "==":
            return 1 if value == rhs else 0
        if op == "!=":
            return 1 if value != rhs else 0
        if op == "<":
            return 1 if value < rhs else 0
        if op == "<=":
            return 1 if value <= rhs else 0
        if op == ">":
            return 1 if value > rhs else 0
        return 1 if value >= rhs else 0

    def parse_unary(self) -> int | None:
        if self.take("!") is not None:
            value = self.parse_unary()
            if value is None:
                return None
            return 0 if value else 1
        return self.parse_primary()

    def parse_primary(self) -> int | None:
        token = self.peek()
        if token is None:
            return None
        if token == "(":
            self.take("(")
            value = self.parse_or()
            if self.take(")") is None:
                return None
            return value
        if re.fullmatch(r"0[xX][0-9A-Fa-f]+|\d+", token):
            self.take()
            return int(token, 0)
        if token == "defined":
            self.take()
            if self.take("(") is not None:
                name = self.take()
                if name is None or not re.fullmatch(r"[A-Za-z_]\w*", name) or self.take(")") is None:
                    return None
            else:
                name = self.take()
                if name is None or not re.fullmatch(r"[A-Za-z_]\w*", name):
                    return None
            return 1 if name in self.defines else 0
        if re.fullmatch(r"[A-Za-z_]\w*", token):
            self.take()
            if self.peek() == "(":
                return self.parse_function_macro(token)
            return self.defines.get(token, 0)
        return None

    def parse_function_macro(self, name: str) -> int | None:
        args: list[int] = []
        if self.take("(") is None:
            return None
        if self.peek() == ")":
            self.take(")")
        else:
            while True:
                value = self.parse_or()
                if value is None:
                    return None
                args.append(value)
                if self.take(",") is not None:
                    continue
                if self.take(")") is not None:
                    break
                return None

        if name == "SDL_VERSION_ATLEAST" and len(args) == 3:
            current = (
                self.defines.get("SDL_MAJOR_VERSION", 0),
                self.defines.get("SDL_MINOR_VERSION", 0),
                self.defines.get("SDL_PATCHLEVEL", 0),
            )
            return 1 if current >= (args[0], args[1], args[2]) else 0
        return None


def eval_simple_if_expr(expr: str, defines: dict[str, int]) -> bool | None:
    tokens = source_if_tokens(expr)
    if tokens is None:
        return None
    value = SourceIfParser(tokens, defines).parse()
    if value is None:
        return None
    return value != 0


def strip_inactive_preprocessor(text: str, defines: dict[str, int]) -> str:
    out: list[str] = []
    local_defines = dict(defines)
    active_stack: list[dict[str, bool]] = []
    current_active = True

    for line in text.splitlines(keepends=True):
        stripped = line.lstrip()
        directive = stripped[1:].strip() if stripped.startswith("#") else None
        if directive is None:
            out.append(line if current_active else "\n")
            continue

        tokens = directive.split(None, 1)
        keyword = tokens[0] if tokens else ""
        rest = tokens[1] if len(tokens) > 1 else ""

        if keyword in {"ifdef", "ifndef", "if"}:
            parent_active = current_active
            if keyword == "ifdef":
                condition = rest.strip() in local_defines
            elif keyword == "ifndef":
                condition = rest.strip() not in local_defines
            else:
                value = eval_simple_if_expr(rest, local_defines)
                condition = True if value is None else value
            current_active = parent_active and condition
            active_stack.append({"parent": parent_active, "taken": current_active})
            out.append("\n")
        elif keyword == "else" and active_stack:
            state = active_stack[-1]
            current_active = state["parent"] and not state["taken"]
            state["taken"] = True
            out.append("\n")
        elif keyword == "elif" and active_stack:
            state = active_stack[-1]
            value = eval_simple_if_expr(rest, local_defines)
            condition = True if value is None else value
            current_active = state["parent"] and not state["taken"] and condition
            state["taken"] = state["taken"] or current_active
            out.append("\n")
        elif keyword == "endif" and active_stack:
            state = active_stack.pop()
            current_active = state["parent"]
            out.append("\n")
        elif keyword == "define" and current_active:
            parts = rest.split(None, 1)
            if parts and re.fullmatch(r"[A-Za-z_]\w*", parts[0]):
                value = 1
                if len(parts) > 1:
                    parsed = eval_simple_if_expr(parts[1], local_defines)
                    if parsed is not None:
                        value = 1 if parsed else 0
                    else:
                        try:
                            value = int(parts[1].strip(), 0)
                        except ValueError:
                            value = 1
                local_defines[parts[0]] = value
            out.append("\n")
        elif keyword == "undef" and current_active:
            name = rest.strip()
            if re.fullmatch(r"[A-Za-z_]\w*", name):
                local_defines.pop(name, None)
            out.append("\n")
        else:
            out.append(line if current_active else "\n")

    return "".join(out)


def iter_scan_sources(root: Path, include_third_party: bool, source_files: list[Path]):
    if source_files:
        for source_file in source_files:
            path = source_file if source_file.is_absolute() else (Path.cwd() / source_file)
            if path.is_file():
                yield path.resolve()
        return

    for path in iter_sources(root, include_third_party):
        yield path.resolve()


def scan_sources(
    root: Path,
    include_third_party: bool,
    source_excludes: list[str],
    source_defines: dict[str, int],
    source_files: list[Path],
) -> list[Hit]:
    heap_res = [re.compile(p) for p in HEAP_PATTERNS]
    decomp_res = [re.compile(p, re.IGNORECASE) for p in DECOMPRESS_PATTERNS]
    scratch_res = [re.compile(p) for p in SCRATCH_PATTERNS]
    storage_res = [re.compile(p) for p in STORAGE_PATTERNS]
    loose_resource_res = [re.compile(p, re.IGNORECASE) for p in LOOSE_RESOURCE_PATTERNS]
    hits: list[Hit] = []

    for path in sorted(set(iter_scan_sources(root, include_third_party, source_files))):
        if source_is_excluded(path, root, source_excludes):
            continue
        try:
            raw_text = path.read_text(errors="ignore")
        except OSError:
            continue
        original_lines = raw_text.splitlines()
        active_text = strip_c_comments(strip_inactive_preprocessor(raw_text, source_defines))
        lines = active_text.splitlines()
        for lineno, line in enumerate(lines, 1):
            if any(expr.search(line) for expr in heap_res):
                hits.append(Hit("heap", Path(source_label(path, root)), lineno, original_lines[lineno - 1].strip()))
            if any(expr.search(line) for expr in decomp_res):
                hits.append(Hit("decompress", Path(source_label(path, root)), lineno, original_lines[lineno - 1].strip()))
            if any(expr.search(line) for expr in scratch_res):
                hits.append(Hit("scratch", Path(source_label(path, root)), lineno, original_lines[lineno - 1].strip()))
            if any(expr.search(line) for expr in storage_res):
                hits.append(Hit("storage", Path(source_label(path, root)), lineno, original_lines[lineno - 1].strip()))
            if any(expr.search(line) for expr in loose_resource_res):
                hits.append(Hit("loose-resource", Path(source_label(path, root)), lineno, original_lines[lineno - 1].strip()))
        for match in MMAP_CALL_PATTERN.finditer(active_text):
            statement = match.group(0)
            if (
                any(expr.search(statement) for expr in MMAP_FORBIDDEN_PATTERNS)
                or not all(expr.search(statement) for expr in MMAP_REQUIRED_PATTERNS)
            ):
                lineno = active_text.count("\n", 0, match.start()) + 1
                source_line = original_lines[lineno - 1].strip() if lineno <= len(original_lines) else "mmap(...)"
                hits.append(Hit("mmap-access", Path(source_label(path, root)), lineno, source_line))

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


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fp:
        while True:
            data = fp.read(1024 * 1024)
            if not data:
                break
            digest.update(data)
    return digest.hexdigest()


def pack_manifest_summary(path: Path) -> dict[str, object]:
    data, chunks = parse_pack_chunks(path)
    archive_chunks: dict[int, list[PackChunk]] = {}
    for chunk in chunks:
        archive_chunks.setdefault(chunk.archive_id, []).append(chunk)

    archive_entries = []
    for archive_id in sorted(archive_chunks):
        grouped = sorted(archive_chunks[archive_id], key=lambda chunk: chunk.chunk_id)
        format_counts: dict[str, int] = {}
        chunk_entries = []
        for chunk in grouped:
            format_name = PACK_FORMAT_NAMES.get(chunk.fmt, str(chunk.fmt))
            format_counts[format_name] = format_counts.get(format_name, 0) + 1
            chunk_entries.append(
                {
                    "id": chunk.chunk_id,
                    "format": format_name,
                    "payload_bytes": chunk.size,
                }
            )
        archive_entries.append(
            {
                "name": PACK_ARCHIVE_NAMES.get(archive_id, str(archive_id)),
                "id": archive_id,
                "chunk_count": len(grouped),
                "payload_bytes": sum(chunk.size for chunk in grouped),
                "max_payload_bytes": max((chunk.size for chunk in grouped), default=0),
                "format_counts": dict(sorted(format_counts.items())),
                "chunks": chunk_entries,
            }
        )

    return {
        "path": str(path.resolve()),
        "size": len(data),
        "archive_count": len(archive_entries),
        "chunk_count": len(chunks),
        "payload_bytes": sum(chunk.size for chunk in chunks),
        "max_payload_bytes": max((chunk.size for chunk in chunks), default=0),
        "archive_summaries": archive_entries,
    }


def compare_manifest_pack(label: str, expected: dict[str, object], actual: dict[str, object]) -> list[str]:
    errors: list[str] = []
    for key in ("size", "archive_count", "chunk_count", "payload_bytes", "max_payload_bytes"):
        if expected.get(key) != actual.get(key):
            errors.append(f"{label}: manifest {key}={expected.get(key)} actual={actual.get(key)}")

    expected_archives = expected.get("archive_summaries")
    actual_archives = actual.get("archive_summaries")
    if expected_archives != actual_archives:
        errors.append(f"{label}: archive decoded-size summary does not match pack")
    return errors


def check_manifest(path: Path) -> tuple[list[str], str]:
    errors: list[str] = []
    report: list[str] = [f"## manifest {path}"]

    try:
        manifest = json.loads(path.read_text())
    except OSError as exc:
        return [f"{path}: cannot read manifest: {exc}"], ""
    except json.JSONDecodeError as exc:
        return [f"{path}: invalid JSON: {exc}"], ""

    if manifest.get("schema") != "sdlpal-embedded-pack-manifest" or manifest.get("version") != 1:
        errors.append(f"{path}: unknown manifest schema/version")

    runtime = manifest.get("runtime", {})
    if runtime.get("runtime_decompression_required") is not False:
        errors.append(f"{path}: manifest does not declare runtime_decompression_required=false")
    if runtime.get("payloads_are_runtime_native") is not True:
        errors.append(f"{path}: manifest does not declare runtime-native payloads")

    checked_layout = 0
    pack_layout = manifest.get("pack_layout")
    if not isinstance(pack_layout, dict):
        errors.append(f"{path}: pack_layout is not an object")
        pack_layout = {}
    layout_packs = pack_layout.get("packs")
    if not isinstance(layout_packs, dict):
        errors.append(f"{path}: pack_layout has no packs object")
        layout_packs = {}
    layout_path_raw = pack_layout.get("path")
    layout_file_packs: dict[str, object] = {}
    if not isinstance(layout_path_raw, str):
        errors.append(f"{path}: pack_layout has no path")
    else:
        layout_path = Path(layout_path_raw)
        try:
            digest = hash_file(layout_path)
            layout_file = json.loads(layout_path.read_text())
        except OSError as exc:
            errors.append(f"{path}: cannot read pack layout {layout_path}: {exc}")
        except json.JSONDecodeError as exc:
            errors.append(f"{path}: invalid pack layout {layout_path}: {exc}")
        else:
            checked_layout = 1
            if pack_layout.get("sha256") != digest:
                errors.append(f"{path}: pack layout {layout_path} sha256 changed")
            raw_layout_file_packs = layout_file.get("packs")
            if isinstance(raw_layout_file_packs, dict):
                layout_file_packs = {
                    label: list(raw_layout_file_packs.get(label, []))
                    for label in ("nor", "tf")
                }
                layout_profile = pack_layout.get("profile")
                if layout_profile is not None:
                    raw_profiles = layout_file.get("profiles")
                    raw_profile = (
                        raw_profiles.get(layout_profile)
                        if isinstance(raw_profiles, dict)
                        and isinstance(layout_profile, str)
                        else None
                    )
                    raw_additions = (
                        raw_profile.get("pack_additions")
                        if isinstance(raw_profile, dict)
                        else None
                    )
                    if not isinstance(raw_additions, dict):
                        errors.append(
                            f"{path}: pack layout {layout_path} has no "
                            f"profile {layout_profile!r}"
                        )
                    else:
                        for label in ("nor", "tf"):
                            additions = raw_additions.get(label, {})
                            if not isinstance(additions, dict):
                                errors.append(
                                    f"{path}: pack layout profile "
                                    f"{layout_profile!r} has invalid {label} additions"
                                )
                                continue
                            layout_file_packs[label].extend(additions)
            else:
                errors.append(f"{path}: pack layout {layout_path} has no packs object")
    layout_overrides = pack_layout.get("overrides")
    if not isinstance(layout_overrides, dict):
        errors.append(f"{path}: pack_layout has no overrides object")
        layout_overrides = {}

    data_dir = Path(str(manifest.get("data_dir", "")))
    source_files = manifest.get("source_files", [])
    if not isinstance(source_files, list):
        errors.append(f"{path}: source_files is not a list")
        source_files = []

    checked_sources = 0
    for item in source_files:
        if not isinstance(item, dict):
            errors.append(f"{path}: malformed source file item")
            continue
        rel = item.get("path")
        if not isinstance(rel, str):
            errors.append(f"{path}: source file without path")
            continue
        source_path = data_dir / rel
        try:
            size = source_path.stat().st_size
            digest = hash_file(source_path)
        except OSError as exc:
            errors.append(f"{path}: cannot read source file {source_path}: {exc}")
            continue
        checked_sources += 1
        if item.get("size") != size:
            errors.append(f"{path}: source file {rel} size changed: manifest={item.get('size')} actual={size}")
        if item.get("sha256") != digest:
            errors.append(f"{path}: source file {rel} sha256 changed")

    packs = manifest.get("packs", {})
    if not isinstance(packs, dict):
        errors.append(f"{path}: packs is not an object")
        packs = {}

    checked_packs = 0
    for label in ("nor", "tf"):
        item = packs.get(label)
        if not isinstance(item, dict):
            errors.append(f"{path}: missing pack manifest for {label}")
            continue
        pack_path_raw = item.get("path")
        if not isinstance(pack_path_raw, str):
            errors.append(f"{path}: pack {label} has no path")
            continue
        pack_path = Path(pack_path_raw)
        try:
            actual = pack_manifest_summary(pack_path)
        except OSError as exc:
            errors.append(f"{path}: cannot read pack {pack_path}: {exc}")
            continue
        except ValueError as exc:
            errors.append(f"{path}: bad pack {pack_path}: {exc}")
            continue
        checked_packs += 1
        errors.extend(compare_manifest_pack(f"{path}:{label}", item, actual))
        if item.get("archives") != layout_packs.get(label):
            errors.append(f"{path}: pack_layout archives for {label} do not match pack manifest")
        if not layout_overrides.get(label) and layout_file_packs and item.get("archives") != layout_file_packs.get(label):
            errors.append(f"{path}: pack manifest archives for {label} do not match pack layout file")

    report.append(f"source_files={checked_sources} packs={checked_packs} pack_layouts={checked_layout}")
    return errors, "\n".join(report)


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


def append_forbidden_symbol_prefix_report(
    report: list[str],
    errors: list[str],
    symbols: list[Symbol],
    prefixes: list[str],
) -> None:
    if not prefixes:
        return

    rows: list[tuple[str, list[Symbol]]] = []
    for prefix in prefixes:
        matching = sorted(
            [symbol for symbol in symbols if symbol.name.startswith(prefix)],
            key=lambda symbol: symbol.name,
        )
        if matching:
            rows.append((prefix, matching))
            errors.append(f"forbidden symbol prefix {prefix}: {len(matching)} symbols")

    report.append("\n## forbidden symbol prefixes")
    if not rows:
        report.append("0")
        return

    for prefix, matching in rows:
        report.append(f"{prefix} hits={len(matching)}")
        for symbol in matching[:80]:
            report.append(f"  {symbol.size:10d} {symbol.kind} {symbol.name}")


def find_forbidden_call_targets(output: str) -> list[str]:
    hits: list[str] = []
    call_expr = re.compile(r"\b(callq?|jmpq?|blx?|b\.w)\b")
    target_expr = re.compile(r"<([^>]+)>")

    for line in output.splitlines():
        if not call_expr.search(line):
            continue
        match = target_expr.search(line)
        if not match:
            continue
        target = match.group(1).split("+", 1)[0]
        target = target.split("@", 1)[0]
        if target in FORBIDDEN_CALL_TARGETS:
            hits.append(line)
    return hits


def check_binary(
    binary: Path,
    budgets: dict[str, int],
    symbol_prefixes: list[str],
    symbol_prefix_budgets: dict[str, int],
    forbidden_symbol_prefixes: list[str],
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

    symbol_res = [re.compile(pattern) for pattern in FORBIDDEN_SYMBOL_PATTERNS]

    objdump_symbols_output = run_tool(["objdump", "-t", str(binary)])
    objdump_symbol_hits = []
    for line in objdump_symbols_output.splitlines():
        if any(expr.search(line) for expr in symbol_res):
            objdump_symbol_hits.append(line)
    report.append("\n## objdump -t forbidden symbols")
    if objdump_symbol_hits:
        errors.append(f"forbidden objdump symbols present: {len(objdump_symbol_hits)}")
        report.extend(objdump_symbol_hits[:200])
    else:
        report.append("0")

    nm_output = run_tool(["nm", "-C", str(binary)])
    symbol_hits = []
    for line in nm_output.splitlines():
        if any(expr.search(line) for expr in symbol_res):
            symbol_hits.append(line)
    if symbol_hits:
        errors.append(f"forbidden symbols present: {len(symbol_hits)}")
        report.append("\n## forbidden symbols")
        report.extend(symbol_hits[:200])

    disassembly_output = run_tool(["objdump", "-d", str(binary)])
    call_hits = find_forbidden_call_targets(disassembly_output)
    report.append("\n## forbidden call targets")
    if call_hits:
        errors.append(f"forbidden call targets present: {len(call_hits)}")
        report.extend(call_hits[:200])
    else:
        report.append("0")

    nm_sized_output = run_tool(["nm", "-S", "--size-sort", "-C", str(binary)])
    symbols = parse_sized_symbols(nm_sized_output)
    append_forbidden_symbol_prefix_report(report, errors, symbols, forbidden_symbol_prefixes)
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


def object_label(path: Path) -> str:
    return str(path)


def object_undefined_hits(path: Path, output: str, symbol_res: list[re.Pattern[str]]) -> list[str]:
    hits: list[str] = []
    current_object = object_label(path)

    for line in output.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.endswith(":"):
            current_object = stripped[:-1]
            continue
        parts = stripped.split()
        if not parts or parts[0] != "U":
            continue
        if any(expr.search(stripped) for expr in symbol_res):
            hits.append(f"{current_object}: {stripped}")
    return hits


def object_relocation_hits(path: Path, output: str, symbol_res: list[re.Pattern[str]]) -> list[str]:
    hits: list[str] = []
    current_object = object_label(path)
    reloc_expr = re.compile(r"\bR_[A-Za-z0-9_]+\b")

    for line in output.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.endswith(":") and "file format" not in stripped:
            current_object = stripped[:-1]
            continue
        if not reloc_expr.search(stripped):
            continue
        if any(expr.search(stripped) for expr in symbol_res):
            hits.append(f"{current_object}: {stripped}")
    return hits


def check_objects(objects: list[Path]) -> tuple[list[str], str]:
    errors: list[str] = []
    report: list[str] = ["## object relocation contract"]
    symbol_res = [re.compile(pattern) for pattern in FORBIDDEN_OBJECT_SYMBOL_PATTERNS]
    undefined_hits: list[str] = []
    relocation_hits: list[str] = []
    checked = 0

    for raw_path in objects:
        path = raw_path.resolve()
        if not path.is_file():
            errors.append(f"object file not found: {path}")
            continue
        checked += 1
        try:
            undefined_hits.extend(object_undefined_hits(path, run_tool(["nm", "-u", str(path)]), symbol_res))
            relocation_hits.extend(object_relocation_hits(path, run_tool(["objdump", "-dr", str(path)]), symbol_res))
        except RuntimeError as exc:
            errors.append(str(exc))

    report.append(f"objects={checked}")
    report.append(f"forbidden undefined relocatable hits={len(undefined_hits)}")
    report.extend(undefined_hits[:200])
    report.append(f"forbidden relocation hits={len(relocation_hits)}")
    report.extend(relocation_hits[:200])

    if undefined_hits:
        errors.append(f"object forbidden undefined symbols: {len(undefined_hits)}")
    if relocation_hits:
        errors.append(f"object forbidden relocations: {len(relocation_hits)}")

    return errors, "\n".join(report)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--object", type=Path, action="append", default=[])
    parser.add_argument("--link-map", type=Path, action="append", default=[])
    parser.add_argument("--pack", type=Path, action="append", default=[])
    parser.add_argument("--manifest", type=Path, action="append", default=[])
    parser.add_argument(
        "--forbid-pack-archive",
        action="append",
        default=[],
        metavar="NAME[,NAME...]",
        help="fail if any checked pack contains these archive IDs or names",
    )
    parser.add_argument("--include-third-party", action="store_true")
    parser.add_argument(
        "--source-exclude",
        action="append",
        default=[],
        metavar="GLOB",
        help="skip source paths or basenames matching GLOB during source scans",
    )
    parser.add_argument(
        "--source-define",
        action="append",
        default=[],
        metavar="NAME",
        help="treat NAME or NAME=VALUE as defined when stripping simple preprocessor blocks before source scans",
    )
    parser.add_argument(
        "--source-file",
        action="append",
        default=[],
        type=Path,
        metavar="PATH",
        help="scan this source path instead of discovering sources under --root; may be repeated",
    )
    parser.add_argument(
        "--source-manifest",
        action="append",
        default=[],
        type=Path,
        metavar="PATH",
        help="checked-in JSON inventory of sources expected in the selected contract profile",
    )
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
    parser.add_argument(
        "--forbid-symbol-prefix",
        action="append",
        default=[],
        metavar="PREFIX",
        help="fail if any ELF symbol name starts with PREFIX",
    )
    parser.add_argument("--fail-on-source", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    hits = scan_sources(
        root,
        args.include_third_party,
        args.source_exclude,
        parse_source_defines(args.source_define),
        args.source_file,
    )

    errors: list[str] = []
    print("# Embedded Contract Check")
    print(f"root: {root}")

    for source_manifest in args.source_manifest:
        manifest_errors, manifest_report = check_source_manifest(root, source_manifest.resolve(), args.source_file)
        if manifest_report:
            print()
            print(manifest_report)
        errors.extend(manifest_errors)

    source_kinds = ("heap", "decompress", "scratch", "storage", "loose-resource", "mmap-access")
    by_kind: dict[str, list[Hit]] = {kind: [] for kind in source_kinds}
    for hit in hits:
        by_kind.setdefault(hit.kind, []).append(hit)

    for kind in source_kinds:
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
            args.forbid_symbol_prefix,
        )
        errors.extend(binary_errors)
        print()
        print(binary_report)

    if args.object:
        object_errors, object_report = check_objects(args.object)
        errors.extend(object_errors)
        print()
        print(object_report)

    pack_size_budgets = parse_pack_size_budget(args.max_pack_size)
    forbidden_archives = parse_archive_ids(args.forbid_pack_archive)
    for raw_pack_path in args.pack:
        pack_path = raw_pack_path.resolve()
        pack_errors, pack_report = check_pack(pack_path, pack_size_budgets.get(pack_path), forbidden_archives)
        errors.extend(pack_errors)
        if pack_report:
            print()
            print(pack_report)

    for raw_manifest_path in args.manifest:
        manifest_path = raw_manifest_path.resolve()
        manifest_errors, manifest_report = check_manifest(manifest_path)
        errors.extend(manifest_errors)
        if manifest_report:
            print()
            print(manifest_report)

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
