#!/usr/bin/env python3
"""Deterministic, integer-only primitives for small-screen PAL UI layouts.

This module deliberately does not know how a dialog or an equipment screen is
drawn.  ``screens.py`` produces a small, fixed set of candidates and this
module validates and ranks them.  Keeping the solver at this level makes the
rules usable by a host-side manifest/header generator without importing SDL,
Pillow, or any target code.

All geometry and scores are integers.  Asset scaling uses unsigned Q16.16
values plus exact source/destination extents; a target can therefore reproduce
the pixel-centre sampler without floating point or host-specific rounding.
"""

from __future__ import annotations

import bisect
import math
from dataclasses import dataclass, field
from enum import Enum
from typing import Iterable, Mapping, Sequence


Q16_ONE = 1 << 16
FUSION_PIXEL_FONT_PX = 10
# Compatibility spelling for callers that only need the hard readability gate.
MIN_READABLE_FONT_PX = FUSION_PIXEL_FONT_PX


class LayoutMode(str, Enum):
    """The only candidate families understood by the fixed scorer."""

    FULL = "full"
    COMPACT = "compact"
    SINGLE_COLUMN = "single_column"
    PAGED = "paged"
    TEXT_ONLY = "text_only"


MODE_ORDER = (
    LayoutMode.FULL,
    LayoutMode.COMPACT,
    LayoutMode.SINGLE_COLUMN,
    LayoutMode.PAGED,
    LayoutMode.TEXT_ONLY,
)

# These weights are part of the layout contract.  Do not tune them per screen:
# a manifest must be reproducible without knowing which host generated it.
MODE_SCORE: Mapping[LayoutMode, int] = {
    LayoutMode.FULL: 5_000,
    LayoutMode.COMPACT: 4_400,
    LayoutMode.SINGLE_COLUMN: 3_800,
    LayoutMode.PAGED: 3_200,
    LayoutMode.TEXT_ONLY: 2_600,
}
FONT_SCORE_PER_PX = 80
EXTRA_PAGE_PENALTY = 120
DECORATION_SCORE = 12
FOCUS_CLEARANCE_SCORE = 4
MAX_SCORED_FOCUS_CLEARANCE = 32


class SamplingPolicy(str, Enum):
    """Sampler names emitted into the deterministic asset manifest."""

    NONE = "none"
    NEAREST_CENTER = "nearest_center"


class AssetSpace(str, Enum):
    """Where an asset transform is applied."""

    OMITTED = "omitted"
    UI = "ui"
    LEGACY_STAGE = "legacy_stage"


class MissingGlyphError(ValueError):
    """Raised before solving when FONT10 lacks a required codepoint."""

    def __init__(self, codepoint: int):
        super().__init__(f"FONT10 has no glyph U+{codepoint:04X}")
        self.codepoint = codepoint


@dataclass(frozen=True, slots=True)
class FontMetrics:
    """Frozen advances extracted from Fusion Pixel Font 10px.

    The BDF/font conversion tool is expected to construct this object from the
    actual glyph set.  There is intentionally no guessed/default advance:
    missing glyphs are an input error, not a reason for host-dependent layout.
    """

    advances: tuple[tuple[int, int], ...]
    line_height: int
    family: str = "fusion-pixel-10"
    pixel_height: int = FUSION_PIXEL_FONT_PX
    cell_width: int = FUSION_PIXEL_FONT_PX
    ascent: int = FUSION_PIXEL_FONT_PX
    descent: int = 0

    def __post_init__(self) -> None:
        if self.pixel_height != FUSION_PIXEL_FONT_PX:
            raise ValueError(
                "layout font must be Fusion Pixel Font at exactly 10px"
            )
        if self.line_height < self.pixel_height:
            raise ValueError("FONT10 line height is below its pixel height")
        if self.cell_width != FUSION_PIXEL_FONT_PX:
            raise ValueError("FONT10 cell width must be exactly 10px")
        if (
            self.ascent < 0
            or self.descent < 0
            or self.ascent + self.descent > self.line_height
        ):
            raise ValueError("FONT10 ascent/descent do not fit line height")
        codepoints = tuple(codepoint for codepoint, _ in self.advances)
        if codepoints != tuple(sorted(codepoints)) or len(codepoints) != len(
            set(codepoints)
        ):
            raise ValueError("FONT10 advances must be sorted and unique")
        if any(
            codepoint < 0 or codepoint > 0x10FFFF or advance < 0
            for codepoint, advance in self.advances
        ):
            raise ValueError("FONT10 advance table contains an invalid entry")

    @classmethod
    def from_pairs(
        cls,
        advances: Iterable[tuple[int, int]],
        *,
        line_height: int,
        family: str = "fusion-pixel-10",
        ascent: int | None = None,
        descent: int | None = None,
    ) -> "FontMetrics":
        if ascent is None and descent is None:
            ascent = FUSION_PIXEL_FONT_PX
            descent = line_height - ascent
        elif ascent is None:
            assert descent is not None
            ascent = line_height - descent
        elif descent is None:
            descent = line_height - ascent
        return cls(
            tuple(sorted(advances)),
            line_height=line_height,
            family=family,
            ascent=ascent,
            descent=descent,
        )

    @classmethod
    def from_font10(
        cls,
        font: object,
        *,
        family: str = "fusion-pixel-10",
    ) -> "FontMetrics":
        """Freeze advances and baseline metrics from ``font.Font10``.

        This deliberately uses a tiny structural interface instead of
        importing ``font.py``.  The font builder can therefore depend on no
        solver code, while callers still get a direct, lossless adapter from a
        parsed FONT10 image.
        """

        try:
            glyphs = tuple(getattr(font, "glyphs"))
            ascent = int(getattr(font, "ascent"))
            descent = int(getattr(font, "descent"))
            pairs = tuple(
                (int(getattr(glyph, "codepoint")), int(getattr(glyph, "advance")))
                for glyph in glyphs
            )
        except (AttributeError, TypeError, ValueError) as error:
            raise TypeError("font is not a parsed FONT10 object") from error
        return cls.from_pairs(
            pairs,
            line_height=ascent + descent,
            family=family,
            ascent=ascent,
            descent=descent,
        )

    @classmethod
    def from_bdf(
        cls,
        font: object,
        codepoints: Iterable[int],
        *,
        family: str = "fusion-pixel-10",
    ) -> "FontMetrics":
        """Freeze the requested advances directly from ``font.BdfFont``."""

        try:
            glyphs = getattr(font, "glyphs")
            ascent = int(getattr(font, "ascent"))
            descent = int(getattr(font, "descent"))
            pairs = []
            for codepoint in sorted(set(codepoints)):
                try:
                    glyph = glyphs[codepoint]
                except (KeyError, TypeError):
                    raise MissingGlyphError(codepoint) from None
                pairs.append(
                    (codepoint, int(getattr(glyph, "dwidth_x")))
                )
        except MissingGlyphError:
            raise
        except (AttributeError, TypeError, ValueError) as error:
            raise TypeError("font is not a parsed BDF object") from error
        return cls.from_pairs(
            pairs,
            line_height=ascent + descent,
            family=family,
            ascent=ascent,
            descent=descent,
        )

    def advance_codepoint(self, codepoint: int) -> int:
        index = bisect.bisect_left(self.advances, (codepoint, -1))
        if (
            index >= len(self.advances)
            or self.advances[index][0] != codepoint
        ):
            raise MissingGlyphError(codepoint)
        return self.advances[index][1]

    def advance(self, character: str) -> int:
        if len(character) != 1:
            raise ValueError("glyph advance requires exactly one character")
        return self.advance_codepoint(ord(character))

    def require_texts(self, texts: Iterable[str]) -> None:
        for text in texts:
            for character in text:
                self.advance(character)

    def to_manifest(self) -> dict[str, object]:
        return {
            "family": self.family,
            "pixel_height": self.pixel_height,
            "cell_width": self.cell_width,
            "cell_height": self.pixel_height,
            "ascent": self.ascent,
            "descent": self.descent,
            "line_height": self.line_height,
            "glyph_count": len(self.advances),
        }


@dataclass(frozen=True, slots=True)
class Rect:
    x: int
    y: int
    w: int
    h: int

    @property
    def right(self) -> int:
        return self.x + self.w

    @property
    def bottom(self) -> int:
        return self.y + self.h

    @property
    def center_x(self) -> int:
        return self.x + self.w // 2

    @property
    def center_y(self) -> int:
        return self.y + self.h // 2

    def is_positive(self) -> bool:
        return self.w > 0 and self.h > 0

    def contains(self, other: "Rect") -> bool:
        return (
            other.x >= self.x
            and other.y >= self.y
            and other.right <= self.right
            and other.bottom <= self.bottom
        )

    def overlaps(self, other: "Rect") -> bool:
        return (
            self.x < other.right
            and other.x < self.right
            and self.y < other.bottom
            and other.y < self.bottom
        )

    def gap_to(self, other: "Rect") -> int:
        """Return the nearest axis gap, or zero for touching/overlap."""

        if self.overlaps(other):
            return 0
        horizontal = max(other.x - self.right, self.x - other.right, 0)
        vertical = max(other.y - self.bottom, self.y - other.bottom, 0)
        if horizontal and vertical:
            return min(horizontal, vertical)
        return horizontal or vertical

    def inset(self, amount: int) -> "Rect":
        return Rect(
            self.x + amount,
            self.y + amount,
            self.w - amount * 2,
            self.h - amount * 2,
        )

    def to_manifest(self) -> list[int]:
        return [self.x, self.y, self.w, self.h]


@dataclass(frozen=True, slots=True)
class SemanticItem:
    """One semantic row whose behavior and sample value are immutable."""

    key: str
    label: str
    return_value: int | None
    role: str = "action"
    critical: bool = True
    selectable: bool = True
    value_sample: str = ""


@dataclass(frozen=True, slots=True)
class AssetSpec:
    """An immutable source asset requiring a deterministic sampling policy."""

    key: str
    role: str
    source_width: int
    source_height: int
    critical: bool = False
    min_scale_q16: int = Q16_ONE // 2
    max_scale_q16: int = Q16_ONE
    scale_group: str = ""

    def __post_init__(self) -> None:
        if not self.key:
            raise ValueError("asset key must be non-empty")
        if self.source_width <= 0 or self.source_height <= 0:
            raise ValueError(f"asset {self.key!r} has a non-positive extent")
        if not 0 < self.min_scale_q16 <= self.max_scale_q16:
            raise ValueError(f"asset {self.key!r} has an invalid scale range")


def scaled_extent(source_extent: int, scale_q16: int) -> int:
    """Round a positive Q16.16 scale to the nearest destination extent."""

    if source_extent <= 0 or scale_q16 <= 0:
        return 0
    return max(1, (source_extent * scale_q16 + Q16_ONE // 2) // Q16_ONE)


def fit_scale_q16(
    source_width: int,
    source_height: int,
    maximum_width: int,
    maximum_height: int,
    maximum_scale_q16: int = Q16_ONE,
) -> int:
    """Return the largest uniform Q16.16 scale fitting the given rectangle."""

    if (
        source_width <= 0
        or source_height <= 0
        or maximum_width <= 0
        or maximum_height <= 0
        or maximum_scale_q16 <= 0
    ):
        return 0
    return min(
        maximum_scale_q16,
        (maximum_width << 16) // source_width,
        (maximum_height << 16) // source_height,
    )


@dataclass(frozen=True, slots=True)
class AssetPolicy:
    """Resolved sampling parameters suitable for a generated C manifest."""

    asset_key: str
    role: str
    visible: bool
    space: AssetSpace
    source_width: int
    source_height: int
    destination_width: int
    destination_height: int
    scale_q16: int
    sampling: SamplingPolicy
    step_x_q16: int
    step_y_q16: int
    phase_x_q16: int
    phase_y_q16: int
    destination: Rect | None = None
    page: int | None = None
    scale_group: str = ""

    @classmethod
    def omitted(cls, spec: AssetSpec) -> "AssetPolicy":
        return cls(
            asset_key=spec.key,
            role=spec.role,
            visible=False,
            space=AssetSpace.OMITTED,
            source_width=spec.source_width,
            source_height=spec.source_height,
            destination_width=0,
            destination_height=0,
            scale_q16=0,
            sampling=SamplingPolicy.NONE,
            step_x_q16=0,
            step_y_q16=0,
            phase_x_q16=0,
            phase_y_q16=0,
            scale_group=spec.scale_group,
        )

    @classmethod
    def resolved(
        cls,
        spec: AssetSpec,
        scale_q16: int,
        space: AssetSpace,
        *,
        destination: Rect | None = None,
        page: int | None = None,
    ) -> "AssetPolicy":
        destination_width = scaled_extent(spec.source_width, scale_q16)
        destination_height = scaled_extent(spec.source_height, scale_q16)
        if destination is not None and (
            destination.w != destination_width
            or destination.h != destination_height
        ):
            raise ValueError(
                f"asset {spec.key!r} destination does not match Q16 scale"
            )
        step_x_q16 = (spec.source_width << 16) // destination_width
        step_y_q16 = (spec.source_height << 16) // destination_height
        return cls(
            asset_key=spec.key,
            role=spec.role,
            visible=True,
            space=space,
            source_width=spec.source_width,
            source_height=spec.source_height,
            destination_width=destination_width,
            destination_height=destination_height,
            scale_q16=scale_q16,
            sampling=SamplingPolicy.NEAREST_CENTER,
            step_x_q16=step_x_q16,
            step_y_q16=step_y_q16,
            phase_x_q16=step_x_q16 // 2,
            phase_y_q16=step_y_q16 // 2,
            destination=destination,
            page=page,
            scale_group=spec.scale_group,
        )

    def to_manifest(
        self,
        *,
        spec: AssetSpec | None = None,
        screen_id: str | None = None,
    ) -> dict[str, object]:
        divisor = math.gcd(self.scale_q16, Q16_ONE)
        numerator = self.scale_q16 // divisor
        denominator = Q16_ONE // divisor
        if spec is not None and spec.key != self.asset_key:
            raise ValueError("asset policy/spec key mismatch")
        if self.visible and spec is not None:
            min_width = scaled_extent(
                spec.source_width, spec.min_scale_q16
            )
            min_height = scaled_extent(
                spec.source_height, spec.min_scale_q16
            )
            max_width = scaled_extent(
                spec.source_width, spec.max_scale_q16
            )
            max_height = scaled_extent(
                spec.source_height, spec.max_scale_q16
            )
        elif self.visible:
            min_width = self.destination_width
            min_height = self.destination_height
            max_width = self.destination_width
            max_height = self.destination_height
        else:
            numerator = 0
            denominator = 1
            min_width = min_height = max_width = max_height = 0
        return {
            # ``name``/``filter`` and the rational bounds are consumed
            # directly by emit_c.py.  ``key``/``sampling`` remain useful to
            # decision-report readers and preserve the game asset identity.
            "name": (
                f"{screen_id}:{self.asset_key}"
                if screen_id is not None
                else self.asset_key
            ),
            "key": self.asset_key,
            "role": self.role,
            "visible": self.visible,
            "space": self.space.value,
            "screen": screen_id,
            "source": [self.source_width, self.source_height],
            "destination_size": [
                self.destination_width,
                self.destination_height,
            ],
            "destination": (
                self.destination.to_manifest()
                if self.destination is not None
                else None
            ),
            "page": self.page if self.page is not None else 0,
            "numerator": numerator,
            "denominator": denominator,
            "scale_q16": self.scale_q16,
            "filter": self.sampling.value,
            "sampling": self.sampling.value,
            "step_q16": [self.step_x_q16, self.step_y_q16],
            "phase_q16": [self.phase_x_q16, self.phase_y_q16],
            "min_width": min_width,
            "min_height": min_height,
            "max_width": max_width,
            "max_height": max_height,
            "scale_group": self.scale_group,
        }


@dataclass(frozen=True, slots=True)
class PlacedElement:
    """One validated rectangle on one candidate page."""

    key: str
    role: str
    rect: Rect
    text: str = ""
    parent: str | None = None
    overlap_group: str | None = None
    reading_order: int | None = None
    semantic_index: int | None = None
    return_value: int | None = None
    content_index: int | None = None
    content_start: int = 0
    content_end: int = 0
    critical: bool = False
    decorative: bool = False
    selectable: bool = False
    blocks_focus: bool = False
    fit_text: bool = False


@dataclass(frozen=True, slots=True)
class LayoutPage:
    index: int
    elements: tuple[PlacedElement, ...]


@dataclass(frozen=True, slots=True)
class LayoutProblem:
    """The immutable semantics shared by every candidate."""

    name: str
    width: int
    height: int
    font_metrics: FontMetrics
    semantic_items: tuple[SemanticItem, ...] = ()
    content: tuple[str, ...] = ()
    assets: tuple[AssetSpec, ...] = ()
    selected_key: str | None = None
    focus: Rect | None = None
    protect_focus: bool = False
    required_keys: tuple[str, ...] = ()
    required_each_page: tuple[str, ...] = ()

    def __post_init__(self) -> None:
        if self.width <= 0 or self.height <= 0:
            raise ValueError("layout viewport must be positive")
        if self.font_metrics.pixel_height != FUSION_PIXEL_FONT_PX:
            raise ValueError(
                f"layout font must be exactly {FUSION_PIXEL_FONT_PX}px"
            )
        item_keys = [item.key for item in self.semantic_items]
        if len(item_keys) != len(set(item_keys)):
            raise ValueError("semantic item keys must be unique")
        asset_keys = [asset.key for asset in self.assets]
        if len(asset_keys) != len(set(asset_keys)):
            raise ValueError("asset keys must be unique")
        selectable_keys = {
            item.key for item in self.semantic_items if item.selectable
        }
        if (
            self.selected_key is not None
            and self.selected_key not in selectable_keys
        ):
            raise ValueError("selected key is not a selectable semantic item")
        viewport = Rect(0, 0, self.width, self.height)
        if self.focus is not None and (
            not self.focus.is_positive() or not viewport.contains(self.focus)
        ):
            raise ValueError("focus rectangle must be inside the viewport")


@dataclass(frozen=True, slots=True)
class LayoutCandidate:
    candidate_id: str
    mode: LayoutMode
    font_px: int
    pages: tuple[LayoutPage, ...]
    initial_page: int = 0
    asset_policies: tuple[AssetPolicy, ...] = ()
    generation_errors: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class CandidateEvaluation:
    candidate: LayoutCandidate
    valid: bool
    score: int
    violations: tuple[str, ...]
    focus_clearance: int


@dataclass(frozen=True, slots=True)
class LayoutSolution:
    problem: LayoutProblem
    candidate: LayoutCandidate
    score: int
    evaluations: tuple[CandidateEvaluation, ...] = field(repr=False)

    def to_manifest(self) -> dict[str, object]:
        """Return one emitter-ready screen decision plus audit metadata.

        The returned object can be inserted directly into a profile's
        ``screens`` array.  Its ``asset_scale_policies`` can likewise be
        concatenated into the profile's ``sampling`` array.  Extra audit keys
        are intentionally ignored by ``emit_c.py``.
        """

        winning_evaluation = next(
            evaluation
            for evaluation in self.evaluations
            if evaluation.candidate == self.candidate
        )
        decoration_count = sum(
            1
            for page in self.candidate.pages
            for element in page.elements
            if element.decorative
        )
        page_penalty = (
            max(0, len(self.candidate.pages) - 1) * EXTRA_PAGE_PENALTY
        )
        semantic_by_page = [
            [
                element
                for element in page.elements
                if element.semantic_index is not None
            ]
            for page in self.candidate.pages
        ]
        page_capacity = max(
            (len(elements) for elements in semantic_by_page),
            default=1,
        )
        row_count = 1
        column_count = 1
        for elements in semantic_by_page:
            if not elements:
                continue
            row_count = max(
                row_count, len({element.rect.y for element in elements})
            )
            per_row: dict[int, int] = {}
            for element in elements:
                per_row[element.rect.y] = per_row.get(element.rect.y, 0) + 1
            column_count = max(column_count, max(per_row.values()))
        flattened_elements = [
            {
                # Emitter-facing names.
                "name": element.key,
                "kind": element.role,
                "page": page.index,
                "visible": True,
                "selectable": element.selectable,
                "priority": 255 if element.critical else 0,
                # Audit-facing aliases and semantic detail.
                "key": element.key,
                "role": element.role,
                "rect": element.rect.to_manifest(),
                "text": element.text,
                "parent": element.parent,
                "reading_order": element.reading_order,
                "semantic_index": element.semantic_index,
                "return_value": (
                    element.return_value
                    if element.return_value is not None
                    else -1
                ),
                "semantic_return_value": element.return_value,
                "critical": element.critical,
                "decorative": element.decorative,
                "blocks_focus": element.blocks_focus,
            }
            for page in self.candidate.pages
            for element in page.elements
        ]
        return {
            "version": 1,
            "name": self.problem.name,
            "screen_id": self.problem.name,
            "variant": self.candidate.mode.value,
            "candidate_id": self.candidate.candidate_id,
            "viewport": [self.problem.width, self.problem.height],
            "mode": self.candidate.mode.value,
            "font_px": self.candidate.font_px,
            "font_metrics": self.problem.font_metrics.to_manifest(),
            "page_count": len(self.candidate.pages),
            "page_capacity": max(1, page_capacity),
            "rows": row_count,
            "columns": column_count,
            "initial_page": self.candidate.initial_page,
            "score": self.score,
            "reasons": [
                f"mode_score={MODE_SCORE[self.candidate.mode]}",
                f"font_score={self.candidate.font_px * FONT_SCORE_PER_PX}",
                f"page_penalty={page_penalty}",
                f"decoration_score={decoration_count * DECORATION_SCORE}",
                (
                    "focus_clearance_score="
                    f"{min(winning_evaluation.focus_clearance, MAX_SCORED_FOCUS_CLEARANCE) * FOCUS_CLEARANCE_SCORE}"
                ),
                f"fixed_score={self.score}",
            ],
            "focus": (
                self.problem.focus.to_manifest()
                if self.problem.focus is not None
                else None
            ),
            "focus_protected": self.problem.protect_focus,
            "selected_key": self.problem.selected_key or "",
            "focus_order": [
                item.key
                for item in self.problem.semantic_items
                if item.selectable
            ],
            "return_values": [
                {
                    "key": item.key,
                    "semantic_index": index,
                    "return_value": item.return_value,
                    "selectable": item.selectable,
                }
                for index, item in enumerate(self.problem.semantic_items)
                if item.selectable
            ],
            "elements": flattened_elements,
            "pages": [
                {
                    "index": page.index,
                    "element_keys": [
                        element.key for element in page.elements
                    ],
                }
                for page in self.candidate.pages
            ],
            "asset_scale_policies": [
                policy.to_manifest(
                    spec=spec,
                    screen_id=self.problem.name,
                )
                for spec, policy in zip(
                    self.problem.assets,
                    self.candidate.asset_policies,
                )
            ],
        }


class LayoutUnsolvable(ValueError):
    """Raised when every supplied candidate violates the semantic contract."""

    def __init__(self, problem: LayoutProblem, evaluations: Sequence[CandidateEvaluation]):
        detail = "; ".join(
            f"{evaluation.candidate.candidate_id}: "
            + ", ".join(evaluation.violations)
            for evaluation in evaluations
        )
        super().__init__(f"no valid layout for {problem.name}: {detail}")
        self.problem = problem
        self.evaluations = tuple(evaluations)


def glyph_width(character: str, metrics: FontMetrics) -> int:
    """Return the actual BDF/FONT10 advance; no guessed fallback exists."""

    return metrics.advance(character)


def measure_text(text: str, metrics: FontMetrics) -> int:
    return sum(glyph_width(character, metrics) for character in text)


def _allowed_overlap(first: PlacedElement, second: PlacedElement) -> bool:
    if first.parent == second.key or second.parent == first.key:
        return True
    return (
        first.overlap_group is not None
        and first.overlap_group == second.overlap_group
    )


def _validate_content_coverage(
    problem: LayoutProblem,
    pages: Sequence[LayoutPage],
    violations: list[str],
) -> None:
    fragments: list[list[tuple[int, int, str, int, int]]] = [
        [] for _ in problem.content
    ]
    for page in pages:
        for element in page.elements:
            if element.content_index is None:
                continue
            if not 0 <= element.content_index < len(problem.content):
                violations.append(
                    f"{element.key}: content index is out of range"
                )
                continue
            order = element.reading_order
            if order is None:
                violations.append(
                    f"{element.key}: content fragment lacks reading order"
                )
                continue
            fragments[element.content_index].append(
                (
                    page.index,
                    order,
                    element.text,
                    element.content_start,
                    element.content_end,
                )
            )

    for content_index, source in enumerate(problem.content):
        cursor = 0
        for _, _, text, start, end in sorted(fragments[content_index]):
            if start != cursor or end < start or end > len(source):
                violations.append(
                    f"content {content_index}: non-contiguous source range"
                )
                break
            if text != source[start:end]:
                violations.append(
                    f"content {content_index}: fragment text changed"
                )
                break
            cursor = end
        if cursor != len(source):
            violations.append(
                f"content {content_index}: source text is not fully visible"
            )


def _validate_asset_policies(
    problem: LayoutProblem,
    candidate: LayoutCandidate,
    viewport: Rect,
    violations: list[str],
) -> None:
    policies = candidate.asset_policies
    if tuple(policy.asset_key for policy in policies) != tuple(
        asset.key for asset in problem.assets
    ):
        violations.append("asset policy order/coverage changed")
        return

    group_scales: dict[str, int] = {}
    for spec, policy in zip(problem.assets, policies):
        if policy.role != spec.role:
            violations.append(f"asset {spec.key}: role changed")
        if (
            policy.source_width != spec.source_width
            or policy.source_height != spec.source_height
        ):
            violations.append(f"asset {spec.key}: source extent changed")
        if not policy.visible:
            if spec.critical:
                violations.append(f"asset {spec.key}: critical asset omitted")
            if (
                policy.space is not AssetSpace.OMITTED
                or policy.scale_q16 != 0
                or policy.sampling is not SamplingPolicy.NONE
            ):
                violations.append(
                    f"asset {spec.key}: omitted policy has sampling state"
                )
            continue

        if policy.space is AssetSpace.OMITTED:
            violations.append(f"asset {spec.key}: visible asset is omitted")
        if not spec.min_scale_q16 <= policy.scale_q16 <= spec.max_scale_q16:
            violations.append(f"asset {spec.key}: scale is outside policy")
        if policy.sampling is not SamplingPolicy.NEAREST_CENTER:
            violations.append(f"asset {spec.key}: unsupported sampler")
        expected_width = scaled_extent(spec.source_width, policy.scale_q16)
        expected_height = scaled_extent(spec.source_height, policy.scale_q16)
        if (
            policy.destination_width != expected_width
            or policy.destination_height != expected_height
        ):
            violations.append(
                f"asset {spec.key}: destination extent disagrees with scale"
            )
        if (
            policy.step_x_q16
            != (spec.source_width << 16) // expected_width
            or policy.step_y_q16
            != (spec.source_height << 16) // expected_height
            or policy.phase_x_q16 != policy.step_x_q16 // 2
            or policy.phase_y_q16 != policy.step_y_q16 // 2
        ):
            violations.append(
                f"asset {spec.key}: fixed-point sampling parameters changed"
            )
        if policy.destination is not None:
            if (
                policy.destination.w != expected_width
                or policy.destination.h != expected_height
                or not viewport.contains(policy.destination)
            ):
                violations.append(
                    f"asset {spec.key}: UI destination is out of bounds"
                )
        if policy.page is not None and not 0 <= policy.page < len(candidate.pages):
            violations.append(f"asset {spec.key}: page is out of range")
        if spec.scale_group:
            previous = group_scales.setdefault(spec.scale_group, policy.scale_q16)
            if previous != policy.scale_q16:
                violations.append(
                    f"asset scale group {spec.scale_group!r} is inconsistent"
                )


def evaluate_candidate(
    problem: LayoutProblem,
    candidate: LayoutCandidate,
) -> CandidateEvaluation:
    """Validate and score one candidate without mutating either input."""

    violations = list(candidate.generation_errors)
    viewport = Rect(0, 0, problem.width, problem.height)

    if candidate.font_px != problem.font_metrics.pixel_height:
        violations.append(
            f"font {candidate.font_px}px is not fixed FONT10"
        )
    if not candidate.pages:
        violations.append("candidate has no pages")
    if tuple(page.index for page in candidate.pages) != tuple(
        range(len(candidate.pages))
    ):
        violations.append("page indices are not contiguous")
    if not 0 <= candidate.initial_page < len(candidate.pages):
        violations.append("initial page is out of range")

    global_keys: set[str] = set()
    semantic_entries: list[tuple[int, int, PlacedElement]] = []
    focus_blockers: list[Rect] = []
    for page in candidate.pages:
        local: dict[str, PlacedElement] = {}
        meaningful_orders: set[int] = set()
        for element in page.elements:
            if not element.key:
                violations.append(f"page {page.index}: empty element key")
                continue
            if element.key in local:
                violations.append(
                    f"page {page.index}: duplicate key {element.key!r}"
                )
            local[element.key] = element
            if not element.rect.is_positive() or not viewport.contains(element.rect):
                violations.append(f"{element.key}: rectangle is out of bounds")
            if element.fit_text:
                try:
                    text_width = measure_text(
                        element.text, problem.font_metrics
                    )
                except MissingGlyphError as error:
                    violations.append(f"{element.key}: {error}")
                    text_width = element.rect.w + 1
                if (
                    element.rect.h < problem.font_metrics.pixel_height
                    or text_width > element.rect.w
                ):
                    violations.append(f"{element.key}: text does not fit")
            if element.semantic_index is not None or element.content_index is not None:
                if element.reading_order is None:
                    violations.append(
                        f"{element.key}: semantic element lacks reading order"
                    )
                elif element.reading_order in meaningful_orders:
                    violations.append(
                        f"page {page.index}: duplicate reading order "
                        f"{element.reading_order}"
                    )
                else:
                    meaningful_orders.add(element.reading_order)
            if element.semantic_index is not None and element.reading_order is not None:
                semantic_entries.append(
                    (page.index, element.reading_order, element)
                )
            if problem.protect_focus and element.blocks_focus:
                focus_blockers.append(element.rect)
            global_keys.add(element.key)

        for element in page.elements:
            if element.parent is not None:
                parent = local.get(element.parent)
                if parent is None:
                    violations.append(
                        f"{element.key}: parent {element.parent!r} is missing"
                    )
                elif not parent.rect.contains(element.rect):
                    violations.append(
                        f"{element.key}: child is outside its parent"
                    )

        elements = page.elements
        for first_index, first in enumerate(elements):
            for second in elements[first_index + 1 :]:
                if first.rect.overlaps(second.rect) and not _allowed_overlap(
                    first, second
                ):
                    violations.append(
                        f"page {page.index}: illegal overlap "
                        f"{first.key!r}/{second.key!r}"
                    )

        for required in problem.required_each_page:
            if required.format(page=page.index) not in local:
                violations.append(
                    f"page {page.index}: required element {required!r} is missing"
                )

    for required in problem.required_keys:
        if required not in global_keys:
            violations.append(f"required element {required!r} is missing")

    expected_items = problem.semantic_items
    ordered_entries = [
        entry for _, _, entry in sorted(semantic_entries, key=lambda item: item[:2])
    ]
    actual_indices = tuple(entry.semantic_index for entry in ordered_entries)
    if actual_indices != tuple(range(len(expected_items))):
        violations.append("functional item order/coverage changed")
    else:
        for semantic_index, element in enumerate(ordered_entries):
            expected = expected_items[semantic_index]
            if (
                element.key != expected.key
                or element.text != expected.label
                or element.return_value != expected.return_value
                or element.selectable != expected.selectable
            ):
                violations.append(
                    f"functional item {expected.key!r} changed"
                )
            if expected.critical and not element.critical:
                violations.append(
                    f"functional item {expected.key!r} lost critical status"
                )

    if problem.selected_key is not None and 0 <= candidate.initial_page < len(
        candidate.pages
    ):
        initial_semantic_keys = {
            element.key
            for element in candidate.pages[candidate.initial_page].elements
            if element.selectable
        }
        if problem.selected_key not in initial_semantic_keys:
            violations.append("selected item is not visible on the initial page")

    _validate_content_coverage(problem, candidate.pages, violations)
    _validate_asset_policies(problem, candidate, viewport, violations)

    focus_clearance = MAX_SCORED_FOCUS_CLEARANCE
    if problem.protect_focus and problem.focus is not None:
        for blocker in focus_blockers:
            if blocker.overlaps(problem.focus):
                violations.append("protected focus is obscured")
                focus_clearance = 0
            else:
                focus_clearance = min(
                    focus_clearance, blocker.gap_to(problem.focus)
                )

    score = (
        MODE_SCORE[candidate.mode]
        + candidate.font_px * FONT_SCORE_PER_PX
        - max(0, len(candidate.pages) - 1) * EXTRA_PAGE_PENALTY
        + sum(
            1
            for page in candidate.pages
            for element in page.elements
            if element.decorative
        )
        * DECORATION_SCORE
        + min(focus_clearance, MAX_SCORED_FOCUS_CLEARANCE)
        * FOCUS_CLEARANCE_SCORE
    )
    if violations:
        score = -1
    return CandidateEvaluation(
        candidate=candidate,
        valid=not violations,
        score=score,
        violations=tuple(violations),
        focus_clearance=focus_clearance,
    )


def solve_layout(
    problem: LayoutProblem,
    candidates: Iterable[LayoutCandidate],
) -> LayoutSolution:
    """Return the highest fixed-score valid candidate.

    Candidate input order is intentionally not a tie breaker.  The stable mode
    order and candidate ID make identical data produce identical manifests.
    """

    evaluations = tuple(
        evaluate_candidate(problem, candidate) for candidate in candidates
    )
    valid = [evaluation for evaluation in evaluations if evaluation.valid]
    if not valid:
        raise LayoutUnsolvable(problem, evaluations)
    mode_rank = {mode: index for index, mode in enumerate(MODE_ORDER)}
    valid.sort(
        key=lambda evaluation: (
            -evaluation.score,
            mode_rank[evaluation.candidate.mode],
            evaluation.candidate.candidate_id,
        )
    )
    winner = valid[0]
    return LayoutSolution(
        problem=problem,
        candidate=winner.candidate,
        score=winner.score,
        evaluations=evaluations,
    )


# Public name used by manifest/header integration.  Keep LayoutSolution as a
# compatibility spelling for callers that think in solver terminology.
LayoutDecision = LayoutSolution
