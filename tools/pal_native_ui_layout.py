#!/usr/bin/env python3
"""Generate native framebuffer, map-view, and dialogue constants.

The generated profile describes a physical render target. 320x200 remains a
legacy script/event coordinate contract; it is not an intermediate framebuffer
and the presenter never resizes a completed legacy scene.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable

import pal_pack_build


VIRTUAL_WIDTH = 320
VIRTUAL_HEIGHT = 200
MIN_DISPLAY_WIDTH = 160
MIN_DISPLAY_HEIGHT = 128
DEFAULT_PROFILES = ((240, 135), (160, 128))
MAP_VIEW_FOCUS_X = 160
MAP_VIEW_FOCUS_Y = 112
FONT_LINE_HEIGHT = 10
DIALOG_PAGE_LINES = 4
DIALOG_SOURCE_LINE_CELLS = 13
DIALOG_POPUP_SINGLE_TEXT_INSET_Y = 10


@dataclass(frozen=True)
class Rect:
    x: int
    y: int
    width: int
    height: int


@dataclass(frozen=True)
class DialogLayout:
    portrait: Rect
    title_x: int
    title_without_portrait_x: int
    title_y: int
    text: Rect
    text_without_portrait: Rect
    line_height: int
    page_lines: int


@dataclass(frozen=True)
class SystemMenuLayout:
    x: int
    y: int
    text_x: int
    text_y: int
    row_height: int
    visible_rows: int


@dataclass(frozen=True)
class Profile:
    name: str
    display_width: int
    display_height: int
    map_view_offset_x: int
    map_view_offset_y: int
    upper: DialogLayout
    lower: DialogLayout
    center_text: Rect
    system_menu: SystemMenuLayout
    font: dict[str, int]


def _clamp(value: int, low: int, high: int) -> int:
    return max(low, min(value, high))


def _origin(focus: int, extent: int, logical_extent: int) -> int:
    return _clamp(focus - extent // 2, 0, logical_extent - extent)


def _validate_profile_size(width: int, height: int) -> None:
    if not MIN_DISPLAY_WIDTH <= width <= VIRTUAL_WIDTH:
        raise ValueError(
            "native PAL display width must be between "
            f"{MIN_DISPLAY_WIDTH} and {VIRTUAL_WIDTH}: {width}"
        )
    if not MIN_DISPLAY_HEIGHT <= height <= VIRTUAL_HEIGHT:
        raise ValueError(
            "native PAL display height must be between "
            f"{MIN_DISPLAY_HEIGHT} and {VIRTUAL_HEIGHT}: {height}"
        )


def build_profile(
    width: int,
    height: int,
    font_summary: dict[str, object],
) -> Profile:
    _validate_profile_size(width, height)

    margin = 4
    gap = 4
    title_gap = 2
    portrait_size = 64 if width >= 200 else 48
    text_width = width - margin * 2 - portrait_size - gap
    block_height = FONT_LINE_HEIGHT * (DIALOG_PAGE_LINES + 1) + title_gap
    lower_title_y = height - margin - block_height
    lower_text_y = lower_title_y + FONT_LINE_HEIGHT + title_gap

    metrics = font_summary["metrics"]
    assert isinstance(metrics, dict)
    font = {
        "glyph_count": int(font_summary["glyph_count"]),
        "image_bytes": int(font_summary["bytes"]),
        "payload_crc32": int(font_summary["payload_crc32"]),
        "cell_width": int(metrics["cell_width"]),
        "cell_height": int(metrics["cell_height"]),
        "ascent": int(metrics["ascent"]),
        "descent": int(metrics["descent"]),
    }
    if font["cell_width"] != 10 or font["cell_height"] != 10:
        raise ValueError("native PAL UI requires the pinned 10x10 FONT10 cell")

    system_row_height = 18
    system_text_y = 12
    system_bottom_inset = 12
    system_visible_rows = max(
        1,
        1 + (height - system_text_y - system_bottom_inset -
             FONT_LINE_HEIGHT) // system_row_height,
    )

    return Profile(
        name=f"{width}x{height}",
        display_width=width,
        display_height=height,
        map_view_offset_x=_origin(
            MAP_VIEW_FOCUS_X, width, VIRTUAL_WIDTH
        ),
        map_view_offset_y=_origin(
            MAP_VIEW_FOCUS_Y, height, VIRTUAL_HEIGHT
        ),
        upper=DialogLayout(
            portrait=Rect(margin, margin, portrait_size, portrait_size),
            title_x=margin + portrait_size + gap,
            # Keep PAL's portrait-free speaker-title inset.  It must not
            # inherit the upper portrait column when opcode 003c uses face 0.
            title_without_portrait_x=12,
            title_y=margin,
            text=Rect(
                margin + portrait_size + gap,
                margin + FONT_LINE_HEIGHT + title_gap,
                text_width,
                FONT_LINE_HEIGHT * DIALOG_PAGE_LINES,
            ),
            text_without_portrait=Rect(
                margin,
                margin + FONT_LINE_HEIGHT + title_gap,
                width - margin * 2,
                FONT_LINE_HEIGHT * DIALOG_PAGE_LINES,
            ),
            line_height=FONT_LINE_HEIGHT,
            page_lines=DIALOG_PAGE_LINES,
        ),
        lower=DialogLayout(
            portrait=Rect(
                width - margin - portrait_size,
                height - margin - portrait_size,
                portrait_size,
                portrait_size,
            ),
            title_x=margin,
            # PAL uses x=4 beside a lower portrait and x=12 without one.
            title_without_portrait_x=12,
            title_y=lower_title_y,
            text=Rect(
                margin,
                lower_text_y,
                text_width,
                FONT_LINE_HEIGHT * DIALOG_PAGE_LINES,
            ),
            text_without_portrait=Rect(
                margin,
                lower_text_y,
                width - margin * 2,
                FONT_LINE_HEIGHT * DIALOG_PAGE_LINES,
            ),
            line_height=FONT_LINE_HEIGHT,
            page_lines=DIALOG_PAGE_LINES,
        ),
        center_text=Rect(
            margin,
            (height - FONT_LINE_HEIGHT * DIALOG_PAGE_LINES) // 2,
            width - margin * 2,
            FONT_LINE_HEIGHT * DIALOG_PAGE_LINES,
        ),
        system_menu=SystemMenuLayout(
            x=0,
            y=0,
            text_x=13,
            text_y=system_text_y,
            row_height=system_row_height,
            visible_rows=system_visible_rows,
        ),
        font=font,
    )


def _macro_rect(prefix: str, rect: Rect) -> list[str]:
    return [
        f"#define {prefix}_X {rect.x}",
        f"#define {prefix}_Y {rect.y}",
        f"#define {prefix}_WIDTH {rect.width}u",
        f"#define {prefix}_HEIGHT {rect.height}u",
    ]


def emit_header(profile: Profile) -> str:
    guard = f"PAL_NATIVE_UI_{profile.name.upper()}_GENERATED_H"
    p = "PAL_NATIVE_UI_GENERATED"
    loading_scale = 3 if profile.display_width >= 200 else 2
    loading_glyph_width = 5
    loading_glyph_height = 7
    loading_glyph_advance = 6
    loading_glyph_count = 7
    loading_label_width = (
        loading_glyph_count * loading_glyph_advance * loading_scale
    )
    loading_label_height = loading_glyph_height * loading_scale
    loading_margin = profile.display_width // 12
    loading_bar_height = 18 if profile.display_width >= 200 else 16
    lines = [
        "/* Generated by tools/pal_native_ui_layout.py; do not edit. */",
        f"/* profile={profile.name} responsive PAL display */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#define PAL_NATIVE_UI_SCHEMA_VERSION 1u",
        f"#define {p}_DISPLAY_WIDTH {profile.display_width}u",
        f"#define {p}_DISPLAY_HEIGHT {profile.display_height}u",
        f"#define {p}_VIRTUAL_WIDTH {VIRTUAL_WIDTH}u",
        f"#define {p}_VIRTUAL_HEIGHT {VIRTUAL_HEIGHT}u",
        f"#define {p}_MAP_VIEW_FOCUS_X {MAP_VIEW_FOCUS_X}",
        f"#define {p}_MAP_VIEW_FOCUS_Y {MAP_VIEW_FOCUS_Y}",
        f"#define {p}_MAP_VIEW_OFFSET_X {profile.map_view_offset_x}u",
        f"#define {p}_MAP_VIEW_OFFSET_Y {profile.map_view_offset_y}u",
        f"#define {p}_FONT_GLYPH_COUNT {profile.font['glyph_count']}u",
        f"#define {p}_FONT_IMAGE_BYTES {profile.font['image_bytes']}u",
        f"#define {p}_FONT_PAYLOAD_CRC32 0x{profile.font['payload_crc32']:08x}u",
        f"#define {p}_FONT_CELL_WIDTH {profile.font['cell_width']}u",
        f"#define {p}_FONT_CELL_HEIGHT {profile.font['cell_height']}u",
        f"#define {p}_FONT_ASCENT {profile.font['ascent']}u",
        f"#define {p}_FONT_DESCENT {profile.font['descent']}u",
        f"#define {p}_DIALOG_LINE_HEIGHT {FONT_LINE_HEIGHT}u",
        f"#define {p}_DIALOG_PAGE_LINES {DIALOG_PAGE_LINES}u",
        f"#define {p}_DIALOG_SOURCE_LINE_CELLS {DIALOG_SOURCE_LINE_CELLS}u",
        f"#define {p}_DIALOG_POPUP_SINGLE_TEXT_INSET_Y {DIALOG_POPUP_SINGLE_TEXT_INSET_Y}u",
        "",
        "/* Original DATA.MKF #9 visual contract; no replacement chrome. */",
        f"#define {p}_UI_ARCHIVE_CHUNK 9u",
        f"#define {p}_BOX_STYLE0_FIRST 0u",
        f"#define {p}_BOX_STYLE1_FIRST 9u",
        f"#define {p}_PLAYER_INFO_FRAME 18u",
        f"#define {p}_SINGLE_LINE_LEFT_FRAME 44u",
        f"#define {p}_SINGLE_LINE_MIDDLE_FRAME 45u",
        f"#define {p}_SINGLE_LINE_RIGHT_FRAME 46u",
        f"#define {p}_ARROW_FRAME 47u",
        f"#define {p}_CURSOR_FIRST_FRAME 66u",
        f"#define {p}_ITEM_BOX_FRAME 70u",
        f"#define {p}_MENU_COLOR 0x4fu",
        f"#define {p}_MENU_INACTIVE_COLOR 0x18u",
        f"#define {p}_MENU_CONFIRMED_COLOR 0x2cu",
        f"#define {p}_MENU_SELECTED_FIRST_COLOR 0xf9u",
        "",
        "/* Chapter-cache loading screen geometry; not gameplay UI. */",
        f"#define {p}_LOADING_GLYPH_WIDTH {loading_glyph_width}u",
        f"#define {p}_LOADING_GLYPH_HEIGHT {loading_glyph_height}u",
        f"#define {p}_LOADING_GLYPH_ADVANCE {loading_glyph_advance}u",
        f"#define {p}_LOADING_GLYPH_COUNT {loading_glyph_count}u",
        f"#define {p}_LOADING_GLYPH_SCALE {loading_scale}u",
        f"#define {p}_LOADING_LABEL_X {(profile.display_width - loading_label_width) // 2}",
        f"#define {p}_LOADING_LABEL_Y {profile.display_height // 5}",
        f"#define {p}_LOADING_LABEL_WIDTH {loading_label_width}u",
        f"#define {p}_LOADING_LABEL_HEIGHT {loading_label_height}u",
        f"#define {p}_LOADING_BAR_X {loading_margin}",
        f"#define {p}_LOADING_BAR_Y {profile.display_height * 3 // 5}",
        f"#define {p}_LOADING_BAR_WIDTH {profile.display_width - 2 * loading_margin}u",
        f"#define {p}_LOADING_BAR_HEIGHT {loading_bar_height}u",
        f"#define {p}_LOADING_BAR_BORDER 2u",
        f"#define {p}_LOADING_PROGRESS_MAX 100u",
        "",
    ]
    lines.extend(_macro_rect(f"{p}_DIALOG_UPPER_PORTRAIT", profile.upper.portrait))
    lines.extend([
        f"#define {p}_DIALOG_UPPER_TITLE_X {profile.upper.title_x}",
        f"#define {p}_DIALOG_UPPER_TITLE_NO_PORTRAIT_X {profile.upper.title_without_portrait_x}",
        f"#define {p}_DIALOG_UPPER_TITLE_Y {profile.upper.title_y}",
    ])
    lines.extend(_macro_rect(f"{p}_DIALOG_UPPER_TEXT", profile.upper.text))
    lines.extend(_macro_rect(
        f"{p}_DIALOG_UPPER_TEXT_NO_PORTRAIT",
        profile.upper.text_without_portrait,
    ))
    lines.append("")
    lines.extend(_macro_rect(f"{p}_DIALOG_LOWER_PORTRAIT", profile.lower.portrait))
    lines.extend([
        f"#define {p}_DIALOG_LOWER_TITLE_X {profile.lower.title_x}",
        f"#define {p}_DIALOG_LOWER_TITLE_NO_PORTRAIT_X {profile.lower.title_without_portrait_x}",
        f"#define {p}_DIALOG_LOWER_TITLE_Y {profile.lower.title_y}",
    ])
    lines.extend(_macro_rect(f"{p}_DIALOG_LOWER_TEXT", profile.lower.text))
    lines.extend(_macro_rect(
        f"{p}_DIALOG_LOWER_TEXT_NO_PORTRAIT",
        profile.lower.text_without_portrait,
    ))
    lines.append("")
    lines.extend(_macro_rect(f"{p}_DIALOG_CENTER_TEXT", profile.center_text))
    lines.extend([
        "",
        "/* System menu starts at the LCD origin and scrolls if required. */",
        f"#define {p}_SYSTEM_MENU_X {profile.system_menu.x}",
        f"#define {p}_SYSTEM_MENU_Y {profile.system_menu.y}",
        f"#define {p}_SYSTEM_MENU_TEXT_X {profile.system_menu.text_x}",
        f"#define {p}_SYSTEM_MENU_TEXT_Y {profile.system_menu.text_y}",
        f"#define {p}_SYSTEM_MENU_ROW_HEIGHT {profile.system_menu.row_height}u",
        f"#define {p}_SYSTEM_MENU_VISIBLE_ROWS {profile.system_menu.visible_rows}u",
    ])
    lines.extend(["", f"#endif /* {guard} */", ""])
    return "\n".join(lines)


def write_if_changed(path: Path, data: bytes, check: bool) -> None:
    if check:
        if not path.is_file() or path.read_bytes() != data:
            raise SystemExit(f"generated native UI output is stale: {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file() or path.read_bytes() != data:
        path.write_bytes(data)


def generate(
    data_dir: Path,
    font_archive: Path,
    output_dir: Path,
    header_dir: Path,
    check: bool,
    profile_sizes: Iterable[tuple[int, int]] = DEFAULT_PROFILES,
) -> None:
    _chunk, summary = pal_pack_build.build_font10_archive_chunk(
        data_dir, font_archive
    )
    font_summary = summary["font10"]
    assert isinstance(font_summary, dict)
    profiles: list[Profile] = []
    for width, height in profile_sizes:
        profile = build_profile(width, height, font_summary)
        profiles.append(profile)
        header = emit_header(profile).encode("utf-8")
        write_if_changed(
            header_dir / f"pal_native_ui_{profile.name}.h", header, check
        )
        payload = json.dumps(
            asdict(profile), ensure_ascii=False, indent=2, sort_keys=True
        ).encode("utf-8") + b"\n"
        write_if_changed(output_dir / f"pal_native_ui_{profile.name}.json", payload, check)

    manifest_payload = {
        "schema": "sdlpal-native-ui-layout",
        "version": 1,
        "rendering": "direct-native-framebuffer",
        "virtual_coordinates": "legacy-events-and-scripts-only",
        "map_view": "native-source-region-centered-on-party-anchor",
        "completed_scene_scaling": False,
        "profiles": [profile.name for profile in profiles],
        "font10": font_summary,
    }
    encoded = json.dumps(
        manifest_payload, ensure_ascii=False, indent=2, sort_keys=True
    ).encode("utf-8") + b"\n"
    manifest_payload["self_sha256"] = hashlib.sha256(encoded).hexdigest()
    encoded = json.dumps(
        manifest_payload, ensure_ascii=False, indent=2, sort_keys=True
    ).encode("utf-8") + b"\n"
    write_if_changed(output_dir / "manifest.json", encoded, check)


def parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-dir", type=Path, required=True)
    parser.add_argument("--font10-archive", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("tmp_ui/layout"))
    parser.add_argument(
        "--header-dir", type=Path,
        default=Path("esp32s3/main/generated"),
    )
    parser.add_argument(
        "--profile",
        action="append",
        metavar="WIDTHxHEIGHT",
        help=(
            "native display profile to generate; repeat for multiple profiles "
            "(default: 240x135 and 160x128)"
        ),
    )
    parser.add_argument("--check", action="store_true")
    return parser.parse_args(argv)


def parse_profile_size(value: str) -> tuple[int, int]:
    parts = value.lower().split("x")
    if len(parts) != 2 or not all(part.isdecimal() for part in parts):
        raise ValueError(f"invalid native PAL profile: {value!r}")
    width, height = (int(part) for part in parts)
    _validate_profile_size(width, height)
    return width, height


def main(argv: Iterable[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        profile_sizes = (
            tuple(parse_profile_size(value) for value in args.profile)
            if args.profile
            else DEFAULT_PROFILES
        )
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    if len(set(profile_sizes)) != len(profile_sizes):
        raise SystemExit("duplicate native PAL profile")
    generate(
        args.data_dir,
        args.font10_archive,
        args.output_dir,
        args.header_dir,
        args.check,
        profile_sizes,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
