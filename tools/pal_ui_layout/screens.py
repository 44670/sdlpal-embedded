#!/usr/bin/env python3
"""PAL screen semantics and deterministic candidate generation.

The public constructors keep game-facing entries separate from presentation:
callers provide ordered ``SemanticItem`` objects and receive a layout whose
pages retain exactly the same keys, labels, order, and return values.  Visual
simplification may remove optional art, but never a functional entry.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Iterable, Sequence

try:
    from .solver import (
        AssetPolicy,
        AssetSpace,
        AssetSpec,
        FontMetrics,
        FUSION_PIXEL_FONT_PX,
        LayoutCandidate,
        LayoutMode,
        LayoutPage,
        LayoutProblem,
        LayoutSolution,
        PlacedElement,
        Q16_ONE,
        Rect,
        SemanticItem,
        fit_scale_q16,
        glyph_width,
        measure_text,
        scaled_extent,
        solve_layout,
    )
except ImportError:  # Direct execution/import with tools/pal_ui_layout on sys.path.
    from solver import (  # type: ignore
        AssetPolicy,
        AssetSpace,
        AssetSpec,
        FontMetrics,
        FUSION_PIXEL_FONT_PX,
        LayoutCandidate,
        LayoutMode,
        LayoutPage,
        LayoutProblem,
        LayoutSolution,
        PlacedElement,
        Q16_ONE,
        Rect,
        SemanticItem,
        fit_scale_q16,
        glyph_width,
        measure_text,
        scaled_extent,
        solve_layout,
    )


class ScreenKind(str, Enum):
    DIALOG = "dialog"
    MENU = "menu"
    ITEM = "item"
    MAGIC = "magic"
    STATUS = "status"
    EQUIP = "equip"
    BATTLE_HUD = "battle_hud"


@dataclass(frozen=True, slots=True)
class ScreenSpec:
    screen_id: str
    kind: ScreenKind
    width: int
    height: int
    font_metrics: FontMetrics
    title: str = ""
    items: tuple[SemanticItem, ...] = ()
    body: tuple[str, ...] = ()
    assets: tuple[AssetSpec, ...] = ()
    selected_key: str | None = None
    focus: Rect | None = None
    protect_focus: bool = False
    modal: bool = True
    portrait: bool = False

    def __post_init__(self) -> None:
        if self.width <= 0 or self.height <= 0:
            raise ValueError("screen dimensions must be positive")
        if self.font_metrics.pixel_height != FUSION_PIXEL_FONT_PX:
            raise ValueError("screen layout requires fixed FONT10 metrics")
        self.font_metrics.require_texts(
            (
                self.title,
                *self.body,
                *(entry.label for entry in self.items),
                *(entry.value_sample for entry in self.items),
                # Paged candidates always format deterministic page counters.
                "0123456789/",
            )
        )

    def problem(self) -> LayoutProblem:
        required_each_page = ("title:p{page}",) if self.title else ()
        return LayoutProblem(
            name=self.screen_id,
            width=self.width,
            height=self.height,
            font_metrics=self.font_metrics,
            semantic_items=self.items,
            content=self.body,
            assets=self.assets,
            selected_key=self.selected_key,
            focus=self.focus,
            protect_focus=self.protect_focus,
            required_each_page=required_each_page,
        )


@dataclass(frozen=True, slots=True)
class _Style:
    mode: LayoutMode
    margin: int
    padding: int
    panel: bool
    paged: bool
    show_optional_art: bool


_STYLES = (
    _Style(LayoutMode.FULL, 4, 5, True, False, True),
    _Style(LayoutMode.COMPACT, 3, 3, True, False, True),
    _Style(LayoutMode.SINGLE_COLUMN, 3, 3, True, False, False),
    _Style(LayoutMode.PAGED, 2, 2, True, True, False),
    _Style(LayoutMode.TEXT_ONLY, 2, 1, False, True, False),
)


def item(
    key: str,
    label: str,
    return_value: int | None,
    *,
    role: str = "action",
    critical: bool = True,
    selectable: bool = True,
    value_sample: str = "",
) -> SemanticItem:
    """Convenience constructor used by tools and tests."""

    return SemanticItem(
        key=key,
        label=label,
        return_value=return_value,
        role=role,
        critical=critical,
        selectable=selectable,
        value_sample=value_sample,
    )


def _items(value: Iterable[SemanticItem]) -> tuple[SemanticItem, ...]:
    result = tuple(value)
    if any(not entry.key for entry in result):
        raise ValueError("functional item key must be non-empty")
    return result


def dialog_screen(
    width: int,
    height: int,
    lines: Sequence[str],
    choices: Iterable[SemanticItem] = (),
    *,
    font_metrics: FontMetrics,
    screen_id: str = "dialog",
    title: str = "",
    focus: Rect | None = None,
    portrait: AssetSpec | None = None,
    selected_key: str | None = None,
) -> ScreenSpec:
    return ScreenSpec(
        screen_id=screen_id,
        kind=ScreenKind.DIALOG,
        width=width,
        height=height,
        font_metrics=font_metrics,
        title=title,
        items=_items(choices),
        body=tuple(lines),
        assets=(portrait,) if portrait is not None else (),
        selected_key=selected_key,
        focus=focus,
        protect_focus=focus is not None,
        modal=False,
        portrait=portrait is not None,
    )


def menu_screen(
    width: int,
    height: int,
    entries: Iterable[SemanticItem],
    *,
    font_metrics: FontMetrics,
    screen_id: str = "menu",
    title: str = "MENU",
    selected_key: str | None = None,
    focus: Rect | None = None,
    modal: bool = True,
) -> ScreenSpec:
    return ScreenSpec(
        screen_id=screen_id,
        kind=ScreenKind.MENU,
        width=width,
        height=height,
        font_metrics=font_metrics,
        title=title,
        items=_items(entries),
        selected_key=selected_key,
        focus=focus,
        protect_focus=focus is not None and not modal,
        modal=modal,
    )


def item_screen(
    width: int,
    height: int,
    entries: Iterable[SemanticItem],
    *,
    font_metrics: FontMetrics,
    screen_id: str = "item",
    title: str = "ITEM",
    selected_key: str | None = None,
    preview: AssetSpec | None = None,
) -> ScreenSpec:
    return ScreenSpec(
        screen_id=screen_id,
        kind=ScreenKind.ITEM,
        width=width,
        height=height,
        font_metrics=font_metrics,
        title=title,
        items=_items(entries),
        assets=(preview,) if preview is not None else (),
        selected_key=selected_key,
    )


def magic_screen(
    width: int,
    height: int,
    entries: Iterable[SemanticItem],
    *,
    font_metrics: FontMetrics,
    screen_id: str = "magic",
    title: str = "MAGIC",
    selected_key: str | None = None,
    preview: AssetSpec | None = None,
) -> ScreenSpec:
    return ScreenSpec(
        screen_id=screen_id,
        kind=ScreenKind.MAGIC,
        width=width,
        height=height,
        font_metrics=font_metrics,
        title=title,
        items=_items(entries),
        assets=(preview,) if preview is not None else (),
        selected_key=selected_key,
    )


def status_screen(
    width: int,
    height: int,
    fields: Iterable[SemanticItem],
    *,
    font_metrics: FontMetrics,
    screen_id: str = "status",
    title: str = "STATUS",
    selected_key: str | None = None,
    portrait: AssetSpec | None = None,
) -> ScreenSpec:
    return ScreenSpec(
        screen_id=screen_id,
        kind=ScreenKind.STATUS,
        width=width,
        height=height,
        font_metrics=font_metrics,
        title=title,
        items=_items(fields),
        assets=(portrait,) if portrait is not None else (),
        selected_key=selected_key,
        portrait=portrait is not None,
    )


def equip_screen(
    width: int,
    height: int,
    slots: Iterable[SemanticItem],
    *,
    font_metrics: FontMetrics,
    screen_id: str = "equip",
    title: str = "EQUIP",
    selected_key: str | None = None,
    preview: AssetSpec | None = None,
) -> ScreenSpec:
    return ScreenSpec(
        screen_id=screen_id,
        kind=ScreenKind.EQUIP,
        width=width,
        height=height,
        font_metrics=font_metrics,
        title=title,
        items=_items(slots),
        assets=(preview,) if preview is not None else (),
        selected_key=selected_key,
    )


def battle_hud_screen(
    width: int,
    height: int,
    commands: Iterable[SemanticItem],
    *,
    font_metrics: FontMetrics,
    screen_id: str = "battle_hud",
    title: str = "",
    selected_key: str | None = None,
    player_focus: Rect,
    stage_assets: Iterable[AssetSpec] = (),
) -> ScreenSpec:
    assets = tuple(stage_assets)
    for asset in assets:
        if not _is_stage_asset(asset):
            raise ValueError(
                f"battle HUD asset {asset.key!r} must use a battle_* role"
            )
    return ScreenSpec(
        screen_id=screen_id,
        kind=ScreenKind.BATTLE_HUD,
        width=width,
        height=height,
        font_metrics=font_metrics,
        title=title,
        items=_items(commands),
        assets=assets,
        selected_key=selected_key,
        focus=player_focus,
        protect_focus=True,
        modal=False,
    )


def _font_for(spec: ScreenSpec, style: _Style) -> int:
    del style
    return spec.font_metrics.pixel_height


def _line_height(spec: ScreenSpec) -> int:
    return spec.font_metrics.line_height


def _columns(kind: ScreenKind, mode: LayoutMode) -> int:
    if mode in (LayoutMode.SINGLE_COLUMN, LayoutMode.PAGED, LayoutMode.TEXT_ONLY):
        return 1
    if mode is LayoutMode.COMPACT:
        return {
            ScreenKind.ITEM: 2,
            ScreenKind.MAGIC: 2,
            ScreenKind.STATUS: 1,
            ScreenKind.EQUIP: 1,
            ScreenKind.BATTLE_HUD: 2,
        }.get(kind, 2)
    return {
        ScreenKind.MENU: 2,
        ScreenKind.ITEM: 3,
        ScreenKind.MAGIC: 2,
        ScreenKind.STATUS: 1,
        ScreenKind.EQUIP: 1,
        ScreenKind.BATTLE_HUD: 4,
    }.get(kind, 1)


def _is_stage_asset(asset: AssetSpec) -> bool:
    return asset.role.startswith("battle_")


def _choose_band(
    spec: ScreenSpec,
    margin: int,
    requested_height: int,
) -> Rect:
    width = spec.width - margin * 2
    candidates = (
        Rect(margin, margin, width, requested_height),
        Rect(
            margin,
            spec.height - margin - requested_height,
            width,
            requested_height,
        ),
    )
    viewport = Rect(0, 0, spec.width, spec.height)
    fitting = [rect for rect in candidates if viewport.contains(rect)]
    if spec.protect_focus and spec.focus is not None:
        clear = [rect for rect in fitting if not rect.overlaps(spec.focus)]
        if clear:
            clear.sort(
                key=lambda rect: (
                    -rect.gap_to(spec.focus),
                    rect.y,
                    rect.x,
                )
            )
            return clear[0]
    if fitting:
        return fitting[0]
    return candidates[0]


def _largest_safe_band(
    spec: ScreenSpec,
    margin: int,
    minimum_height: int,
) -> Rect:
    if not spec.protect_focus or spec.focus is None:
        return Rect(
            margin,
            margin,
            spec.width - margin * 2,
            spec.height - margin * 2,
        )
    top_height = spec.focus.y - margin
    bottom_y = spec.focus.bottom
    bottom_height = spec.height - margin - bottom_y
    choices = [
        Rect(margin, margin, spec.width - margin * 2, top_height),
        Rect(margin, bottom_y, spec.width - margin * 2, bottom_height),
    ]
    choices = [choice for choice in choices if choice.h >= minimum_height]
    if not choices:
        return _choose_band(spec, margin, minimum_height)
    choices.sort(key=lambda rect: (-rect.h, rect.y))
    return choices[0]


def _wrap_ranges(
    text: str,
    maximum_width: int,
    font_metrics: FontMetrics,
) -> tuple[tuple[int, int, str], ...]:
    if not text:
        return ()
    result: list[tuple[int, int, str]] = []
    start = 0
    cursor = 0
    width = 0
    while cursor < len(text):
        character_width = glyph_width(text[cursor], font_metrics)
        if cursor > start and width + character_width > maximum_width:
            result.append((start, cursor, text[start:cursor]))
            start = cursor
            width = 0
        width += character_width
        cursor += 1
    result.append((start, cursor, text[start:cursor]))
    return tuple(result)


def _panel_element(
    page_index: int,
    rect: Rect,
    spec: ScreenSpec,
) -> PlacedElement:
    return PlacedElement(
        key=f"panel:p{page_index}",
        role="panel",
        rect=rect,
        decorative=True,
        blocks_focus=spec.protect_focus,
    )


def _title_element(
    page_index: int,
    rect: Rect,
    title: str,
    parent: str | None,
) -> PlacedElement:
    return PlacedElement(
        key=f"title:p{page_index}",
        role="title",
        rect=rect,
        text=title,
        parent=parent,
        critical=True,
        blocks_focus=parent is None,
        fit_text=True,
    )


def _page_indicator(
    page_index: int,
    page_count: int,
    rect: Rect,
    parent: str | None,
) -> PlacedElement:
    return PlacedElement(
        key=f"page_indicator:p{page_index}",
        role="page_indicator",
        rect=rect,
        text=f"{page_index + 1}/{page_count}",
        parent=parent,
        blocks_focus=parent is None,
        fit_text=True,
    )


def _resolve_assets(
    spec: ScreenSpec,
    style: _Style,
    pages: tuple[LayoutPage, ...],
    art_rect: Rect | None,
    art_parent: str | None,
) -> tuple[tuple[LayoutPage, ...], tuple[AssetPolicy, ...]]:
    """Attach manifest policies and UI asset rectangles to page zero."""

    if not spec.assets:
        return pages, ()

    stage_scale_by_group: dict[str, int] = {}
    for asset in spec.assets:
        if not _is_stage_asset(asset):
            continue
        group = asset.scale_group or asset.key
        screen_scale = min(
            (spec.width << 16) // 320,
            (spec.height << 16) // 200,
        )
        current = stage_scale_by_group.get(group, Q16_ONE)
        stage_scale_by_group[group] = min(
            current, screen_scale, asset.max_scale_q16
        )

    policies: list[AssetPolicy] = []
    added: list[PlacedElement] = []
    ui_assets = [asset for asset in spec.assets if not _is_stage_asset(asset)]
    slot_height = (
        art_rect.h // len(ui_assets)
        if art_rect is not None and ui_assets
        else 0
    )
    ui_index = 0
    for asset in spec.assets:
        if _is_stage_asset(asset):
            group = asset.scale_group or asset.key
            policies.append(
                AssetPolicy.resolved(
                    asset,
                    stage_scale_by_group[group],
                    AssetSpace.LEGACY_STAGE,
                )
            )
            continue

        show = (
            art_rect is not None
            and (
                style.show_optional_art
                or asset.critical
            )
            and style.mode is not LayoutMode.TEXT_ONLY
        )
        if not show:
            policies.append(AssetPolicy.omitted(asset))
            continue

        slot = Rect(
            art_rect.x,
            art_rect.y + ui_index * slot_height,
            art_rect.w,
            (
                art_rect.bottom - (art_rect.y + ui_index * slot_height)
                if ui_index == len(ui_assets) - 1
                else slot_height
            ),
        )
        ui_index += 1
        scale_q16 = fit_scale_q16(
            asset.source_width,
            asset.source_height,
            slot.w,
            slot.h,
            asset.max_scale_q16,
        )
        if scale_q16 <= 0:
            policies.append(AssetPolicy.omitted(asset))
            continue
        destination_width = scaled_extent(asset.source_width, scale_q16)
        destination_height = scaled_extent(asset.source_height, scale_q16)
        destination = Rect(
            slot.x + (slot.w - destination_width) // 2,
            slot.y + (slot.h - destination_height) // 2,
            destination_width,
            destination_height,
        )
        policies.append(
            AssetPolicy.resolved(
                asset,
                scale_q16,
                AssetSpace.UI,
                destination=destination,
                page=0,
            )
        )
        added.append(
            PlacedElement(
                key=f"asset:{asset.key}:p0",
                role=asset.role,
                rect=destination,
                parent=art_parent,
                decorative=True,
            )
        )

    if added and pages:
        first = pages[0]
        pages = (
            LayoutPage(first.index, first.elements + tuple(added)),
            *pages[1:],
        )
    return pages, tuple(policies)


def _initial_page(
    spec: ScreenSpec,
    pages: Sequence[LayoutPage],
) -> int:
    if spec.selected_key is None:
        return 0
    for page in pages:
        if any(
            element.key == spec.selected_key
            and element.selectable
            for element in page.elements
        ):
            return page.index
    return 0


def _list_candidate(spec: ScreenSpec, style: _Style) -> LayoutCandidate:
    font_px = _font_for(spec, style)
    line_height = _line_height(spec)
    columns = _columns(spec.kind, style.mode)
    header_height = line_height if spec.title else 0
    footer_height = line_height if style.paged else 0

    ui_assets = [asset for asset in spec.assets if not _is_stage_asset(asset)]
    reserve_art = bool(ui_assets) and (
        style.show_optional_art or any(asset.critical for asset in ui_assets)
    ) and style.mode is not LayoutMode.TEXT_ONLY

    rows_needed = (len(spec.items) + columns - 1) // columns
    desired_height = (
        style.padding * 2
        + header_height
        + rows_needed * line_height
        + footer_height
    )
    if spec.modal:
        outer = Rect(
            style.margin,
            style.margin,
            spec.width - style.margin * 2,
            spec.height - style.margin * 2,
        )
    elif spec.kind is ScreenKind.BATTLE_HUD:
        # PAL battle actors naturally occupy the lower half of the canonical
        # stage.  Keep command chrome in the generated top band for both
        # certified profiles; the camera still protects the explicit focus
        # rectangle and the preview verifies the fit-all player fixture.
        outer = Rect(
            style.margin,
            style.margin,
            spec.width - style.margin * 2,
            desired_height,
        )
    elif style.paged:
        outer = _largest_safe_band(
            spec,
            style.margin,
            style.padding * 2 + header_height + footer_height + line_height,
        )
    else:
        outer = _choose_band(spec, style.margin, desired_height)

    parent_template = "panel:p{page}" if style.panel else None
    inner = outer.inset(style.padding)
    if spec.title:
        inner = Rect(inner.x, inner.y + header_height, inner.w, inner.h - header_height)
    if style.paged:
        inner = Rect(inner.x, inner.y, inner.w, inner.h - footer_height)

    art_width = 0
    if reserve_art:
        art_width = min(72, max(28, inner.w // 3))
    gap = 2 if art_width else 0
    list_rect = Rect(
        inner.x + art_width + gap,
        inner.y,
        inner.w - art_width - gap,
        inner.h,
    )
    art_rect = (
        Rect(inner.x, inner.y, art_width, inner.h)
        if art_width
        else None
    )
    column_gap = 2
    cell_width = (
        list_rect.w - column_gap * (columns - 1)
    ) // columns
    row_count = list_rect.h // line_height if line_height > 0 else 0
    capacity = max(0, row_count * columns)
    if style.paged:
        per_page = max(1, capacity)
        chunks = [
            spec.items[offset : offset + per_page]
            for offset in range(0, len(spec.items), per_page)
        ] or [()]
    else:
        chunks = [spec.items[:capacity]]

    page_count = len(chunks)
    pages: list[LayoutPage] = []
    semantic_offset = 0
    for page_index, chunk in enumerate(chunks):
        panel_key = (
            parent_template.format(page=page_index)
            if parent_template is not None
            else None
        )
        elements: list[PlacedElement] = []
        if style.panel:
            elements.append(_panel_element(page_index, outer, spec))
        if spec.title:
            elements.append(
                _title_element(
                    page_index,
                    Rect(
                        outer.x + style.padding,
                        outer.y + style.padding,
                        outer.w - style.padding * 2,
                        header_height,
                    ),
                    spec.title,
                    panel_key,
                )
            )

        for local_index, entry in enumerate(chunk):
            row = local_index // columns
            column = local_index % columns
            cell = Rect(
                list_rect.x + column * (cell_width + column_gap),
                list_rect.y + row * line_height,
                cell_width,
                line_height,
            )
            value_width = (
                measure_text(entry.value_sample, spec.font_metrics)
                if entry.value_sample
                else 0
            )
            value_gap = 2 if value_width else 0
            rect = Rect(
                cell.x,
                cell.y,
                cell.w - value_gap - value_width,
                cell.h,
            )
            semantic_index = semantic_offset + local_index
            elements.append(
                PlacedElement(
                    key=entry.key,
                    role=entry.role,
                    rect=rect,
                    text=entry.label,
                    parent=panel_key,
                    reading_order=local_index,
                    semantic_index=semantic_index,
                    return_value=entry.return_value,
                    critical=entry.critical,
                    selectable=entry.selectable,
                    blocks_focus=panel_key is None and spec.protect_focus,
                    fit_text=True,
                )
            )
            if entry.value_sample:
                elements.append(
                    PlacedElement(
                        key=f"{entry.key}:value",
                        role=f"{entry.role}_value",
                        rect=Rect(
                            cell.right - value_width,
                            cell.y,
                            value_width,
                            cell.h,
                        ),
                        text=entry.value_sample,
                        parent=panel_key,
                        critical=entry.critical,
                        fit_text=True,
                    )
                )
        semantic_offset += len(chunk)

        if style.paged:
            elements.append(
                _page_indicator(
                    page_index,
                    page_count,
                    Rect(
                        outer.x + style.padding,
                        outer.bottom - style.padding - footer_height,
                        outer.w - style.padding * 2,
                        footer_height,
                    ),
                    panel_key,
                )
            )
        pages.append(LayoutPage(page_index, tuple(elements)))

    pages_tuple, policies = _resolve_assets(
        spec,
        style,
        tuple(pages),
        art_rect,
        "panel:p0" if style.panel else None,
    )
    return LayoutCandidate(
        candidate_id=f"{spec.kind.value}:{style.mode.value}",
        mode=style.mode,
        font_px=font_px,
        pages=pages_tuple,
        initial_page=_initial_page(spec, pages_tuple),
        asset_policies=policies,
    )


@dataclass(frozen=True, slots=True)
class _DialogToken:
    kind: str
    text: str
    content_index: int | None = None
    content_start: int = 0
    content_end: int = 0
    semantic_index: int | None = None


def _dialog_candidate(spec: ScreenSpec, style: _Style) -> LayoutCandidate:
    font_px = _font_for(spec, style)
    line_height = _line_height(spec)
    header_height = line_height if spec.title else 0
    footer_height = line_height if style.paged else 0
    portrait_assets = [
        asset for asset in spec.assets if not _is_stage_asset(asset)
    ]
    reserve_portrait = bool(portrait_assets) and (
        style.show_optional_art or any(asset.critical for asset in portrait_assets)
    ) and style.mode is not LayoutMode.TEXT_ONLY
    base_width = spec.width - style.margin * 2 - style.padding * 2
    portrait_width = (
        min(64, max(28, base_width // 4)) if reserve_portrait else 0
    )
    text_width = base_width - portrait_width - (2 if portrait_width else 0)

    tokens: list[_DialogToken] = []
    for content_index, source in enumerate(spec.body):
        for start, end, fragment in _wrap_ranges(
            source, text_width, spec.font_metrics
        ):
            tokens.append(
                _DialogToken(
                    "content",
                    fragment,
                    content_index=content_index,
                    content_start=start,
                    content_end=end,
                )
            )
    for semantic_index, entry in enumerate(spec.items):
        tokens.append(
            _DialogToken(
                "semantic",
                entry.label,
                semantic_index=semantic_index,
            )
        )

    desired_height = (
        style.padding * 2
        + header_height
        + len(tokens) * line_height
        + footer_height
    )
    if reserve_portrait:
        minimum_art_height = max(
            scaled_extent(asset.source_height, asset.min_scale_q16)
            for asset in portrait_assets
        )
        desired_height = max(
            desired_height,
            style.padding * 2 + header_height + footer_height + minimum_art_height,
        )
    if style.paged:
        outer = _largest_safe_band(
            spec,
            style.margin,
            style.padding * 2 + header_height + footer_height + line_height,
        )
    else:
        outer = _choose_band(spec, style.margin, desired_height)
    inner = outer.inset(style.padding)
    if spec.title:
        inner = Rect(inner.x, inner.y + header_height, inner.w, inner.h - header_height)
    if style.paged:
        inner = Rect(inner.x, inner.y, inner.w, inner.h - footer_height)

    art_rect = (
        Rect(inner.x, inner.y, portrait_width, inner.h)
        if portrait_width
        else None
    )
    token_rect = Rect(
        inner.x + portrait_width + (2 if portrait_width else 0),
        inner.y,
        inner.w - portrait_width - (2 if portrait_width else 0),
        inner.h,
    )
    capacity = max(0, token_rect.h // line_height)
    if style.paged:
        per_page = max(1, capacity)
        chunks = [
            tokens[offset : offset + per_page]
            for offset in range(0, len(tokens), per_page)
        ] or [[]]
    else:
        chunks = [tokens[:capacity]]

    page_count = len(chunks)
    pages: list[LayoutPage] = []
    global_reading_order = 0
    for page_index, chunk in enumerate(chunks):
        panel_key = f"panel:p{page_index}" if style.panel else None
        elements: list[PlacedElement] = []
        if style.panel:
            elements.append(_panel_element(page_index, outer, spec))
        if spec.title:
            elements.append(
                _title_element(
                    page_index,
                    Rect(
                        outer.x + style.padding,
                        outer.y + style.padding,
                        outer.w - style.padding * 2,
                        header_height,
                    ),
                    spec.title,
                    panel_key,
                )
            )
        for row, token in enumerate(chunk):
            rect = Rect(
                token_rect.x,
                token_rect.y + row * line_height,
                token_rect.w,
                line_height,
            )
            if token.kind == "semantic":
                assert token.semantic_index is not None
                entry = spec.items[token.semantic_index]
                elements.append(
                    PlacedElement(
                        key=entry.key,
                        role=entry.role,
                        rect=rect,
                        text=entry.label,
                        parent=panel_key,
                        reading_order=global_reading_order,
                        semantic_index=token.semantic_index,
                        return_value=entry.return_value,
                        critical=entry.critical,
                        selectable=entry.selectable,
                        blocks_focus=panel_key is None and spec.protect_focus,
                        fit_text=True,
                    )
                )
            else:
                assert token.content_index is not None
                elements.append(
                    PlacedElement(
                        key=(
                            f"body:{token.content_index}:"
                            f"{token.content_start}:p{page_index}"
                        ),
                        role="dialog_text",
                        rect=rect,
                        text=token.text,
                        parent=panel_key,
                        reading_order=global_reading_order,
                        content_index=token.content_index,
                        content_start=token.content_start,
                        content_end=token.content_end,
                        critical=True,
                        blocks_focus=panel_key is None and spec.protect_focus,
                        fit_text=True,
                    )
                )
            global_reading_order += 1

        if style.paged:
            elements.append(
                _page_indicator(
                    page_index,
                    page_count,
                    Rect(
                        outer.x + style.padding,
                        outer.bottom - style.padding - footer_height,
                        outer.w - style.padding * 2,
                        footer_height,
                    ),
                    panel_key,
                )
            )
        pages.append(LayoutPage(page_index, tuple(elements)))

    pages_tuple, policies = _resolve_assets(
        spec,
        style,
        tuple(pages),
        art_rect,
        "panel:p0" if style.panel else None,
    )
    return LayoutCandidate(
        candidate_id=f"{spec.kind.value}:{style.mode.value}",
        mode=style.mode,
        font_px=font_px,
        pages=pages_tuple,
        initial_page=_initial_page(spec, pages_tuple),
        asset_policies=policies,
    )


def candidates_for_screen(spec: ScreenSpec) -> tuple[LayoutCandidate, ...]:
    """Generate the five required candidates in fixed order."""

    builder = _dialog_candidate if spec.kind is ScreenKind.DIALOG else _list_candidate
    return tuple(builder(spec, style) for style in _STYLES)


def solve_screen(spec: ScreenSpec) -> LayoutSolution:
    return solve_layout(spec.problem(), candidates_for_screen(spec))
