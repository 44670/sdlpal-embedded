#!/usr/bin/env python3
"""End-to-end compiler and verifier for native small-display PAL layouts.

This is the only module which turns source data into target coefficients.
Target code includes the generated header; it never parses JSON, measures
text, chooses a layout, or derives a scale ratio at runtime.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import sys
import tempfile
import unicodedata
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping, Sequence

from .camera import (
    BattleCameraPolicy,
    battle_camera_policy,
    camera_vectors_for_profile,
    profile_camera_constants,
)
from .emit_c import emit_profile_header
from .font import (
    BDF_MEMBER,
    BDF_MEMBER_BYTES,
    BDF_MEMBER_SHA256,
    FONT10_BITMAP_BYTES,
    FONT10_CELL_HEIGHT,
    FONT10_CELL_WIDTH,
    FONT10_RECORD_BYTES,
    Font10,
    RELEASE_ASSET,
    RELEASE_SHA256,
    RELEASE_URL,
    RELEASE_VERSION,
    build_font10,
    collect_pal_corpus_characters,
    extract_locked_bdf,
    parse_font10,
)
from .geometry import Rect as CameraRect
from .preview import (
    IndexedImage,
    decode_pal_palette,
    decode_pal_rle,
    render_asset_fixture,
    render_camera_vector,
    render_loading,
    render_screen,
)
from .profiles import (
    DisplayProfile,
    certified_profiles,
    loading_layout,
    parse_resolution,
)
from .profiles import PAL_STAGE_HEIGHT, PAL_STAGE_WIDTH
from .screens import (
    battle_hud_screen,
    dialog_screen,
    equip_screen,
    item,
    item_screen,
    magic_screen,
    menu_screen,
    solve_screen,
    status_screen,
)
from .solver import AssetSpec, FontMetrics, Q16_ONE, Rect, measure_text


SCHEMA = "sdlpal-native-ui-layout"
SCHEMA_VERSION = 1
DEFAULT_DATA_DIR = Path("/mnt/hgfs/deb13/PAL")
DEFAULT_PROFILES = ("240x135", "160x128")
PROFILE_SPEC_RE = re.compile(
    r"^(?P<resolution>[1-9][0-9]*[xX][1-9][0-9]*)"
    r"(?:@(?P<left>[0-9]+),(?P<top>[0-9]+),"
    r"(?P<right>[0-9]+),(?P<bottom>[0-9]+))?$"
)


class LayoutCheckError(ValueError):
    """A source, generated artifact, or reproducibility check failed."""


@dataclass(frozen=True, slots=True)
class PalCorpus:
    words: tuple[str, ...]
    messages: tuple[str, ...]
    rendered_messages: tuple[str, ...]
    source_files: tuple[tuple[str, int, str], ...]


@dataclass(frozen=True, slots=True)
class AssetClassExtent:
    archive: str
    role: str
    source_width: int
    source_height: int
    frame_count: int
    source_bytes: int
    source_sha256: str
    dimensions: tuple[tuple[int, int], ...]

    def to_manifest(self) -> dict[str, object]:
        return {
            "archive": self.archive,
            "role": self.role,
            "source_max": [self.source_width, self.source_height],
            "frame_count": self.frame_count,
            "unique_dimension_count": len(self.dimensions),
            "source_bytes": self.source_bytes,
            "source_sha256": self.source_sha256,
        }


@dataclass(frozen=True, slots=True)
class LayoutAssetInventory:
    portrait: AssetClassExtent
    item_preview: AssetClassExtent
    battle_player: AssetClassExtent
    battle_enemy: AssetClassExtent
    battle_fire: AssetClassExtent
    ui_sprite: AssetClassExtent
    battle_effect: AssetClassExtent

    def to_manifest(self) -> dict[str, object]:
        return {
            extent.role: extent.to_manifest()
            for extent in (
                self.portrait,
                self.item_preview,
                self.battle_player,
                self.battle_enemy,
                self.battle_fire,
                self.ui_sprite,
                self.battle_effect,
            )
        }


@dataclass(frozen=True, slots=True)
class CompiledBundle:
    artifacts: Mapping[str, bytes]
    target_headers: Mapping[str, bytes]
    profiles: tuple[Mapping[str, object], ...]


@dataclass(frozen=True, slots=True)
class PreviewFixtures:
    palette: tuple[tuple[int, int, int], ...]
    assets: Mapping[str, IndexedImage]


def _stable_json_bytes(value: object) -> bytes:
    return (
        json.dumps(
            value,
            ensure_ascii=False,
            sort_keys=True,
            indent=2,
        )
        + "\n"
    ).encode("utf-8")


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _find_file(root: Path, name: str) -> Path:
    for candidate in (root / name, root / name.lower()):
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(root / name)


def _mkf_chunk(path: Path, index: int) -> bytes:
    data = path.read_bytes()
    if len(data) < 8:
        raise LayoutCheckError(f"not an MKF archive: {path}")
    first = struct.unpack_from("<I", data, 0)[0]
    if first < 8 or first % 4 or first > len(data):
        raise LayoutCheckError(f"invalid MKF offset table: {path}")
    count = first // 4 - 1
    if not 0 <= index < count:
        raise LayoutCheckError(
            f"{path} contains {count} chunks, requested #{index}"
        )
    start, end = struct.unpack_from("<II", data, index * 4)
    if start > end or end > len(data):
        raise LayoutCheckError(
            f"invalid MKF range {path} #{index}: {start}..{end}"
        )
    return data[start:end]


def _dialog_visible_text(text: str) -> str:
    """Mirror the legacy dialog control-byte shape for layout measurement."""

    visible: list[str] = []
    cursor = 0
    while cursor < len(text):
        character = text[cursor]
        if character == "~":
            break
        if character == "$":
            cursor = min(len(text), cursor + 3)
            continue
        if character in "-'@\"()":
            cursor += 1
            continue
        if character == "\\" and cursor + 1 < len(text):
            cursor += 1
            character = text[cursor]
        if not unicodedata.category(character).startswith("C"):
            visible.append(character)
        cursor += 1
    return "".join(visible)


def load_pal_corpus(data_dir: str | Path) -> PalCorpus:
    root = Path(data_dir)
    word_path = _find_file(root, "WORD.DAT")
    message_path = _find_file(root, "M.MSG")
    sss_path = _find_file(root, "SSS.MKF")
    word_data = word_path.read_bytes()
    message_data = message_path.read_bytes()
    offset_data = _mkf_chunk(sss_path, 3)
    if len(offset_data) < 8 or len(offset_data) % 4:
        raise LayoutCheckError("SSS.MKF message offsets are malformed")
    offsets = struct.unpack(f"<{len(offset_data) // 4}I", offset_data)

    words = tuple(
        word_data[offset : offset + 10]
        .rstrip(b" \0")
        .decode("cp950", errors="strict")
        for offset in range(0, len(word_data), 10)
    )
    messages: list[str] = []
    for index, (start, end) in enumerate(zip(offsets, offsets[1:])):
        if start > end or end > len(message_data):
            raise LayoutCheckError(
                f"bad M.MSG offset range #{index}: {start}..{end}"
            )
        messages.append(
            message_data[start:end].decode("cp950", errors="strict")
        )

    source_files = tuple(
        (
            path.name,
            path.stat().st_size,
            _sha256(path.read_bytes()),
        )
        for path in (word_path, message_path, sss_path)
    )
    return PalCorpus(
        words=words,
        messages=tuple(messages),
        rendered_messages=tuple(
            _dialog_visible_text(message) for message in messages
        ),
        source_files=source_files,
    )


def _rle_extent(image: bytes) -> tuple[int, int] | None:
    offset = 4 if image[:4] == b"\x02\0\0\0" else 0
    if len(image) < offset + 4:
        return None
    width, height = struct.unpack_from("<HH", image, offset)
    if not (0 < width <= 320 and 0 < height <= 200):
        return None
    return width, height


def _sprite_extents(image: bytes) -> tuple[tuple[int, int], ...]:
    if len(image) < 4:
        return ()
    table_entries = struct.unpack_from("<H", image, 0)[0]
    if table_entries < 2 or table_entries * 2 > len(image):
        return ()
    result: list[tuple[int, int]] = []
    # The first word counts the terminal offset as well as frame offsets.
    for index in range(table_entries - 1):
        offset_words = struct.unpack_from("<H", image, index * 2)[0]
        offset = offset_words * 2
        if offset >= len(image):
            continue
        extent = _rle_extent(image[offset:])
        if extent is not None:
            result.append(extent)
    return tuple(result)


def _asset_archive_extent(
    data_dir: Path,
    archive: str,
    role: str,
    *,
    sprite: bool,
) -> AssetClassExtent:
    # This import is host tooling only.  It performs the mandatory offline
    # YJ decode before dimensions are inspected; target code sees native data.
    import pal_pack_build

    source_path = _find_file(data_dir, f"{archive}.MKF")
    chunks = pal_pack_build.load_archive(data_dir, archive)
    extents: list[tuple[int, int]] = []
    for chunk in chunks:
        if not chunk.present:
            continue
        if sprite:
            extents.extend(_sprite_extents(chunk.payload))
        else:
            extent = _rle_extent(chunk.payload)
            if extent is not None:
                extents.append(extent)
    if not extents:
        raise LayoutCheckError(
            f"{archive}.MKF contains no auditable native RLE frame"
        )
    return AssetClassExtent(
        archive=archive,
        role=role,
        source_width=max(width for width, _ in extents),
        source_height=max(height for _, height in extents),
        frame_count=len(extents),
        source_bytes=source_path.stat().st_size,
        source_sha256=_sha256(source_path.read_bytes()),
        dimensions=tuple(sorted(set(extents))),
    )


def _asset_archive_chunk_extent(
    data_dir: Path,
    archive: str,
    chunk_index: int,
    role: str,
    *,
    sprite: bool,
) -> AssetClassExtent:
    """Audit one native chunk whose frames form a distinct runtime class."""

    import pal_pack_build

    source_path = _find_file(data_dir, f"{archive}.MKF")
    chunks = pal_pack_build.load_archive(data_dir, archive)
    if not 0 <= chunk_index < len(chunks) or not chunks[chunk_index].present:
        raise LayoutCheckError(
            f"{archive}.MKF lacks required chunk #{chunk_index}"
        )
    payload = chunks[chunk_index].payload
    if sprite:
        extents = _sprite_extents(payload)
    else:
        extent = _rle_extent(payload)
        extents = (extent,) if extent is not None else ()
    if not extents:
        raise LayoutCheckError(
            f"{archive}.MKF #{chunk_index} contains no auditable native RLE frame"
        )
    return AssetClassExtent(
        archive=f"{archive}#{chunk_index}",
        role=role,
        source_width=max(width for width, _ in extents),
        source_height=max(height for _, height in extents),
        frame_count=len(extents),
        source_bytes=source_path.stat().st_size,
        source_sha256=_sha256(source_path.read_bytes()),
        dimensions=tuple(sorted(set(extents))),
    )


def audit_asset_inventory(
    data_dir: str | Path,
) -> LayoutAssetInventory:
    root = Path(data_dir)
    return LayoutAssetInventory(
        portrait=_asset_archive_extent(
            root, "RGM", "portrait", sprite=False
        ),
        item_preview=_asset_archive_extent(
            root, "BALL", "item_preview", sprite=False
        ),
        battle_player=_asset_archive_extent(
            root, "F", "battle_player", sprite=True
        ),
        battle_enemy=_asset_archive_extent(
            root, "ABC", "battle_enemy", sprite=True
        ),
        battle_fire=_asset_archive_extent(
            root, "FIRE", "battle_fire", sprite=True
        ),
        ui_sprite=_asset_archive_chunk_extent(
            root, "DATA", 9, "ui_sprite", sprite=True
        ),
        battle_effect=_asset_archive_chunk_extent(
            root, "DATA", 10, "battle_effect", sprite=True
        ),
    )


def _sprite_frame(
    payload: bytes,
    frame_index: int,
    *,
    source: str,
) -> bytes:
    if len(payload) < 4:
        raise LayoutCheckError(f"{source}: short sprite table")
    table_entries = struct.unpack_from("<H", payload, 0)[0]
    frame_count = table_entries - 1
    if (
        table_entries < 2
        or table_entries * 2 > len(payload)
        or not 0 <= frame_index < frame_count
    ):
        raise LayoutCheckError(f"{source}: invalid sprite frame index")
    start_words, end_words = struct.unpack_from(
        "<HH", payload, frame_index * 2
    )
    start = start_words * 2
    end = end_words * 2
    if start >= end or end > len(payload):
        raise LayoutCheckError(
            f"{source}: invalid sprite frame range {start}..{end}"
        )
    return payload[start:end]


def load_preview_fixtures(data_dir: str | Path) -> PreviewFixtures:
    """Load a deterministic real-art subset for review screenshots only."""

    import pal_pack_build

    root = Path(data_dir)

    def direct(archive: str, chunk_index: int) -> IndexedImage:
        chunks = pal_pack_build.load_archive(root, archive)
        if not 0 <= chunk_index < len(chunks):
            raise LayoutCheckError(
                f"{archive}.MKF lacks preview chunk #{chunk_index}"
            )
        payload = chunks[chunk_index].payload
        return decode_pal_rle(
            payload,
            source=f"{archive}.MKF#{chunk_index}",
        )

    def sprite(
        archive: str,
        chunk_index: int,
        frame_index: int,
    ) -> IndexedImage:
        chunks = pal_pack_build.load_archive(root, archive)
        if not 0 <= chunk_index < len(chunks):
            raise LayoutCheckError(
                f"{archive}.MKF lacks preview chunk #{chunk_index}"
            )
        source = f"{archive}.MKF#{chunk_index}:frame{frame_index}"
        frame = _sprite_frame(
            chunks[chunk_index].payload,
            frame_index,
            source=source,
        )
        return decode_pal_rle(frame, source=source)

    palettes = pal_pack_build.load_archive(root, "PAT")
    if not palettes:
        raise LayoutCheckError("PAT.MKF has no preview palette")
    return PreviewFixtures(
        palette=decode_pal_palette(palettes[0].payload),
        assets={
            "portrait": direct("RGM", 1),
            "item_preview": direct("BALL", 1),
            "battle_player": sprite("F", 0, 0),
            "battle_enemy": sprite("ABC", 1, 2),
            "battle_fire": sprite("FIRE", 0, 0),
            "ui_sprite": sprite("DATA", 9, 40),
            "battle_effect": sprite("DATA", 10, 0),
        },
    )


def default_asset_inventory() -> LayoutAssetInventory:
    """Small synthetic inventory for pure unit tests and API callers."""

    def extent(
        archive: str,
        role: str,
        width: int,
        height: int,
    ) -> AssetClassExtent:
        return AssetClassExtent(
            archive,
            role,
            width,
            height,
            1,
            0,
            "0" * 64,
            ((width, height),),
        )

    return LayoutAssetInventory(
        portrait=extent("RGM", "portrait", 64, 64),
        item_preview=extent("BALL", "item_preview", 48, 48),
        battle_player=extent("F", "battle_player", 48, 48),
        battle_enemy=extent("ABC", "battle_enemy", 64, 64),
        battle_fire=extent("FIRE", "battle_fire", 96, 80),
        ui_sprite=extent("DATA#9", "ui_sprite", 32, 32),
        battle_effect=extent("DATA#10", "battle_effect", 90, 90),
    )


def parse_profile_spec(value: str) -> DisplayProfile:
    match = PROFILE_SPEC_RE.fullmatch(value.strip())
    if match is None:
        raise LayoutCheckError(
            f"invalid profile {value!r}; expected WIDTHxHEIGHT"
            " or WIDTHxHEIGHT@LEFT,TOP,RIGHT,BOTTOM"
        )
    base = parse_resolution(match.group("resolution"))
    if match.group("left") is None:
        return base
    return DisplayProfile(
        base.width,
        base.height,
        safe_left=int(match.group("left")),
        safe_top=int(match.group("top")),
        safe_right=int(match.group("right")),
        safe_bottom=int(match.group("bottom")),
    )


def _word(corpus: PalCorpus, index: int, fallback: str) -> str:
    if 0 <= index < len(corpus.words) and corpus.words[index]:
        return corpus.words[index]
    return fallback


def _unique_longest_words(
    corpus: PalCorpus,
    metrics: FontMetrics,
    count: int,
) -> tuple[tuple[int, str], ...]:
    ranked = sorted(
        (
            (measure_text(text, metrics), index, text)
            for index, text in enumerate(corpus.words)
            if text
        ),
        key=lambda row: (-row[0], row[1], row[2]),
    )
    result: list[tuple[int, str]] = []
    seen: set[str] = set()
    for _, index, text in ranked:
        if text in seen:
            continue
        seen.add(text)
        result.append((index, text))
        if len(result) == count:
            break
    return tuple(result)


def _focus_rect(profile: DisplayProfile, width: int, height: int) -> Rect:
    safe = profile.safe_rect
    anchor_x = profile.player_anchor[0] - safe.x
    anchor_y = profile.player_anchor[1] - safe.y
    x = max(0, min(safe.width - width, anchor_x - width // 2))
    y = max(0, min(safe.height - height, anchor_y - height // 2))
    return Rect(x, y, width, height)


def _translate_rect(value: object, dx: int, dy: int) -> None:
    if isinstance(value, list) and len(value) == 4:
        value[0] = int(value[0]) + dx
        value[1] = int(value[1]) + dy


def _physicalize_screen(
    screen: dict[str, object],
    profile: DisplayProfile,
) -> dict[str, object]:
    """Translate a safe-local solver result into physical display pixels."""

    dx = profile.safe_rect.x
    dy = profile.safe_rect.y
    screen["viewport"] = [profile.width, profile.height]
    focus = screen.get("focus")
    if focus is not None:
        _translate_rect(focus, dx, dy)
    elements = screen.get("elements")
    if isinstance(elements, list):
        for element in elements:
            if isinstance(element, dict):
                _translate_rect(element.get("rect"), dx, dy)
    policies = screen.get("asset_scale_policies")
    if isinstance(policies, list):
        for policy in policies:
            if isinstance(policy, dict) and policy.get("destination") is not None:
                _translate_rect(policy.get("destination"), dx, dy)
    return screen


def _normalize_stage_policy(
    policy: dict[str, object],
    profile: DisplayProfile,
) -> None:
    """Replace Q16-derived ratios with the profile's exact stage ratio."""

    if policy.get("space") != "legacy_stage":
        return
    constants = profile_camera_constants(profile)
    scale = constants.stage_scale
    source = policy.get("source")
    if not isinstance(source, list) or len(source) != 2:
        raise LayoutCheckError("stage asset policy has no source extent")
    source_width = int(source[0])
    source_height = int(source[1])
    destination_width = max(
        1,
        (source_width * scale.numerator + scale.denominator // 2)
        // scale.denominator,
    )
    destination_height = max(
        1,
        (source_height * scale.numerator + scale.denominator // 2)
        // scale.denominator,
    )
    policy["numerator"] = scale.numerator
    policy["denominator"] = scale.denominator
    policy["scale_q16"] = scale.q16
    policy["destination_size"] = [destination_width, destination_height]
    step_x = (source_width << 16) // destination_width
    step_y = (source_height << 16) // destination_height
    policy["step_q16"] = [step_x, step_y]
    policy["phase_q16"] = [step_x // 2, step_y // 2]


def _stage_sampling(profile: DisplayProfile) -> dict[str, object]:
    constants = profile_camera_constants(profile)
    fixed = constants.stage_fixed
    return {
        "name": "stage",
        "key": "stage",
        "role": "legacy_stage",
        "visible": True,
        "space": "legacy_stage",
        "screen": None,
        "page": 0,
        "source": [fixed.source.w, fixed.source.h],
        "destination_size": [
            fixed.destination.w,
            fixed.destination.h,
        ],
        "destination": [
            fixed.destination.x,
            fixed.destination.y,
            fixed.destination.w,
            fixed.destination.h,
        ],
        "numerator": fixed.scale_numerator,
        "denominator": fixed.scale_denominator,
        "scale_q16": fixed.scale_q16,
        "filter": "nearest_center",
        "sampling": "nearest_center",
        "step_q16": [
            fixed.source_step_q16,
            fixed.source_step_q16,
        ],
        "phase_q16": [
            fixed.source_phase_q16,
            fixed.source_phase_q16,
        ],
        "min_width": fixed.destination.w,
        "min_height": fixed.destination.h,
        "max_width": fixed.destination.w,
        "max_height": fixed.destination.h,
        "scale_group": "legacy_stage",
    }


def _catalog_policy(
    *,
    profile: DisplayProfile,
    screen: str,
    asset_class: str,
    source_width: int,
    source_height: int,
    space: str,
    box: Sequence[int] | None,
) -> dict[str, object]:
    if space == "legacy_stage":
        scale = profile_camera_constants(profile).stage_scale
        numerator = scale.numerator
        denominator = scale.denominator
        destination_rect = None
    elif space == "ui":
        if box is None or len(box) != 4:
            raise LayoutCheckError(
                f"{screen}:{asset_class} has no generated UI asset box"
            )
        box_width = int(box[2])
        box_height = int(box[3])
        candidates = (
            (1, 1),
            (box_width, source_width),
            (box_height, source_height),
        )
        numerator, denominator = candidates[0]
        for candidate_numerator, candidate_denominator in candidates[1:]:
            if (
                candidate_numerator * denominator
                < numerator * candidate_denominator
            ):
                numerator = candidate_numerator
                denominator = candidate_denominator
        divisor = math.gcd(numerator, denominator)
        numerator //= divisor
        denominator //= divisor
        destination_rect = [0, 0, 0, 0]
    else:
        raise LayoutCheckError(f"unsupported catalog space {space!r}")

    destination_width = max(
        1,
        (source_width * numerator + denominator // 2) // denominator,
    )
    destination_height = max(
        1,
        (source_height * numerator + denominator // 2) // denominator,
    )
    if destination_rect is not None:
        assert box is not None
        if destination_width > int(box[2]) or destination_height > int(box[3]):
            raise LayoutCheckError(
                f"{screen}:{asset_class} catalog entry exceeds generated box"
            )
        destination_rect = [
            int(box[0]) + (int(box[2]) - destination_width) // 2,
            int(box[1]) + (int(box[3]) - destination_height) // 2,
            destination_width,
            destination_height,
        ]
    step_x = (source_width << 16) // destination_width
    step_y = (source_height << 16) // destination_height
    return {
        "name": (
            f"catalog:{screen}:{asset_class}:"
            f"{source_width}x{source_height}"
        ),
        "key": asset_class,
        "role": asset_class,
        "asset_class": asset_class,
        "catalog": True,
        "visible": True,
        "space": space,
        "screen": screen,
        "page": 0,
        "source": [source_width, source_height],
        "destination_size": [destination_width, destination_height],
        "destination": destination_rect,
        "numerator": numerator,
        "denominator": denominator,
        "scale_q16": (numerator << 16) // denominator,
        "filter": "nearest_center",
        "sampling": "nearest_center",
        "step_q16": [step_x, step_y],
        "phase_q16": [step_x // 2, step_y // 2],
        "min_width": destination_width,
        "min_height": destination_height,
        "max_width": destination_width,
        "max_height": destination_height,
        "scale_group": (
            "battle_stage" if space == "legacy_stage" else screen
        ),
    }


def _asset_sampling_catalog(
    profile: DisplayProfile,
    base_sampling: Sequence[dict[str, object]],
    inventory: LayoutAssetInventory,
) -> list[dict[str, object]]:
    by_name = {
        str(policy["name"]): policy for policy in base_sampling
    }
    definitions = (
        (
            "status",
            "portrait",
            inventory.portrait,
            "status:status_portrait",
            "ui",
        ),
        (
            "equip",
            "item_preview",
            inventory.item_preview,
            "equip:equip_preview",
            "ui",
        ),
        (
            "battle_hud",
            "battle_player",
            inventory.battle_player,
            "battle_hud:battle_player",
            "legacy_stage",
        ),
        (
            "battle_hud",
            "battle_enemy",
            inventory.battle_enemy,
            "battle_hud:battle_enemy",
            "legacy_stage",
        ),
        (
            "battle_hud",
            "battle_fire",
            inventory.battle_fire,
            "battle_hud:battle_fire",
            "legacy_stage",
        ),
        (
            "battle_hud",
            "ui_sprite",
            inventory.ui_sprite,
            None,
            "legacy_stage",
        ),
        (
            "battle_hud",
            "battle_effect",
            inventory.battle_effect,
            "battle_hud:battle_effect",
            "legacy_stage",
        ),
    )
    result: list[dict[str, object]] = []
    for screen, asset_class, extent, base_name, space in definitions:
        base = by_name.get(base_name) if base_name is not None else None
        if base_name is not None and (
            base is None or not bool(base.get("visible"))
        ):
            continue
        box_value = base.get("destination") if base is not None else None
        box = (
            box_value
            if isinstance(box_value, Sequence)
            and not isinstance(box_value, (str, bytes, bytearray))
            else None
        )
        for source_width, source_height in extent.dimensions:
            result.append(
                _catalog_policy(
                    profile=profile,
                    screen=screen,
                    asset_class=asset_class,
                    source_width=source_width,
                    source_height=source_height,
                    space=space,
                    box=box,
                )
            )
    return result


def _screen_specs(
    profile: DisplayProfile,
    metrics: FontMetrics,
    corpus: PalCorpus,
    labels: Mapping[str, str],
    assets: LayoutAssetInventory,
) -> tuple[object, ...]:
    safe = profile.safe_rect
    width, height = safe.width, safe.height
    focus = _focus_rect(profile, 36, 36)
    ranked_messages = sorted(
        (
            (measure_text(text, metrics), index, text)
            for index, text in enumerate(corpus.rendered_messages)
            if text
        ),
        key=lambda row: (-row[0], row[1]),
    )
    dialog_lines = tuple(row[2] for row in ranked_messages[:4])
    longest_words = _unique_longest_words(corpus, metrics, 24)

    opening = (
        item("new_game", _word(corpus, 7, "新的故事"), 0),
        item("load_game", _word(corpus, 8, "舊的回憶"), 1),
    )
    game = tuple(
        item(f"game_{value}", _word(corpus, word_id, fallback), value)
        for value, word_id, fallback in (
            (1, 3, labels["status"]),
            (2, 4, labels["magic"]),
            (3, 5, labels["items"]),
            (4, 6, labels["system"]),
        )
    )
    system = tuple(
        item(f"system_{value}", _word(corpus, word_id, fallback), value)
        for value, word_id, fallback in (
            (1, 11, labels["save"]),
            (2, 12, labels["load"]),
            (3, 13, "音樂"),
            (4, 14, "音效"),
            (5, 15, labels["exit"]),
            (6, 606, labels["battle"]),
        )
    )
    slots = tuple(
        item(
            f"save_slot_{slot}",
            _word(corpus, 42 + slot, f"進度 {slot}"),
            slot,
        )
        for slot in range(1, 6)
    )
    confirmation = (
        item("no", _word(corpus, 19, labels["cancel"]), 0),
        item("yes", _word(corpus, 20, labels["confirm"]), 1),
    )
    list_items = tuple(
        item(f"word_{index}", text, index)
        for index, text in longest_words
    )
    party_name = _word(corpus, 36, "李逍遙")
    status_party = item(
        "status_party",
        party_name,
        None,
        role="party_selector",
    )
    status_fields = tuple(
        item(
            f"status_{index}",
            _word(corpus, index, fallback),
            None,
            role="stat_label",
            selectable=False,
            value_sample=value_sample,
        )
        for index, fallback, value_sample in (
            (2, "經驗", "99999"),
            (48, "等級", "99"),
            (49, "體力", "999/999"),
            (50, "真氣", "999/999"),
            (51, "武術", "9999"),
            (52, "靈力", "9999"),
            (53, "防禦", "9999"),
            (54, "身法", "9999"),
            (55, "吉運", "9999"),
        )
    )
    equip_labels = (
        "頭部",
        "披掛",
        "身體",
        "手持",
        "腳部",
        "佩戴",
    )
    equip_value_samples = tuple(
        (
            longest_words[index][1]
            if index < len(longest_words)
            else _word(corpus, 7, "夢幻之旅")
        )
        for index in range(len(equip_labels))
    )
    equip_party = item(
        "equip_party",
        party_name,
        None,
        role="party_selector",
    )
    equipment = tuple(
        item(
            f"equip_{index}",
            label,
            None,
            role="equipment_label",
            selectable=False,
            value_sample=equip_value_samples[index],
        )
        for index, label in enumerate(equip_labels)
    )
    battle_main = (
        item("battle_attack", labels["attack"], 0),
        item("battle_magic", _word(corpus, 4, labels["magic"]), 1),
        item("battle_coop_magic", labels["coop_magic"], 2),
        item("battle_misc", labels["misc"], 3),
        item(
            "battle_current_player",
            party_name,
            None,
            role="party_selector",
            selectable=False,
        ),
        item(
            "battle_hp",
            "999",
            None,
            role="stat_label",
            selectable=False,
        ),
        item(
            "battle_mp",
            "999",
            None,
            role="stat_label",
            selectable=False,
        ),
        item(
            "battle_time",
            "100",
            None,
            role="stat_label",
            selectable=False,
        ),
    )
    battle_misc = tuple(
        item(f"battle_misc_{value}", _word(corpus, word_id, fallback), value)
        for value, word_id, fallback in (
            (1, 5, labels["items"]),
            (2, 58, labels["defend"]),
            (3, 56, labels["all"]),
            (4, 59, labels["flee"]),
            (5, 60, labels["status"]),
        )
    )
    battle_item_action = (
        item("battle_use_item", _word(corpus, 23, labels["use"]), 1),
        item("battle_throw_item", _word(corpus, 24, "投擲"), 2),
    )

    portrait = AssetSpec(
        "dialog_portrait",
        "portrait",
        assets.portrait.source_width,
        assets.portrait.source_height,
        min_scale_q16=Q16_ONE // 4,
    )
    status_portrait = AssetSpec(
        "status_portrait",
        "portrait",
        assets.portrait.source_width,
        assets.portrait.source_height,
        min_scale_q16=Q16_ONE // 4,
    )
    item_preview = AssetSpec(
        "item_preview",
        "item_preview",
        assets.item_preview.source_width,
        assets.item_preview.source_height,
        min_scale_q16=Q16_ONE // 2,
    )
    equip_preview = AssetSpec(
        "equip_preview",
        "equipment_preview",
        assets.item_preview.source_width,
        assets.item_preview.source_height,
        min_scale_q16=Q16_ONE // 2,
    )
    battle_assets = (
        AssetSpec(
            "battle_player",
            "battle_player",
            assets.battle_player.source_width,
            assets.battle_player.source_height,
            scale_group="battle_stage",
        ),
        AssetSpec(
            "battle_enemy",
            "battle_enemy",
            assets.battle_enemy.source_width,
            assets.battle_enemy.source_height,
            scale_group="battle_stage",
        ),
        AssetSpec(
            "battle_fire",
            "battle_fire",
            assets.battle_fire.source_width,
            assets.battle_fire.source_height,
            scale_group="battle_stage",
        ),
        AssetSpec(
            "battle_effect",
            "battle_effect",
            assets.battle_effect.source_width,
            assets.battle_effect.source_height,
            scale_group="battle_stage",
        ),
    )
    return (
        dialog_screen(
            width,
            height,
            dialog_lines,
            confirmation,
            font_metrics=metrics,
            screen_id="dialog",
            focus=focus,
            portrait=portrait,
            selected_key="yes",
        ),
        menu_screen(
            width,
            height,
            opening,
            font_metrics=metrics,
            screen_id="opening_menu",
            title="",
            selected_key="new_game",
        ),
        menu_screen(
            width,
            height,
            game,
            font_metrics=metrics,
            screen_id="game_menu",
            title="",
            selected_key="game_1",
            focus=focus,
            modal=False,
        ),
        menu_screen(
            width,
            height,
            system,
            font_metrics=metrics,
            screen_id="system_menu",
            title=labels["system"],
            selected_key="system_1",
        ),
        menu_screen(
            width,
            height,
            slots,
            font_metrics=metrics,
            screen_id="save_slots",
            title=labels["save"],
            selected_key="save_slot_1",
        ),
        menu_screen(
            width,
            height,
            confirmation,
            font_metrics=metrics,
            screen_id="confirmation",
            title="",
            selected_key="yes",
        ),
        item_screen(
            width,
            height,
            list_items,
            font_metrics=metrics,
            screen_id="item",
            title=_word(corpus, 5, labels["items"]),
            selected_key=list_items[0].key,
            preview=item_preview,
        ),
        magic_screen(
            width,
            height,
            list_items,
            font_metrics=metrics,
            screen_id="magic",
            title=_word(corpus, 4, labels["magic"]),
            selected_key=list_items[0].key,
            preview=item_preview,
        ),
        status_screen(
            width,
            height,
            (status_party, *status_fields),
            font_metrics=metrics,
            screen_id="status",
            title=_word(corpus, 3, labels["status"]),
            selected_key=status_party.key,
            portrait=status_portrait,
        ),
        equip_screen(
            width,
            height,
            (equip_party, *equipment),
            font_metrics=metrics,
            screen_id="equip",
            title=_word(corpus, 22, labels["equipment"]),
            selected_key=equip_party.key,
            preview=equip_preview,
        ),
        battle_hud_screen(
            width,
            height,
            battle_main,
            font_metrics=metrics,
            screen_id="battle_hud",
            selected_key=battle_main[0].key,
            player_focus=focus,
            stage_assets=battle_assets,
        ),
        menu_screen(
            width,
            height,
            battle_misc,
            font_metrics=metrics,
            screen_id="battle_misc",
            title=labels["misc"],
            selected_key=battle_misc[0].key,
            focus=focus,
            modal=False,
        ),
        menu_screen(
            width,
            height,
            battle_item_action,
            font_metrics=metrics,
            screen_id="battle_item_action",
            title=labels["items"],
            selected_key=battle_item_action[0].key,
            focus=focus,
            modal=False,
        ),
    )


def _text_audit(
    screens: Sequence[Mapping[str, object]],
    corpus: PalCorpus,
    metrics: FontMetrics,
) -> dict[str, object]:
    dialog = next(screen for screen in screens if screen["name"] == "dialog")
    dialog_elements = [
        element
        for element in dialog["elements"]  # type: ignore[index]
        if isinstance(element, dict)
        and element.get("kind") == "dialog_text"
    ]
    if not dialog_elements:
        raise LayoutCheckError("dialog layout exposes no text row")
    widths = [int(element["rect"][2]) for element in dialog_elements]
    text_width = min(widths)
    rows_by_page: dict[int, int] = {}
    for element in dialog_elements:
        page = int(element["page"])
        rows_by_page[page] = rows_by_page.get(page, 0) + 1
    rows_per_page = max(rows_by_page.values())
    if text_width <= 0 or rows_per_page <= 0:
        raise LayoutCheckError("dialog flow coefficients are invalid")

    max_message_lines = 0
    max_message_pages = 0
    widest_glyph = 0
    for message in corpus.rendered_messages:
        line_width = 0
        lines = 1
        for character in message:
            advance = metrics.advance(character)
            widest_glyph = max(widest_glyph, advance)
            if advance > text_width:
                raise LayoutCheckError(
                    f"glyph U+{ord(character):04X} is wider than dialog row"
                )
            if line_width and line_width + advance > text_width:
                lines += 1
                line_width = 0
            line_width += advance
        max_message_lines = max(max_message_lines, lines)
        pages = (lines + rows_per_page - 1) // rows_per_page
        max_message_pages = max(max_message_pages, pages)

    item_rows = [
        element
        for screen in screens
        if screen["name"] in ("item", "magic")
        for element in screen["elements"]  # type: ignore[index]
        if isinstance(element, dict)
        and bool(element.get("selectable"))
    ]
    if not item_rows:
        raise LayoutCheckError("item/magic layouts expose no selectable row")
    minimum_item_width = min(int(element["rect"][2]) for element in item_rows)
    widest_word = max(
        (measure_text(word, metrics) for word in corpus.words),
        default=0,
    )
    if widest_word > minimum_item_width:
        raise LayoutCheckError(
            f"actual word width {widest_word} exceeds generated item row "
            f"{minimum_item_width}"
        )

    return {
        "word_count": len(corpus.words),
        "message_count": len(corpus.messages),
        "dialog_text_width": text_width,
        "dialog_rows_per_page": rows_per_page,
        "max_message_lines": max_message_lines,
        "max_message_pages": max_message_pages,
        "minimum_item_row_width": minimum_item_width,
        "widest_word": widest_word,
        "widest_glyph": widest_glyph,
        "clipped_text_count": 0,
    }


def compile_profile(
    profile: DisplayProfile,
    metrics: FontMetrics,
    corpus: PalCorpus,
    labels: Mapping[str, str],
    assets: LayoutAssetInventory | None = None,
    font_identity: Mapping[str, object] | None = None,
) -> dict[str, object]:
    if assets is None:
        assets = default_asset_inventory()
    screen_manifests: list[dict[str, object]] = []
    sampling: list[dict[str, object]] = [_stage_sampling(profile)]
    for spec in _screen_specs(profile, metrics, corpus, labels, assets):
        solution = solve_screen(spec)  # type: ignore[arg-type]
        screen = _physicalize_screen(
            solution.to_manifest(),
            profile,
        )
        policies = screen.get("asset_scale_policies", [])
        if not isinstance(policies, list):
            raise LayoutCheckError("screen asset policies are malformed")
        for policy in policies:
            if not isinstance(policy, dict):
                raise LayoutCheckError("screen asset policy is malformed")
            _normalize_stage_policy(policy, profile)
            sampling.append(policy)
        screen_manifests.append(screen)

    battle_screen = next(
        (
            screen
            for screen in screen_manifests
            if screen.get("name") == "battle_hud"
        ),
        None,
    )
    if battle_screen is None:
        raise LayoutCheckError("profile has no battle HUD")
    panels = [
        element
        for element in battle_screen.get("elements", [])  # type: ignore[union-attr]
        if isinstance(element, dict)
        and element.get("kind") == "panel"
        and int(element.get("page", -1)) == 0
        and bool(element.get("blocks_focus"))
    ]
    if len(panels) != 1:
        raise LayoutCheckError(
            "battle HUD must expose exactly one page-zero occluding panel"
        )
    panel_rect = panels[0].get("rect")
    if (
        not isinstance(panel_rect, list)
        or len(panel_rect) != 4
        or any(not isinstance(value, int) for value in panel_rect)
    ):
        raise LayoutCheckError("battle HUD panel rectangle is malformed")
    policy: BattleCameraPolicy = battle_camera_policy(
        profile,
        CameraRect(*panel_rect),
    )
    constants = profile_camera_constants(profile, battle_policy=policy)

    catalog = _asset_sampling_catalog(profile, sampling, assets)
    sampling.extend(catalog)
    text_audit = _text_audit(screen_manifests, corpus, metrics)
    font_manifest = metrics.to_manifest()
    font_manifest.update(
        font_identity
        if font_identity is not None
        else {
            "image_bytes": 0,
            "payload_crc32": 0,
            "sha256": "0" * 64,
        }
    )
    return {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "name": profile.name,
        "generator": "tools/pal_layout_check.py",
        "coefficient_origin": "python_generated_only",
        "public_abi_version": 1,
        "display": {
            "width": profile.width,
            "height": profile.height,
        },
        "safe_rect": [
            constants.safe_rect.x,
            constants.safe_rect.y,
            constants.safe_rect.w,
            constants.safe_rect.h,
        ],
        "stage_rect": [
            constants.stage_rect.x,
            constants.stage_rect.y,
            constants.stage_rect.w,
            constants.stage_rect.h,
        ],
        "stage_source": [PAL_STAGE_WIDTH, PAL_STAGE_HEIGHT],
        "player_anchor": [
            constants.player_anchor.x,
            constants.player_anchor.y,
        ],
        "font": font_manifest,
        "loading": loading_layout(profile),
        "sampling": sampling,
        "sampling_catalog_count": len(catalog),
        "screens": screen_manifests,
        "camera": constants.as_manifest(),
        "battle_camera_policy": policy.as_manifest(),
        "camera_vectors": list(
            camera_vectors_for_profile(profile, battle_policy=policy)
        ),
        "text_audit": text_audit,
        "asset_inventory": assets.to_manifest(),
        "gameplay_contract": {
            "canonical_width": PAL_STAGE_WIDTH,
            "canonical_height": PAL_STAGE_HEIGHT,
            "presentation_only": True,
            "changes_game_state": False,
        },
    }


def _profile_preview_artifacts(
    profile: Mapping[str, object],
    font: Font10,
    fixtures: PreviewFixtures,
) -> dict[str, bytes]:
    result: dict[str, bytes] = {}
    profile_name = str(profile["name"])
    for screen in profile["screens"]:  # type: ignore[index]
        if not isinstance(screen, Mapping):
            raise LayoutCheckError("screen manifest is malformed")
        name = str(screen["name"])
        page_count = int(screen["page_count"])
        for page in range(page_count):
            path = (
                f"{profile_name}/previews/{name}-p{page + 1}.ppm"
            )
            result[path] = render_screen(
                profile,
                screen,
                page=page,
                font=font,
                assets=fixtures.assets,
                palette=fixtures.palette,
            ).ppm_bytes()
    result[
        f"{profile_name}/previews/loading-p50.ppm"
    ] = render_loading(profile, percent=50).ppm_bytes()
    for role, image in sorted(fixtures.assets.items()):
        result[
            f"{profile_name}/previews/asset-{role}.ppm"
        ] = render_asset_fixture(
            profile,
            role,
            image,
            fixtures.palette,
        ).ppm_bytes()
    vectors = profile.get("camera_vectors")
    if not isinstance(vectors, list):
        raise LayoutCheckError("camera vector manifest is malformed")
    for index, vector in enumerate(vectors):
        if not isinstance(vector, Mapping):
            raise LayoutCheckError(
                f"camera vector #{index} is malformed"
            )
        name = str(vector.get("name", f"vector_{index}"))
        safe_name = "".join(
            character
            if character.isalnum() or character in "-_"
            else "_"
            for character in name
        )
        result[
            f"{profile_name}/previews/camera-{safe_name}.ppm"
        ] = render_camera_vector(profile, vector).ppm_bytes()
    return result


def compile_bundle(
    *,
    data_dir: str | Path,
    font10_archive: str | Path,
    profiles: Iterable[DisplayProfile] = certified_profiles(),
) -> CompiledBundle:
    # Imported here to keep the package modules usable without importing the
    # full resource-pack builder.
    from pal_pack_build import FONT10_UI_LABELS

    requested_profiles = tuple(profiles)
    if not requested_profiles:
        raise LayoutCheckError("at least one display profile is required")
    profile_names = [profile.name for profile in requested_profiles]
    if len(profile_names) != len(set(profile_names)):
        raise LayoutCheckError("display profile names must be unique")

    corpus = load_pal_corpus(data_dir)
    asset_inventory = audit_asset_inventory(data_dir)
    preview_fixtures = load_preview_fixtures(data_dir)
    archive_path = Path(font10_archive)
    bdf = extract_locked_bdf(archive_path)
    codepoints = collect_pal_corpus_characters(
        data_dir,
        extra_texts=FONT10_UI_LABELS.values(),
    )
    font10_image = build_font10(bdf, codepoints)
    font10 = parse_font10(font10_image)
    metrics = FontMetrics.from_font10(font10)
    font_identity = {
        "image_bytes": len(font10_image),
        "payload_crc32": font10.payload_crc32,
        "sha256": _sha256(font10_image),
    }

    compiled_profiles = tuple(
        compile_profile(
            profile,
            metrics,
            corpus,
            FONT10_UI_LABELS,
            asset_inventory,
            font_identity,
        )
        for profile in requested_profiles
    )
    artifacts: dict[str, bytes] = {
        "font/font10.bin": font10_image,
    }
    target_headers: dict[str, bytes] = {}
    for profile in compiled_profiles:
        name = str(profile["name"])
        header_name = f"pal_ui_layout_{name}.h"
        header = emit_profile_header(profile).encode("utf-8")
        artifacts[f"{name}/layout.json"] = _stable_json_bytes(profile)
        artifacts[f"{name}/{header_name}"] = header
        artifacts.update(
            _profile_preview_artifacts(
                profile,
                font10,
                preview_fixtures,
            )
        )
        target_headers[header_name] = header

    font_summary = {
        "schema": "sdlpal-font10-layout-input",
        "version": 1,
        "release": RELEASE_VERSION,
        "release_url": RELEASE_URL,
        "archive": {
            "filename": RELEASE_ASSET,
            "bytes": archive_path.stat().st_size,
            "sha256": _sha256(archive_path.read_bytes()),
            "locked_sha256": RELEASE_SHA256,
        },
        "bdf": {
            "member": BDF_MEMBER,
            "bytes": BDF_MEMBER_BYTES,
            "sha256": BDF_MEMBER_SHA256,
        },
        "font10": {
            "bytes": len(font10_image),
            "sha256": _sha256(font10_image),
            "glyph_count": len(font10.glyphs),
            "payload_crc32": font10.payload_crc32,
            "cell_width": FONT10_CELL_WIDTH,
            "cell_height": FONT10_CELL_HEIGHT,
            "bitmap_bytes": FONT10_BITMAP_BYTES,
            "record_bytes": FONT10_RECORD_BYTES,
            "ascent": font10.ascent,
            "descent": font10.descent,
        },
        "corpus": {
            "word_count": len(corpus.words),
            "message_count": len(corpus.messages),
            "codepoint_count": len(codepoints),
            "source_files": [
                {"name": name, "bytes": size, "sha256": digest}
                for name, size, digest in corpus.source_files
            ],
            "ui_labels": dict(sorted(FONT10_UI_LABELS.items())),
        },
    }
    artifacts["font/font10.json"] = _stable_json_bytes(font_summary)

    bundle_manifest = {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "generator": "tools/pal_layout_check.py",
        "coefficient_origin": "python_generated_only",
        "profiles": [
            {
                "name": profile["name"],
                "layout_sha256": _sha256(
                    artifacts[f"{profile['name']}/layout.json"]
                ),
                "header_sha256": _sha256(
                    artifacts[
                        f"{profile['name']}/pal_ui_layout_"
                        f"{profile['name']}.h"
                    ]
                ),
                "screen_count": len(profile["screens"]),  # type: ignore[arg-type]
                "sampling_policy_count": len(
                    profile["sampling"]  # type: ignore[arg-type]
                ),
            }
            for profile in compiled_profiles
        ],
        "font10_sha256": _sha256(font10_image),
        "preview_fixtures": {
            role: {
                "source": image.source,
                "width": image.width,
                "height": image.height,
                "indices_sha256": _sha256(image.indices),
                "opaque_sha256": _sha256(image.opaque),
            }
            for role, image in sorted(preview_fixtures.assets.items())
        },
        "artifact_hashes": {
            path: _sha256(payload)
            for path, payload in sorted(artifacts.items())
        },
    }
    artifacts["manifest.json"] = _stable_json_bytes(bundle_manifest)
    ordered_artifacts = dict(sorted(artifacts.items()))
    ordered_headers = dict(sorted(target_headers.items()))
    return CompiledBundle(
        artifacts=ordered_artifacts,
        target_headers=ordered_headers,
        profiles=compiled_profiles,
    )


def _assert_reproducible(first: CompiledBundle, second: CompiledBundle) -> None:
    if first.artifacts != second.artifacts:
        differing = sorted(
            set(first.artifacts)
            | set(second.artifacts)
        )
        differing = [
            path
            for path in differing
            if first.artifacts.get(path) != second.artifacts.get(path)
        ]
        raise LayoutCheckError(
            "layout compiler is not byte-deterministic: "
            + ", ".join(differing)
        )
    if first.target_headers != second.target_headers:
        raise LayoutCheckError("target generated headers are not deterministic")


def _write_artifacts(root: Path, artifacts: Mapping[str, bytes]) -> None:
    for relative, payload in artifacts.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)


def _check_artifacts(root: Path, artifacts: Mapping[str, bytes]) -> None:
    failures: list[str] = []
    for relative, expected in artifacts.items():
        path = root / relative
        if not path.is_file():
            failures.append(f"missing {path}")
        elif path.read_bytes() != expected:
            failures.append(f"stale {path}")
    if failures:
        raise LayoutCheckError("; ".join(failures))


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Compile PAL native UI layouts and all target-side coefficients"
        )
    )
    parser.add_argument("--data-dir", type=Path, default=DEFAULT_DATA_DIR)
    parser.add_argument("--font10-archive", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--target-header-dir",
        type=Path,
        help="also write/check the headers included by target builds",
    )
    parser.add_argument(
        "--profile",
        action="append",
        default=[],
        metavar="WIDTHxHEIGHT[@L,T,R,B]",
        help="repeatable; defaults to 240x135 and 160x128",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="compare generated bytes with existing output instead of writing",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    profile_values = args.profile or list(DEFAULT_PROFILES)
    profiles = tuple(parse_profile_spec(value) for value in profile_values)
    first = compile_bundle(
        data_dir=args.data_dir,
        font10_archive=args.font10_archive,
        profiles=profiles,
    )
    # Compile from the immutable source inputs again.  This catches accidental
    # dependence on set/dict iteration or mutable module state.
    second = compile_bundle(
        data_dir=args.data_dir,
        font10_archive=args.font10_archive,
        profiles=profiles,
    )
    _assert_reproducible(first, second)

    if args.check:
        _check_artifacts(args.output_dir, first.artifacts)
        if args.target_header_dir is not None:
            _check_artifacts(args.target_header_dir, first.target_headers)
    else:
        _write_artifacts(args.output_dir, first.artifacts)
        if args.target_header_dir is not None:
            _write_artifacts(args.target_header_dir, first.target_headers)

    summary = {
        "profiles": [profile.name for profile in profiles],
        "artifact_count": len(first.artifacts),
        "font10_bytes": len(first.artifacts["font/font10.bin"]),
        "target_header_count": len(first.target_headers),
        "mode": "check" if args.check else "write",
    }
    sys.stdout.write(
        json.dumps(summary, sort_keys=True, separators=(",", ":")) + "\n"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (LayoutCheckError, FileNotFoundError, ValueError) as error:
        sys.stderr.write(f"pal layout check: {error}\n")
        raise SystemExit(1) from error
