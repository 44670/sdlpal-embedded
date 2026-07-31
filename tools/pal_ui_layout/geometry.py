"""Deterministic integer geometry primitives for PAL native-screen layouts.

This module deliberately has no rendering or game-state dependencies.  The same
rules can therefore be mirrored by a small C implementation without inheriting
Python-specific floating-point or banker-rounding behaviour.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Literal, Protocol, Sequence


Align = Literal["start", "center", "end"]
Justify = Literal["start", "center", "end", "space-between"]
FlowDirection = Literal["horizontal", "vertical"]


class RectLike(Protocol):
    """Structural bridge to ``profiles.ProfileRect`` and similar records."""

    x: int
    y: int
    width: int
    height: int


def _require_int(name: str, value: int) -> None:
    if not isinstance(value, int) or isinstance(value, bool):
        raise TypeError(f"{name} must be an int")


def _require_nonnegative(name: str, value: int) -> None:
    _require_int(name, value)
    if value < 0:
        raise ValueError(f"{name} must be nonnegative")


def _aligned_offset(available: int, used: int, align: Align) -> int:
    """Return a deterministic offset; center ties go to top/left."""

    if align == "start":
        return 0
    if align == "center":
        return (available - used) // 2
    if align == "end":
        return available - used
    raise ValueError(f"unsupported alignment: {align!r}")


def _distributed_sizes(total: int, count: int) -> tuple[int, ...]:
    """Split *total* into integer tracks, assigning remainder left/top first."""

    _require_nonnegative("total", total)
    if count <= 0:
        raise ValueError("count must be positive")
    quotient, remainder = divmod(total, count)
    return tuple(
        quotient + (1 if index < remainder else 0) for index in range(count)
    )


@dataclass(frozen=True, slots=True)
class Point:
    x: int
    y: int

    def __post_init__(self) -> None:
        _require_int("x", self.x)
        _require_int("y", self.y)

    def translated(self, dx: int, dy: int) -> "Point":
        _require_int("dx", dx)
        _require_int("dy", dy)
        return Point(self.x + dx, self.y + dy)


@dataclass(frozen=True, slots=True)
class Size:
    w: int
    h: int

    def __post_init__(self) -> None:
        _require_nonnegative("w", self.w)
        _require_nonnegative("h", self.h)


@dataclass(frozen=True, slots=True)
class Insets:
    left: int = 0
    top: int = 0
    right: int = 0
    bottom: int = 0

    def __post_init__(self) -> None:
        _require_nonnegative("left", self.left)
        _require_nonnegative("top", self.top)
        _require_nonnegative("right", self.right)
        _require_nonnegative("bottom", self.bottom)


@dataclass(frozen=True, slots=True)
class Rect:
    """A half-open integer rectangle: ``[x, right) x [y, bottom)``."""

    x: int
    y: int
    w: int
    h: int

    def __post_init__(self) -> None:
        _require_int("x", self.x)
        _require_int("y", self.y)
        _require_nonnegative("w", self.w)
        _require_nonnegative("h", self.h)

    @classmethod
    def from_rect_like(cls, rect: RectLike | "Rect" | object) -> "Rect":
        """Convert ProfileRect or another x/y/width/height (or w/h) record."""

        if isinstance(rect, cls):
            return rect
        try:
            x = getattr(rect, "x")
            y = getattr(rect, "y")
            if hasattr(rect, "width"):
                width = getattr(rect, "width")
                height = getattr(rect, "height")
            else:
                width = getattr(rect, "w")
                height = getattr(rect, "h")
        except AttributeError as exc:
            raise TypeError("expected a rectangle-compatible value") from exc
        return cls(x, y, width, height)

    @property
    def left(self) -> int:
        return self.x

    @property
    def top(self) -> int:
        return self.y

    @property
    def right(self) -> int:
        return self.x + self.w

    @property
    def bottom(self) -> int:
        return self.y + self.h

    # ProfileRect spells these fields out.  Providing read-only aliases keeps
    # generic layout consumers source-compatible in both directions.
    @property
    def width(self) -> int:
        return self.w

    @property
    def height(self) -> int:
        return self.h

    @property
    def center(self) -> Point:
        return Point(self.x + self.w // 2, self.y + self.h // 2)

    @property
    def size(self) -> Size:
        return Size(self.w, self.h)

    @property
    def area(self) -> int:
        return self.w * self.h

    @property
    def is_empty(self) -> bool:
        return self.w == 0 or self.h == 0

    def translated(self, dx: int, dy: int) -> "Rect":
        _require_int("dx", dx)
        _require_int("dy", dy)
        return Rect(self.x + dx, self.y + dy, self.w, self.h)

    def inset(self, insets: Insets) -> "Rect":
        width = self.w - insets.left - insets.right
        height = self.h - insets.top - insets.bottom
        if width < 0 or height < 0:
            raise ValueError("insets exceed rectangle dimensions")
        return Rect(
            self.x + insets.left,
            self.y + insets.top,
            width,
            height,
        )

    def expanded(self, amount: int) -> "Rect":
        _require_nonnegative("amount", amount)
        return Rect(
            self.x - amount,
            self.y - amount,
            self.w + amount * 2,
            self.h + amount * 2,
        )

    def contains_point(self, point: Point) -> bool:
        return (
            self.left <= point.x < self.right
            and self.top <= point.y < self.bottom
        )

    def contains_rect(self, other: "Rect") -> bool:
        if other.is_empty:
            return (
                self.left <= other.left <= self.right
                and self.top <= other.top <= self.bottom
            )
        return (
            self.left <= other.left
            and other.right <= self.right
            and self.top <= other.top
            and other.bottom <= self.bottom
        )

    def intersects(self, other: "Rect") -> bool:
        return (
            self.left < other.right
            and other.left < self.right
            and self.top < other.bottom
            and other.top < self.bottom
        )

    def intersection(self, other: "Rect") -> "Rect":
        left = max(self.left, other.left)
        top = max(self.top, other.top)
        right = max(left, min(self.right, other.right))
        bottom = max(top, min(self.bottom, other.bottom))
        return Rect(left, top, right - left, bottom - top)

    def union(self, other: "Rect") -> "Rect":
        if self.is_empty:
            return other
        if other.is_empty:
            return self
        left = min(self.left, other.left)
        top = min(self.top, other.top)
        right = max(self.right, other.right)
        bottom = max(self.bottom, other.bottom)
        return Rect(left, top, right - left, bottom - top)


def union_rects(rects: Iterable[Rect]) -> Rect | None:
    result: Rect | None = None
    for rect in rects:
        result = rect if result is None else result.union(rect)
    return result


def safe_area(width: int, height: int, insets: Insets = Insets()) -> Rect:
    """Return the drawable rectangle after applying display edge insets."""

    _require_nonnegative("width", width)
    _require_nonnegative("height", height)
    return Rect(0, 0, width, height).inset(insets)


def as_rect(value: RectLike | Rect | object) -> Rect:
    """Public compatibility adapter for ProfileRect and solver.Rect."""

    return Rect.from_rect_like(value)


@dataclass(frozen=True, slots=True)
class LayoutResult:
    rects: tuple[Rect, ...]
    content_bounds: Rect
    overflow_indices: tuple[int, ...]

    @property
    def fits(self) -> bool:
        return not self.overflow_indices


def _line_offsets(
    available: int,
    used: int,
    item_count: int,
    base_gap: int,
    justify: Justify,
) -> tuple[int, tuple[int, ...]]:
    if justify != "space-between":
        return (
            _aligned_offset(available, used, justify),
            (base_gap,) * max(0, item_count - 1),
        )
    if item_count <= 1:
        return (0, ())
    item_extent = used - base_gap * (item_count - 1)
    free_for_gaps = available - item_extent
    if available < used:
        return (0, (base_gap,) * (item_count - 1))
    return (0, _distributed_sizes(free_for_gaps, item_count - 1))


def flow(
    container: Rect,
    items: Sequence[Size],
    *,
    direction: FlowDirection = "horizontal",
    gap: int = 0,
    line_gap: int | None = None,
    wrap: bool = True,
    justify: Justify = "start",
    align: Align = "start",
) -> LayoutResult:
    """Lay out variably-sized items in deterministic rows or columns.

    Wrapping is greedy.  Centering uses integer floor division, and
    ``space-between`` assigns any remainder to the earliest gaps.  Rectangles
    are still returned for oversized items; ``overflow_indices`` identifies
    every result not fully contained by *container*.
    """

    if direction not in ("horizontal", "vertical"):
        raise ValueError(f"unsupported flow direction: {direction!r}")
    _require_nonnegative("gap", gap)
    if line_gap is None:
        line_gap = gap
    _require_nonnegative("line_gap", line_gap)
    if justify not in ("start", "center", "end", "space-between"):
        raise ValueError(f"unsupported justification: {justify!r}")
    if align not in ("start", "center", "end"):
        raise ValueError(f"unsupported alignment: {align!r}")

    horizontal = direction == "horizontal"
    main_limit = container.w if horizontal else container.h

    # Each entry is (original index, main extent, cross extent).
    lines: list[list[tuple[int, int, int]]] = []
    current: list[tuple[int, int, int]] = []
    current_main = 0
    for index, size in enumerate(items):
        main = size.w if horizontal else size.h
        cross = size.h if horizontal else size.w
        needed = main if not current else gap + main
        if wrap and current and current_main + needed > main_limit:
            lines.append(current)
            current = []
            current_main = 0
            needed = main
        current.append((index, main, cross))
        current_main += needed
    if current:
        lines.append(current)

    if not lines:
        empty = Rect(container.x, container.y, 0, 0)
        return LayoutResult((), empty, ())

    placed: list[Rect | None] = [None] * len(items)
    cross_cursor = 0
    for line in lines:
        line_cross = max(entry[2] for entry in line)
        used_main = sum(entry[1] for entry in line) + gap * (len(line) - 1)
        main_cursor, actual_gaps = _line_offsets(
            main_limit, used_main, len(line), gap, justify
        )
        for line_index, (item_index, main, cross) in enumerate(line):
            cross_offset = _aligned_offset(line_cross, cross, align)
            if horizontal:
                rect = Rect(
                    container.x + main_cursor,
                    container.y + cross_cursor + cross_offset,
                    main,
                    cross,
                )
            else:
                rect = Rect(
                    container.x + cross_cursor + cross_offset,
                    container.y + main_cursor,
                    cross,
                    main,
                )
            placed[item_index] = rect
            main_cursor += main
            if line_index < len(actual_gaps):
                main_cursor += actual_gaps[line_index]
        cross_cursor += line_cross + line_gap

    rects = tuple(rect for rect in placed if rect is not None)
    bounds = union_rects(rects)
    assert bounds is not None
    overflow = tuple(
        index
        for index, rect in enumerate(rects)
        if not container.contains_rect(rect)
    )
    return LayoutResult(rects, bounds, overflow)


def grid(
    container: Rect,
    count: int,
    *,
    columns: int,
    rows: int | None = None,
    gap_x: int = 0,
    gap_y: int = 0,
    cell_size: Size | None = None,
    horizontal_align: Align = "start",
    vertical_align: Align = "start",
) -> LayoutResult:
    """Lay out *count* cells in a row-major grid.

    With no explicit ``cell_size``, the available extent is divided among the
    tracks and remainders go to the left/top tracks.  With an explicit size,
    the whole grid block can be aligned inside the container.
    """

    _require_nonnegative("count", count)
    if columns <= 0:
        raise ValueError("columns must be positive")
    _require_nonnegative("gap_x", gap_x)
    _require_nonnegative("gap_y", gap_y)
    if horizontal_align not in ("start", "center", "end"):
        raise ValueError(f"unsupported alignment: {horizontal_align!r}")
    if vertical_align not in ("start", "center", "end"):
        raise ValueError(f"unsupported alignment: {vertical_align!r}")

    required_rows = (count + columns - 1) // columns
    if rows is None:
        rows = required_rows
    if rows <= 0:
        if count == 0:
            empty = Rect(container.x, container.y, 0, 0)
            return LayoutResult((), empty, ())
        raise ValueError("rows must be positive")
    if count > columns * rows:
        raise ValueError("grid does not have enough cells")
    if count == 0:
        empty = Rect(container.x, container.y, 0, 0)
        return LayoutResult((), empty, ())

    if cell_size is None:
        width_for_cells = container.w - gap_x * (columns - 1)
        height_for_cells = container.h - gap_y * (rows - 1)
        if width_for_cells < 0 or height_for_cells < 0:
            raise ValueError("grid gaps exceed container dimensions")
        column_widths = _distributed_sizes(width_for_cells, columns)
        row_heights = _distributed_sizes(height_for_cells, rows)
        origin_x = container.x
        origin_y = container.y
    else:
        block_w = cell_size.w * columns + gap_x * (columns - 1)
        block_h = cell_size.h * rows + gap_y * (rows - 1)
        column_widths = (cell_size.w,) * columns
        row_heights = (cell_size.h,) * rows
        origin_x = container.x + _aligned_offset(
            container.w, block_w, horizontal_align
        )
        origin_y = container.y + _aligned_offset(
            container.h, block_h, vertical_align
        )

    column_x: list[int] = []
    cursor = origin_x
    for width in column_widths:
        column_x.append(cursor)
        cursor += width + gap_x
    row_y: list[int] = []
    cursor = origin_y
    for height in row_heights:
        row_y.append(cursor)
        cursor += height + gap_y

    rects = tuple(
        Rect(
            column_x[index % columns],
            row_y[index // columns],
            column_widths[index % columns],
            row_heights[index // columns],
        )
        for index in range(count)
    )
    bounds = union_rects(rects)
    assert bounds is not None
    overflow = tuple(
        index
        for index, rect in enumerate(rects)
        if not container.contains_rect(rect)
    )
    return LayoutResult(rects, bounds, overflow)
