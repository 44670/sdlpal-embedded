#!/usr/bin/env python3
"""Audit the target boundary for host-generated UI coefficients.

The layout compiler is allowed to solve geometry and sampling policy.  Target
code is only allowed to copy the generated records and execute their integer
transforms.  This audit deliberately checks that boundary in source form; the
embedded Makefile separately compiles the same headers and budgets their ELF
symbols.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path
from typing import Iterable


GENERATED_HEADERS = (
    "esp32s3/main/generated/pal_ui_layout_240x135.h",
    "esp32s3/main/generated/pal_ui_layout_160x128.h",
)

REQUIRED_LOADING_MACROS = (
    "PAL_UI_GENERATED_LOADING_LABEL_X",
    "PAL_UI_GENERATED_LOADING_LABEL_Y",
    "PAL_UI_GENERATED_LOADING_LABEL_WIDTH",
    "PAL_UI_GENERATED_LOADING_LABEL_HEIGHT",
    "PAL_UI_GENERATED_LOADING_GLYPH_WIDTH",
    "PAL_UI_GENERATED_LOADING_GLYPH_HEIGHT",
    "PAL_UI_GENERATED_LOADING_GLYPH_ADVANCE",
    "PAL_UI_GENERATED_LOADING_GLYPH_SCALE",
    "PAL_UI_GENERATED_LOADING_GLYPH_COUNT",
    "PAL_UI_GENERATED_LOADING_BAR_X",
    "PAL_UI_GENERATED_LOADING_BAR_Y",
    "PAL_UI_GENERATED_LOADING_BAR_WIDTH",
    "PAL_UI_GENERATED_LOADING_BAR_HEIGHT",
    "PAL_UI_GENERATED_LOADING_BAR_BORDER",
    "PAL_UI_GENERATED_LOADING_PROGRESS_MAX",
)

REQUIRED_GENERATED_MACROS = (
    "PAL_UI_GENERATED_COEFFICIENTS_ONLY",
    "PAL_UI_GENERATED_PUBLIC_ABI_VERSION",
    "PAL_UI_GENERATED_CAMERA_KIND_MAP",
    "PAL_UI_GENERATED_CAMERA_KIND_BATTLE",
    "PAL_UI_GENERATED_CAMERA_MODE_FOLLOW",
    "PAL_UI_GENERATED_CAMERA_MODE_SCRIPTED",
    "PAL_UI_GENERATED_CAMERA_MODE_IDLE",
    "PAL_UI_GENERATED_CAMERA_MODE_ACTOR_TARGET",
    "PAL_UI_GENERATED_CAMERA_MODE_FIT_ALL",
    "PAL_UI_GENERATED_NO_SCREEN",
    "PAL_UI_GENERATED_ELEMENT_FLAG_VISIBLE",
    "PAL_UI_GENERATED_ELEMENT_FLAG_SELECTABLE",
    "PAL_UI_GENERATED_ELEMENT_FLAG_CRITICAL",
    "PAL_UI_GENERATED_SAMPLING_FLAG_VISIBLE",
    "PAL_UI_GENERATED_SAMPLING_FLAG_LEGACY_STAGE",
    "PAL_UI_GENERATED_SAMPLING_FLAG_CATALOG",
    "PAL_UI_GENERATED_CAMERA_FLAG_CLAMPED",
    "PAL_UI_GENERATED_FILTER_NEAREST_CENTER",
    "PAL_UI_GENERATED_FILTER_BOX_2X2",
    "PAL_UI_GENERATED_FILTER_NONE",
    "PAL_UI_GENERATED_ASSET_CLASS_NONE",
    "PAL_UI_GENERATED_ASSET_CLASS_LEGACY_STAGE",
    "PAL_UI_GENERATED_ASSET_CLASS_PORTRAIT",
    "PAL_UI_GENERATED_ASSET_CLASS_ITEM_PREVIEW",
    "PAL_UI_GENERATED_ASSET_CLASS_BATTLE_PLAYER",
    "PAL_UI_GENERATED_ASSET_CLASS_BATTLE_ENEMY",
    "PAL_UI_GENERATED_ASSET_CLASS_BATTLE_FIRE",
    "PAL_UI_GENERATED_ASSET_CLASS_UI_SPRITE",
    "PAL_UI_GENERATED_ASSET_CLASS_BATTLE_EFFECT",
    "PAL_UI_GENERATED_ASSET_CLASS_COUNT",
    "PAL_UI_GENERATED_DISPLAY_WIDTH",
    "PAL_UI_GENERATED_DISPLAY_HEIGHT",
    "PAL_UI_GENERATED_SAFE_X",
    "PAL_UI_GENERATED_SAFE_Y",
    "PAL_UI_GENERATED_SAFE_WIDTH",
    "PAL_UI_GENERATED_SAFE_HEIGHT",
    "PAL_UI_GENERATED_STAGE_X",
    "PAL_UI_GENERATED_STAGE_Y",
    "PAL_UI_GENERATED_STAGE_WIDTH",
    "PAL_UI_GENERATED_STAGE_HEIGHT",
    "PAL_UI_GENERATED_STAGE_SOURCE_WIDTH",
    "PAL_UI_GENERATED_STAGE_SOURCE_HEIGHT",
    "PAL_UI_GENERATED_STAGE_DESTINATION_X",
    "PAL_UI_GENERATED_STAGE_DESTINATION_Y",
    "PAL_UI_GENERATED_STAGE_DESTINATION_WIDTH",
    "PAL_UI_GENERATED_STAGE_DESTINATION_HEIGHT",
    "PAL_UI_GENERATED_STAGE_SCALE_NUMERATOR",
    "PAL_UI_GENERATED_STAGE_SCALE_DENOMINATOR",
    "PAL_UI_GENERATED_STAGE_SCALE_Q16",
    "PAL_UI_GENERATED_STAGE_STEP_X_Q16",
    "PAL_UI_GENERATED_STAGE_STEP_Y_Q16",
    "PAL_UI_GENERATED_STAGE_PHASE_X_Q16",
    "PAL_UI_GENERATED_STAGE_PHASE_Y_Q16",
    "PAL_UI_GENERATED_STAGE_SAMPLE_X_COUNT",
    "PAL_UI_GENERATED_STAGE_SAMPLE_Y_COUNT",
    "PAL_UI_GENERATED_BATTLE_FIT_SCREEN_X",
    "PAL_UI_GENERATED_BATTLE_FIT_SCREEN_Y",
    "PAL_UI_GENERATED_BATTLE_FIT_SCREEN_WIDTH",
    "PAL_UI_GENERATED_BATTLE_FIT_SCREEN_HEIGHT",
    "PAL_UI_GENERATED_BATTLE_FIT_SCALE_NUMERATOR",
    "PAL_UI_GENERATED_BATTLE_FIT_SCALE_DENOMINATOR",
    "PAL_UI_GENERATED_BATTLE_FIT_SCALE_Q16",
    "PAL_UI_GENERATED_BATTLE_FIT_STEP_Q16",
    "PAL_UI_GENERATED_BATTLE_FIT_PHASE_Q16",
    "PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_X_COUNT",
    "PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_Y_COUNT",
    "PAL_UI_GENERATED_FIXED_Q_SHIFT",
    "PAL_UI_GENERATED_PLAYER_ANCHOR_X",
    "PAL_UI_GENERATED_PLAYER_ANCHOR_Y",
    "PAL_UI_GENERATED_FONT_CELL_WIDTH",
    "PAL_UI_GENERATED_FONT_CELL_HEIGHT",
    "PAL_UI_GENERATED_FONT_ASCENT",
    "PAL_UI_GENERATED_FONT_DESCENT",
    "PAL_UI_GENERATED_FONT_LINE_HEIGHT",
    "PAL_UI_GENERATED_FONT_GLYPH_COUNT",
    "PAL_UI_GENERATED_FONT_IMAGE_BYTES",
    "PAL_UI_GENERATED_FONT_PAYLOAD_CRC32",
    "PAL_UI_GENERATED_ELEMENT_COUNT",
    "PAL_UI_GENERATED_FOCUS_COUNT",
    "PAL_UI_GENERATED_CAMERA_VECTOR_COUNT",
    "PAL_UI_GENERATED_SAMPLING_CATALOG_COUNT",
) + REQUIRED_LOADING_MACROS

REQUIRED_GENERATED_TABLES = (
    "pal_ui_generated_safe_rect",
    "pal_ui_generated_stage_rect",
    "pal_ui_generated_stage_sample_x",
    "pal_ui_generated_stage_sample_y",
    "pal_ui_generated_battle_camera_policy",
    "pal_ui_generated_battle_fit_sample_x",
    "pal_ui_generated_battle_fit_sample_y",
    "pal_ui_generated_sampling",
    "pal_ui_generated_elements",
    "pal_ui_generated_focus_order",
    "pal_ui_generated_screens",
    "pal_ui_generated_camera_vectors",
)

_MANIFEST_RE = re.compile(
    r"^/\* profile=[^ ]+ manifest_sha256=[0-9a-f]{64} \*/$",
    re.MULTILINE,
)


def _normalise(expression: str) -> str:
    return re.sub(r"\s+", "", expression)


def _struct_fields(source: str, struct_name: str) -> tuple[str, ...]:
    match = re.search(
        rf"typedef\s+struct\s+{re.escape(struct_name)}\s*\{{"
        rf"(?P<body>.*?)\}}\s*{re.escape(struct_name)}\s*;",
        source,
        re.DOTALL,
    )
    if match is None:
        return ()
    return tuple(
        field.group(1)
        for field in re.finditer(
            r"\b(?:u?int(?:8|16|32|64)_t|PalUi[A-Za-z0-9_]+)\s+"
            r"([A-Za-z_][A-Za-z0-9_]*)\s*;",
            match.group("body"),
        )
    )


def _function_body(source: str, function_name: str) -> str | None:
    match = re.search(
        rf"\b{re.escape(function_name)}\s*\([^;]*?\)\s*\{{",
        source,
        re.DOTALL,
    )
    if match is None:
        return None
    opening = source.find("{", match.start())
    depth = 1
    index = opening + 1
    while index < len(source) and depth:
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
        index += 1
    if depth:
        return None
    return source[opening + 1 : index - 1]


def _out_assignments(body: str) -> dict[str, str]:
    return {
        match.group("field"): _normalise(match.group("value"))
        for match in re.finditer(
            r"out->(?P<field>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*"
            r"(?P<value>.*?);",
            body,
            re.DOTALL,
        )
    }


def audit_generated_header(path: Path, source: str) -> list[str]:
    errors: list[str] = []
    shown = path.as_posix()
    if not source.startswith(
        "/* Generated by tools/pal_ui_layout; do not edit. */\n"
    ):
        errors.append(f"{shown}: missing Python-generator banner")
    if _MANIFEST_RE.search(source) is None:
        errors.append(f"{shown}: missing generated manifest SHA-256")
    for macro in REQUIRED_GENERATED_MACROS:
        if re.search(rf"^#define\s+{re.escape(macro)}\b", source, re.MULTILINE) is None:
            errors.append(f"{shown}: missing generated macro {macro}")
    if re.search(
        r"^#define\s+PAL_UI_GENERATED_COEFFICIENTS_ONLY\s+1u$",
        source,
        re.MULTILINE,
    ) is None:
        errors.append(
            f"{shown}: generated-only coefficient marker must equal 1u"
        )
    if re.search(
        r"^#define\s+PAL_UI_GENERATED_PUBLIC_ABI_VERSION\s+1u$",
        source,
        re.MULTILINE,
    ) is None:
        errors.append(f"{shown}: unsupported public layout ABI")
    for table in REQUIRED_GENERATED_TABLES:
        declaration = re.search(
            rf"static\s+const\s+[^;=]+\b{re.escape(table)}(?:\[\])?\s+",
            source,
        )
        if declaration is None:
            errors.append(f"{shown}: {table} is absent or not static const")
    if re.search(r"\bstatic\s+(?!const\b)[^;\n]*\bpal_ui_generated_", source):
        errors.append(f"{shown}: generated table has writable storage")
    return errors


def _audit_copy_accessor(
    runtime_header: str,
    runtime_source: str,
    struct_name: str,
    function_name: str,
    exceptions: dict[str, str] | None = None,
) -> list[str]:
    errors: list[str] = []
    fields = _struct_fields(runtime_header, struct_name)
    body = _function_body(runtime_source, function_name)
    if not fields:
        return [f"embedded/pal_ui_layout_runtime.h: missing {struct_name}"]
    if body is None:
        return [f"embedded/pal_ui_layout_runtime.c: missing {function_name}"]
    assignments = _out_assignments(body)
    expected_exceptions = exceptions or {}
    for field in fields:
        expected = expected_exceptions.get(field, f"source->{field}")
        actual = assignments.get(field)
        if actual != _normalise(expected):
            errors.append(
                f"embedded/pal_ui_layout_runtime.c: {function_name} "
                f"must copy {field} from generated data; got {actual!r}"
            )
    return errors


def audit_runtime_boundary(
    runtime_header: str,
    runtime_source: str,
    scaler_source: str,
    board_source: str,
    video_source: str,
    rle_header: str,
    rle_source: str,
) -> list[str]:
    errors: list[str] = []
    if "#include PAL_UI_LAYOUT_GENERATED_HEADER" not in runtime_source:
        errors.append(
            "embedded/pal_ui_layout_runtime.c: generated-header include boundary is missing"
        )
    if "PAL_UI_LAYOUT_GENERATED_HEADER must name one generated profile header" not in runtime_source:
        errors.append(
            "embedded/pal_ui_layout_runtime.c: missing fail-closed generated-header guard"
        )
    if "PAL_UI_GENERATED_COEFFICIENTS_ONLY != 1u" not in runtime_source:
        errors.append(
            "embedded/pal_ui_layout_runtime.c: missing generated-only "
            "coefficient marker guard"
        )
    if (
        "!defined(PAL_UI_GENERATED_PUBLIC_ABI_VERSION)" not in runtime_source
        or "PAL_UI_GENERATED_PUBLIC_ABI_VERSION != 1u" not in runtime_source
    ):
        errors.append(
            "embedded/pal_ui_layout_runtime.c: missing fail-closed "
            "generated/public ABI version guard"
        )
    for assertion in (
        "pal_ui_public_variant_ids_match",
        "pal_ui_public_screen_ids_match",
        "pal_ui_public_element_ids_match",
        "pal_ui_public_camera_kind_ids_match",
        "pal_ui_public_camera_mode_ids_match",
        "pal_ui_public_asset_class_ids_match",
        "pal_ui_public_filter_ids_match",
        "pal_ui_public_flag_ids_match",
        "pal_ui_public_no_screen_id_matches",
    ):
        if assertion not in runtime_source:
            errors.append(
                "embedded/pal_ui_layout_runtime.c: missing generated/public "
                f"ABI assertion {assertion}"
            )
    if re.search(r'#include\s+"generated/pal_ui_layout_[^"]+"', runtime_source):
        errors.append(
            "embedded/pal_ui_layout_runtime.c: runtime hard-codes a profile header"
        )

    profile_fields = _struct_fields(runtime_header, "PalUiLayoutProfile")
    profile_body = _function_body(runtime_source, "PalUiLayout_GetProfile")
    if profile_body is None:
        errors.append("embedded/pal_ui_layout_runtime.c: missing PalUiLayout_GetProfile")
    else:
        assignments = _out_assignments(profile_body)
        for field in profile_fields:
            actual = assignments.get(field)
            if actual is None or not (
                re.fullmatch(r"PAL_UI_[A-Z0-9_]+", actual)
                or re.fullmatch(
                    r"copy_rect\(pal_ui_generated_[a-z0-9_]+\)", actual
                )
            ):
                errors.append(
                    "embedded/pal_ui_layout_runtime.c: "
                    f"PalUiLayout_GetProfile must source {field} from the "
                    f"generated header; got {actual!r}"
                )

    loading_body = _function_body(
        board_source, "CardputerExtreme_ShowLoading"
    )
    if loading_body is None:
        errors.append(
            "esp32s3/main/cardputer_extreme_board.c: missing generated "
            "loading screen"
        )
    else:
        for macro in REQUIRED_LOADING_MACROS:
            if macro not in loading_body:
                errors.append(
                    "esp32s3/main/cardputer_extreme_board.c: loading "
                    f"geometry must consume {macro}"
                )
    if (
        "PAL_UI_LAYOUT_MIGRATED_BEGIN loading" not in board_source
        or "PAL_UI_LAYOUT_MIGRATED_END loading" not in board_source
    ):
        errors.append(
            "esp32s3/main/cardputer_extreme_board.c: loading geometry "
            "is outside the reviewed migration block"
        )
    begin_body = _function_body(board_source, "CardputerExtreme_Begin")
    if (
        begin_body is None
        or "PalUiLayout_ValidateGenerated" not in begin_body
        or "CardputerExtreme_ScalerValidateGeneratedMap" not in begin_body
    ):
        errors.append(
            "esp32s3/main/cardputer_extreme_board.c: board startup must "
            "fail closed on generated layout and exact stage-map validation"
        )

    argb_body = _function_body(
        board_source, "CardputerExtreme_FlushArgb8888Texture"
    )
    if argb_body is None:
        errors.append(
            "esp32s3/main/cardputer_extreme_board.c: missing ARGB "
            "presentation compatibility path"
        )
    else:
        for dimension, macro in (
            ("width", "PAL_UI_GENERATED_STAGE_SOURCE_WIDTH"),
            ("height", "PAL_UI_GENERATED_STAGE_SOURCE_HEIGHT"),
        ):
            if (
                f"{dimension} != {macro}" not in argb_body
            ):
                errors.append(
                    "esp32s3/main/cardputer_extreme_board.c: ARGB "
                    f"{dimension} sampling must be fixed by {macro}"
                )
        for accessor in (
            "CardputerExtreme_ScalerSourceX",
            "CardputerExtreme_ScalerSourceY",
        ):
            if accessor not in argb_body:
                errors.append(
                    "esp32s3/main/cardputer_extreme_board.c: ARGB "
                    f"sampling must consume generated map via {accessor}"
                )
        if "scaled_source_coordinate" in argb_body:
            errors.append(
                "esp32s3/main/cardputer_extreme_board.c: ARGB path "
                "must not derive source coordinates on target"
            )

    video_body = _function_body(
        video_source, "PalEngineBridge_RenderPresentIndexed"
    )
    if video_body is None:
        errors.append(
            "esp32s3/engine_bridge/pal_engine_target_video.c: missing "
            "indexed presentation boundary"
        )
    else:
        for macro in (
            "PAL_UI_GENERATED_STAGE_SOURCE_WIDTH",
            "PAL_UI_GENERATED_STAGE_SOURCE_HEIGHT",
            "PAL_UI_GENERATED_STAGE_DESTINATION_WIDTH",
            "PAL_UI_GENERATED_STAGE_DESTINATION_HEIGHT",
        ):
            if macro not in video_body:
                errors.append(
                    "esp32s3/engine_bridge/pal_engine_target_video.c: "
                    f"presentation boundary must consume {macro}"
                )

    errors.extend(
        _audit_copy_accessor(
            runtime_header,
            runtime_source,
            "PalUiLayoutScreen",
            "PalUiLayout_GetScreen",
        )
    )
    errors.extend(
        _audit_copy_accessor(
            runtime_header,
            runtime_source,
            "PalUiLayoutElement",
            "PalUiLayout_GetElement",
            {"rect": "copy_rect(source->rect)"},
        )
    )
    errors.extend(
        _audit_copy_accessor(
            runtime_header,
            runtime_source,
            "PalUiLayoutSampling",
            "PalUiLayout_GetSampling",
            {
                "fixed_q_shift": "PAL_UI_GENERATED_FIXED_Q_SHIFT",
                "destination": "copy_rect(source->destination)",
            },
        )
    )
    errors.extend(
        _audit_copy_accessor(
            runtime_header,
            runtime_source,
            "PalUiLayoutCameraVector",
            "PalUiLayout_GetCameraVector",
            {
                "source": "copy_rect(source->bounds)",
                "screen": "copy_rect(source->screen)",
                "fixed_q_shift": "PAL_UI_GENERATED_FIXED_Q_SHIFT",
            },
        )
    )
    errors.extend(
        _audit_copy_accessor(
            runtime_header,
            runtime_source,
            "PalUiLayoutBattleCameraPolicy",
            "PalUiLayout_GetBattleCameraPolicy",
            {
                "arena": "copy_rect(source->arena)",
                "hud_rect": "copy_rect(source->hud_rect)",
                "content_rect": "copy_rect(source->content_rect)",
                "focus_screen": "copy_rect(source->focus_screen)",
                "fit_screen": "copy_rect(source->fit_screen)",
                "fixed_q_shift": "PAL_UI_GENERATED_FIXED_Q_SHIFT",
            },
        )
    )
    for accessor, table in (
        (
            "PalUiLayout_BattleFitSourceX",
            "pal_ui_generated_battle_fit_sample_x",
        ),
        (
            "PalUiLayout_BattleFitSourceY",
            "pal_ui_generated_battle_fit_sample_y",
        ),
    ):
        accessor_body = _function_body(runtime_source, accessor)
        if accessor_body is None or table not in accessor_body:
            errors.append(
                "embedded/pal_ui_layout_runtime.c: "
                f"{accessor} must read Python-generated {table}"
            )
    resolve_body = _function_body(
        runtime_source, "PalUiLayout_ResolveBattleCamera"
    )
    if resolve_body is None:
        errors.append(
            "embedded/pal_ui_layout_runtime.c: missing live battle "
            "camera evaluator"
        )
    else:
        for required in (
            "PalUiLayout_GetBattleCameraPolicy",
            "PAL_UI_LAYOUT_BATTLE_FORCE_FIT_ALL",
            "policy.focus_source_width",
            "policy.focus_source_height",
            "policy.fit_screen",
        ):
            if required not in resolve_body:
                errors.append(
                    "embedded/pal_ui_layout_runtime.c: live battle camera "
                    f"must consume generated policy field {required}"
                )
    for function_name in (
        "PalUiLayout_GetListTemplate",
        "PalUiLayout_GetListSlot",
        "PalUiLayout_GetListPageCount",
    ):
        if _function_body(runtime_source, function_name) is None:
            errors.append(
                "embedded/pal_ui_layout_runtime.c: missing live list "
                f"template API {function_name}"
            )
    font_identity_body = _function_body(
        runtime_source, "PalUiLayout_Font10IdentityMatches"
    )
    if font_identity_body is None:
        errors.append(
            "embedded/pal_ui_layout_runtime.c: missing FONT10 identity gate"
        )
    else:
        for macro in (
            "PAL_UI_GENERATED_FONT_GLYPH_COUNT",
            "PAL_UI_GENERATED_FONT_IMAGE_BYTES",
            "PAL_UI_GENERATED_FONT_PAYLOAD_CRC32",
            "PAL_UI_GENERATED_FONT_CELL_WIDTH",
            "PAL_UI_GENERATED_FONT_CELL_HEIGHT",
            "PAL_UI_GENERATED_FONT_ASCENT",
            "PAL_UI_GENERATED_FONT_DESCENT",
        ):
            if macro not in font_identity_body:
                errors.append(
                    "embedded/pal_ui_layout_runtime.c: FONT10 identity "
                    f"must consume {macro}"
                )

    scaler_body = _function_body(
        scaler_source, "CardputerExtreme_ScaleIndexedStrip"
    )
    if scaler_body is None:
        errors.append(
            "esp32s3/main/cardputer_extreme_scaler.c: missing strip scaler"
        )
    else:
        for local, macro in (
            ("source_width", "PAL_UI_GENERATED_STAGE_SOURCE_WIDTH"),
            ("source_height", "PAL_UI_GENERATED_STAGE_SOURCE_HEIGHT"),
            (
                "destination_width",
                "PAL_UI_GENERATED_STAGE_DESTINATION_WIDTH",
            ),
            (
                "destination_height",
                "PAL_UI_GENERATED_STAGE_DESTINATION_HEIGHT",
            ),
        ):
            if re.search(
                rf"\b{local}\s*=\s*{macro}\s*;", scaler_body
            ) is None:
                errors.append(
                    "esp32s3/main/cardputer_extreme_scaler.c: "
                    f"{local} must come from {macro}"
                )
        for accessor in (
            "CardputerExtreme_ScalerSourceX",
            "CardputerExtreme_ScalerSourceY",
        ):
            if accessor not in scaler_body:
                errors.append(
                    "esp32s3/main/cardputer_extreme_scaler.c: strip "
                    f"scaler must consume generated map via {accessor}"
                )
        if "scaled_source_coordinate" in scaler_body:
            errors.append(
                "esp32s3/main/cardputer_extreme_scaler.c: strip scaler "
                "must not derive pixel mappings on target"
            )
    for accessor, table in (
        (
            "CardputerExtreme_ScalerSourceX",
            "pal_ui_generated_stage_sample_x",
        ),
        (
            "CardputerExtreme_ScalerSourceY",
            "pal_ui_generated_stage_sample_y",
        ),
    ):
        accessor_body = _function_body(scaler_source, accessor)
        if accessor_body is None or table not in accessor_body:
            errors.append(
                "esp32s3/main/cardputer_extreme_scaler.c: "
                f"{accessor} must read Python-generated {table}"
            )
    validation_body = _function_body(
        scaler_source,
        "CardputerExtreme_ScalerValidateGeneratedMap",
    )
    if (
        validation_body is None
        or "expected_nearest_center" not in validation_body
        or "CardputerExtreme_ScalerSourceX" not in validation_body
        or "CardputerExtreme_ScalerSourceY" not in validation_body
    ):
        errors.append(
            "esp32s3/main/cardputer_extreme_scaler.c: generated stage "
            "sample maps lack exact boot-time validation"
        )

    layout_sampling_fields = set(
        _struct_fields(runtime_header, "PalUiLayoutSampling")
    )
    rle_sampling_fields = _struct_fields(rle_header, "PalUiRleSampling")
    required_rle_fields = (
        "source_width",
        "source_height",
        "destination_width",
        "destination_height",
        "fixed_q_shift",
        "step_x_q16",
        "step_y_q16",
        "phase_x_q16",
        "phase_y_q16",
    )
    if rle_sampling_fields != required_rle_fields:
        errors.append(
            "embedded/pal_ui_rle_downsample.h: downsampler coefficient "
            "shape changed outside the generated-layout contract"
        )
    missing = set(rle_sampling_fields) - layout_sampling_fields
    if missing:
        errors.append(
            "embedded/pal_ui_rle_downsample.h: coefficients absent from "
            f"generated sampling records: {sorted(missing)}"
        )
    if "PAL_UI_GENERATED_" in rle_source or '"generated/' in rle_source:
        errors.append(
            "embedded/pal_ui_rle_downsample.c: generic rasterizer must "
            "receive generated coefficients instead of selecting a profile"
        )
    if "PAL_UI_GENERATED_FIXED_Q_SHIFT" not in runtime_source:
        errors.append(
            "embedded/pal_ui_layout_runtime.c: generated fixed-point "
            "shift is not consumed"
        )
    if re.search(r"\b(?:float|double)\b", runtime_source + rle_source):
        errors.append(
            "layout/camera/downsampling runtime contains floating-point policy"
        )
    return errors


def audit_repository(root: str | Path) -> tuple[str, ...]:
    root_path = Path(root)
    errors: list[str] = []

    for relative in GENERATED_HEADERS:
        path = root_path / relative
        try:
            source = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            errors.append(f"{relative}: cannot read generated header: {exc}")
            continue
        errors.extend(audit_generated_header(Path(relative), source))

    paths = {
        "runtime_header": "embedded/pal_ui_layout_runtime.h",
        "runtime_source": "embedded/pal_ui_layout_runtime.c",
        "scaler_source": "esp32s3/main/cardputer_extreme_scaler.c",
        "board_source": "esp32s3/main/cardputer_extreme_board.c",
        "video_source": "esp32s3/engine_bridge/pal_engine_target_video.c",
        "rle_header": "embedded/pal_ui_rle_downsample.h",
        "rle_source": "embedded/pal_ui_rle_downsample.c",
    }
    sources: dict[str, str] = {}
    for key, relative in paths.items():
        try:
            sources[key] = (root_path / relative).read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            errors.append(f"{relative}: cannot read contract source: {exc}")
    if len(sources) == len(paths):
        errors.extend(audit_runtime_boundary(**sources))
    try:
        from .lint import cardputer_lint_config, lint_config

        errors.extend(
            f"{diagnostic.path}:{diagnostic.line}: {diagnostic.code}: "
            f"{diagnostic.message}"
            for diagnostic in lint_config(
                root_path, cardputer_lint_config()
            )
        )
    except (OSError, ValueError) as exc:
        errors.append(f"Cardputer generated-coordinate lint failed: {exc}")
    return tuple(errors)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    errors = audit_repository(args.root)
    if errors:
        for error in errors:
            print(error)
        print(
            f"pal-ui-generated-coefficient-contract: FAIL errors={len(errors)}"
        )
        return 1
    print(
        "pal-ui-generated-coefficient-contract: PASS "
        f"profiles={len(GENERATED_HEADERS)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
