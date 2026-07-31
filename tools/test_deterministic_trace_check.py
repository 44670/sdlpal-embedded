#!/usr/bin/env python3
"""Focused tests for immutable deterministic trace metadata pins."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from deterministic_trace_check import (
    PINNED_TRACE_HASH_FIELDS,
    check_required_pinned_trace_hashes,
)


SCRIPT = Path(__file__).with_name("deterministic_trace_check.py")
VALID_HASHES = {
    field: format(index + 1, "064x")
    for index, field in enumerate(PINNED_TRACE_HASH_FIELDS)
}


class PinnedTraceMetadataTests(unittest.TestCase):
    def test_complete_sha256_set_is_accepted(self) -> None:
        self.assertEqual(check_required_pinned_trace_hashes(VALID_HASHES), [])

    def test_every_missing_sha256_is_rejected(self) -> None:
        for field in PINNED_TRACE_HASH_FIELDS:
            with self.subTest(field=field):
                metadata = dict(VALID_HASHES)
                del metadata[field]
                errors = check_required_pinned_trace_hashes(metadata)
                self.assertTrue(any(field in error and "missing" in error for error in errors), errors)

    def test_malformed_sha256_values_are_rejected(self) -> None:
        malformed = ("", "0" * 63, "0" * 65, "g" * 64, 0, None)
        for field in PINNED_TRACE_HASH_FIELDS:
            for value in malformed:
                with self.subTest(field=field, value=value):
                    metadata = dict(VALID_HASHES)
                    metadata[field] = value
                    errors = check_required_pinned_trace_hashes(metadata)
                    self.assertTrue(any(field in error and "64 hexadecimal" in error for error in errors), errors)

    def test_cli_mode_fails_when_a_required_pin_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            trace = root / "trace.txt"
            trace.write_text("0 present state=1\n", encoding="utf-8")
            metadata = dict(VALID_HASHES)
            del metadata["golden_event_trace_sha256"]
            metadata_path = root / "metadata.json"
            metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
            result = subprocess.run(
                (
                    sys.executable,
                    "-B",
                    str(SCRIPT),
                    "--metadata",
                    str(metadata_path),
                    "--require-pinned-traces",
                    str(trace),
                    str(trace),
                ),
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("golden_event_trace_sha256", result.stdout)
            self.assertIn("missing", result.stdout)


if __name__ == "__main__":
    unittest.main()
