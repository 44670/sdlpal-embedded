#!/usr/bin/env python3
"""Emit deterministic, header-only C data for one resolved UI profile.

The target never parses the JSON manifest and never runs a layout solver.  It
includes the generated header and executes only the integer sampling/camera
formulas represented by these constants and test vectors.
"""

from __future__ import annotations

import json
import re
from math import gcd
from pathlib import Path
from typing import Any, Mapping, Sequence


SCHEMA_VERSION = 1
FIXED_Q_SHIFT = 16
FIXED_Q_ONE = 1 << FIXED_Q_SHIFT
ELEMENT_FLAG_VISIBLE = 1 << 0
ELEMENT_FLAG_SELECTABLE = 1 << 1
ELEMENT_FLAG_CRITICAL = 1 << 2
SAMPLING_FLAG_VISIBLE = 1 << 0
SAMPLING_FLAG_LEGACY_STAGE = 1 << 1
SAMPLING_FLAG_CATALOG = 1 << 2
CAMERA_FLAG_CLAMPED = 1 << 0
NO_SCREEN_ID = 255
VARIANT_IDS = {
    "full": 0,
    "compact": 1,
    "single_column": 2,
    "paged": 3,
    "text_only": 4,
}
FILTER_IDS = {
    "nearest_center": 0,
    "box_2x2": 1,
    "none": 255,
}
ASSET_CLASS_IDS = {
    "none": 0,
    "legacy_stage": 1,
    "portrait": 2,
    "item_preview": 3,
    "battle_player": 4,
    "battle_enemy": 5,
    "battle_fire": 6,
    "ui_sprite": 7,
    "battle_effect": 8,
}
ASSET_CLASS_ALIASES = {
    "stage": "legacy_stage",
    "equipment_preview": "item_preview",
}


class EmitError(ValueError):
    """The resolved manifest is incomplete or unsafe to represent in C."""


def _mapping(value: object, context: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise EmitError(f"{context} must be an object")
    return value


def _sequence(value: object, context: str) -> Sequence[Any]:
    if not isinstance(value, Sequence) or isinstance(
        value, (str, bytes, bytearray)
    ):
        raise EmitError(f"{context} must be an array")
    return value


def _integer(
    value: object,
    context: str,
    *,
    minimum: int | None = None,
    maximum: int | None = None,
) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise EmitError(f"{context} must be an integer")
    if minimum is not None and value < minimum:
        raise EmitError(f"{context} must be >= {minimum}")
    if maximum is not None and value > maximum:
        raise EmitError(f"{context} must be <= {maximum}")
    return value


def _name(value: object, context: str) -> str:
    if not isinstance(value, str) or not value:
        raise EmitError(f"{context} must be a non-empty string")
    return value


def _rect(
    value: object, context: str
) -> tuple[int, int, int, int]:
    if isinstance(value, Sequence) and not isinstance(
        value, (str, bytes, bytearray)
    ):
        if len(value) != 4:
            raise EmitError(f"{context} must contain four integers")
        return (
            _integer(
                value[0], f"{context}[0]", minimum=-32768, maximum=32767
            ),
            _integer(
                value[1], f"{context}[1]", minimum=-32768, maximum=32767
            ),
            _integer(
                value[2], f"{context}[2]", minimum=0, maximum=65535
            ),
            _integer(
                value[3], f"{context}[3]", minimum=0, maximum=65535
            ),
        )
    rect = _mapping(value, context)
    width_key = "w" if "w" in rect else "width"
    height_key = "h" if "h" in rect else "height"
    return (
        _integer(rect.get("x"), f"{context}.x", minimum=-32768, maximum=32767),
        _integer(rect.get("y"), f"{context}.y", minimum=-32768, maximum=32767),
        _integer(
            rect.get(width_key),
            f"{context}.{width_key}",
            minimum=0,
            maximum=65535,
        ),
        _integer(
            rect.get(height_key),
            f"{context}.{height_key}",
            minimum=0,
            maximum=65535,
        ),
    )


def _identifier(value: str) -> str:
    ident = re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").upper()
    if not ident:
        raise EmitError(f"name {value!r} has no C identifier characters")
    if ident[0].isdigit():
        ident = f"N_{ident}"
    return ident


def _guard(profile_name: str) -> str:
    return f"PAL_UI_LAYOUT_{_identifier(profile_name)}_GENERATED_H"


def _u(value: int) -> str:
    if value < 0:
        return str(value)
    return f"{value}u"


def _rect_initializer(rect: tuple[int, int, int, int]) -> str:
    return (
        "{ "
        f"{rect[0]}, {rect[1]}, {_u(rect[2])}, {_u(rect[3])}"
        " }"
    )


def _rect_contains(
    outer: tuple[int, int, int, int],
    inner: tuple[int, int, int, int],
) -> bool:
    return (
        inner[2] > 0
        and inner[3] > 0
        and inner[0] >= outer[0]
        and inner[1] >= outer[1]
        and inner[0] + inner[2] <= outer[0] + outer[2]
        and inner[1] + inner[3] <= outer[1] + outer[3]
    )


def _rect_intersects(
    first: tuple[int, int, int, int],
    second: tuple[int, int, int, int],
) -> bool:
    return (
        first[0] < second[0] + second[2]
        and second[0] < first[0] + first[2]
        and first[1] < second[1] + second[3]
        and second[1] < first[1] + first[3]
    )


def _nearest_center_axis_map(
    source_extent: int,
    destination_extent: int,
) -> tuple[int, ...]:
    """Freeze the exact nearest-centre destination-to-source map."""

    return tuple(
        ((2 * index + 1) * source_extent) // (2 * destination_extent)
        for index in range(destination_extent)
    )


def _append_u16_array(
    lines: list[str],
    declaration: str,
    values: Sequence[int],
) -> None:
    lines.append(declaration)
    for first in range(0, len(values), 12):
        row = ", ".join(_u(value) for value in values[first : first + 12])
        lines.append(f"    {row},")
    lines.append("};")


def _stable_json(value: object) -> str:
    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    )


def _manifest_digest(manifest: Mapping[str, Any]) -> str:
    import hashlib

    canonical = _stable_json(manifest).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def emit_profile_header(profile: Mapping[str, Any]) -> str:
    """Return a byte-stable C header for one resolved profile manifest."""

    schema_version = _integer(
        profile.get("schema_version"),
        "schema_version",
        minimum=SCHEMA_VERSION,
        maximum=SCHEMA_VERSION,
    )
    profile_name = _name(profile.get("name"), "name")
    display = _mapping(profile.get("display"), "display")
    display_width = _integer(
        display.get("width"), "display.width", minimum=1, maximum=65535
    )
    display_height = _integer(
        display.get("height"), "display.height", minimum=1, maximum=65535
    )
    safe = _rect(profile.get("safe_rect"), "safe_rect")
    stage = _rect(profile.get("stage_rect"), "stage_rect")
    if stage[2] == 0 or stage[3] == 0:
        raise EmitError("stage_rect must be positive")
    stage_source_value = _sequence(
        profile.get("stage_source"),
        "stage_source",
    )
    if len(stage_source_value) != 2:
        raise EmitError("stage_source must contain exactly two integers")
    stage_source_width = _integer(
        stage_source_value[0],
        "stage_source[0]",
        minimum=1,
        maximum=65535,
    )
    stage_source_height = _integer(
        stage_source_value[1],
        "stage_source[1]",
        minimum=1,
        maximum=65535,
    )
    if (
        stage[2] * stage_source_height
        <= stage[3] * stage_source_width
    ):
        stage_scale_numerator = stage[2]
        stage_scale_denominator = stage_source_width
    else:
        stage_scale_numerator = stage[3]
        stage_scale_denominator = stage_source_height
    stage_divisor = gcd(
        stage_scale_numerator, stage_scale_denominator
    )
    stage_scale_numerator //= stage_divisor
    stage_scale_denominator //= stage_divisor
    if (
        stage_source_width
        * stage_scale_numerator
        // stage_scale_denominator
        != stage[2]
        or stage_source_height
        * stage_scale_numerator
        // stage_scale_denominator
        != stage[3]
    ):
        raise EmitError(
            "stage_rect is not a contained uniform stage transform"
        )
    stage_scale_q16 = (
        stage_scale_numerator << FIXED_Q_SHIFT
    ) // stage_scale_denominator
    stage_step_x_q16 = (
        stage_source_width << FIXED_Q_SHIFT
    ) // stage[2]
    stage_step_y_q16 = (
        stage_source_height << FIXED_Q_SHIFT
    ) // stage[3]
    stage_phase_x_q16 = stage_step_x_q16 // 2
    stage_phase_y_q16 = stage_step_y_q16 // 2
    stage_sample_x = _nearest_center_axis_map(
        stage_source_width,
        stage[2],
    )
    stage_sample_y = _nearest_center_axis_map(
        stage_source_height,
        stage[3],
    )

    battle_value = profile.get("battle_camera_policy")
    if battle_value is None:
        battle_arena = (0, 0, stage_source_width, stage_source_height)
        battle_hud = (0, 0, 0, 0)
        battle_content = safe
        battle_focus_source = (
            min(battle_arena[2], battle_content[2]),
            min(battle_arena[3], battle_content[3]),
        )
        battle_focus_screen = (
            battle_content[0]
            + (battle_content[2] - battle_focus_source[0]) // 2,
            battle_content[1]
            + (battle_content[3] - battle_focus_source[1]) // 2,
            battle_focus_source[0],
            battle_focus_source[1],
        )
        common = gcd(battle_arena[2], battle_arena[3])
        fit_units = min(
            common,
            battle_content[2] * common // battle_arena[2],
            battle_content[3] * common // battle_arena[3],
        )
        fit_divisor = gcd(fit_units, common)
        battle_fit_numerator = fit_units // fit_divisor
        battle_fit_denominator = common // fit_divisor
        battle_fit_screen = (
            battle_content[0]
            + (
                battle_content[2]
                - battle_arena[2]
                * battle_fit_numerator
                // battle_fit_denominator
            )
            // 2,
            battle_content[1]
            + (
                battle_content[3]
                - battle_arena[3]
                * battle_fit_numerator
                // battle_fit_denominator
            )
            // 2,
            battle_arena[2]
            * battle_fit_numerator
            // battle_fit_denominator,
            battle_arena[3]
            * battle_fit_numerator
            // battle_fit_denominator,
        )
        battle_padding = 4
        battle_max_players = 3
        battle_mapping: Mapping[str, Any] | None = None
    else:
        battle_mapping = _mapping(
            battle_value,
            "battle_camera_policy",
        )
        battle_arena = _rect(
            battle_mapping.get("arena"),
            "battle_camera_policy.arena",
        )
        battle_hud = _rect(
            battle_mapping.get("hud_rect"),
            "battle_camera_policy.hud_rect",
        )
        battle_content = _rect(
            battle_mapping.get("content_rect"),
            "battle_camera_policy.content_rect",
        )
        focus_source_value = _sequence(
            battle_mapping.get("focus_source"),
            "battle_camera_policy.focus_source",
        )
        if len(focus_source_value) != 2:
            raise EmitError(
                "battle_camera_policy.focus_source must be a pair"
            )
        battle_focus_source = (
            _integer(
                focus_source_value[0],
                "battle_camera_policy.focus_source[0]",
                minimum=1,
                maximum=65535,
            ),
            _integer(
                focus_source_value[1],
                "battle_camera_policy.focus_source[1]",
                minimum=1,
                maximum=65535,
            ),
        )
        battle_focus_screen = _rect(
            battle_mapping.get("focus_screen"),
            "battle_camera_policy.focus_screen",
        )
        battle_fit_screen = _rect(
            battle_mapping.get("fit_screen"),
            "battle_camera_policy.fit_screen",
        )
        fit_scale_value = _sequence(
            battle_mapping.get("fit_scale"),
            "battle_camera_policy.fit_scale",
        )
        if len(fit_scale_value) != 2:
            raise EmitError(
                "battle_camera_policy.fit_scale must be a pair"
            )
        battle_fit_numerator = _integer(
            fit_scale_value[0],
            "battle_camera_policy.fit_scale[0]",
            minimum=1,
            maximum=65535,
        )
        battle_fit_denominator = _integer(
            fit_scale_value[1],
            "battle_camera_policy.fit_scale[1]",
            minimum=1,
            maximum=65535,
        )
        battle_padding = _integer(
            battle_mapping.get("padding"),
            "battle_camera_policy.padding",
            minimum=0,
            maximum=255,
        )
        battle_max_players = _integer(
            battle_mapping.get("max_players"),
            "battle_camera_policy.max_players",
            minimum=1,
            maximum=255,
        )

    battle_fit_scale_q16 = (
        battle_fit_numerator << FIXED_Q_SHIFT
    ) // battle_fit_denominator
    battle_fit_step_q16 = (
        battle_fit_denominator << FIXED_Q_SHIFT
    ) // battle_fit_numerator
    battle_fit_phase_q16 = battle_fit_step_q16 // 2
    if battle_max_players != 3:
        raise EmitError(
            "battle_camera_policy.max_players must equal runtime capacity 3"
        )
    if (
        battle_hud != (0, 0, 0, 0)
        and not _rect_contains(safe, battle_hud)
    ):
        raise EmitError(
            "battle_camera_policy.hud_rect must be zero or inside safe_rect"
        )
    if (
        battle_arena[2] * battle_fit_numerator
        % battle_fit_denominator
        or battle_arena[3] * battle_fit_numerator
        % battle_fit_denominator
    ):
        raise EmitError(
            "battle_camera_policy.fit_scale must exactly divide both arena axes"
        )
    if (
        battle_arena[2] == 0
        or battle_arena[3] == 0
        or not _rect_contains(safe, battle_content)
        or not _rect_contains(battle_content, battle_focus_screen)
        or not _rect_contains(battle_content, battle_fit_screen)
        or _rect_intersects(battle_hud, battle_content)
        or battle_focus_screen[2] != battle_focus_source[0]
        or battle_focus_screen[3] != battle_focus_source[1]
        or battle_fit_screen[2]
        != battle_arena[2]
        * battle_fit_numerator
        // battle_fit_denominator
        or battle_fit_screen[3]
        != battle_arena[3]
        * battle_fit_numerator
        // battle_fit_denominator
    ):
        raise EmitError("battle camera policy geometry is inconsistent")
    battle_fit_sample_x = _nearest_center_axis_map(
        battle_arena[2],
        battle_fit_screen[2],
    )
    battle_fit_sample_y = _nearest_center_axis_map(
        battle_arena[3],
        battle_fit_screen[3],
    )
    if battle_mapping is not None:
        for field_name, expected in (
            ("fit_scale_q16", battle_fit_scale_q16),
            ("fit_step_q16", battle_fit_step_q16),
            ("fit_phase_q16", battle_fit_phase_q16),
        ):
            if _integer(
                battle_mapping.get(field_name),
                f"battle_camera_policy.{field_name}",
                minimum=0,
                maximum=0xFFFFFFFF,
            ) != expected:
                raise EmitError(
                    f"battle_camera_policy.{field_name} disagrees with scale"
                )
        for field_name, expected in (
            ("fit_sample_x", battle_fit_sample_x),
            ("fit_sample_y", battle_fit_sample_y),
        ):
            supplied = tuple(
                _integer(
                    value,
                    f"battle_camera_policy.{field_name}[{index}]",
                    minimum=0,
                    maximum=65535,
                )
                for index, value in enumerate(
                    _sequence(
                        battle_mapping.get(field_name),
                        f"battle_camera_policy.{field_name}",
                    )
                )
            )
            if supplied != expected:
                raise EmitError(
                    f"battle_camera_policy.{field_name} is not exact"
                )

    anchor = _sequence(profile.get("player_anchor"), "player_anchor")
    if len(anchor) != 2:
        raise EmitError("player_anchor must contain exactly two integers")
    anchor_x = _integer(
        anchor[0], "player_anchor[0]", minimum=-32768, maximum=32767
    )
    anchor_y = _integer(
        anchor[1], "player_anchor[1]", minimum=-32768, maximum=32767
    )

    font = _mapping(profile.get("font"), "font")
    font_cell_width = _integer(
        font.get("cell_width"), "font.cell_width", minimum=1, maximum=255
    )
    font_cell_height = _integer(
        font.get("cell_height"), "font.cell_height", minimum=1, maximum=255
    )
    font_ascent = _integer(
        font.get("ascent"), "font.ascent", minimum=-128, maximum=127
    )
    font_descent = _integer(
        font.get("descent"), "font.descent", minimum=-128, maximum=127
    )
    font_line_height = _integer(
        font.get("line_height"), "font.line_height", minimum=1, maximum=255
    )
    font_glyph_count = _integer(
        font.get("glyph_count", 0),
        "font.glyph_count",
        minimum=0,
        maximum=0xFFFFFFFF,
    )
    font_image_bytes = _integer(
        font.get("image_bytes", 0),
        "font.image_bytes",
        minimum=0,
        maximum=0xFFFFFFFF,
    )
    font_payload_crc32 = _integer(
        font.get("payload_crc32", 0),
        "font.payload_crc32",
        minimum=0,
        maximum=0xFFFFFFFF,
    )
    if font_image_bytes != 0 and (
        font_glyph_count == 0
        or font_image_bytes != 32 + font_glyph_count * 16
    ):
        raise EmitError(
            "font.image_bytes disagrees with FONT10 glyph_count"
        )
    if font_image_bytes == 0 and font_payload_crc32 != 0:
        raise EmitError(
            "font.payload_crc32 requires a non-empty FONT10 image"
        )

    loading = _mapping(profile.get("loading"), "loading")
    loading_label = _rect(loading.get("label_rect"), "loading.label_rect")
    loading_bar = _rect(loading.get("bar_rect"), "loading.bar_rect")
    loading_glyph_width = _integer(
        loading.get("glyph_width"),
        "loading.glyph_width",
        minimum=1,
        maximum=8,
    )
    loading_glyph_height = _integer(
        loading.get("glyph_height"),
        "loading.glyph_height",
        minimum=1,
        maximum=255,
    )
    loading_glyph_advance = _integer(
        loading.get("glyph_advance"),
        "loading.glyph_advance",
        minimum=1,
        maximum=255,
    )
    loading_glyph_scale = _integer(
        loading.get("glyph_scale"),
        "loading.glyph_scale",
        minimum=1,
        maximum=255,
    )
    loading_glyph_count = _integer(
        loading.get("glyph_count"),
        "loading.glyph_count",
        minimum=1,
        maximum=255,
    )
    loading_bar_border = _integer(
        loading.get("bar_border"),
        "loading.bar_border",
        minimum=1,
        maximum=65535,
    )
    loading_progress_max = _integer(
        loading.get("progress_max"),
        "loading.progress_max",
        minimum=1,
        maximum=255,
    )
    if (
        not _rect_contains(safe, loading_label)
        or not _rect_contains(safe, loading_bar)
        or loading_glyph_advance < loading_glyph_width
        or loading_label[2]
        != loading_glyph_count
        * loading_glyph_advance
        * loading_glyph_scale
        or loading_label[3]
        != loading_glyph_height * loading_glyph_scale
        or loading_bar_border * 2 >= loading_bar[2]
        or loading_bar_border * 2 >= loading_bar[3]
    ):
        raise EmitError("loading geometry is inconsistent or outside safe_rect")

    policies = [
        _mapping(item, f"sampling[{index}]")
        for index, item in enumerate(
            _sequence(profile.get("sampling"), "sampling")
        )
    ]
    policy_names = [
        _name(item.get("name"), f"sampling[{index}].name")
        for index, item in enumerate(policies)
    ]
    if len(set(policy_names)) != len(policy_names):
        raise EmitError("sampling policy names must be unique")
    if len(policies) > 65535:
        raise EmitError("sampling policy count does not fit uint16 id")
    policy_asset_classes: list[str] = []
    for index, policy in enumerate(policies):
        value = policy.get("asset_class", policy.get("role", "none"))
        asset_class = _name(value, f"sampling[{index}].asset_class")
        asset_class = ASSET_CLASS_ALIASES.get(asset_class, asset_class)
        if asset_class not in ASSET_CLASS_IDS:
            asset_class = "none"
        policy_asset_classes.append(asset_class)
    catalog_count = sum(
        1 for policy in policies if bool(policy.get("catalog", False))
    )

    screens = [
        _mapping(item, f"screens[{index}]")
        for index, item in enumerate(
            _sequence(profile.get("screens"), "screens")
        )
    ]
    screen_names = [
        _name(item.get("name"), f"screens[{index}].name")
        for index, item in enumerate(screens)
    ]
    if len(set(screen_names)) != len(screen_names):
        raise EmitError("screen names must be unique")
    if len(screens) > 255:
        raise EmitError("screen count does not fit uint8 screen_id")

    element_rows: list[dict[str, int | str]] = []
    focus_rows: list[int] = []
    screen_rows: list[dict[str, int | str]] = []
    element_kind_names: set[str] = set()
    for screen_index, screen in enumerate(screens):
        variant_name = _name(
            screen.get("variant"), f"screens[{screen_index}].variant"
        )
        if variant_name not in VARIANT_IDS:
            raise EmitError(
                f"screens[{screen_index}].variant is unsupported: "
                f"{variant_name!r}"
            )
        elements = [
            _mapping(
                item,
                f"screens[{screen_index}].elements[{element_index}]",
            )
            for element_index, item in enumerate(
                _sequence(
                    screen.get("elements"),
                    f"screens[{screen_index}].elements",
                )
            )
        ]
        local_names = [
            _name(
                item.get("name"),
                f"screens[{screen_index}].elements[{element_index}].name",
            )
            for element_index, item in enumerate(elements)
        ]
        if len(set(local_names)) != len(local_names):
            raise EmitError(
                f"screen {screen_names[screen_index]!r} has duplicate "
                "element names"
            )
        local_lookup = {
            element_name: index
            for index, element_name in enumerate(local_names)
        }
        element_first = len(element_rows)
        for element_index, element in enumerate(elements):
            kind = _name(
                element.get("kind"),
                (
                    f"screens[{screen_index}].elements"
                    f"[{element_index}].kind"
                ),
            )
            element_kind_names.add(kind)
            flags = 0
            if bool(element.get("visible", True)):
                flags |= ELEMENT_FLAG_VISIBLE
            if bool(element.get("selectable", False)):
                flags |= ELEMENT_FLAG_SELECTABLE
            if bool(element.get("critical", False)):
                flags |= ELEMENT_FLAG_CRITICAL
            element_rows.append(
                {
                    "screen": screen_index,
                    "local_id": element_index,
                    "name": local_names[element_index],
                    "kind": kind,
                    "rect": _rect(
                        element.get("rect"),
                        (
                            f"screens[{screen_index}].elements"
                            f"[{element_index}].rect"
                        ),
                    ),
                    "page": _integer(
                        element.get("page", 0),
                        (
                            f"screens[{screen_index}].elements"
                            f"[{element_index}].page"
                        ),
                        minimum=0,
                        maximum=65535,
                    ),
                    "priority": _integer(
                        element.get("priority", 0),
                        (
                            f"screens[{screen_index}].elements"
                            f"[{element_index}].priority"
                        ),
                        minimum=0,
                        maximum=255,
                    ),
                    "flags": flags,
                    "return_value": _integer(
                        element.get("return_value", -1),
                        (
                            f"screens[{screen_index}].elements"
                            f"[{element_index}].return_value"
                        ),
                        minimum=-32768,
                        maximum=32767,
                    ),
                }
            )

        focus_first = len(focus_rows)
        focus_order = _sequence(
            screen.get("focus_order", ()),
            f"screens[{screen_index}].focus_order",
        )
        for focus_index, element_name_value in enumerate(focus_order):
            element_name = _name(
                element_name_value,
                (
                    f"screens[{screen_index}].focus_order"
                    f"[{focus_index}]"
                ),
            )
            if element_name not in local_lookup:
                raise EmitError(
                    f"screen {screen_names[screen_index]!r} focus element "
                    f"{element_name!r} is absent"
                )
            focus_rows.append(element_first + local_lookup[element_name])

        page_count = _integer(
            screen.get("page_count", 1),
            f"screens[{screen_index}].page_count",
            minimum=1,
            maximum=255,
        )
        screen_rows.append(
            {
                "name": screen_names[screen_index],
                "variant": VARIANT_IDS[variant_name],
                "rows": _integer(
                    screen.get("rows", 1),
                    f"screens[{screen_index}].rows",
                    minimum=1,
                    maximum=255,
                ),
                "columns": _integer(
                    screen.get("columns", 1),
                    f"screens[{screen_index}].columns",
                    minimum=1,
                    maximum=255,
                ),
                "page_count": page_count,
                "page_capacity": _integer(
                    screen.get("page_capacity", len(elements) or 1),
                    f"screens[{screen_index}].page_capacity",
                    minimum=1,
                    maximum=255,
                ),
                "initial_page": _integer(
                    screen.get("initial_page", 0),
                    f"screens[{screen_index}].initial_page",
                    minimum=0,
                    maximum=page_count - 1,
                ),
                "element_first": element_first,
                "element_count": len(elements),
                "focus_first": focus_first,
                "focus_count": len(focus_order),
            }
        )

    if len(element_rows) > 65535:
        raise EmitError("element count does not fit uint16")
    if len(focus_rows) > 65535:
        raise EmitError("focus count does not fit uint16")

    kind_ids = {
        name: index for index, name in enumerate(sorted(element_kind_names))
    }

    camera_vectors = [
        _mapping(item, f"camera_vectors[{index}]")
        for index, item in enumerate(
            _sequence(
                profile.get("camera_vectors", ()), "camera_vectors"
            )
        )
    ]
    if len(camera_vectors) > 65535:
        raise EmitError("camera vector count does not fit uint16")
    camera_kind_ids = {"map": 0, "battle": 1}
    camera_mode_ids = {
        "follow": 0,
        "scripted": 1,
        "idle": 2,
        "actor_target": 3,
        "fit_all": 4,
    }
    generated_abi_lines = [
        f"#define PAL_UI_GENERATED_NO_SCREEN {_u(NO_SCREEN_ID)}",
        (
            "#define PAL_UI_GENERATED_ELEMENT_FLAG_VISIBLE "
            f"{_u(ELEMENT_FLAG_VISIBLE)}"
        ),
        (
            "#define PAL_UI_GENERATED_ELEMENT_FLAG_SELECTABLE "
            f"{_u(ELEMENT_FLAG_SELECTABLE)}"
        ),
        (
            "#define PAL_UI_GENERATED_ELEMENT_FLAG_CRITICAL "
            f"{_u(ELEMENT_FLAG_CRITICAL)}"
        ),
        (
            "#define PAL_UI_GENERATED_SAMPLING_FLAG_VISIBLE "
            f"{_u(SAMPLING_FLAG_VISIBLE)}"
        ),
        (
            "#define PAL_UI_GENERATED_SAMPLING_FLAG_LEGACY_STAGE "
            f"{_u(SAMPLING_FLAG_LEGACY_STAGE)}"
        ),
        (
            "#define PAL_UI_GENERATED_SAMPLING_FLAG_CATALOG "
            f"{_u(SAMPLING_FLAG_CATALOG)}"
        ),
        (
            "#define PAL_UI_GENERATED_CAMERA_FLAG_CLAMPED "
            f"{_u(CAMERA_FLAG_CLAMPED)}"
        ),
    ]
    generated_abi_lines.extend(
        (
            "#define PAL_UI_GENERATED_FILTER_"
            f"{_identifier(name)} {_u(value)}"
        )
        for name, value in sorted(
            FILTER_IDS.items(), key=lambda item: item[1]
        )
    )
    generated_abi_lines.extend(
        (
            "#define PAL_UI_GENERATED_ASSET_CLASS_"
            f"{_identifier(name)} {_u(value)}"
        )
        for name, value in sorted(
            ASSET_CLASS_IDS.items(), key=lambda item: item[1]
        )
    )
    generated_abi_lines.append(
        "#define PAL_UI_GENERATED_ASSET_CLASS_COUNT "
        f"{_u(len(ASSET_CLASS_IDS))}"
    )

    digest = _manifest_digest(profile)
    lines: list[str] = [
        "/* Generated by tools/pal_ui_layout; do not edit. */",
        f"/* profile={profile_name} manifest_sha256={digest} */",
        f"#ifndef {_guard(profile_name)}",
        f"#define {_guard(profile_name)}",
        "",
        "#include <stdint.h>",
        "",
        "#if defined(__GNUC__)",
        "#define PAL_UI_GENERATED_UNUSED __attribute__((unused))",
        "#else",
        "#define PAL_UI_GENERATED_UNUSED",
        "#endif",
        "",
        f"#define PAL_UI_LAYOUT_SCHEMA_VERSION {_u(schema_version)}",
        "#define PAL_UI_GENERATED_COEFFICIENTS_ONLY 1u",
        (
            "#define PAL_UI_GENERATED_PUBLIC_ABI_VERSION "
            f"{_u(_integer(profile.get('public_abi_version', 1), 'public_abi_version', minimum=1, maximum=1))}"
        ),
        (
            "#define PAL_UI_GENERATED_CAMERA_KIND_MAP "
            f"{_u(camera_kind_ids['map'])}"
        ),
        (
            "#define PAL_UI_GENERATED_CAMERA_KIND_BATTLE "
            f"{_u(camera_kind_ids['battle'])}"
        ),
        (
            "#define PAL_UI_GENERATED_CAMERA_MODE_FOLLOW "
            f"{_u(camera_mode_ids['follow'])}"
        ),
        (
            "#define PAL_UI_GENERATED_CAMERA_MODE_SCRIPTED "
            f"{_u(camera_mode_ids['scripted'])}"
        ),
        (
            "#define PAL_UI_GENERATED_CAMERA_MODE_IDLE "
            f"{_u(camera_mode_ids['idle'])}"
        ),
        (
            "#define PAL_UI_GENERATED_CAMERA_MODE_ACTOR_TARGET "
            f"{_u(camera_mode_ids['actor_target'])}"
        ),
        (
            "#define PAL_UI_GENERATED_CAMERA_MODE_FIT_ALL "
            f"{_u(camera_mode_ids['fit_all'])}"
        ),
        *generated_abi_lines,
        f"#define PAL_UI_GENERATED_DISPLAY_WIDTH {_u(display_width)}",
        f"#define PAL_UI_GENERATED_DISPLAY_HEIGHT {_u(display_height)}",
        f"#define PAL_UI_GENERATED_SAFE_X {safe[0]}",
        f"#define PAL_UI_GENERATED_SAFE_Y {safe[1]}",
        f"#define PAL_UI_GENERATED_SAFE_WIDTH {_u(safe[2])}",
        f"#define PAL_UI_GENERATED_SAFE_HEIGHT {_u(safe[3])}",
        f"#define PAL_UI_GENERATED_STAGE_X {stage[0]}",
        f"#define PAL_UI_GENERATED_STAGE_Y {stage[1]}",
        f"#define PAL_UI_GENERATED_STAGE_WIDTH {_u(stage[2])}",
        f"#define PAL_UI_GENERATED_STAGE_HEIGHT {_u(stage[3])}",
        (
            "#define PAL_UI_GENERATED_STAGE_SOURCE_WIDTH "
            f"{_u(stage_source_width)}"
        ),
        (
            "#define PAL_UI_GENERATED_STAGE_SOURCE_HEIGHT "
            f"{_u(stage_source_height)}"
        ),
        (
            "#define PAL_UI_GENERATED_STAGE_DESTINATION_X "
            f"{stage[0]}"
        ),
        (
            "#define PAL_UI_GENERATED_STAGE_DESTINATION_Y "
            f"{stage[1]}"
        ),
        (
            "#define PAL_UI_GENERATED_STAGE_DESTINATION_WIDTH "
            f"{_u(stage[2])}"
        ),
        (
            "#define PAL_UI_GENERATED_STAGE_DESTINATION_HEIGHT "
            f"{_u(stage[3])}"
        ),
        (
            "#define PAL_UI_GENERATED_STAGE_SCALE_NUMERATOR "
            f"{_u(stage_scale_numerator)}"
        ),
        (
            "#define PAL_UI_GENERATED_STAGE_SCALE_DENOMINATOR "
            f"{_u(stage_scale_denominator)}"
        ),
        (
            "#define PAL_UI_GENERATED_STAGE_SCALE_Q16 "
            f"{_u(stage_scale_q16)}"
        ),
        (
            "#define PAL_UI_GENERATED_STAGE_STEP_X_Q16 "
            f"{_u(stage_step_x_q16)}"
        ),
        (
            "#define PAL_UI_GENERATED_STAGE_STEP_Y_Q16 "
            f"{_u(stage_step_y_q16)}"
        ),
        (
            "#define PAL_UI_GENERATED_STAGE_PHASE_X_Q16 "
            f"{_u(stage_phase_x_q16)}"
        ),
        (
            "#define PAL_UI_GENERATED_STAGE_PHASE_Y_Q16 "
            f"{_u(stage_phase_y_q16)}"
        ),
        (
            "#define PAL_UI_GENERATED_STAGE_SAMPLE_X_COUNT "
            f"{_u(len(stage_sample_x))}"
        ),
        (
            "#define PAL_UI_GENERATED_STAGE_SAMPLE_Y_COUNT "
            f"{_u(len(stage_sample_y))}"
        ),
        (
            "#define PAL_UI_GENERATED_BATTLE_FIT_SCALE_NUMERATOR "
            f"{_u(battle_fit_numerator)}"
        ),
        (
            "#define PAL_UI_GENERATED_BATTLE_FIT_SCREEN_X "
            f"{battle_fit_screen[0]}"
        ),
        (
            "#define PAL_UI_GENERATED_BATTLE_FIT_SCREEN_Y "
            f"{battle_fit_screen[1]}"
        ),
        (
            "#define PAL_UI_GENERATED_BATTLE_FIT_SCREEN_WIDTH "
            f"{_u(battle_fit_screen[2])}"
        ),
        (
            "#define PAL_UI_GENERATED_BATTLE_FIT_SCREEN_HEIGHT "
            f"{_u(battle_fit_screen[3])}"
        ),
        (
            "#define PAL_UI_GENERATED_BATTLE_FIT_SCALE_DENOMINATOR "
            f"{_u(battle_fit_denominator)}"
        ),
        (
            "#define PAL_UI_GENERATED_BATTLE_FIT_SCALE_Q16 "
            f"{_u(battle_fit_scale_q16)}"
        ),
        (
            "#define PAL_UI_GENERATED_BATTLE_FIT_STEP_Q16 "
            f"{_u(battle_fit_step_q16)}"
        ),
        (
            "#define PAL_UI_GENERATED_BATTLE_FIT_PHASE_Q16 "
            f"{_u(battle_fit_phase_q16)}"
        ),
        (
            "#define PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_X_COUNT "
            f"{_u(len(battle_fit_sample_x))}"
        ),
        (
            "#define PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_Y_COUNT "
            f"{_u(len(battle_fit_sample_y))}"
        ),
        f"#define PAL_UI_GENERATED_PLAYER_ANCHOR_X {anchor_x}",
        f"#define PAL_UI_GENERATED_PLAYER_ANCHOR_Y {anchor_y}",
        f"#define PAL_UI_GENERATED_FONT_CELL_WIDTH {_u(font_cell_width)}",
        f"#define PAL_UI_GENERATED_FONT_CELL_HEIGHT {_u(font_cell_height)}",
        f"#define PAL_UI_GENERATED_FONT_ASCENT {font_ascent}",
        f"#define PAL_UI_GENERATED_FONT_DESCENT {font_descent}",
        f"#define PAL_UI_GENERATED_FONT_LINE_HEIGHT {_u(font_line_height)}",
        (
            "#define PAL_UI_GENERATED_FONT_GLYPH_COUNT "
            f"{_u(font_glyph_count)}"
        ),
        (
            "#define PAL_UI_GENERATED_FONT_IMAGE_BYTES "
            f"{_u(font_image_bytes)}"
        ),
        (
            "#define PAL_UI_GENERATED_FONT_PAYLOAD_CRC32 "
            f"0x{font_payload_crc32:08x}u"
        ),
        f"#define PAL_UI_GENERATED_LOADING_LABEL_X {loading_label[0]}",
        f"#define PAL_UI_GENERATED_LOADING_LABEL_Y {loading_label[1]}",
        (
            "#define PAL_UI_GENERATED_LOADING_LABEL_WIDTH "
            f"{_u(loading_label[2])}"
        ),
        (
            "#define PAL_UI_GENERATED_LOADING_LABEL_HEIGHT "
            f"{_u(loading_label[3])}"
        ),
        (
            "#define PAL_UI_GENERATED_LOADING_GLYPH_WIDTH "
            f"{_u(loading_glyph_width)}"
        ),
        (
            "#define PAL_UI_GENERATED_LOADING_GLYPH_HEIGHT "
            f"{_u(loading_glyph_height)}"
        ),
        (
            "#define PAL_UI_GENERATED_LOADING_GLYPH_ADVANCE "
            f"{_u(loading_glyph_advance)}"
        ),
        (
            "#define PAL_UI_GENERATED_LOADING_GLYPH_SCALE "
            f"{_u(loading_glyph_scale)}"
        ),
        (
            "#define PAL_UI_GENERATED_LOADING_GLYPH_COUNT "
            f"{_u(loading_glyph_count)}"
        ),
        f"#define PAL_UI_GENERATED_LOADING_BAR_X {loading_bar[0]}",
        f"#define PAL_UI_GENERATED_LOADING_BAR_Y {loading_bar[1]}",
        (
            "#define PAL_UI_GENERATED_LOADING_BAR_WIDTH "
            f"{_u(loading_bar[2])}"
        ),
        (
            "#define PAL_UI_GENERATED_LOADING_BAR_HEIGHT "
            f"{_u(loading_bar[3])}"
        ),
        (
            "#define PAL_UI_GENERATED_LOADING_BAR_BORDER "
            f"{_u(loading_bar_border)}"
        ),
        (
            "#define PAL_UI_GENERATED_LOADING_PROGRESS_MAX "
            f"{_u(loading_progress_max)}"
        ),
        (
            "#define PAL_UI_GENERATED_FIXED_Q_SHIFT "
            f"{_u(FIXED_Q_SHIFT)}"
        ),
        "",
        "typedef struct PalUiGeneratedRect {",
        "    int16_t x;",
        "    int16_t y;",
        "    uint16_t width;",
        "    uint16_t height;",
        "} PalUiGeneratedRect;",
        "",
        "typedef struct PalUiGeneratedBattleCameraPolicy {",
        "    PalUiGeneratedRect arena;",
        "    PalUiGeneratedRect hud_rect;",
        "    PalUiGeneratedRect content_rect;",
        "    PalUiGeneratedRect focus_screen;",
        "    PalUiGeneratedRect fit_screen;",
        "    uint16_t focus_source_width;",
        "    uint16_t focus_source_height;",
        "    uint16_t fit_numerator;",
        "    uint16_t fit_denominator;",
        "    uint32_t fit_scale_q16;",
        "    uint32_t fit_step_q16;",
        "    uint32_t fit_phase_q16;",
        "    uint8_t padding;",
        "    uint8_t max_players;",
        "} PalUiGeneratedBattleCameraPolicy;",
        "",
        "typedef struct PalUiGeneratedSampling {",
        "    uint16_t role;",
        "    uint8_t asset_class;",
        "    uint8_t filter;",
        "    uint8_t flags;",
        "    uint8_t screen_id;",
        "    uint16_t page;",
        "    uint16_t numerator;",
        "    uint16_t denominator;",
        "    uint16_t source_width;",
        "    uint16_t source_height;",
        "    uint16_t destination_width;",
        "    uint16_t destination_height;",
        "    uint32_t scale_q16;",
        "    uint32_t step_x_q16;",
        "    uint32_t step_y_q16;",
        "    uint32_t phase_x_q16;",
        "    uint32_t phase_y_q16;",
        "    uint16_t min_width;",
        "    uint16_t min_height;",
        "    uint16_t max_width;",
        "    uint16_t max_height;",
        "    PalUiGeneratedRect destination;",
        "} PalUiGeneratedSampling;",
        "",
        "typedef struct PalUiGeneratedElement {",
        "    PalUiGeneratedRect rect;",
        "    int16_t return_value;",
        "    uint16_t page;",
        "    uint16_t local_id;",
        "    uint8_t screen_id;",
        "    uint8_t kind;",
        "    uint8_t priority;",
        "    uint8_t flags;",
        "} PalUiGeneratedElement;",
        "",
        "typedef struct PalUiGeneratedScreen {",
        "    uint16_t element_first;",
        "    uint16_t element_count;",
        "    uint16_t focus_first;",
        "    uint16_t focus_count;",
        "    uint8_t variant;",
        "    uint8_t rows;",
        "    uint8_t columns;",
        "    uint8_t page_count;",
        "    uint8_t page_capacity;",
        "    uint8_t initial_page;",
        "} PalUiGeneratedScreen;",
        "",
        "typedef struct PalUiGeneratedCameraVector {",
        "    PalUiGeneratedRect bounds;",
        "    PalUiGeneratedRect screen;",
        "    int16_t focus_x;",
        "    int16_t focus_y;",
        "    int16_t camera_x;",
        "    int16_t camera_y;",
        "    int16_t desired_x;",
        "    int16_t desired_y;",
        "    int16_t projected_focus_x;",
        "    int16_t projected_focus_y;",
        "    uint16_t scale_numerator;",
        "    uint16_t scale_denominator;",
        "    uint32_t scale_q16;",
        "    uint32_t source_step_q16;",
        "    uint32_t source_phase_q16;",
        "    uint8_t kind;",
        "    uint8_t mode;",
        "    uint8_t flags;",
        "} PalUiGeneratedCameraVector;",
        "",
        "enum PalUiGeneratedVariant {",
    ]
    for variant, variant_id in VARIANT_IDS.items():
        lines.append(
            f"    PAL_UI_VARIANT_{_identifier(variant)} = {variant_id},"
        )
    lines += ["};", "", "enum PalUiGeneratedSamplingRole {"]
    for index, policy_name in enumerate(policy_names):
        if bool(policies[index].get("catalog", False)):
            continue
        lines.append(
            "    "
            f"PAL_UI_SAMPLE_{_identifier(policy_name)} = {index},"
        )
    lines += [
        f"    PAL_UI_SAMPLE_COUNT = {len(policy_names)}",
        "};",
        "",
        "enum PalUiGeneratedAssetClass {",
    ]
    for asset_class, asset_class_id in ASSET_CLASS_IDS.items():
        lines.append(
            "    "
            f"PAL_UI_ASSET_CLASS_{_identifier(asset_class)} = "
            f"{asset_class_id},"
        )
    lines += [
        f"    PAL_UI_ASSET_CLASS_COUNT = {len(ASSET_CLASS_IDS)}",
        "};",
        "",
        "enum PalUiGeneratedScreenId {",
    ]
    for index, screen_name in enumerate(screen_names):
        lines.append(
            f"    PAL_UI_SCREEN_{_identifier(screen_name)} = {index},"
        )
    lines += [
        f"    PAL_UI_SCREEN_COUNT = {len(screen_names)}",
        "};",
        "",
        "enum PalUiGeneratedElementKind {",
    ]
    for kind_name, kind_id in kind_ids.items():
        lines.append(
            f"    PAL_UI_ELEMENT_{_identifier(kind_name)} = {kind_id},"
        )
    lines += [
        f"    PAL_UI_ELEMENT_KIND_COUNT = {len(kind_ids)}",
        "};",
        "",
        (
            "static const PalUiGeneratedRect pal_ui_generated_safe_rect "
            "PAL_UI_GENERATED_UNUSED = "
            f"{_rect_initializer(safe)};"
        ),
        (
            "static const PalUiGeneratedRect pal_ui_generated_stage_rect "
            "PAL_UI_GENERATED_UNUSED = "
            f"{_rect_initializer(stage)};"
        ),
        (
            "static const PalUiGeneratedBattleCameraPolicy "
            "pal_ui_generated_battle_camera_policy "
            "PAL_UI_GENERATED_UNUSED = { "
            f"{_rect_initializer(battle_arena)}, "
            f"{_rect_initializer(battle_hud)}, "
            f"{_rect_initializer(battle_content)}, "
            f"{_rect_initializer(battle_focus_screen)}, "
            f"{_rect_initializer(battle_fit_screen)}, "
            f"{_u(battle_focus_source[0])}, "
            f"{_u(battle_focus_source[1])}, "
            f"{_u(battle_fit_numerator)}, "
            f"{_u(battle_fit_denominator)}, "
            f"{_u(battle_fit_scale_q16)}, "
            f"{_u(battle_fit_step_q16)}, "
            f"{_u(battle_fit_phase_q16)}, "
            f"{_u(battle_padding)}, {_u(battle_max_players)}"
            " };"
        ),
        "",
    ]
    _append_u16_array(
        lines,
        (
            "static const uint16_t pal_ui_generated_stage_sample_x[] "
            "PAL_UI_GENERATED_UNUSED = {"
        ),
        stage_sample_x,
    )
    lines.append("")
    _append_u16_array(
        lines,
        (
            "static const uint16_t pal_ui_generated_stage_sample_y[] "
            "PAL_UI_GENERATED_UNUSED = {"
        ),
        stage_sample_y,
    )
    lines.append("")
    _append_u16_array(
        lines,
        (
            "static const uint16_t "
            "pal_ui_generated_battle_fit_sample_x[] "
            "PAL_UI_GENERATED_UNUSED = {"
        ),
        battle_fit_sample_x,
    )
    lines.append("")
    _append_u16_array(
        lines,
        (
            "static const uint16_t "
            "pal_ui_generated_battle_fit_sample_y[] "
            "PAL_UI_GENERATED_UNUSED = {"
        ),
        battle_fit_sample_y,
    )
    lines += [
        "",
        "static const PalUiGeneratedSampling pal_ui_generated_sampling[] "
        "PAL_UI_GENERATED_UNUSED = {",
    ]
    for index, policy in enumerate(policies):
        visible = bool(policy.get("visible", True))
        numerator = _integer(
            policy.get("numerator", 1 if visible else 0),
            f"sampling[{index}].numerator",
            minimum=0 if not visible else 1,
            maximum=65535,
        )
        denominator = _integer(
            policy.get("denominator", 1),
            f"sampling[{index}].denominator",
            minimum=1,
            maximum=65535,
        )
        filter_name = _name(
            policy.get(
                "filter", "nearest_center" if visible else "none"
            ),
            f"sampling[{index}].filter",
        )
        if filter_name not in FILTER_IDS:
            raise EmitError(
                f"sampling[{index}].filter is unsupported: "
                f"{filter_name!r}"
            )
        min_width = _integer(
            policy.get("min_width", 1),
            f"sampling[{index}].min_width",
            minimum=0,
            maximum=65535,
        )
        min_height = _integer(
            policy.get("min_height", 1),
            f"sampling[{index}].min_height",
            minimum=0,
            maximum=65535,
        )
        max_width = _integer(
            policy.get("max_width", 65535),
            f"sampling[{index}].max_width",
            minimum=min_width,
            maximum=65535,
        )
        max_height = _integer(
            policy.get("max_height", 65535),
            f"sampling[{index}].max_height",
            minimum=min_height,
            maximum=65535,
        )
        source_size = _sequence(
            policy.get("source", (0, 0)),
            f"sampling[{index}].source",
        )
        destination_size = _sequence(
            policy.get("destination_size", (0, 0)),
            f"sampling[{index}].destination_size",
        )
        step_q16 = _sequence(
            policy.get("step_q16", (0, 0)),
            f"sampling[{index}].step_q16",
        )
        phase_q16 = _sequence(
            policy.get("phase_q16", (0, 0)),
            f"sampling[{index}].phase_q16",
        )
        for field_name, field_value in (
            ("source", source_size),
            ("destination_size", destination_size),
            ("step_q16", step_q16),
            ("phase_q16", phase_q16),
        ):
            if len(field_value) != 2:
                raise EmitError(
                    f"sampling[{index}].{field_name} must be a pair"
                )
        source_width = _integer(
            source_size[0],
            f"sampling[{index}].source[0]",
            minimum=0,
            maximum=65535,
        )
        source_height = _integer(
            source_size[1],
            f"sampling[{index}].source[1]",
            minimum=0,
            maximum=65535,
        )
        destination_width = _integer(
            destination_size[0],
            f"sampling[{index}].destination_size[0]",
            minimum=0,
            maximum=65535,
        )
        destination_height = _integer(
            destination_size[1],
            f"sampling[{index}].destination_size[1]",
            minimum=0,
            maximum=65535,
        )
        scale_q16 = _integer(
            policy.get(
                "scale_q16",
                (numerator << FIXED_Q_SHIFT) // denominator
                if numerator
                else 0,
            ),
            f"sampling[{index}].scale_q16",
            minimum=0,
            maximum=0xFFFFFFFF,
        )
        default_step_x = (
            (source_width << FIXED_Q_SHIFT) // destination_width
            if destination_width
            else 0
        )
        default_step_y = (
            (source_height << FIXED_Q_SHIFT) // destination_height
            if destination_height
            else 0
        )
        step_x = _integer(
            step_q16[0] or default_step_x,
            f"sampling[{index}].step_q16[0]",
            minimum=0,
            maximum=0xFFFFFFFF,
        )
        step_y = _integer(
            step_q16[1] or default_step_y,
            f"sampling[{index}].step_q16[1]",
            minimum=0,
            maximum=0xFFFFFFFF,
        )
        phase_x = _integer(
            phase_q16[0] or step_x // 2,
            f"sampling[{index}].phase_q16[0]",
            minimum=0,
            maximum=0xFFFFFFFF,
        )
        phase_y = _integer(
            phase_q16[1] or step_y // 2,
            f"sampling[{index}].phase_q16[1]",
            minimum=0,
            maximum=0xFFFFFFFF,
        )
        screen_name = policy.get("screen")
        if screen_name is None:
            screen_id = NO_SCREEN_ID
        else:
            screen_text = _name(
                screen_name, f"sampling[{index}].screen"
            )
            if screen_text not in screen_names:
                raise EmitError(
                    f"sampling[{index}].screen {screen_text!r} is absent"
                )
            screen_id = screen_names.index(screen_text)
        page = _integer(
            policy.get("page", 0) if visible else 0,
            f"sampling[{index}].page",
            minimum=0,
            maximum=65535,
        )
        destination_value = policy.get("destination")
        destination_rect = (
            _rect(
                destination_value,
                f"sampling[{index}].destination",
            )
            if destination_value is not None
            else (0, 0, 0, 0)
        )
        flags = SAMPLING_FLAG_VISIBLE if visible else 0
        if str(policy.get("space", "")) == "legacy_stage":
            flags |= SAMPLING_FLAG_LEGACY_STAGE
        if bool(policy.get("catalog", False)):
            flags |= SAMPLING_FLAG_CATALOG
        asset_class_id = ASSET_CLASS_IDS[
            policy_asset_classes[index]
        ]
        lines += [
            f"    /* {policy_names[index]} */",
            "    { "
            f"{_u(index)}, {_u(asset_class_id)}, "
            f"{_u(FILTER_IDS[filter_name])}, "
            f"{_u(flags)}, {_u(screen_id)}, {_u(page)}, "
            f"{_u(numerator)}, {_u(denominator)}, "
            f"{_u(source_width)}, {_u(source_height)}, "
            f"{_u(destination_width)}, {_u(destination_height)}, "
            f"{_u(scale_q16)}, {_u(step_x)}, {_u(step_y)}, "
            f"{_u(phase_x)}, {_u(phase_y)}, "
            f"{_u(min_width)}, {_u(min_height)}, "
            f"{_u(max_width)}, {_u(max_height)}, "
            f"{_rect_initializer(destination_rect)}"
            " },",
        ]
    lines += [
        "};",
        "",
        "static const PalUiGeneratedElement pal_ui_generated_elements[] "
        "PAL_UI_GENERATED_UNUSED = {",
    ]
    for row in element_rows:
        rect = row["rect"]
        assert isinstance(rect, tuple)
        kind = row["kind"]
        assert isinstance(kind, str)
        lines += [
            f"    /* {row['screen']}:{row['name']} */",
            "    { "
            f"{_rect_initializer(rect)}, {row['return_value']}, "
            f"{_u(int(row['page']))}, {_u(int(row['local_id']))}, "
            f"{_u(int(row['screen']))}, {_u(kind_ids[kind])}, "
            f"{_u(int(row['priority']))}, {_u(int(row['flags']))}"
            " },",
        ]
    lines += [
        "};",
        "",
        "static const uint16_t pal_ui_generated_focus_order[] "
        "PAL_UI_GENERATED_UNUSED = {",
    ]
    if focus_rows:
        for value in focus_rows:
            lines.append(f"    {_u(value)},")
    else:
        # Strict C99 rejects an empty initializer on some toolchains.
        lines.append("    0u,")
    lines += [
        "};",
        "",
        "static const PalUiGeneratedScreen pal_ui_generated_screens[] "
        "PAL_UI_GENERATED_UNUSED = {",
    ]
    for row in screen_rows:
        lines += [
            f"    /* {row['name']} */",
            "    { "
            f"{_u(int(row['element_first']))}, "
            f"{_u(int(row['element_count']))}, "
            f"{_u(int(row['focus_first']))}, "
            f"{_u(int(row['focus_count']))}, "
            f"{_u(int(row['variant']))}, {_u(int(row['rows']))}, "
            f"{_u(int(row['columns']))}, "
            f"{_u(int(row['page_count']))}, "
            f"{_u(int(row['page_capacity']))}, "
            f"{_u(int(row['initial_page']))}"
            " },",
        ]
    lines += [
        "};",
        "",
        "static const PalUiGeneratedCameraVector "
        "pal_ui_generated_camera_vectors[] PAL_UI_GENERATED_UNUSED = {",
    ]
    if camera_vectors:
        for index, vector in enumerate(camera_vectors):
            kind_name = _name(
                vector.get("kind"), f"camera_vectors[{index}].kind"
            )
            mode_name = _name(
                vector.get("mode"), f"camera_vectors[{index}].mode"
            )
            if kind_name not in camera_kind_ids:
                raise EmitError(
                    f"camera_vectors[{index}].kind is unsupported"
                )
            if mode_name not in camera_mode_ids:
                raise EmitError(
                    f"camera_vectors[{index}].mode is unsupported"
                )
            bounds = _rect(
                vector.get("bounds"),
                f"camera_vectors[{index}].bounds",
            )
            scale_q16 = _integer(
                vector.get("scale_q16", FIXED_Q_ONE),
                f"camera_vectors[{index}].scale_q16",
                minimum=1,
                maximum=0xFFFFFFFF,
            )
            default_scale_divisor = gcd(scale_q16, FIXED_Q_ONE)
            scale_value = _sequence(
                vector.get(
                    "scale",
                    (
                        scale_q16 // default_scale_divisor,
                        FIXED_Q_ONE // default_scale_divisor,
                    ),
                ),
                f"camera_vectors[{index}].scale",
            )
            if len(scale_value) != 2:
                raise EmitError(
                    f"camera_vectors[{index}].scale must be a pair"
                )
            scale_numerator = _integer(
                scale_value[0],
                f"camera_vectors[{index}].scale[0]",
                minimum=1,
                maximum=65535,
            )
            scale_denominator = _integer(
                scale_value[1],
                f"camera_vectors[{index}].scale[1]",
                minimum=1,
                maximum=65535,
            )
            if (
                (scale_numerator << FIXED_Q_SHIFT) // scale_denominator
                != scale_q16
            ):
                raise EmitError(
                    f"camera_vectors[{index}].scale disagrees with scale_q16"
                )
            default_screen = (
                stage if scale_numerator < scale_denominator else safe
            )
            screen_rect = _rect(
                vector.get("screen", default_screen),
                f"camera_vectors[{index}].screen",
            )
            focus = _sequence(
                vector.get("focus"), f"camera_vectors[{index}].focus"
            )
            origin = _sequence(
                vector.get("camera"), f"camera_vectors[{index}].camera"
            )
            desired = _sequence(
                vector.get("desired", origin),
                f"camera_vectors[{index}].desired",
            )
            if (
                len(focus) != 2
                or len(origin) != 2
                or len(desired) != 2
            ):
                raise EmitError(
                    f"camera_vectors[{index}] focus/camera/desired "
                    "must be pairs"
                )
            focus_x = _integer(
                focus[0],
                f"camera_vectors[{index}].focus[0]",
                minimum=-32768,
                maximum=32767,
            )
            focus_y = _integer(
                focus[1],
                f"camera_vectors[{index}].focus[1]",
                minimum=-32768,
                maximum=32767,
            )
            camera_x = _integer(
                origin[0],
                f"camera_vectors[{index}].camera[0]",
                minimum=-32768,
                maximum=32767,
            )
            camera_y = _integer(
                origin[1],
                f"camera_vectors[{index}].camera[1]",
                minimum=-32768,
                maximum=32767,
            )
            desired_x = _integer(
                desired[0],
                f"camera_vectors[{index}].desired[0]",
                minimum=-32768,
                maximum=32767,
            )
            desired_y = _integer(
                desired[1],
                f"camera_vectors[{index}].desired[1]",
                minimum=-32768,
                maximum=32767,
            )
            default_projected = (
                screen_rect[0]
                + (focus_x - camera_x)
                * scale_numerator
                // scale_denominator,
                screen_rect[1]
                + (focus_y - camera_y)
                * scale_numerator
                // scale_denominator,
            )
            projected = _sequence(
                vector.get("projected_focus", default_projected),
                f"camera_vectors[{index}].projected_focus",
            )
            if len(projected) != 2:
                raise EmitError(
                    f"camera_vectors[{index}].projected_focus must be a pair"
                )
            projected_x = _integer(
                projected[0],
                f"camera_vectors[{index}].projected_focus[0]",
                minimum=-32768,
                maximum=32767,
            )
            projected_y = _integer(
                projected[1],
                f"camera_vectors[{index}].projected_focus[1]",
                minimum=-32768,
                maximum=32767,
            )
            source_step_q16 = _integer(
                vector.get(
                    "source_step_q16",
                    (
                        scale_denominator << FIXED_Q_SHIFT
                    ) // scale_numerator,
                ),
                f"camera_vectors[{index}].source_step_q16",
                minimum=1,
                maximum=0xFFFFFFFF,
            )
            source_phase_q16 = _integer(
                vector.get(
                    "source_phase_q16",
                    source_step_q16 // 2,
                ),
                f"camera_vectors[{index}].source_phase_q16",
                minimum=0,
                maximum=0xFFFFFFFF,
            )
            expected_step_q16 = (
                (scale_denominator << FIXED_Q_SHIFT) // scale_numerator
            )
            if (
                source_step_q16 != expected_step_q16
                or source_phase_q16 != expected_step_q16 // 2
            ):
                raise EmitError(
                    f"camera_vectors[{index}] source step/phase "
                    "disagree with scale"
                )
            if (
                projected_x != default_projected[0]
                or projected_y != default_projected[1]
            ):
                raise EmitError(
                    f"camera_vectors[{index}].projected_focus "
                    "disagrees with generated coefficients"
                )
            if "scale" in vector and (
                screen_rect[2]
                != bounds[2] * scale_numerator // scale_denominator
                or screen_rect[3]
                != bounds[3] * scale_numerator // scale_denominator
            ):
                raise EmitError(
                    f"camera_vectors[{index}].screen extent "
                    "disagrees with scale"
                )
            flags = (
                CAMERA_FLAG_CLAMPED
                if bool(vector.get("clamped", False))
                else 0
            )
            lines += [
                f"    /* {index}:{kind_name}:{mode_name} */",
                "    { "
                f"{_rect_initializer(bounds)}, "
                f"{_rect_initializer(screen_rect)}, "
                f"{focus_x}, {focus_y}, {camera_x}, {camera_y}, "
                f"{desired_x}, {desired_y}, "
                f"{projected_x}, {projected_y}, "
                f"{_u(scale_numerator)}, {_u(scale_denominator)}, "
                f"{_u(scale_q16)}, {_u(source_step_q16)}, "
                f"{_u(source_phase_q16)}, "
                f"{_u(camera_kind_ids[kind_name])}, "
                f"{_u(camera_mode_ids[mode_name])}, {_u(flags)}"
                " },",
            ]
    else:
        lines.append(
            "    { { 0, 0, 0u, 0u }, { 0, 0, 0u, 0u }, "
            f"0, 0, 0, 0, 0, 0, 0, 0, 1u, 1u, {FIXED_Q_ONE}u, "
            f"{FIXED_Q_ONE}u, {FIXED_Q_ONE // 2}u, 0u, 0u, 0u }},"
        )
    lines += [
        "};",
        "",
        f"#define PAL_UI_GENERATED_ELEMENT_COUNT {_u(len(element_rows))}",
        f"#define PAL_UI_GENERATED_FOCUS_COUNT {_u(len(focus_rows))}",
        (
            "#define PAL_UI_GENERATED_CAMERA_VECTOR_COUNT "
            f"{_u(len(camera_vectors))}"
        ),
        (
            "#define PAL_UI_GENERATED_SAMPLING_CATALOG_COUNT "
            f"{_u(catalog_count)}"
        ),
        "",
        "#undef PAL_UI_GENERATED_UNUSED",
        "",
        f"#endif /* {_guard(profile_name)} */",
        "",
    ]
    return "\n".join(lines)


def write_profile_header(
    profile: Mapping[str, Any], output: Path
) -> None:
    """Write one header, avoiding a timestamp so repeated output is identical."""

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(emit_profile_header(profile), encoding="utf-8")
