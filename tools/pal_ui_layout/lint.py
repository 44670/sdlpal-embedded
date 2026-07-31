"""Lint migrated UI code for hand-written physical-display coordinates.

The layout compiler, rather than target C code, owns display dimensions,
anchors, and concrete ``PAL_XY`` coordinates.  This module deliberately has
no "scan the repository" mode: callers provide an explicit, declarative list
of migrated files and (optionally) the line ranges or marked blocks in those
files that are in scope.

Generated headers and test-vector sources can be present in the declaration,
but must be labelled with the corresponding ``role``.  Small test-vector
blocks embedded in a migrated source can instead be excluded with
``test_vector_ranges`` or ``test_vector_blocks``.
"""

from __future__ import annotations

import argparse
from bisect import bisect_right
from dataclasses import dataclass
import json
from pathlib import Path
import re
import sys
from typing import Iterable, Mapping, Sequence


FORBIDDEN_COORDINATE_VALUES = frozenset((128, 135, 160, 200, 240, 320))
ALLOW_ANNOTATION = "PAL_UI_LAYOUT_LINT_ALLOW"
TEST_VECTOR_BEGIN = "PAL_UI_LAYOUT_TEST_VECTOR_BEGIN"
TEST_VECTOR_END = "PAL_UI_LAYOUT_TEST_VECTOR_END"

_INTEGER_LITERAL_RE = re.compile(
    r"(?<![A-Za-z0-9_.])"
    r"(?P<literal>(?:0|[1-9][0-9]*)(?:[uUlL]+)?)"
    r"(?![A-Za-z0-9_.])"
)
_PAL_XY_RE = re.compile(r"\bPAL_XY\s*\(")
_C_INTEGER_RE = re.compile(
    r"(?:0[xX][0-9A-Fa-f]+|0[bB][01]+|0[0-7]+|[1-9][0-9]*|0)"
    r"(?:[uUlL]+)?"
)
_CONSTANT_EXPRESSION_PUNCTUATION_RE = re.compile(r"^[\s()+\-*/%<>&|^~!?:]*$")


class LintConfigurationError(ValueError):
    """Raised when a declarative scan target is ambiguous or malformed."""


@dataclass(frozen=True, order=True)
class LineRange:
    """Inclusive one-based line range."""

    first: int
    last: int

    def __post_init__(self) -> None:
        if self.first < 1 or self.last < self.first:
            raise LintConfigurationError(
                f"invalid line range {self.first}:{self.last}"
            )

    def lines(self, line_count: int) -> range:
        if self.first > line_count:
            return range(0)
        return range(self.first, min(self.last, line_count) + 1)


@dataclass(frozen=True, order=True)
class MarkerBlock:
    """All source lines strictly between paired marker strings."""

    begin: str
    end: str

    def __post_init__(self) -> None:
        if not self.begin or not self.end:
            raise LintConfigurationError("marker strings must not be empty")
        if self.begin == self.end:
            raise LintConfigurationError("begin and end markers must differ")


@dataclass(frozen=True)
class ScanTarget:
    """One explicitly selected file.

    ``role`` is one of:

    - ``"migrated"``: lint selected source lines;
    - ``"generated"``: generated tables/headers are intentionally exempt;
    - ``"test_vector"``: deterministic coordinate fixtures are exempt.

    With no ``line_ranges`` or ``marker_blocks``, every line in a migrated
    file is selected.  If either selector collection is non-empty, their union
    is selected.  Test-vector selectors are then subtracted.
    """

    path: str
    role: str = "migrated"
    line_ranges: tuple[LineRange, ...] = ()
    marker_blocks: tuple[MarkerBlock, ...] = ()
    test_vector_ranges: tuple[LineRange, ...] = ()
    test_vector_blocks: tuple[MarkerBlock, ...] = ()

    def __post_init__(self) -> None:
        if not self.path:
            raise LintConfigurationError("scan target path must not be empty")
        if self.role not in {"migrated", "generated", "test_vector"}:
            raise LintConfigurationError(
                f"invalid role {self.role!r} for {self.path!r}"
            )


# This is deliberately an inventory, not a glob.  Add a source only after its
# physical layout coordinates are owned by generated data.  Test fixtures and
# generated headers are listed to make their exemption visible in review.
CARDPUTER_LINT_TARGETS = (
    ScanTarget("esp32s3/engine_bridge/pal_engine_target_video.c"),
    ScanTarget("esp32s3/main/cardputer_extreme_scaler.c"),
    ScanTarget("esp32s3/main/cardputer_extreme_scaler.h"),
    ScanTarget("esp32s3/main/cardputer_extreme_board.h"),
    ScanTarget(
        "esp32s3/main/cardputer_extreme_board.c",
        marker_blocks=(
            MarkerBlock(
                "PAL_UI_LAYOUT_MIGRATED_BEGIN presentation",
                "PAL_UI_LAYOUT_MIGRATED_END presentation",
            ),
            MarkerBlock(
                "PAL_UI_LAYOUT_MIGRATED_BEGIN loading",
                "PAL_UI_LAYOUT_MIGRATED_END loading",
            ),
        ),
    ),
    ScanTarget("embedded/pal_ui_layout_runtime.c"),
    ScanTarget("embedded/pal_ui_layout_runtime.h"),
    ScanTarget("embedded/pal_ui_rle_downsample.c"),
    ScanTarget("embedded/pal_ui_rle_downsample.h"),
    ScanTarget(
        "esp32s3/main/generated/pal_ui_layout_240x135.h",
        role="generated",
    ),
    ScanTarget(
        "esp32s3/main/generated/pal_ui_layout_160x128.h",
        role="generated",
    ),
    ScanTarget(
        "esp32s3/native_shim/cardputer_extreme_scaler_test.c",
        role="test_vector",
    ),
    ScanTarget(
        "embedded/pal_ui_rle_downsample_smoke.c",
        role="test_vector",
    ),
)


@dataclass(frozen=True)
class LintConfig:
    """Complete lint declaration.

    The empty default is intentional: it is safe for a caller that has not yet
    declared which UI files have migrated to generated layouts.
    """

    targets: tuple[ScanTarget, ...] = ()
    forbidden_values: frozenset[int] = FORBIDDEN_COORDINATE_VALUES
    allow_annotation: str = ALLOW_ANNOTATION
    implicit_test_vector_markers: MarkerBlock | None = MarkerBlock(
        TEST_VECTOR_BEGIN,
        TEST_VECTOR_END,
    )

    def __post_init__(self) -> None:
        if not self.allow_annotation:
            raise LintConfigurationError("allow annotation must not be empty")
        if any(value < 0 for value in self.forbidden_values):
            raise LintConfigurationError("forbidden values must be non-negative")


@dataclass(frozen=True, order=True)
class Diagnostic:
    path: str
    line: int
    column: int
    code: str
    message: str

    def __str__(self) -> str:
        return (
            f"{self.path}:{self.line}:{self.column}: "
            f"{self.code}: {self.message}"
        )


def _line_ranges_from_values(values: object, field: str) -> tuple[LineRange, ...]:
    if values is None:
        return ()
    if not isinstance(values, Sequence) or isinstance(values, (str, bytes)):
        raise LintConfigurationError(f"{field} must be a sequence")
    result: list[LineRange] = []
    for item in values:
        if (
            not isinstance(item, Sequence)
            or isinstance(item, (str, bytes))
            or len(item) != 2
        ):
            raise LintConfigurationError(
                f"{field} entries must be [first, last] pairs"
            )
        result.append(LineRange(int(item[0]), int(item[1])))
    return tuple(result)


def _marker_blocks_from_values(
    values: object,
    field: str,
) -> tuple[MarkerBlock, ...]:
    if values is None:
        return ()
    if not isinstance(values, Sequence) or isinstance(values, (str, bytes)):
        raise LintConfigurationError(f"{field} must be a sequence")
    result: list[MarkerBlock] = []
    for item in values:
        if not isinstance(item, Mapping):
            raise LintConfigurationError(
                f"{field} entries must contain begin/end strings"
            )
        result.append(MarkerBlock(str(item["begin"]), str(item["end"])))
    return tuple(result)


def config_from_mapping(value: Mapping[str, object]) -> LintConfig:
    """Build a config from JSON-compatible data.

    This helper keeps the check driver's migrated-file inventory declarative;
    it performs no path discovery and supplies no repository-wide defaults.
    """

    raw_targets = value.get("targets", ())
    if not isinstance(raw_targets, Sequence) or isinstance(
        raw_targets, (str, bytes)
    ):
        raise LintConfigurationError("targets must be a sequence")

    targets: list[ScanTarget] = []
    for raw in raw_targets:
        if not isinstance(raw, Mapping):
            raise LintConfigurationError("each target must be a mapping")
        try:
            path = str(raw["path"])
        except KeyError as exc:
            raise LintConfigurationError("target is missing path") from exc
        targets.append(
            ScanTarget(
                path=path,
                role=str(raw.get("role", "migrated")),
                line_ranges=_line_ranges_from_values(
                    raw.get("line_ranges"), "line_ranges"
                ),
                marker_blocks=_marker_blocks_from_values(
                    raw.get("marker_blocks"), "marker_blocks"
                ),
                test_vector_ranges=_line_ranges_from_values(
                    raw.get("test_vector_ranges"), "test_vector_ranges"
                ),
                test_vector_blocks=_marker_blocks_from_values(
                    raw.get("test_vector_blocks"), "test_vector_blocks"
                ),
            )
        )

    forbidden = value.get("forbidden_values", FORBIDDEN_COORDINATE_VALUES)
    if not isinstance(forbidden, Iterable) or isinstance(forbidden, (str, bytes)):
        raise LintConfigurationError("forbidden_values must be an iterable")
    return LintConfig(
        targets=tuple(targets),
        forbidden_values=frozenset(int(item) for item in forbidden),
        allow_annotation=str(value.get("allow_annotation", ALLOW_ANNOTATION)),
    )


def _marker_lines(
    lines: Sequence[str],
    block: MarkerBlock,
    path: str,
) -> set[int]:
    selected: set[int] = set()
    begin_line: int | None = None
    found = False

    for line_number, line in enumerate(lines, 1):
        has_begin = block.begin in line
        has_end = block.end in line
        if has_begin:
            if begin_line is not None:
                raise LintConfigurationError(
                    f"{path}:{line_number}: nested marker {block.begin!r}"
                )
            begin_line = line_number
            found = True
        if has_end:
            if begin_line is None:
                raise LintConfigurationError(
                    f"{path}:{line_number}: unmatched marker {block.end!r}"
                )
            selected.update(range(begin_line + 1, line_number))
            begin_line = None

    if begin_line is not None:
        raise LintConfigurationError(
            f"{path}:{begin_line}: marker {block.begin!r} has no "
            f"{block.end!r}"
        )
    if not found:
        raise LintConfigurationError(
            f"{path}: marker {block.begin!r} was not found"
        )
    return selected


def _selected_lines(
    target: ScanTarget,
    lines: Sequence[str],
    config: LintConfig,
) -> set[int]:
    line_count = len(lines)
    if target.line_ranges or target.marker_blocks:
        selected: set[int] = set()
        for span in target.line_ranges:
            selected.update(span.lines(line_count))
        for block in target.marker_blocks:
            selected.update(_marker_lines(lines, block, target.path))
    else:
        selected = set(range(1, line_count + 1))

    for span in target.test_vector_ranges:
        selected.difference_update(span.lines(line_count))
    for block in target.test_vector_blocks:
        selected.difference_update(_marker_lines(lines, block, target.path))

    implicit = config.implicit_test_vector_markers
    if implicit is not None and any(implicit.begin in line for line in lines):
        selected.difference_update(_marker_lines(lines, implicit, target.path))
    return selected


def _mask_source(
    text: str,
    python_comments: bool,
    allow_annotation: str,
) -> tuple[str, set[int]]:
    """Replace comments and literals with spaces while preserving positions."""

    output = list(text)
    allow_lines: set[int] = set()
    length = len(text)
    index = 0
    line = 1

    def mask_and_record_comment(first: int, last: int) -> None:
        nonlocal line
        comment_line = line
        comment_text: list[str] = []
        for offset in range(first, last):
            char = text[offset]
            comment_text.append(char)
            if char == "\n":
                if allow_annotation in "".join(comment_text):
                    allow_lines.add(comment_line)
                comment_text.clear()
                comment_line += 1
                line += 1
            else:
                output[offset] = " "
        if comment_text and allow_annotation in "".join(comment_text):
            allow_lines.add(comment_line)

    while index < length:
        char = text[index]
        next_char = text[index + 1] if index + 1 < length else ""

        if char == "\n":
            line += 1
            index += 1
            continue

        if char == "/" and next_char == "/":
            end = text.find("\n", index + 2)
            if end < 0:
                end = length
            mask_and_record_comment(index, end)
            index = end
            continue

        if char == "/" and next_char == "*":
            end_marker = text.find("*/", index + 2)
            end = length if end_marker < 0 else end_marker + 2
            mask_and_record_comment(index, end)
            index = end
            continue

        if python_comments and char == "#":
            end = text.find("\n", index + 1)
            if end < 0:
                end = length
            mask_and_record_comment(index, end)
            index = end
            continue

        if char in {'"', "'"}:
            quote = char
            output[index] = " "
            index += 1
            while index < length:
                char = text[index]
                if char == "\n":
                    line += 1
                    index += 1
                    continue
                output[index] = " "
                if char == "\\" and index + 1 < length:
                    index += 1
                    if text[index] == "\n":
                        line += 1
                    else:
                        output[index] = " "
                    index += 1
                    continue
                index += 1
                if char == quote:
                    break
            continue

        index += 1

    return "".join(output), allow_lines


def _literal_value(literal: str) -> int:
    core = re.sub(r"[uUlL]+$", "", literal)
    return int(core, 10)


def _constant_integer_expression(expression: str) -> bool:
    """Return whether an expression is made solely from integers/operators."""

    if _C_INTEGER_RE.search(expression) is None:
        return False
    remainder = _C_INTEGER_RE.sub("", expression)
    return _CONSTANT_EXPRESSION_PUNCTUATION_RE.fullmatch(remainder) is not None


def _call_arguments(
    source: str,
    opening_parenthesis: int,
) -> tuple[tuple[str, ...], int] | None:
    depth = 1
    start = opening_parenthesis + 1
    arguments: list[str] = []
    index = start
    while index < len(source):
        char = source[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                arguments.append(source[start:index])
                return tuple(arguments), index + 1
        elif char == "," and depth == 1:
            arguments.append(source[start:index])
            start = index + 1
        index += 1
    return None


def _line_starts(text: str) -> tuple[int, ...]:
    starts = [0]
    starts.extend(index + 1 for index, char in enumerate(text) if char == "\n")
    return tuple(starts)


def _position(starts: Sequence[int], offset: int) -> tuple[int, int]:
    line_index = bisect_right(starts, offset) - 1
    return line_index + 1, offset - starts[line_index] + 1


def _display_path(root: Path, path: Path, declared: str) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return Path(declared).as_posix()


def _lint_target(
    root: Path,
    target: ScanTarget,
    config: LintConfig,
) -> list[Diagnostic]:
    if target.role != "migrated":
        return []

    path = Path(target.path)
    if not path.is_absolute():
        path = root / path
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise LintConfigurationError(
            f"cannot read scan target {target.path!r}: {exc}"
        ) from exc

    raw_lines = text.splitlines()
    selected = _selected_lines(target, raw_lines, config)
    masked, allow_lines = _mask_source(
        text,
        path.suffix.lower() == ".py",
        config.allow_annotation,
    )
    starts = _line_starts(masked)
    shown_path = _display_path(root, path, target.path)
    diagnostics: list[Diagnostic] = []
    covered_calls: list[tuple[int, int]] = []

    for match in _PAL_XY_RE.finditer(masked):
        call = _call_arguments(masked, match.end() - 1)
        if call is None:
            continue
        arguments, end = call
        if len(arguments) < 2:
            continue
        constant_axes = [
            name
            for name, expression in zip(("x", "y"), arguments[:2])
            if _constant_integer_expression(expression)
        ]
        if not constant_axes:
            continue
        line, column = _position(starts, match.start())
        if line not in selected:
            continue
        covered_calls.append((match.start(), end))
        if line in allow_lines:
            continue
        axes = " and ".join(constant_axes)
        diagnostics.append(
            Diagnostic(
                path=shown_path,
                line=line,
                column=column,
                code="PUL002",
                message=(
                    f"PAL_XY has hand-written constant {axes} coordinate; "
                    "use generated layout fields/macros"
                ),
            )
        )

    for match in _INTEGER_LITERAL_RE.finditer(masked):
        value = _literal_value(match.group("literal"))
        if value not in config.forbidden_values:
            continue
        if any(first <= match.start() < last for first, last in covered_calls):
            continue
        line, column = _position(starts, match.start())
        if line not in selected or line in allow_lines:
            continue
        diagnostics.append(
            Diagnostic(
                path=shown_path,
                line=line,
                column=column,
                code="PUL001",
                message=(
                    f"hand-written coordinate/dimension literal "
                    f"{match.group('literal')} resolves to {value}; "
                    "use a generated layout field/macro"
                ),
            )
        )

    return diagnostics


def lint_config(
    root: str | Path,
    config: LintConfig,
) -> tuple[Diagnostic, ...]:
    """Lint exactly the files declared by ``config``."""

    root_path = Path(root)
    diagnostics: list[Diagnostic] = []
    for target in config.targets:
        diagnostics.extend(_lint_target(root_path, target, config))
    return tuple(sorted(diagnostics))


def format_diagnostics(diagnostics: Iterable[Diagnostic]) -> str:
    """Return deterministic compiler-style diagnostics, with a final newline."""

    lines = [str(item) for item in sorted(diagnostics)]
    return "".join(f"{line}\n" for line in lines)


def cardputer_lint_config() -> LintConfig:
    """Return the reviewed Cardputer migration inventory."""

    return LintConfig(targets=CARDPUTER_LINT_TARGETS)


def load_config(path: str | Path) -> LintConfig:
    """Load a declarative JSON configuration for a downstream migration."""

    config_path = Path(path)
    try:
        value = json.loads(config_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise LintConfigurationError(
            f"cannot load lint config {config_path}: {exc}"
        ) from exc
    if not isinstance(value, Mapping):
        raise LintConfigurationError("lint config root must be an object")
    return config_from_mapping(value)


def _argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "reject hand-written physical coordinates in an explicit "
            "migrated-UI inventory"
        )
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path.cwd(),
        help="repository root used to resolve target paths (default: cwd)",
    )
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument(
        "--profile",
        choices=("cardputer",),
        help="use one reviewed built-in target inventory",
    )
    source.add_argument(
        "--config",
        type=Path,
        help="load an explicit JSON target inventory",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entry point.  A profile/config is mandatory by design."""

    arguments = _argument_parser().parse_args(argv)
    try:
        config = (
            cardputer_lint_config()
            if arguments.profile == "cardputer"
            else load_config(arguments.config)
        )
        diagnostics = lint_config(arguments.root, config)
    except LintConfigurationError as exc:
        print(f"pal-ui-layout-lint: configuration error: {exc}", file=sys.stderr)
        return 2

    if diagnostics:
        sys.stdout.write(format_diagnostics(diagnostics))
        return 1

    role_counts = {
        role: sum(target.role == role for target in config.targets)
        for role in ("migrated", "generated", "test_vector")
    }
    print(
        "pal-ui-layout-lint: PASS "
        f"migrated={role_counts['migrated']} "
        f"generated={role_counts['generated']} "
        f"test_vector={role_counts['test_vector']}"
    )
    return 0


__all__ = [
    "ALLOW_ANNOTATION",
    "CARDPUTER_LINT_TARGETS",
    "Diagnostic",
    "FORBIDDEN_COORDINATE_VALUES",
    "LineRange",
    "LintConfig",
    "LintConfigurationError",
    "MarkerBlock",
    "ScanTarget",
    "TEST_VECTOR_BEGIN",
    "TEST_VECTOR_END",
    "cardputer_lint_config",
    "config_from_mapping",
    "format_diagnostics",
    "load_config",
    "lint_config",
    "main",
]


if __name__ == "__main__":
    raise SystemExit(main())
