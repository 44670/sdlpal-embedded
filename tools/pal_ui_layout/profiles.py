"""Display profile primitives shared by the layout compiler and its tests."""

from __future__ import annotations

from dataclasses import dataclass
import re


PAL_STAGE_WIDTH = 320
PAL_STAGE_HEIGHT = 200
PAL_LEGACY_PLAYER_X = 160
PAL_LEGACY_PLAYER_Y = 112


@dataclass(frozen=True, order=True)
class ProfileRect:
    """An integer rectangle with an exclusive right/bottom edge."""

    x: int
    y: int
    width: int
    height: int

    @property
    def right(self) -> int:
        return self.x + self.width

    @property
    def bottom(self) -> int:
        return self.y + self.height

    @property
    def center_x(self) -> int:
        return self.x + self.width // 2

    @property
    def center_y(self) -> int:
        return self.y + self.height // 2

    def contains(self, other: "ProfileRect") -> bool:
        return (
            other.width >= 0
            and other.height >= 0
            and self.x <= other.x
            and self.y <= other.y
            and other.right <= self.right
            and other.bottom <= self.bottom
        )


@dataclass(frozen=True)
class DisplayProfile:
    """Physical display and safe-area inputs to the compiler.

    ``safe_*`` values are in physical pixels.  A zero margin means the whole
    display is available.  The target implementation may reserve a smaller
    safe area for a board bezel or a persistent hardware overlay without
    changing any screen solver.
    """

    width: int
    height: int
    safe_left: int = 0
    safe_top: int = 0
    safe_right: int = 0
    safe_bottom: int = 0
    font_pixel_size: int = 10

    def __post_init__(self) -> None:
        if self.width <= 0 or self.height <= 0:
            raise ValueError("display dimensions must be positive")
        if min(
            self.safe_left,
            self.safe_top,
            self.safe_right,
            self.safe_bottom,
        ) < 0:
            raise ValueError("safe-area margins must be non-negative")
        if self.safe_left + self.safe_right >= self.width:
            raise ValueError("horizontal safe-area margins consume the display")
        if self.safe_top + self.safe_bottom >= self.height:
            raise ValueError("vertical safe-area margins consume the display")
        if self.font_pixel_size != 10:
            raise ValueError("the native UI contract fixes the font at 10px")

    @property
    def name(self) -> str:
        return f"{self.width}x{self.height}"

    @property
    def screen_rect(self) -> ProfileRect:
        return ProfileRect(0, 0, self.width, self.height)

    @property
    def safe_rect(self) -> ProfileRect:
        return ProfileRect(
            self.safe_left,
            self.safe_top,
            self.width - self.safe_left - self.safe_right,
            self.height - self.safe_top - self.safe_bottom,
        )

    @property
    def player_anchor(self) -> tuple[int, int]:
        """Preferred physical focus point for map and battle cameras."""

        safe = self.safe_rect
        return safe.center_x, safe.center_y

    def contain_stage_rect(
        self,
        source_width: int = PAL_STAGE_WIDTH,
        source_height: int = PAL_STAGE_HEIGHT,
    ) -> ProfileRect:
        """Largest centered, aspect-preserving stage rectangle.

        The calculation is integer-only and intentionally rounds down.  It
        therefore gives the existing exact Cardputer mappings:
        320x200 -> 216x135 on 240x135 and -> 160x100 on 160x128.
        """

        if source_width <= 0 or source_height <= 0:
            raise ValueError("source dimensions must be positive")

        safe = self.safe_rect
        if safe.width * source_height <= safe.height * source_width:
            width = safe.width
            height = source_height * width // source_width
        else:
            height = safe.height
            width = source_width * height // source_height
        return ProfileRect(
            safe.x + (safe.width - width) // 2,
            safe.y + (safe.height - height) // 2,
            width,
            height,
        )

    def legacy_player_presented(
        self,
        source_width: int = PAL_STAGE_WIDTH,
        source_height: int = PAL_STAGE_HEIGHT,
    ) -> tuple[int, int]:
        """Where legacy (160,112) appears under the contain transform."""

        stage = self.contain_stage_rect(source_width, source_height)
        x = stage.x + (
            PAL_LEGACY_PLAYER_X * stage.width + source_width // 2
        ) // source_width
        y = stage.y + (
            PAL_LEGACY_PLAYER_Y * stage.height + source_height // 2
        ) // source_height
        return x, y


_RESOLUTION_RE = re.compile(r"^(?P<width>[1-9][0-9]*)[xX](?P<height>[1-9][0-9]*)$")


def parse_resolution(value: str) -> DisplayProfile:
    match = _RESOLUTION_RE.fullmatch(value.strip())
    if match is None:
        raise ValueError(f"invalid resolution {value!r}; expected WIDTHxHEIGHT")
    return DisplayProfile(
        width=int(match.group("width")),
        height=int(match.group("height")),
    )


def loading_layout(profile: DisplayProfile) -> dict[str, object]:
    """Resolve the target cache-loading chrome in physical pixels.

    The target keeps a tiny built-in 5x7 ``LOADING`` bitmap because this
    screen is shown while a chapter pack is being rebuilt.  Its placement and
    magnification are still profile-owned: target C only consumes the emitted
    integers and never derives a display-specific layout.
    """

    safe = profile.safe_rect
    glyph_width = 5
    glyph_height = 7
    glyph_advance = 6
    glyph_count = 7
    glyph_scale = max(1, min(3, safe.width // 80))
    label_width = glyph_count * glyph_advance * glyph_scale
    label_height = glyph_height * glyph_scale
    label_x = safe.x + (safe.width - label_width) // 2
    label_y = safe.y + safe.height // 5 - glyph_scale + 1

    bar_margin = max(1, safe.width // 12)
    bar_height = glyph_scale * 6
    bar_border = max(1, glyph_scale - 1)
    return {
        "label_rect": [
            label_x,
            label_y,
            label_width,
            label_height,
        ],
        "glyph_width": glyph_width,
        "glyph_height": glyph_height,
        "glyph_advance": glyph_advance,
        "glyph_scale": glyph_scale,
        "glyph_count": glyph_count,
        "bar_rect": [
            safe.x + bar_margin,
            safe.y + safe.height * 3 // 5,
            safe.width - 2 * bar_margin,
            bar_height,
        ],
        "bar_border": bar_border,
        "progress_max": 100,
    }


def certified_profiles() -> tuple[DisplayProfile, ...]:
    return (
        DisplayProfile(240, 135),
        DisplayProfile(160, 128),
    )
