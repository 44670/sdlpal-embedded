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
    parser.add_argument("--include-third-party", action="store_true")
    parser.add_argument("--max", action="append", default=[], metavar="NAME=BYTES")
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

    if errors:
        print("\n## FAIL")
        for error in errors:
            print(f"- {error}")
        return 1

    print("\n## PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
