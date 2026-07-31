#!/usr/bin/env python3
"""Tests for the generated-coefficient target boundary."""

from __future__ import annotations

import unittest
from pathlib import Path

from pal_ui_layout.contract import (
    audit_generated_header,
    audit_repository,
    audit_runtime_boundary,
)


REPOSITORY = Path(__file__).resolve().parents[1]


def _read(relative: str) -> str:
    return (REPOSITORY / relative).read_text(encoding="utf-8")


def _runtime_sources() -> dict[str, str]:
    return {
        "runtime_header": _read("embedded/pal_ui_layout_runtime.h"),
        "runtime_source": _read("embedded/pal_ui_layout_runtime.c"),
        "scaler_source": _read(
            "esp32s3/main/cardputer_extreme_scaler.c"
        ),
        "board_source": _read("esp32s3/main/cardputer_extreme_board.c"),
        "video_source": _read(
            "esp32s3/engine_bridge/pal_engine_target_video.c"
        ),
        "rle_header": _read("embedded/pal_ui_rle_downsample.h"),
        "rle_source": _read("embedded/pal_ui_rle_downsample.c"),
    }


class GeneratedCoefficientContractTests(unittest.TestCase):
    def test_repository_target_boundary_is_generated_only(self) -> None:
        self.assertEqual(audit_repository(REPOSITORY), ())

    def test_profile_literal_in_runtime_accessor_fails(self) -> None:
        sources = _runtime_sources()
        sources["runtime_source"] = sources["runtime_source"].replace(
            "PAL_UI_GENERATED_STAGE_SCALE_NUMERATOR;",
            "27u;",
            1,
        )
        errors = audit_runtime_boundary(**sources)
        self.assertTrue(
            any("stage_scale_numerator" in error for error in errors),
            errors,
        )

    def test_sampling_literal_in_runtime_accessor_fails(self) -> None:
        sources = _runtime_sources()
        sources["runtime_source"] = sources["runtime_source"].replace(
            "out->step_x_q16 = source->step_x_q16;",
            "out->step_x_q16 = 97090u;",
            1,
        )
        errors = audit_runtime_boundary(**sources)
        self.assertTrue(
            any("GetSampling must copy step_x_q16" in error for error in errors),
            errors,
        )

    def test_scaler_literal_extent_fails(self) -> None:
        sources = _runtime_sources()
        sources["scaler_source"] = sources["scaler_source"].replace(
            "PAL_UI_GENERATED_STAGE_DESTINATION_WIDTH;",
            "216u;",
            1,
        )
        errors = audit_runtime_boundary(**sources)
        self.assertTrue(
            any("destination_width must come from" in error for error in errors),
            errors,
        )

    def test_scaler_must_consume_generated_sample_maps(self) -> None:
        sources = _runtime_sources()
        sources["scaler_source"] = sources["scaler_source"].replace(
            "pal_ui_generated_stage_sample_x[destination_x]",
            "source_width - 1u",
            1,
        )
        errors = audit_runtime_boundary(**sources)
        self.assertTrue(
            any("pal_ui_generated_stage_sample_x" in error for error in errors),
            errors,
        )

    def test_argb_path_must_reuse_generated_sample_maps(self) -> None:
        sources = _runtime_sources()
        sources["board_source"] = sources["board_source"].replace(
            "CardputerExtreme_ScalerSourceX(",
            "UntrustedSourceX(",
            1,
        )
        errors = audit_runtime_boundary(**sources)
        self.assertTrue(
            any("CardputerExtreme_ScalerSourceX" in error for error in errors),
            errors,
        )

    def test_downsampler_coefficient_not_in_generated_record_fails(self) -> None:
        sources = _runtime_sources()
        sources["rle_header"] = sources["rle_header"].replace(
            "uint32_t phase_y_q16;",
            "uint32_t phase_y_q16;\n    uint32_t target_scale_magic;",
            1,
        )
        errors = audit_runtime_boundary(**sources)
        self.assertTrue(
            any("coefficient shape changed" in error for error in errors),
            errors,
        )
        self.assertTrue(
            any("target_scale_magic" in error for error in errors),
            errors,
        )

    def test_fixed_point_shift_must_come_from_generated_header(self) -> None:
        sources = _runtime_sources()
        sources["runtime_source"] = sources["runtime_source"].replace(
            "out->fixed_q_shift = PAL_UI_GENERATED_FIXED_Q_SHIFT;",
            "out->fixed_q_shift = 16u;",
            1,
        )
        errors = audit_runtime_boundary(**sources)
        self.assertTrue(
            any("fixed_q_shift" in error for error in errors),
            errors,
        )

    def test_public_abi_version_guard_is_fail_closed(self) -> None:
        sources = _runtime_sources()
        sources["runtime_source"] = sources["runtime_source"].replace(
            "!defined(PAL_UI_GENERATED_PUBLIC_ABI_VERSION)",
            "defined(PAL_UI_GENERATED_PUBLIC_ABI_VERSION)",
            1,
        )
        errors = audit_runtime_boundary(**sources)
        self.assertTrue(
            any("ABI version guard" in error for error in errors),
            errors,
        )

    def test_font_identity_includes_generated_metrics(self) -> None:
        sources = _runtime_sources()
        sources["runtime_source"] = sources["runtime_source"].replace(
            "cell_width == PAL_UI_GENERATED_FONT_CELL_WIDTH",
            "cell_width != 0",
            1,
        )
        errors = audit_runtime_boundary(**sources)
        self.assertTrue(
            any("FONT_CELL_WIDTH" in error for error in errors),
            errors,
        )

    def test_loading_geometry_literal_fails(self) -> None:
        sources = _runtime_sources()
        sources["board_source"] = sources["board_source"].replace(
            "PAL_UI_GENERATED_LOADING_BAR_WIDTH",
            "200u",
        )
        errors = audit_runtime_boundary(**sources)
        self.assertTrue(
            any("LOADING_BAR_WIDTH" in error for error in errors),
            errors,
        )

    def test_argb_source_extent_must_be_generated(self) -> None:
        sources = _runtime_sources()
        sources["board_source"] = sources["board_source"].replace(
            "width != PAL_UI_GENERATED_STAGE_SOURCE_WIDTH",
            "width == 0",
            1,
        )
        errors = audit_runtime_boundary(**sources)
        self.assertTrue(
            any("ARGB width sampling" in error for error in errors),
            errors,
        )

    def test_generated_header_requires_banner_digest_and_const_tables(self) -> None:
        path = Path(
            "esp32s3/main/generated/pal_ui_layout_240x135.h"
        )
        source = _read(path.as_posix())
        self.assertEqual(audit_generated_header(path, source), [])
        errors = audit_generated_header(
            path,
            source.replace(
                "/* Generated by tools/pal_ui_layout; do not edit. */",
                "/* handwritten */",
                1,
            ),
        )
        self.assertTrue(any("generator banner" in error for error in errors))
        errors = audit_generated_header(
            path,
            source.replace("static const PalUiGeneratedSampling", "static PalUiGeneratedSampling", 1),
        )
        self.assertTrue(any("not static const" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
