#!/usr/bin/env python3
"""Tests for the migrated-UI coordinate ownership lint."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import io
import json
import tempfile
import unittest
from pathlib import Path

from pal_ui_layout.lint import (
    CARDPUTER_LINT_TARGETS,
    LineRange,
    LintConfig,
    LintConfigurationError,
    MarkerBlock,
    ScanTarget,
    cardputer_lint_config,
    config_from_mapping,
    format_diagnostics,
    lint_config,
    main,
)


class LayoutLintTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="pal-layout-lint-")
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write(self, name: str, source: str) -> None:
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")

    def test_default_config_scans_nothing(self) -> None:
        self.write("migrated.c", "int width = 240;\n")
        self.assertEqual(lint_config(self.root, LintConfig()), ())

    def test_dimensions_and_pal_xy_constants_are_rejected(self) -> None:
        self.write(
            "migrated.c",
            """\
int width = 240;
int legacy_height = 200u;
int rle_control_byte = 0x80u;
PAL_POS dynamic = PAL_XY(layout->x, layout->y);
PAL_POS bad = PAL_XY(16 + 2, -7);
""",
        )
        diagnostics = lint_config(
            self.root,
            LintConfig(targets=(ScanTarget("migrated.c"),)),
        )
        self.assertEqual(
            [(item.line, item.column, item.code) for item in diagnostics],
            [(1, 13, "PUL001"), (2, 21, "PUL001"), (5, 15, "PUL002")],
        )
        self.assertIn("resolves to 200", diagnostics[1].message)
        self.assertIn("constant x and y coordinate", diagnostics[2].message)

    def test_pal_xy_with_one_constant_axis_is_rejected_once(self) -> None:
        self.write(
            "mixed.c",
            "PAL_POS p = PAL_XY(PAL_UI_GENERATED_X, 160);\n",
        )
        diagnostics = lint_config(
            self.root,
            LintConfig(targets=(ScanTarget("mixed.c"),)),
        )
        self.assertEqual(len(diagnostics), 1)
        self.assertEqual(diagnostics[0].code, "PUL002")
        self.assertIn("constant y coordinate", diagnostics[0].message)

    def test_comments_strings_generated_macros_and_allow_are_accepted(self) -> None:
        self.write(
            "clean.c",
            """\
// Legacy canvas was 320 by 200.
const char *description = "240x135 PAL_XY(1, 2)";
PAL_POS p = PAL_XY(PAL_UI_GENERATED_X, PAL_UI_GENERATED_Y);
int interoperability_width = 240; // PAL_UI_LAYOUT_LINT_ALLOW: wire ABI
/* 160 128 PAL_XY(4, 5) */
""",
        )
        self.assertEqual(
            lint_config(
                self.root,
                LintConfig(targets=(ScanTarget("clean.c"),)),
            ),
            (),
        )

    def test_allow_annotation_must_be_in_a_comment(self) -> None:
        self.write(
            "not_a_comment.c",
            """\
const char *fake = "PAL_UI_LAYOUT_LINT_ALLOW";
int width = 240;
""",
        )
        diagnostics = lint_config(
            self.root,
            LintConfig(targets=(ScanTarget("not_a_comment.c"),)),
        )
        self.assertEqual([(item.line, item.code) for item in diagnostics], [(2, "PUL001")])

    def test_custom_allow_annotation_is_honored(self) -> None:
        self.write("custom.c", "int width = 240; // EXEMPT_COORD: protocol\n")
        diagnostics = lint_config(
            self.root,
            LintConfig(
                targets=(ScanTarget("custom.c"),),
                allow_annotation="EXEMPT_COORD",
            ),
        )
        self.assertEqual(diagnostics, ())

    def test_line_range_limits_the_scope(self) -> None:
        self.write(
            "partial.c",
            """\
int legacy_only = 320;
int migrated_width = 240;
int legacy_again = 200;
""",
        )
        diagnostics = lint_config(
            self.root,
            LintConfig(
                targets=(
                    ScanTarget(
                        "partial.c",
                        line_ranges=(LineRange(2, 2),),
                    ),
                )
            ),
        )
        self.assertEqual([(item.line, item.code) for item in diagnostics], [(2, "PUL001")])

    def test_marker_block_selects_only_migrated_code(self) -> None:
        self.write(
            "blocks.c",
            """\
int legacy = 320;
// NATIVE_UI_BEGIN
int migrated = 135;
// NATIVE_UI_END
int still_legacy = 160;
""",
        )
        diagnostics = lint_config(
            self.root,
            LintConfig(
                targets=(
                    ScanTarget(
                        "blocks.c",
                        marker_blocks=(
                            MarkerBlock("NATIVE_UI_BEGIN", "NATIVE_UI_END"),
                        ),
                    ),
                )
            ),
        )
        self.assertEqual([(item.line, item.code) for item in diagnostics], [(3, "PUL001")])

    def test_explicit_and_implicit_test_vector_blocks_are_exempt(self) -> None:
        self.write(
            "vectors.c",
            """\
int migrated = 240;
// FIXTURE_BEGIN
PAL_POS expected = PAL_XY(160, 112);
// FIXTURE_END
// PAL_UI_LAYOUT_TEST_VECTOR_BEGIN
int expected_width = 320;
// PAL_UI_LAYOUT_TEST_VECTOR_END
""",
        )
        diagnostics = lint_config(
            self.root,
            LintConfig(
                targets=(
                    ScanTarget(
                        "vectors.c",
                        test_vector_blocks=(
                            MarkerBlock("FIXTURE_BEGIN", "FIXTURE_END"),
                        ),
                    ),
                )
            ),
        )
        self.assertEqual([(item.line, item.code) for item in diagnostics], [(1, "PUL001")])

    def test_generated_and_test_vector_targets_are_exempt(self) -> None:
        self.write("layout_generated.h", "#define DISPLAY_WIDTH 240\n")
        self.write("layout_vectors.c", "PAL_POS p = PAL_XY(160, 112);\n")
        config = LintConfig(
            targets=(
                ScanTarget("layout_generated.h", role="generated"),
                ScanTarget("layout_vectors.c", role="test_vector"),
            )
        )
        self.assertEqual(lint_config(self.root, config), ())

    def test_cardputer_profile_is_an_explicit_clean_inventory(self) -> None:
        repository = Path(__file__).resolve().parents[1]
        config = cardputer_lint_config()
        self.assertEqual(config.targets, CARDPUTER_LINT_TARGETS)
        self.assertEqual(
            tuple(target.path for target in config.targets if target.role == "migrated"),
            (
                "esp32s3/engine_bridge/pal_engine_target_video.c",
                "esp32s3/main/cardputer_extreme_scaler.c",
                "esp32s3/main/cardputer_extreme_scaler.h",
                "esp32s3/main/cardputer_extreme_board.h",
                "esp32s3/main/cardputer_extreme_board.c",
                "embedded/pal_ui_layout_runtime.c",
                "embedded/pal_ui_layout_runtime.h",
                "embedded/pal_ui_rle_downsample.c",
                "embedded/pal_ui_rle_downsample.h",
            ),
        )
        self.assertEqual(
            {target.role for target in config.targets},
            {"migrated", "generated", "test_vector"},
        )
        self.assertEqual(lint_config(repository, config), ())

    def test_cli_requires_an_inventory_and_runs_json_config(self) -> None:
        with redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as context:
                main([])
        self.assertEqual(context.exception.code, 2)

        self.write("screen.c", "int width = 240;\n")
        config_path = self.root / "lint.json"
        config_path.write_text(
            json.dumps({"targets": [{"path": "screen.c"}]}),
            encoding="utf-8",
        )
        output = io.StringIO()
        with redirect_stdout(output):
            result = main(
                [
                    "--root",
                    str(self.root),
                    "--config",
                    str(config_path),
                ]
            )
        self.assertEqual(result, 1)
        self.assertEqual(
            output.getvalue(),
            (
                "screen.c:1:13: PUL001: hand-written "
                "coordinate/dimension literal 240 resolves to 240; "
                "use a generated layout field/macro\n"
            ),
        )

    def test_cardputer_cli_reports_reviewed_role_counts(self) -> None:
        repository = Path(__file__).resolve().parents[1]
        output = io.StringIO()
        with redirect_stdout(output):
            result = main(
                [
                    "--root",
                    str(repository),
                    "--profile",
                    "cardputer",
                ]
            )
        self.assertEqual(result, 0)
        self.assertEqual(
            output.getvalue(),
            "pal-ui-layout-lint: PASS migrated=9 generated=2 test_vector=2\n",
        )

    def test_mapping_config_and_diagnostics_are_stable(self) -> None:
        self.write("z.c", "int h = 135;\n")
        self.write("a.c", "int w = 240;\n")
        config = config_from_mapping(
            {
                "targets": [
                    {"path": "z.c", "line_ranges": [[1, 1]]},
                    {"path": "a.c"},
                ]
            }
        )
        first = format_diagnostics(lint_config(self.root, config))
        second = format_diagnostics(lint_config(self.root, config))
        self.assertEqual(first, second)
        self.assertEqual(
            [line.split(":", 1)[0] for line in first.splitlines()],
            ["a.c", "z.c"],
        )
        self.assertTrue(first.endswith("\n"))

    def test_missing_or_unbalanced_markers_fail_closed(self) -> None:
        self.write("broken.c", "// START\nint width = 240;\n")
        with self.assertRaisesRegex(LintConfigurationError, "has no"):
            lint_config(
                self.root,
                LintConfig(
                    targets=(
                        ScanTarget(
                            "broken.c",
                            marker_blocks=(MarkerBlock("START", "END"),),
                        ),
                    )
                ),
            )
        with self.assertRaisesRegex(LintConfigurationError, "was not found"):
            lint_config(
                self.root,
                LintConfig(
                    targets=(
                        ScanTarget(
                            "broken.c",
                            marker_blocks=(MarkerBlock("ABSENT", "END"),),
                        ),
                    )
                ),
            )


if __name__ == "__main__":
    unittest.main()
