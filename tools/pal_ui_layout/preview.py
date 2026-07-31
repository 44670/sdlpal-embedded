#!/usr/bin/env python3
"""Dependency-free PPM previews for resolved layout manifests."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Sequence

from .font import FONT10_CELL_HEIGHT, Font10, Font10Glyph


class PreviewError(ValueError):
    """The manifest cannot be rendered as a bounded preview."""


_BACKGROUND = (9, 13, 24)
_SAFE = (20, 29, 48)
_STAGE = (35, 48, 69)
_ELEMENT_COLORS = (
    (49, 130, 206),
    (80, 180, 120),
    (218, 150, 62),
    (171, 104, 208),
    (205, 82, 92),
    (74, 178, 190),
)
_FOCUS = (255, 240, 96)
_FOCUS_BACKGROUND = (76, 65, 22)
_TEXT = (236, 244, 255)
_TITLE_TEXT = (112, 224, 255)
_PAGE_TEXT = (168, 184, 208)
_PLAYER_FOCUS = (64, 236, 180)
_TARGET_FOCUS = (255, 112, 96)
_CAMERA_CENTER = (255, 224, 80)

_LOADING_GLYPHS = (
    (0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F),  # L
    (0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E),  # O
    (0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11),  # A
    (0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E),  # D
    (0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F),  # I
    (0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11),  # N
    (0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E),  # G
)


@dataclass(frozen=True, slots=True)
class IndexedImage:
    """One strictly decoded PAL RLE fixture used by host previews."""

    source: str
    width: int
    height: int
    indices: bytes
    opaque: bytes

    def __post_init__(self) -> None:
        area = self.width * self.height
        if self.width <= 0 or self.height <= 0:
            raise PreviewError(f"{self.source}: non-positive image extent")
        if len(self.indices) != area or len(self.opaque) != area:
            raise PreviewError(f"{self.source}: decoded image size mismatch")


def decode_pal_rle(data: bytes, *, source: str = "<memory>") -> IndexedImage:
    """Decode one native PAL RLE record for visual host verification.

    This decoder is preview-only.  Target code consumes the unchanged RLE
    stream through ``PalUiRle_ComposeRgb565Strip`` and never allocates a
    decoded sprite.
    """

    header = 4 if data[:4] == b"\x02\0\0\0" else 0
    if len(data) < header + 4:
        raise PreviewError(f"{source}: short PAL RLE header")
    width = int.from_bytes(data[header : header + 2], "little")
    height = int.from_bytes(data[header + 2 : header + 4], "little")
    if not (0 < width <= 320 and 0 < height <= 200):
        raise PreviewError(
            f"{source}: invalid PAL RLE extent {width}x{height}"
        )

    area = width * height
    indices = bytearray(area)
    opaque = bytearray(area)
    source_index = 0
    cursor = header + 4
    while source_index < area:
        if cursor >= len(data):
            raise PreviewError(f"{source}: truncated PAL RLE token")
        token = data[cursor]
        cursor += 1
        transparent = bool(token & 0x80) and token <= 0x80 + width
        count = token - 0x80 if transparent else token
        if count <= 0 or count > area - source_index:
            raise PreviewError(f"{source}: invalid PAL RLE span {count}")
        if transparent:
            source_index += count
            continue
        if cursor + count > len(data):
            raise PreviewError(f"{source}: truncated PAL RLE literal")
        indices[source_index : source_index + count] = data[
            cursor : cursor + count
        ]
        opaque[source_index : source_index + count] = b"\x01" * count
        cursor += count
        source_index += count
    return IndexedImage(
        source=source,
        width=width,
        height=height,
        indices=bytes(indices),
        opaque=bytes(opaque),
    )


def decode_pal_palette(data: bytes) -> tuple[tuple[int, int, int], ...]:
    """Decode the first 256-color, six-bit PAL palette."""

    if len(data) < 256 * 3:
        raise PreviewError("short PAL palette")
    components = data[: 256 * 3]
    if any(component > 63 for component in components):
        raise PreviewError("PAL palette component exceeds six bits")
    return tuple(
        (
            components[index] * 4,
            components[index + 1] * 4,
            components[index + 2] * 4,
        )
        for index in range(0, 256 * 3, 3)
    )


def _sequence(value: object, context: str) -> Sequence[Any]:
    if not isinstance(value, Sequence) or isinstance(
        value, (str, bytes, bytearray)
    ):
        raise PreviewError(f"{context} must be an array")
    return value


def _mapping(value: object, context: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise PreviewError(f"{context} must be an object")
    return value


def _integer(value: object, context: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise PreviewError(f"{context} must be an integer")
    return value


def _rect(
    value: object, context: str
) -> tuple[int, int, int, int]:
    if isinstance(value, Sequence) and not isinstance(
        value, (str, bytes, bytearray)
    ):
        if len(value) != 4:
            raise PreviewError(f"{context} must contain four integers")
        return tuple(
            _integer(item, f"{context}[{index}]")
            for index, item in enumerate(value)
        )  # type: ignore[return-value]
    rect = _mapping(value, context)
    width_key = "w" if "w" in rect else "width"
    height_key = "h" if "h" in rect else "height"
    return (
        _integer(rect.get("x"), f"{context}.x"),
        _integer(rect.get("y"), f"{context}.y"),
        _integer(rect.get(width_key), f"{context}.{width_key}"),
        _integer(rect.get(height_key), f"{context}.{height_key}"),
    )


class Canvas:
    """Tiny RGB canvas; previews must not require Pillow."""

    def __init__(
        self, width: int, height: int, color: tuple[int, int, int]
    ) -> None:
        if width <= 0 or height <= 0:
            raise PreviewError("preview dimensions must be positive")
        self.width = width
        self.height = height
        self.pixels = bytearray(color * (width * height))

    def _set(self, x: int, y: int, color: tuple[int, int, int]) -> None:
        if 0 <= x < self.width and 0 <= y < self.height:
            offset = (y * self.width + x) * 3
            self.pixels[offset : offset + 3] = bytes(color)

    def _require_rect(
        self,
        rect: tuple[int, int, int, int],
        context: str,
    ) -> None:
        x, y, width, height = rect
        if (
            width < 0
            or height < 0
            or x < 0
            or y < 0
            or x + width > self.width
            or y + height > self.height
        ):
            raise PreviewError(
                f"{context} rectangle {rect!r} exceeds "
                f"{self.width}x{self.height}"
            )

    def fill(
        self,
        rect: tuple[int, int, int, int],
        color: tuple[int, int, int],
    ) -> None:
        self._require_rect(rect, "fill")
        x, y, width, height = rect
        row = bytes(color) * width
        for yy in range(y, y + height):
            offset = (yy * self.width + x) * 3
            self.pixels[offset : offset + len(row)] = row

    def frame(
        self,
        rect: tuple[int, int, int, int],
        color: tuple[int, int, int],
        thickness: int = 1,
    ) -> None:
        self._require_rect(rect, "frame")
        x, y, width, height = rect
        if width <= 0 or height <= 0:
            return
        for inset in range(max(1, thickness)):
            left = x + inset
            top = y + inset
            right = x + width - 1 - inset
            bottom = y + height - 1 - inset
            if left > right or top > bottom:
                break
            for xx in range(left, right + 1):
                self._set(xx, top, color)
                self._set(xx, bottom, color)
            for yy in range(top, bottom + 1):
                self._set(left, yy, color)
                self._set(right, yy, color)

    def line(
        self,
        first: tuple[int, int],
        second: tuple[int, int],
        color: tuple[int, int, int],
    ) -> None:
        """Draw an integer Bresenham line clipped to the canvas."""

        x0, y0 = first
        x1, y1 = second
        delta_x = abs(x1 - x0)
        step_x = 1 if x0 < x1 else -1
        delta_y = -abs(y1 - y0)
        step_y = 1 if y0 < y1 else -1
        error = delta_x + delta_y
        while True:
            self._set(x0, y0, color)
            if x0 == x1 and y0 == y1:
                break
            doubled = error * 2
            if doubled >= delta_y:
                error += delta_y
                x0 += step_x
            if doubled <= delta_x:
                error += delta_x
                y0 += step_y

    def crosshair(
        self,
        point: tuple[int, int],
        color: tuple[int, int, int],
        radius: int = 3,
    ) -> None:
        x, y = point
        self.line((x - radius, y), (x + radius, y), color)
        self.line((x, y - radius), (x, y + radius), color)

    def text(
        self,
        rect: tuple[int, int, int, int],
        value: str,
        glyphs: Mapping[int, Font10Glyph],
        color: tuple[int, int, int],
        *,
        align: str = "left",
    ) -> None:
        """Draw actual FONT10 pixels and fail closed on any clipping."""

        self._require_rect(rect, "text")
        x, y, width, height = rect
        records: list[Font10Glyph] = []
        for character in value:
            glyph = glyphs.get(ord(character))
            if glyph is None:
                raise PreviewError(
                    f"preview FONT10 lacks U+{ord(character):04X}"
                )
            records.append(glyph)
        text_width = sum(glyph.advance for glyph in records)
        if text_width > width or height < FONT10_CELL_HEIGHT:
            raise PreviewError(
                f"text {value!r} needs {text_width}x"
                f"{FONT10_CELL_HEIGHT}, has {width}x{height}"
            )
        if align == "right":
            cursor_x = x + width - text_width
        elif align == "center":
            cursor_x = x + (width - text_width) // 2
        elif align == "left":
            cursor_x = x
        else:
            raise PreviewError(f"unsupported text alignment {align!r}")
        glyph_y = y + (height - FONT10_CELL_HEIGHT) // 2
        for glyph in records:
            for glyph_row in range(FONT10_CELL_HEIGHT):
                for glyph_column in range(10):
                    if glyph.pixel(glyph_column, glyph_row):
                        pixel_x = cursor_x + glyph_column
                        pixel_y = glyph_y + glyph_row
                        if not (
                            x <= pixel_x < x + width
                            and y <= pixel_y < y + height
                        ):
                            raise PreviewError(
                                f"glyph U+{glyph.codepoint:04X} paints "
                                "outside its generated text rectangle"
                            )
                        self._set(
                            pixel_x,
                            pixel_y,
                            color,
                        )
            cursor_x += glyph.advance

    def indexed_nearest(
        self,
        image: IndexedImage,
        destination: tuple[int, int, int, int],
        palette: Sequence[tuple[int, int, int]],
    ) -> None:
        """Composite one real RLE fixture with target-equivalent sampling."""

        self._require_rect(destination, "indexed image")
        if len(palette) != 256:
            raise PreviewError("indexed preview palette must have 256 colors")
        x, y, width, height = destination
        if width <= 0 or height <= 0:
            raise PreviewError("indexed preview destination is empty")
        for destination_y in range(height):
            source_y = (
                (destination_y * 2 + 1) * image.height
            ) // (height * 2)
            for destination_x in range(width):
                source_x = (
                    (destination_x * 2 + 1) * image.width
                ) // (width * 2)
                source_offset = source_y * image.width + source_x
                if image.opaque[source_offset]:
                    self._set(
                        x + destination_x,
                        y + destination_y,
                        palette[image.indices[source_offset]],
                    )

    def ppm_bytes(self) -> bytes:
        return (
            f"P6\n{self.width} {self.height}\n255\n".encode("ascii")
            + bytes(self.pixels)
        )


def _catalog_destination(
    profile: Mapping[str, Any],
    *,
    screen_name: str,
    role: str,
    image: IndexedImage,
) -> tuple[int, int, int, int] | None:
    aliases = {
        "equipment_preview": "item_preview",
    }
    asset_class = aliases.get(role, role)
    sampling = _sequence(profile.get("sampling", ()), "sampling")
    matches: list[tuple[int, int, int, int]] = []
    for index, value in enumerate(sampling):
        policy = _mapping(value, f"sampling[{index}]")
        if (
            not bool(policy.get("catalog", False))
            or str(policy.get("screen", "")) != screen_name
            or str(policy.get("asset_class", "")) != asset_class
            or list(_sequence(policy.get("source", ()), "sampling.source"))
            != [image.width, image.height]
            or not bool(policy.get("visible", False))
            or policy.get("destination") is None
        ):
            continue
        matches.append(
            _rect(policy.get("destination"), "sampling.destination")
        )
    if len(matches) > 1:
        raise PreviewError(
            f"ambiguous preview sampling for {screen_name}:{asset_class}:"
            f"{image.width}x{image.height}"
        )
    return matches[0] if matches else None


def _catalog_destination_size(
    profile: Mapping[str, Any],
    *,
    screen_name: str,
    role: str,
    image: IndexedImage,
) -> tuple[int, int]:
    sampling = _sequence(profile.get("sampling", ()), "sampling")
    matches: list[tuple[int, int]] = []
    for index, value in enumerate(sampling):
        policy = _mapping(value, f"sampling[{index}]")
        if (
            not bool(policy.get("catalog", False))
            or str(policy.get("screen", "")) != screen_name
            or str(policy.get("asset_class", "")) != role
            or list(_sequence(policy.get("source", ()), "sampling.source"))
            != [image.width, image.height]
            or not bool(policy.get("visible", False))
        ):
            continue
        size = _sequence(
            policy.get("destination_size", ()),
            "sampling.destination_size",
        )
        if len(size) != 2:
            raise PreviewError("sampling destination size arity mismatch")
        matches.append(
            (
                _integer(size[0], "sampling.destination_size[0]"),
                _integer(size[1], "sampling.destination_size[1]"),
            )
        )
    if len(matches) != 1:
        raise PreviewError(
            f"expected one preview sampling for "
            f"{screen_name}:{role}:{image.width}x{image.height}, "
            f"got {len(matches)}"
        )
    return matches[0]


def _draw_battle_fixtures(
    canvas: Canvas,
    profile: Mapping[str, Any],
    assets: Mapping[str, IndexedImage],
    palette: Sequence[tuple[int, int, int]],
) -> None:
    vectors = _sequence(profile.get("camera_vectors", ()), "camera_vectors")
    fit_all: Mapping[str, Any] | None = None
    for index, value in enumerate(vectors):
        vector = _mapping(value, f"camera_vectors[{index}]")
        if (
            str(vector.get("kind", "")) == "battle"
            and str(vector.get("mode", "")) == "fit_all"
        ):
            fit_all = vector
            break
    if fit_all is None:
        raise PreviewError("battle HUD preview has no fit-all camera vector")

    world = _rect(fit_all.get("bounds"), "battle camera.bounds")
    screen = _rect(fit_all.get("screen"), "battle camera.screen")
    scale = _sequence(fit_all.get("scale"), "battle camera.scale")
    if len(scale) != 2:
        raise PreviewError("battle camera scale arity mismatch")
    numerator = _integer(scale[0], "battle camera.scale[0]")
    denominator = _integer(scale[1], "battle camera.scale[1]")
    placements = (
        ("battle_player", "player"),
        ("battle_enemy", "target"),
    )
    for role, subject_key in placements:
        image = assets.get(role)
        subject_value = fit_all.get(subject_key)
        if image is None or subject_value is None:
            raise PreviewError(f"battle fixture lacks {role}/{subject_key}")
        subject = _project_camera_rect(
            _rect(subject_value, f"battle camera.{subject_key}"),
            world,
            screen,
            numerator,
            denominator,
        )
        destination_width, destination_height = _catalog_destination_size(
            profile,
            screen_name="battle_hud",
            role=role,
            image=image,
        )
        destination = (
            subject[0] + subject[2] // 2 - destination_width // 2,
            subject[1] + subject[3] - destination_height,
            destination_width,
            destination_height,
        )
        canvas.indexed_nearest(image, destination, palette)


def _element_text_color(kind: str) -> tuple[int, int, int]:
    if kind == "title":
        return _TITLE_TEXT
    if kind == "page_indicator":
        return _PAGE_TEXT
    return _TEXT


def _visible_selected_key(
    screen: Mapping[str, Any],
    elements: Sequence[Any],
    page: int,
) -> str:
    selected = str(screen.get("selected_key", ""))
    focus_order = tuple(
        str(item)
        for item in _sequence(screen.get("focus_order", ()), "focus_order")
    )
    visible = {
        str(_mapping(value, "element").get("name", ""))
        for value in elements
        if _integer(_mapping(value, "element").get("page", 0), "element.page")
        == page
        and bool(_mapping(value, "element").get("selectable", False))
    }
    if selected in visible:
        return selected
    return next((key for key in focus_order if key in visible), "")


def render_screen(
    profile: Mapping[str, Any],
    screen: Mapping[str, Any],
    *,
    page: int = 0,
    font: Font10 | None = None,
    assets: Mapping[str, IndexedImage] | None = None,
    palette: Sequence[tuple[int, int, int]] | None = None,
) -> Canvas:
    """Render one resolved page with real FONT10 and optional real RLE art."""

    display = _mapping(profile.get("display"), "display")
    width = _integer(display.get("width"), "display.width")
    height = _integer(display.get("height"), "display.height")
    canvas = Canvas(width, height, _BACKGROUND)
    safe = _rect(profile.get("safe_rect"), "safe_rect")
    stage = _rect(profile.get("stage_rect"), "stage_rect")
    canvas.fill(safe, _SAFE)
    canvas.fill(stage, _STAGE)
    canvas.frame(safe, (105, 120, 145))
    canvas.frame(stage, (110, 135, 165))

    elements = _sequence(screen.get("elements"), "elements")
    page_count = _integer(screen.get("page_count", 1), "page_count")
    if not 0 <= page < page_count:
        raise PreviewError(f"page {page} is outside 0..{page_count - 1}")
    selected_key = _visible_selected_key(screen, elements, page)
    glyphs = font.by_codepoint() if font is not None else None
    screen_name = str(screen.get("name", ""))

    focus_value = screen.get("focus")
    if focus_value is not None:
        focus = _rect(focus_value, "focus")
        canvas.frame(focus, _PLAYER_FOCUS)
        canvas.crosshair(
            (focus[0] + focus[2] // 2, focus[1] + focus[3] // 2),
            _PLAYER_FOCUS,
        )
    if (
        screen_name == "battle_hud"
        and assets is not None
        and palette is not None
    ):
        _draw_battle_fixtures(canvas, profile, assets, palette)

    for index, value in enumerate(elements):
        element = _mapping(value, f"elements[{index}]")
        element_page = _integer(
            element.get("page", 0), f"elements[{index}].page"
        )
        if element_page != page:
            continue
        rect = _rect(element.get("rect"), f"elements[{index}].rect")
        canvas._require_rect(rect, f"elements[{index}]")
        kind = str(element.get("kind", ""))
        name = str(element.get("name", ""))
        text_value = str(element.get("text", ""))
        selected = bool(element.get("selectable", False)) and (
            name == selected_key
        )
        color = _ELEMENT_COLORS[index % len(_ELEMENT_COLORS)]

        if kind == "panel":
            canvas.fill(rect, (13, 24, 42))
            canvas.frame(rect, (80, 116, 156))
            continue

        image = None
        if assets is not None:
            image = assets.get(
                "item_preview" if kind == "equipment_preview" else kind
            )
        if image is not None and palette is not None:
            destination = _catalog_destination(
                profile,
                screen_name=screen_name,
                role=kind,
                image=image,
            )
            if destination is None:
                if not kind.startswith("battle_"):
                    raise PreviewError(
                        f"no generated preview sampling for "
                        f"{screen_name}:{kind}:"
                        f"{image.width}x{image.height}"
                    )
            else:
                canvas.fill(rect, tuple(component // 5 for component in color))
                canvas.frame(rect, color)
                canvas.indexed_nearest(image, destination, palette)
                continue

        if text_value:
            canvas.fill(
                rect,
                _FOCUS_BACKGROUND
                if selected
                else tuple(component // 7 for component in color),
            )
            if glyphs is not None:
                canvas.text(
                    rect,
                    text_value,
                    glyphs,
                    _FOCUS if selected else _element_text_color(kind),
                    align="right" if kind == "page_indicator" else "left",
                )
            else:
                canvas.frame(rect, color)
            continue

        canvas.fill(rect, tuple(component // 3 for component in color))
        canvas.frame(rect, color)
    return canvas


def render_loading(
    profile: Mapping[str, Any],
    *,
    percent: int = 50,
) -> Canvas:
    """Render the exact generated chapter-cache LOADING chrome."""

    display = _mapping(profile.get("display"), "display")
    width = _integer(display.get("width"), "display.width")
    height = _integer(display.get("height"), "display.height")
    loading = _mapping(profile.get("loading"), "loading")
    label = _rect(loading.get("label_rect"), "loading.label_rect")
    bar = _rect(loading.get("bar_rect"), "loading.bar_rect")
    glyph_width = _integer(
        loading.get("glyph_width"), "loading.glyph_width"
    )
    glyph_height = _integer(
        loading.get("glyph_height"), "loading.glyph_height"
    )
    glyph_advance = _integer(
        loading.get("glyph_advance"), "loading.glyph_advance"
    )
    glyph_scale = _integer(
        loading.get("glyph_scale"), "loading.glyph_scale"
    )
    glyph_count = _integer(
        loading.get("glyph_count"), "loading.glyph_count"
    )
    border = _integer(loading.get("bar_border"), "loading.bar_border")
    progress_max = _integer(
        loading.get("progress_max"), "loading.progress_max"
    )
    if (
        glyph_width != 5
        or glyph_height != 7
        or glyph_count != len(_LOADING_GLYPHS)
        or glyph_advance < glyph_width
        or glyph_scale <= 0
        or border <= 0
        or progress_max <= 0
        or not 0 <= percent <= progress_max
    ):
        raise PreviewError("invalid generated LOADING coefficients")

    canvas = Canvas(width, height, (0, 8, 24))
    canvas._require_rect(label, "loading label")
    canvas._require_rect(bar, "loading bar")
    for character, rows in enumerate(_LOADING_GLYPHS):
        origin_x = label[0] + character * glyph_advance * glyph_scale
        for row, bits in enumerate(rows):
            for column in range(glyph_width):
                if bits & (1 << (glyph_width - 1 - column)):
                    canvas.fill(
                        (
                            origin_x + column * glyph_scale,
                            label[1] + row * glyph_scale,
                            glyph_scale,
                            glyph_scale,
                        ),
                        (232, 248, 255),
                    )

    canvas.fill(bar, (232, 248, 255))
    inner = (
        bar[0] + border,
        bar[1] + border,
        bar[2] - border * 2,
        bar[3] - border * 2,
    )
    if inner[2] <= 0 or inner[3] <= 0:
        raise PreviewError("LOADING bar border consumes the bar")
    canvas.fill(inner, (24, 48, 72))
    filled = inner[2] * percent // progress_max
    if filled:
        canvas.fill(
            (inner[0], inner[1], filled, inner[3]),
            (0, 184, 248),
        )
    return canvas


def render_asset_fixture(
    profile: Mapping[str, Any],
    role: str,
    image: IndexedImage,
    palette: Sequence[tuple[int, int, int]],
) -> Canvas:
    """Render one real source asset at its generated certified scale."""

    display = _mapping(profile.get("display"), "display")
    width = _integer(display.get("width"), "display.width")
    height = _integer(display.get("height"), "display.height")
    safe = _rect(profile.get("safe_rect"), "safe_rect")
    stage = _rect(profile.get("stage_rect"), "stage_rect")
    screen_name = {
        "portrait": "status",
        "item_preview": "equip",
        "battle_player": "battle_hud",
        "battle_enemy": "battle_hud",
        "battle_fire": "battle_hud",
        "ui_sprite": "battle_hud",
        "battle_effect": "battle_hud",
    }.get(role)
    if screen_name is None:
        raise PreviewError(f"unsupported preview asset role {role!r}")
    destination_width, destination_height = _catalog_destination_size(
        profile,
        screen_name=screen_name,
        role=role,
        image=image,
    )
    destination = (
        stage[0] + (stage[2] - destination_width) // 2,
        stage[1] + (stage[3] - destination_height) // 2,
        destination_width,
        destination_height,
    )
    canvas = Canvas(width, height, _BACKGROUND)
    canvas.fill(safe, _SAFE)
    canvas.fill(stage, _STAGE)
    canvas.frame(safe, (105, 120, 145))
    canvas.frame(stage, (110, 135, 165))
    canvas.indexed_nearest(image, destination, palette)
    canvas.frame(destination, _CAMERA_CENTER)
    return canvas


def _project_axis(
    coordinate: int,
    world_start: int,
    screen_start: int,
    numerator: int,
    denominator: int,
) -> int:
    return screen_start + (
        (coordinate - world_start) * numerator // denominator
    )


def _project_camera_rect(
    rect: tuple[int, int, int, int],
    world: tuple[int, int, int, int],
    screen: tuple[int, int, int, int],
    numerator: int,
    denominator: int,
) -> tuple[int, int, int, int]:
    left = _project_axis(
        rect[0], world[0], screen[0], numerator, denominator
    )
    top = _project_axis(
        rect[1], world[1], screen[1], numerator, denominator
    )
    right = screen[0] - (
        -((rect[0] + rect[2] - world[0]) * numerator)
        // denominator
    )
    bottom = screen[1] - (
        -((rect[1] + rect[3] - world[1]) * numerator)
        // denominator
    )
    return left, top, right - left, bottom - top


def render_camera_vector(
    profile: Mapping[str, Any],
    vector: Mapping[str, Any],
) -> Canvas:
    """Render a generated map/battle camera vector as a visual diagnostic."""

    display = _mapping(profile.get("display"), "display")
    width = _integer(display.get("width"), "display.width")
    height = _integer(display.get("height"), "display.height")
    safe = _rect(profile.get("safe_rect"), "safe_rect")
    world = _rect(vector.get("bounds"), "camera.bounds")
    screen = _rect(vector.get("screen"), "camera.screen")
    focus = _sequence(vector.get("focus"), "camera.focus")
    projected = _sequence(
        vector.get("projected_focus"), "camera.projected_focus"
    )
    scale = _sequence(vector.get("scale"), "camera.scale")
    if len(focus) != 2 or len(projected) != 2 or len(scale) != 2:
        raise PreviewError("camera point/scale arity mismatch")
    numerator = _integer(scale[0], "camera.scale[0]")
    denominator = _integer(scale[1], "camera.scale[1]")
    if numerator <= 0 or denominator <= 0:
        raise PreviewError("camera scale must be positive")

    canvas = Canvas(width, height, _BACKGROUND)
    canvas.fill(safe, _SAFE)
    canvas.fill(screen, _STAGE)
    canvas.frame(safe, (105, 120, 145))
    canvas.frame(
        screen,
        _TARGET_FOCUS if bool(vector.get("clamped", False)) else _PLAYER_FOCUS,
    )

    grid = 32
    first_x = ((world[0] + grid - 1) // grid) * grid
    for world_x in range(first_x, world[0] + world[2], grid):
        x = _project_axis(
            world_x, world[0], screen[0], numerator, denominator
        )
        canvas.line(
            (x, screen[1]),
            (x, screen[1] + screen[3] - 1),
            (47, 65, 88),
        )
    first_y = ((world[1] + grid - 1) // grid) * grid
    for world_y in range(first_y, world[1] + world[3], grid):
        y = _project_axis(
            world_y, world[1], screen[1], numerator, denominator
        )
        canvas.line(
            (screen[0], y),
            (screen[0] + screen[2] - 1, y),
            (47, 65, 88),
        )

    if str(vector.get("kind", "")) == "battle":
        subjects = (
            ("player", _PLAYER_FOCUS, 1),
            ("actor", _CAMERA_CENTER, 2),
            ("target", _TARGET_FOCUS, 2),
        )
        for key, color, thickness in subjects:
            value = vector.get(key)
            if value is None:
                continue
            subject = _rect(value, f"camera.{key}")
            projected_subject = _project_camera_rect(
                subject,
                world,
                screen,
                numerator,
                denominator,
            )
            canvas.frame(projected_subject, color, thickness)

    screen_center = (
        screen[0] + screen[2] // 2,
        screen[1] + screen[3] // 2,
    )
    canvas.crosshair(screen_center, _CAMERA_CENTER, 4)
    projected_point = (
        _integer(projected[0], "camera.projected_focus[0]"),
        _integer(projected[1], "camera.projected_focus[1]"),
    )
    calculated = (
        _project_axis(
            _integer(focus[0], "camera.focus[0]"),
            world[0],
            screen[0],
            numerator,
            denominator,
        ),
        _project_axis(
            _integer(focus[1], "camera.focus[1]"),
            world[1],
            screen[1],
            numerator,
            denominator,
        ),
    )
    if calculated != projected_point:
        raise PreviewError(
            f"camera projected focus {projected_point} != {calculated}"
        )
    canvas.crosshair(projected_point, _PLAYER_FOCUS, 5)
    return canvas


def write_profile_previews(
    profile: Mapping[str, Any],
    output_dir: Path,
    *,
    png: bool = False,
    font: Font10 | None = None,
    assets: Mapping[str, IndexedImage] | None = None,
    palette: Sequence[tuple[int, int, int]] | None = None,
) -> list[Path]:
    """Write every resolved page as PPM and optionally lossless PNG."""

    output_dir.mkdir(parents=True, exist_ok=True)
    written: list[Path] = []
    screens = _sequence(profile.get("screens"), "screens")
    for screen_index, value in enumerate(screens):
        screen = _mapping(value, f"screens[{screen_index}]")
        name = str(screen.get("name", f"screen_{screen_index}"))
        safe_name = "".join(
            char if char.isalnum() or char in "-_" else "_"
            for char in name
        )
        page_count = _integer(
            screen.get("page_count", 1),
            f"screens[{screen_index}].page_count",
        )
        for page in range(page_count):
            canvas = render_screen(
                profile,
                screen,
                page=page,
                font=font,
                assets=assets,
                palette=palette,
            )
            stem = f"{safe_name}-p{page + 1}"
            ppm_path = output_dir / f"{stem}.ppm"
            ppm_path.write_bytes(canvas.ppm_bytes())
            written.append(ppm_path)
            if png:
                try:
                    from PIL import Image
                except ImportError as exc:
                    raise PreviewError(
                        "PNG output requested but Pillow is unavailable"
                    ) from exc
                png_path = output_dir / f"{stem}.png"
                image = Image.frombytes(
                    "RGB",
                    (canvas.width, canvas.height),
                    bytes(canvas.pixels),
                )
                image.save(png_path, format="PNG", optimize=False)
                written.append(png_path)
    return written
