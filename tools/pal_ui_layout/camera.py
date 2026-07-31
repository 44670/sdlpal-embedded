"""Integer-only map and battle camera reference implementation.

The PAL simulation remains in its canonical 320x200 coordinate space.  These
functions derive presentation-only source viewports and uniform sampling
coefficients.  Their outputs are plain integers suitable for generated C
tables; no result is intended to be written back into gameplay or save state.
"""

from __future__ import annotations

from dataclasses import dataclass
from fractions import Fraction
from math import gcd
from typing import Iterable, Literal, Sequence

from .geometry import Point, Rect, Size, union_rects
from .profiles import (
    PAL_LEGACY_PLAYER_X,
    PAL_LEGACY_PLAYER_Y,
    PAL_STAGE_HEIGHT,
    PAL_STAGE_WIDTH,
    DisplayProfile,
)


Q16_SHIFT = 16
Q16_ONE = 1 << Q16_SHIFT
LEGACY_STAGE_SIZE = Size(PAL_STAGE_WIDTH, PAL_STAGE_HEIGHT)
LEGACY_PLAYER_ANCHOR = Point(PAL_LEGACY_PLAYER_X, PAL_LEGACY_PLAYER_Y)
CameraMode = Literal["map_native", "map_stage", "battle_focus", "battle_fit_all"]


def _clamp(value: int, lower: int, upper: int) -> int:
    if lower > upper:
        raise ValueError("invalid clamp interval")
    return min(max(value, lower), upper)


def _ceil_div(numerator: int, denominator: int) -> int:
    if denominator <= 0:
        raise ValueError("denominator must be positive")
    return -((-numerator) // denominator)


def _coerce_point(value: Point | tuple[int, int]) -> Point:
    if isinstance(value, Point):
        return value
    if len(value) != 2:
        raise ValueError("point tuple must contain exactly two integers")
    return Point(value[0], value[1])


def _coerce_rect(value: object) -> Rect:
    if isinstance(value, Rect):
        return value
    try:
        x = getattr(value, "x")
        y = getattr(value, "y")
        if hasattr(value, "width"):
            width = getattr(value, "width")
            height = getattr(value, "height")
        else:
            width = getattr(value, "w")
            height = getattr(value, "h")
    except AttributeError as exc:
        raise TypeError("expected a Rect/ProfileRect-compatible value") from exc
    return Rect(x, y, width, height)


def _coerce_entity_rect(value: object) -> Rect:
    """Accept a sprite bound or a point represented as a one-pixel bound."""

    if isinstance(value, Point):
        return Rect(value.x, value.y, 1, 1)
    if isinstance(value, tuple) and len(value) == 2:
        point = _coerce_point(value)
        return Rect(point.x, point.y, 1, 1)
    rect = _coerce_rect(value)
    if rect.w <= 0 or rect.h <= 0:
        raise ValueError("battle entity bounds must be positive")
    return rect


@dataclass(frozen=True, slots=True)
class UniformScale:
    """A positive reduced rational scale shared by both axes."""

    numerator: int
    denominator: int

    def __post_init__(self) -> None:
        if (
            not isinstance(self.numerator, int)
            or isinstance(self.numerator, bool)
            or not isinstance(self.denominator, int)
            or isinstance(self.denominator, bool)
        ):
            raise TypeError("scale terms must be integers")
        if self.numerator <= 0 or self.denominator <= 0:
            raise ValueError("scale terms must be positive")
        divisor = gcd(self.numerator, self.denominator)
        object.__setattr__(self, "numerator", self.numerator // divisor)
        object.__setattr__(self, "denominator", self.denominator // divisor)

    @classmethod
    def from_axes(
        cls,
        x_numerator: int,
        x_denominator: int,
        y_numerator: int,
        y_denominator: int,
    ) -> "UniformScale":
        """Reject a transform that would use different X/Y scales."""

        if x_denominator <= 0 or y_denominator <= 0:
            raise ValueError("scale denominators must be positive")
        if x_numerator * y_denominator != y_numerator * x_denominator:
            raise ValueError("non-uniform camera scaling is forbidden")
        return cls(x_numerator, x_denominator)

    @classmethod
    def fit(
        cls,
        source: Size,
        destination: Size,
        *,
        allow_upscale: bool = False,
    ) -> "UniformScale":
        """Largest exact rational scale fitting *source* in *destination*."""

        if source.w <= 0 or source.h <= 0:
            raise ValueError("source dimensions must be positive")
        if destination.w <= 0 or destination.h <= 0:
            raise ValueError("destination dimensions must be positive")

        # Cross multiplication keeps the limiting-axis decision exact; the
        # horizontal axis wins an exact tie.
        if destination.w * source.h <= destination.h * source.w:
            numerator, denominator = destination.w, source.w
        else:
            numerator, denominator = destination.h, source.h
        if not allow_upscale and numerator > denominator:
            return cls(1, 1)
        return cls(numerator, denominator)

    @property
    def q16(self) -> int:
        return (self.numerator << Q16_SHIFT) // self.denominator

    @property
    def source_step_q16(self) -> int:
        """Q16 source pixels advanced per destination pixel."""

        return (self.denominator << Q16_SHIFT) // self.numerator

    @property
    def source_phase_q16(self) -> int:
        """Nearest-centre sampler phase paired with ``source_step_q16``."""

        return self.source_step_q16 // 2

    def floor(self, value: int) -> int:
        return value * self.numerator // self.denominator

    def ceil(self, value: int) -> int:
        return _ceil_div(value * self.numerator, self.denominator)

    def inverse_floor(self, value: int) -> int:
        return value * self.denominator // self.numerator


ONE_TO_ONE = UniformScale(1, 1)


def stage_scale(
    profile: DisplayProfile,
    source_size: Size = LEGACY_STAGE_SIZE,
) -> UniformScale:
    """Uniform source-to-display scale used for a contained PAL stage."""

    safe = Rect.from_rect_like(profile.safe_rect)
    return UniformScale.fit(source_size, safe.size)


@dataclass(frozen=True, slots=True)
class CameraFixedParams:
    """Renderer-facing integer coefficients suitable for a generated header."""

    source: Rect
    destination: Rect
    scale_numerator: int
    scale_denominator: int
    scale_q16: int
    source_step_q16: int
    source_phase_q16: int

    def as_manifest(self) -> dict[str, object]:
        return {
            "source": [self.source.x, self.source.y, self.source.w, self.source.h],
            "destination": [
                self.destination.x,
                self.destination.y,
                self.destination.w,
                self.destination.h,
            ],
            "scale": [self.scale_numerator, self.scale_denominator],
            "scale_q16": self.scale_q16,
            "source_step_q16": self.source_step_q16,
            "source_phase_q16": self.source_phase_q16,
        }


@dataclass(frozen=True, slots=True)
class CameraView:
    """One presentation-only view into canonical PAL coordinates."""

    mode: CameraMode
    world_rect: Rect
    screen_rect: Rect
    scale: UniformScale
    desired_origin: Point
    was_clamped: bool

    def __post_init__(self) -> None:
        if self.world_rect.w <= 0 or self.world_rect.h <= 0:
            raise ValueError("camera world rectangle must be positive")
        if self.screen_rect.w <= 0 or self.screen_rect.h <= 0:
            raise ValueError("camera screen rectangle must be positive")
        if self.scale.floor(self.world_rect.w) != self.screen_rect.w:
            raise ValueError("screen width does not match uniform scale")
        if self.scale.floor(self.world_rect.h) != self.screen_rect.h:
            raise ValueError("screen height does not match uniform scale")

    @property
    def fixed(self) -> CameraFixedParams:
        return CameraFixedParams(
            source=self.world_rect,
            destination=self.screen_rect,
            scale_numerator=self.scale.numerator,
            scale_denominator=self.scale.denominator,
            scale_q16=self.scale.q16,
            source_step_q16=self.scale.source_step_q16,
            source_phase_q16=self.scale.source_phase_q16,
        )

    def world_to_screen(self, point: Point | tuple[int, int]) -> Point:
        point = _coerce_point(point)
        return Point(
            self.screen_rect.x + self.scale.floor(point.x - self.world_rect.x),
            self.screen_rect.y + self.scale.floor(point.y - self.world_rect.y),
        )

    def screen_to_world(self, point: Point | tuple[int, int]) -> Point:
        point = _coerce_point(point)
        return Point(
            self.world_rect.x
            + self.scale.inverse_floor(point.x - self.screen_rect.x),
            self.world_rect.y
            + self.scale.inverse_floor(point.y - self.screen_rect.y),
        )

    def project_rect(self, rect: object) -> Rect:
        source = _coerce_rect(rect)
        left = self.screen_rect.x + self.scale.floor(
            source.left - self.world_rect.left
        )
        top = self.screen_rect.y + self.scale.floor(
            source.top - self.world_rect.top
        )
        right = self.screen_rect.x + self.scale.ceil(
            source.right - self.world_rect.left
        )
        bottom = self.screen_rect.y + self.scale.ceil(
            source.bottom - self.world_rect.top
        )
        return Rect(left, top, right - left, bottom - top)


@dataclass(frozen=True, slots=True)
class ProfileCameraConstants:
    """Per-display constants consumed by the generated target tables."""

    profile_name: str
    safe_rect: Rect
    player_anchor: Point
    stage_rect: Rect
    stage_scale: UniformScale
    stage_fixed: CameraFixedParams
    battle_policy: "BattleCameraPolicy"

    def as_manifest(self) -> dict[str, object]:
        return {
            "profile": self.profile_name,
            "safe_rect": [
                self.safe_rect.x,
                self.safe_rect.y,
                self.safe_rect.w,
                self.safe_rect.h,
            ],
            "player_anchor": [self.player_anchor.x, self.player_anchor.y],
            "stage": self.stage_fixed.as_manifest(),
            "battle": self.battle_policy.as_manifest(),
        }


@dataclass(frozen=True, slots=True)
class BattleCameraPolicy:
    """Generated HUD-free camera geometry used by the live C evaluator."""

    arena: Rect
    hud_rect: Rect
    content_rect: Rect
    focus_source: Size
    focus_screen: Rect
    fit_scale: UniformScale
    fit_screen: Rect
    padding: int = 4
    max_players: int = 3

    def __post_init__(self) -> None:
        if self.arena.w <= 0 or self.arena.h <= 0:
            raise ValueError("battle arena must be positive")
        if self.content_rect.w <= 0 or self.content_rect.h <= 0:
            raise ValueError("battle content rectangle must be positive")
        if self.hud_rect.intersects(self.content_rect):
            raise ValueError("battle content rectangle overlaps HUD")
        if self.focus_source.w <= 0 or self.focus_source.h <= 0:
            raise ValueError("battle focus viewport must be positive")
        if self.padding < 0 or not 0 < self.max_players <= 255:
            raise ValueError("battle policy limits are invalid")
        if self.fit_scale.floor(self.arena.w) != self.fit_screen.w or (
            self.fit_scale.floor(self.arena.h) != self.fit_screen.h
        ):
            raise ValueError("battle fit screen disagrees with scale")

    def as_manifest(self) -> dict[str, object]:
        def rect_values(rect: Rect) -> list[int]:
            return [rect.x, rect.y, rect.w, rect.h]

        return {
            "arena": rect_values(self.arena),
            "hud_rect": rect_values(self.hud_rect),
            "content_rect": rect_values(self.content_rect),
            "focus_source": [self.focus_source.w, self.focus_source.h],
            "focus_screen": rect_values(self.focus_screen),
            "fit_screen": rect_values(self.fit_screen),
            "fit_scale": [
                self.fit_scale.numerator,
                self.fit_scale.denominator,
            ],
            "fit_scale_q16": self.fit_scale.q16,
            "fit_step_q16": self.fit_scale.source_step_q16,
            "fit_phase_q16": self.fit_scale.source_phase_q16,
            "fit_sample_x": [
                (
                    (index * 2 + 1) * self.arena.w
                    // (self.fit_screen.w * 2)
                )
                for index in range(self.fit_screen.w)
            ],
            "fit_sample_y": [
                (
                    (index * 2 + 1) * self.arena.h
                    // (self.fit_screen.h * 2)
                )
                for index in range(self.fit_screen.h)
            ],
            "padding": self.padding,
            "max_players": self.max_players,
        }


def _largest_hud_free_rect(safe: Rect, hud: Rect) -> Rect:
    overlap = safe.intersection(hud)
    if overlap.is_empty:
        return safe
    candidates = (
        Rect(safe.x, overlap.bottom, safe.w, safe.bottom - overlap.bottom),
        Rect(safe.x, safe.y, safe.w, overlap.y - safe.y),
        Rect(overlap.right, safe.y, safe.right - overlap.right, safe.h),
        Rect(safe.x, safe.y, overlap.x - safe.x, safe.h),
    )
    positive = tuple(rect for rect in candidates if rect.w > 0 and rect.h > 0)
    if not positive:
        raise ValueError("battle HUD consumes the complete safe area")

    def score(rect: Rect) -> tuple[Fraction, int, int, int]:
        scale = min(
            Fraction(rect.w, PAL_STAGE_WIDTH),
            Fraction(rect.h, PAL_STAGE_HEIGHT),
            Fraction(1, 1),
        )
        return scale, rect.w * rect.h, rect.w, rect.h

    return max(positive, key=score)


def battle_camera_policy(
    profile: DisplayProfile,
    hud_rect: object,
    *,
    padding: int = 4,
    max_players: int = 3,
) -> BattleCameraPolicy:
    """Freeze a HUD exclusion and both live battle camera transforms."""

    safe = Rect.from_rect_like(profile.safe_rect)
    hud = _coerce_rect(hud_rect)
    content = _largest_hud_free_rect(safe, hud)
    arena = Rect(0, 0, PAL_STAGE_WIDTH, PAL_STAGE_HEIGHT)
    focus_size = Size(min(arena.w, content.w), min(arena.h, content.h))
    focus_screen = _centered_screen_rect(content, focus_size, ONE_TO_ONE)
    # A camera rectangle is projected with one exact rational on both axes.
    # Restrict the denominator to a divisor of gcd(source axes), so both
    # destination extents are integral and the projected arena cannot gain a
    # one-pixel ceil fringe at the HUD boundary.
    common = gcd(arena.w, arena.h)
    fit_units = min(
        common,
        content.w * common // arena.w,
        content.h * common // arena.h,
    )
    if fit_units <= 0:
        raise ValueError("battle content rectangle is too small")
    fit_scale = UniformScale(fit_units, common)
    fit_screen = _centered_screen_rect(content, arena.size, fit_scale)
    return BattleCameraPolicy(
        arena=arena,
        hud_rect=hud,
        content_rect=content,
        focus_source=focus_size,
        focus_screen=focus_screen,
        fit_scale=fit_scale,
        fit_screen=fit_screen,
        padding=padding,
        max_players=max_players,
    )


def default_battle_camera_policy(
    profile: DisplayProfile,
) -> BattleCameraPolicy:
    """Policy matching the generated two-row top HUD candidate."""

    safe = Rect.from_rect_like(profile.safe_rect)
    margin = 4
    hud = Rect(
        safe.x + margin,
        safe.y + margin,
        safe.w - margin * 2,
        30,
    )
    return battle_camera_policy(profile, hud)


def profile_camera_constants(
    profile: DisplayProfile,
    *,
    battle_policy: BattleCameraPolicy | None = None,
) -> ProfileCameraConstants:
    safe = Rect.from_rect_like(profile.safe_rect)
    stage = Rect.from_rect_like(profile.contain_stage_rect())
    scale = stage_scale(profile)
    fixed = CameraFixedParams(
        source=Rect(0, 0, PAL_STAGE_WIDTH, PAL_STAGE_HEIGHT),
        destination=stage,
        scale_numerator=scale.numerator,
        scale_denominator=scale.denominator,
        scale_q16=scale.q16,
        source_step_q16=scale.source_step_q16,
        source_phase_q16=scale.source_phase_q16,
    )
    return ProfileCameraConstants(
        profile_name=profile.name,
        safe_rect=safe,
        player_anchor=Point(*profile.player_anchor),
        stage_rect=stage,
        stage_scale=scale,
        stage_fixed=fixed,
        battle_policy=(
            battle_policy
            if battle_policy is not None
            else default_battle_camera_policy(profile)
        ),
    )


def _centered_screen_rect(safe: Rect, world_size: Size, scale: UniformScale) -> Rect:
    width = scale.floor(world_size.w)
    height = scale.floor(world_size.h)
    if width <= 0 or height <= 0:
        raise ValueError("scale collapses camera viewport")
    if width > safe.w or height > safe.h:
        raise ValueError("scaled camera viewport exceeds safe area")
    return Rect(
        safe.x + (safe.w - width) // 2,
        safe.y + (safe.h - height) // 2,
        width,
        height,
    )


def _clamped_world_rect(
    bounds: Rect,
    requested: Size,
    desired_origin: Point,
) -> tuple[Rect, bool]:
    """Clamp a requested integer viewport; centre maps smaller than the view."""

    width = min(bounds.w, requested.w)
    height = min(bounds.h, requested.h)
    if width <= 0 or height <= 0:
        raise ValueError("world bounds must be positive")

    if bounds.w <= width:
        x = bounds.x
    else:
        x = _clamp(desired_origin.x, bounds.left, bounds.right - width)
    if bounds.h <= height:
        y = bounds.y
    else:
        y = _clamp(desired_origin.y, bounds.top, bounds.bottom - height)
    return Rect(x, y, width, height), x != desired_origin.x or y != desired_origin.y


def map_camera(
    profile: DisplayProfile,
    world_bounds: object,
    player_world: Point | tuple[int, int],
    *,
    script_offset: Point | tuple[int, int] = Point(0, 0),
    downsample_stage: bool = False,
) -> CameraView:
    """Derive a player-centred map camera, with optional stage downsampling.

    ``script_offset`` is a camera-space offset in canonical world pixels:
    positive X/Y moves the source viewport right/down and therefore presents
    the player left/up.  For legacy state use :func:`map_camera_from_legacy`;
    it derives this value from ``partyoffset`` without mutating either field.

    Native mode presents one source pixel per display pixel.  Stage mode keeps
    original map tiles and sprites unchanged, presents a 320x200 world window,
    and emits one uniform endpoint-downsampling ratio (27/40 for 240x135 and
    1/2 for 160x128).
    """

    bounds = _coerce_rect(world_bounds)
    if bounds.w <= 0 or bounds.h <= 0:
        raise ValueError("world bounds must be positive")
    player = _coerce_point(player_world)
    offset = _coerce_point(script_offset)
    safe = Rect.from_rect_like(profile.safe_rect)

    if downsample_stage:
        requested = LEGACY_STAGE_SIZE
        scale = stage_scale(profile)
        mode: CameraMode = "map_stage"
    else:
        requested = safe.size
        scale = ONE_TO_ONE
        mode = "map_native"

    focus = Point(player.x + offset.x, player.y + offset.y)
    desired = Point(
        focus.x - requested.w // 2,
        focus.y - requested.h // 2,
    )
    world, was_clamped = _clamped_world_rect(bounds, requested, desired)
    screen = _centered_screen_rect(safe, world.size, scale)
    return CameraView(mode, world, screen, scale, desired, was_clamped)


def map_camera_from_legacy(
    profile: DisplayProfile,
    world_bounds: object,
    canonical_viewport: Point | tuple[int, int],
    party_offset: Point | tuple[int, int] = LEGACY_PLAYER_ANCHOR,
    *,
    extra_script_offset: Point | tuple[int, int] = Point(0, 0),
    downsample_stage: bool = False,
) -> CameraView:
    """Bridge legacy viewport/partyoffset state to a native map camera.

    The canonical player world position is ``viewport + partyoffset``.  The
    legacy script pan is ``(160,112) - partyoffset``.  Keeping both terms here
    makes viewport/partyoffset counter-motion deterministic while ensuring that
    only the derived render viewport is clamped or scaled.
    """

    viewport = _coerce_point(canonical_viewport)
    party = _coerce_point(party_offset)
    extra = _coerce_point(extra_script_offset)
    player = Point(viewport.x + party.x, viewport.y + party.y)
    script = Point(
        LEGACY_PLAYER_ANCHOR.x - party.x + extra.x,
        LEGACY_PLAYER_ANCHOR.y - party.y + extra.y,
    )
    return map_camera(
        profile,
        world_bounds,
        player,
        script_offset=script,
        downsample_stage=downsample_stage,
    )


def _padded_required_bounds(
    arena: Rect,
    rects: Iterable[Rect],
    padding: int,
) -> Rect | None:
    visible_rects: list[Rect] = []
    for rect in rects:
        visible = rect.intersection(arena)
        if visible.is_empty:
            raise ValueError(
                "battle entity is completely outside the canonical arena"
            )
        visible_rects.append(visible)
    required = union_rects(visible_rects)
    if required is None:
        return None
    visible = required.expanded(padding).intersection(arena)
    if visible.is_empty:
        raise ValueError("padded battle bounds do not intersect the arena")
    return visible


def _focus_origin_axis(
    desired: int,
    arena_start: int,
    arena_end: int,
    viewport_extent: int,
    required_start: int,
    required_end: int,
) -> int | None:
    """Closest legal origin to *desired* that contains the required interval."""

    lower = max(arena_start, required_end - viewport_extent)
    upper = min(arena_end - viewport_extent, required_start)
    if lower > upper:
        return None
    return _clamp(desired, lower, upper)


def battle_camera(
    profile: DisplayProfile,
    arena_bounds: object = Rect(0, 0, PAL_STAGE_WIDTH, PAL_STAGE_HEIGHT),
    *,
    players: Sequence[object],
    actor: object | None = None,
    target: object | None = None,
    padding: int = 4,
    policy: BattleCameraPolicy | None = None,
    primary_player: int | None = None,
    force_fit_all: bool = False,
) -> CameraView:
    """Frame a canonical battle without changing animation coordinates.

    The first choice is a one-to-one crop centred on the player group.  Its
    origin is shifted only as far as necessary to contain actor and target.
    If all required rectangles cannot coexist in that crop, the deterministic
    fallback contains the complete canonical arena with one uniform endpoint
    scale.  X/Y scales can never diverge.
    """

    if padding < 0:
        raise ValueError("padding must be nonnegative")
    arena = _coerce_rect(arena_bounds)
    if arena.w <= 0 or arena.h <= 0:
        raise ValueError("battle arena must be positive")
    player_rects = tuple(_coerce_entity_rect(player) for player in players)
    actor_rect = _coerce_entity_rect(actor) if actor is not None else None
    target_rect = _coerce_entity_rect(target) if target is not None else None
    if primary_player is not None and (
        not isinstance(primary_player, int)
        or isinstance(primary_player, bool)
        or not 0 <= primary_player < len(player_rects)
    ):
        raise ValueError("primary_player must index the player sequence")
    resolved_policy = policy or default_battle_camera_policy(profile)
    safe = resolved_policy.content_rect
    if arena != resolved_policy.arena:
        # Custom arenas keep the generated HUD-free destination, while their
        # crop/fit source extents are derived from the supplied canonical
        # bounds using the same fixed integer policy.
        focus_source = Size(
            min(arena.w, resolved_policy.focus_source.w),
            min(arena.h, resolved_policy.focus_source.h),
        )
    else:
        focus_source = resolved_policy.focus_source

    clipped_players: tuple[Rect, ...] = tuple(
        player.intersection(arena) for player in player_rects
    )
    if any(player.is_empty for player in clipped_players):
        raise ValueError(
            "battle player is completely outside the canonical arena"
        )
    clipped_actor = (
        actor_rect.intersection(arena) if actor_rect is not None else None
    )
    if clipped_actor is not None and clipped_actor.is_empty:
        raise ValueError(
            "battle actor is completely outside the canonical arena"
        )
    clipped_target = (
        target_rect.intersection(arena) if target_rect is not None else None
    )
    if clipped_target is not None and clipped_target.is_empty:
        raise ValueError(
            "battle target is completely outside the canonical arena"
        )

    if primary_player is not None:
        focus_rect = clipped_players[primary_player]
    else:
        focus_rect = union_rects(clipped_players)
    if focus_rect is None:
        focus_rect = clipped_actor or clipped_target or Rect(
            arena.center.x, arena.center.y, 1, 1
        )

    required_inputs = list(clipped_players)
    if clipped_actor is not None:
        required_inputs.append(clipped_actor)
    if clipped_target is not None:
        required_inputs.append(clipped_target)
    if not required_inputs:
        required_inputs.append(focus_rect)
    required = _padded_required_bounds(arena, required_inputs, padding)
    assert required is not None

    desired = Point(
        focus_rect.center.x - focus_source.w // 2,
        focus_rect.center.y - focus_source.h // 2,
    )
    can_crop = (
        arena.w >= focus_source.w
        and arena.h >= focus_source.h
        and required.w <= focus_source.w
        and required.h <= focus_source.h
    )
    if can_crop and not force_fit_all:
        origin_x = _focus_origin_axis(
            desired.x,
            arena.left,
            arena.right,
            focus_source.w,
            required.left,
            required.right,
        )
        origin_y = _focus_origin_axis(
            desired.y,
            arena.top,
            arena.bottom,
            focus_source.h,
            required.top,
            required.bottom,
        )
        if origin_x is not None and origin_y is not None:
            world = Rect(
                origin_x,
                origin_y,
                focus_source.w,
                focus_source.h,
            )
            screen = _centered_screen_rect(
                safe,
                focus_source,
                ONE_TO_ONE,
            )
            return CameraView(
                "battle_focus",
                world,
                screen,
                ONE_TO_ONE,
                desired,
                Point(origin_x, origin_y) != desired,
            )

    if arena == resolved_policy.arena:
        scale = resolved_policy.fit_scale
        screen = resolved_policy.fit_screen
    else:
        scale = UniformScale.fit(arena.size, safe.size)
        screen = _centered_screen_rect(safe, arena.size, scale)
    return CameraView(
        "battle_fit_all",
        arena,
        screen,
        scale,
        desired,
        True,
    )


@dataclass(frozen=True, slots=True)
class MapCameraTestVector:
    """Literal host/C conformance vector for the legacy map bridge."""

    name: str
    profile_width: int
    profile_height: int
    world_bounds: Rect
    canonical_viewport: Point
    party_offset: Point
    extra_script_offset: Point
    downsample_stage: bool
    expected_world: Rect
    expected_screen: Rect
    expected_scale: UniformScale
    expected_desired_origin: Point
    expected_projected_focus: Point
    expected_was_clamped: bool

    def as_c_manifest(self) -> dict[str, object]:
        """Return the camera-vector schema consumed by ``emit_c.py``."""

        scripted = (
            self.party_offset != LEGACY_PLAYER_ANCHOR
            or self.extra_script_offset != Point(0, 0)
        )
        focus = Point(
            self.canonical_viewport.x
            + LEGACY_PLAYER_ANCHOR.x
            + self.extra_script_offset.x,
            self.canonical_viewport.y
            + LEGACY_PLAYER_ANCHOR.y
            + self.extra_script_offset.y,
        )
        return {
            "name": self.name,
            "kind": "map",
            "mode": "scripted" if scripted else "follow",
            "bounds": [
                self.expected_world.x,
                self.expected_world.y,
                self.expected_world.w,
                self.expected_world.h,
            ],
            "focus": [focus.x, focus.y],
            "camera": [self.expected_world.x, self.expected_world.y],
            "desired": [
                self.expected_desired_origin.x,
                self.expected_desired_origin.y,
            ],
            "projected_focus": [
                self.expected_projected_focus.x,
                self.expected_projected_focus.y,
            ],
            "scale_q16": self.expected_scale.q16,
            "scale": [
                self.expected_scale.numerator,
                self.expected_scale.denominator,
            ],
            "source_step_q16": self.expected_scale.source_step_q16,
            "source_phase_q16": self.expected_scale.source_phase_q16,
            "screen": [
                self.expected_screen.x,
                self.expected_screen.y,
                self.expected_screen.w,
                self.expected_screen.h,
            ],
            "clamped": self.expected_was_clamped,
        }


@dataclass(frozen=True, slots=True)
class BattleCameraTestVector:
    """Literal single-player actor/target vector consumable by C tests."""

    name: str
    profile_width: int
    profile_height: int
    arena: Rect
    player: Rect
    actor: Rect | None
    target: Rect | None
    padding: int
    expected_mode: CameraMode
    expected_world: Rect
    expected_screen: Rect
    expected_scale: UniformScale
    expected_desired_origin: Point
    expected_projected_focus: Point
    expected_was_clamped: bool

    def as_c_manifest(self) -> dict[str, object]:
        """Return the camera-vector schema consumed by ``emit_c.py``."""

        if self.expected_mode == "battle_fit_all":
            mode = "fit_all"
        elif self.actor is not None or self.target is not None:
            mode = "actor_target"
        else:
            mode = "idle"
        focus = self.player.center
        return {
            "name": self.name,
            "kind": "battle",
            "mode": mode,
            "arena": [
                self.arena.x,
                self.arena.y,
                self.arena.w,
                self.arena.h,
            ],
            "player": [
                self.player.x,
                self.player.y,
                self.player.w,
                self.player.h,
            ],
            "actor": (
                [
                    self.actor.x,
                    self.actor.y,
                    self.actor.w,
                    self.actor.h,
                ]
                if self.actor is not None
                else None
            ),
            "target": (
                [
                    self.target.x,
                    self.target.y,
                    self.target.w,
                    self.target.h,
                ]
                if self.target is not None
                else None
            ),
            "padding": self.padding,
            "bounds": [
                self.expected_world.x,
                self.expected_world.y,
                self.expected_world.w,
                self.expected_world.h,
            ],
            "focus": [focus.x, focus.y],
            "camera": [self.expected_world.x, self.expected_world.y],
            "desired": [
                self.expected_desired_origin.x,
                self.expected_desired_origin.y,
            ],
            "projected_focus": [
                self.expected_projected_focus.x,
                self.expected_projected_focus.y,
            ],
            "scale_q16": self.expected_scale.q16,
            "scale": [
                self.expected_scale.numerator,
                self.expected_scale.denominator,
            ],
            "source_step_q16": self.expected_scale.source_step_q16,
            "source_phase_q16": self.expected_scale.source_phase_q16,
            "screen": [
                self.expected_screen.x,
                self.expected_screen.y,
                self.expected_screen.w,
                self.expected_screen.h,
            ],
            "clamped": self.expected_was_clamped,
        }


# Expected outputs are literals rather than values calculated at import time.
# A generated C test can consume the same records, while Python tests compare
# the reference implementation against them.
MAP_CAMERA_C_TEST_VECTORS: tuple[MapCameraTestVector, ...] = (
    MapCameraTestVector(
        name="map_240_center_native",
        profile_width=240,
        profile_height=135,
        world_bounds=Rect(0, 0, 2048, 2048),
        canonical_viewport=Point(500, 400),
        party_offset=Point(160, 112),
        extra_script_offset=Point(0, 0),
        downsample_stage=False,
        expected_world=Rect(540, 445, 240, 135),
        expected_screen=Rect(0, 0, 240, 135),
        expected_scale=UniformScale(1, 1),
        expected_desired_origin=Point(540, 445),
        expected_projected_focus=Point(120, 67),
        expected_was_clamped=False,
    ),
    MapCameraTestVector(
        name="map_160_script_pan",
        profile_width=160,
        profile_height=128,
        world_bounds=Rect(0, 0, 2048, 2048),
        canonical_viewport=Point(500, 400),
        party_offset=Point(140, 102),
        extra_script_offset=Point(3, -2),
        downsample_stage=False,
        expected_world=Rect(583, 446, 160, 128),
        expected_screen=Rect(0, 0, 160, 128),
        expected_scale=UniformScale(1, 1),
        expected_desired_origin=Point(583, 446),
        expected_projected_focus=Point(80, 64),
        expected_was_clamped=False,
    ),
    MapCameraTestVector(
        name="map_240_stage_downsample",
        profile_width=240,
        profile_height=135,
        world_bounds=Rect(0, 0, 2048, 2048),
        canonical_viewport=Point(500, 400),
        party_offset=Point(160, 112),
        extra_script_offset=Point(0, 0),
        downsample_stage=True,
        expected_world=Rect(500, 412, 320, 200),
        expected_screen=Rect(12, 0, 216, 135),
        expected_scale=UniformScale(27, 40),
        expected_desired_origin=Point(500, 412),
        expected_projected_focus=Point(120, 67),
        expected_was_clamped=False,
    ),
    MapCameraTestVector(
        name="map_160_edge_clamp",
        profile_width=160,
        profile_height=128,
        world_bounds=Rect(0, 0, 640, 400),
        canonical_viewport=Point(-120, -82),
        party_offset=Point(140, 102),
        extra_script_offset=Point(0, 0),
        downsample_stage=False,
        expected_world=Rect(0, 0, 160, 128),
        expected_screen=Rect(0, 0, 160, 128),
        expected_scale=UniformScale(1, 1),
        expected_desired_origin=Point(-40, -34),
        expected_projected_focus=Point(40, 30),
        expected_was_clamped=True,
    ),
)


BATTLE_CAMERA_C_TEST_VECTORS: tuple[BattleCameraTestVector, ...] = (
    BattleCameraTestVector(
        name="battle_240_player_actor_target",
        profile_width=240,
        profile_height=135,
        arena=Rect(0, 0, 320, 200),
        player=Rect(180, 140, 32, 28),
        actor=Rect(180, 140, 32, 28),
        target=Rect(80, 100, 24, 24),
        padding=4,
        expected_mode="battle_focus",
        expected_world=Rect(76, 96, 240, 101),
        expected_screen=Rect(0, 34, 240, 101),
        expected_scale=UniformScale(1, 1),
        expected_desired_origin=Point(76, 104),
        expected_projected_focus=Point(120, 92),
        expected_was_clamped=True,
    ),
    BattleCameraTestVector(
        name="battle_240_fit_all",
        profile_width=240,
        profile_height=135,
        arena=Rect(0, 0, 320, 200),
        player=Rect(230, 145, 30, 30),
        actor=Rect(230, 145, 30, 30),
        target=Rect(25, 90, 30, 30),
        padding=4,
        expected_mode="battle_fit_all",
        expected_world=Rect(0, 0, 320, 200),
        expected_screen=Rect(40, 34, 160, 100),
        expected_scale=UniformScale(1, 2),
        expected_desired_origin=Point(125, 110),
        expected_projected_focus=Point(162, 114),
        expected_was_clamped=True,
    ),
    BattleCameraTestVector(
        name="battle_160_fit_all",
        profile_width=160,
        profile_height=128,
        arena=Rect(0, 0, 320, 200),
        player=Rect(230, 145, 30, 30),
        actor=Rect(230, 145, 30, 30),
        target=Rect(25, 90, 30, 30),
        padding=4,
        expected_mode="battle_fit_all",
        expected_world=Rect(0, 0, 320, 200),
        expected_screen=Rect(8, 36, 144, 90),
        expected_scale=UniformScale(9, 20),
        expected_desired_origin=Point(165, 113),
        expected_projected_focus=Point(118, 108),
        expected_was_clamped=True,
    ),
)


def camera_c_test_vectors(
) -> tuple[
    tuple[MapCameraTestVector, ...],
    tuple[BattleCameraTestVector, ...],
]:
    """Return the immutable vectors used by Python and generated C tests."""

    return MAP_CAMERA_C_TEST_VECTORS, BATTLE_CAMERA_C_TEST_VECTORS


def camera_vectors_for_profile(
    profile: DisplayProfile,
    *,
    battle_policy: BattleCameraPolicy | None = None,
) -> tuple[dict[str, object], ...]:
    """Generate shared C vectors from live formulas for one exact profile.

    The literal records above certify the two default profiles.  Re-evaluating
    their input fixtures here keeps custom safe areas and generated HUD
    exclusions inspectable instead of silently reusing incompatible output
    coordinates.
    """

    resolved_policy = (
        battle_policy
        if battle_policy is not None
        else default_battle_camera_policy(profile)
    )
    map_inputs = tuple(
        vector
        for vector in MAP_CAMERA_C_TEST_VECTORS
        if (
            vector.profile_width == profile.width
            and vector.profile_height == profile.height
        )
    )
    # Exercise both evaluator branches for every resolution.  The literal
    # records provide canonical close/far inputs; their outputs are always
    # recomputed against the exact generated policy below.
    battle_inputs = (
        BATTLE_CAMERA_C_TEST_VECTORS[0],
        BATTLE_CAMERA_C_TEST_VECTORS[1],
    )
    if not map_inputs:
        map_inputs = (MAP_CAMERA_C_TEST_VECTORS[0],)
    manifests: list[dict[str, object]] = []
    for vector in map_inputs:
        view = map_camera_from_legacy(
            profile,
            vector.world_bounds,
            vector.canonical_viewport,
            vector.party_offset,
            extra_script_offset=vector.extra_script_offset,
            downsample_stage=vector.downsample_stage,
        )
        scripted = (
            vector.party_offset != LEGACY_PLAYER_ANCHOR
            or vector.extra_script_offset != Point(0, 0)
        )
        focus = Point(
            vector.canonical_viewport.x
            + LEGACY_PLAYER_ANCHOR.x
            + vector.extra_script_offset.x,
            vector.canonical_viewport.y
            + LEGACY_PLAYER_ANCHOR.y
            + vector.extra_script_offset.y,
        )
        projected = view.world_to_screen(focus)
        manifests.append(
            {
                "name": vector.name,
                "kind": "map",
                "mode": "scripted" if scripted else "follow",
                "bounds": [
                    view.world_rect.x,
                    view.world_rect.y,
                    view.world_rect.w,
                    view.world_rect.h,
                ],
                "focus": [focus.x, focus.y],
                "camera": [view.world_rect.x, view.world_rect.y],
                "desired": [
                    view.desired_origin.x,
                    view.desired_origin.y,
                ],
                "projected_focus": [projected.x, projected.y],
                "scale_q16": view.scale.q16,
                "scale": [
                    view.scale.numerator,
                    view.scale.denominator,
                ],
                "source_step_q16": view.scale.source_step_q16,
                "source_phase_q16": view.scale.source_phase_q16,
                "screen": [
                    view.screen_rect.x,
                    view.screen_rect.y,
                    view.screen_rect.w,
                    view.screen_rect.h,
                ],
                "clamped": view.was_clamped,
            }
        )

    for vector in battle_inputs:
        vector_name = (
            f"battle_{profile.width}_player_actor_target"
            if vector is BATTLE_CAMERA_C_TEST_VECTORS[0]
            else f"battle_{profile.width}_fit_all"
        )
        view = battle_camera(
            profile,
            vector.arena,
            players=(vector.player,),
            actor=vector.actor,
            target=vector.target,
            padding=resolved_policy.padding,
            policy=resolved_policy,
            primary_player=0,
        )
        if view.mode == "battle_fit_all":
            mode = "fit_all"
        elif vector.actor is not None or vector.target is not None:
            mode = "actor_target"
        else:
            mode = "idle"
        focus = vector.player.intersection(vector.arena).center
        projected = view.world_to_screen(focus)
        manifests.append(
            {
                "name": vector_name,
                "kind": "battle",
                "mode": mode,
                "arena": [
                    vector.arena.x,
                    vector.arena.y,
                    vector.arena.w,
                    vector.arena.h,
                ],
                "player": [
                    vector.player.x,
                    vector.player.y,
                    vector.player.w,
                    vector.player.h,
                ],
                "actor": (
                    [
                        vector.actor.x,
                        vector.actor.y,
                        vector.actor.w,
                        vector.actor.h,
                    ]
                    if vector.actor is not None
                    else None
                ),
                "target": (
                    [
                        vector.target.x,
                        vector.target.y,
                        vector.target.w,
                        vector.target.h,
                    ]
                    if vector.target is not None
                    else None
                ),
                "padding": resolved_policy.padding,
                "bounds": [
                    view.world_rect.x,
                    view.world_rect.y,
                    view.world_rect.w,
                    view.world_rect.h,
                ],
                "focus": [focus.x, focus.y],
                "camera": [view.world_rect.x, view.world_rect.y],
                "desired": [
                    view.desired_origin.x,
                    view.desired_origin.y,
                ],
                "projected_focus": [projected.x, projected.y],
                "scale_q16": view.scale.q16,
                "scale": [
                    view.scale.numerator,
                    view.scale.denominator,
                ],
                "source_step_q16": view.scale.source_step_q16,
                "source_phase_q16": view.scale.source_phase_q16,
                "screen": [
                    view.screen_rect.x,
                    view.screen_rect.y,
                    view.screen_rect.w,
                    view.screen_rect.h,
                ],
                "clamped": view.was_clamped,
            }
        )
    return tuple(manifests)
