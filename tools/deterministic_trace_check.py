#!/usr/bin/env python3
"""Compare deterministic SDLPAL route traces.

The trace format is intentionally line-oriented:

    <frame> <tag> key=value key=value ...

Comment lines beginning with "#" and blank lines are ignored.  The current
deterministic harness emits "present" records with tick, screen, dir, and keys
    fields; script/save/scene checkpoints use the same record shape.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


PINNED_TRACE_HASH_FIELDS = (
    "golden_trace_sha256",
    "golden_script_trace_sha256",
    "golden_event_trace_sha256",
)
SHA256_PATTERN = re.compile(r"[0-9a-fA-F]{64}")


@dataclass(frozen=True)
class TraceRecord:
    path: Path
    line_no: int
    frame: int
    tag: str
    fields: dict[str, str]
    raw: str


@dataclass(frozen=True)
class Mismatch:
    index: int
    field: str
    golden: TraceRecord | None
    candidate: TraceRecord | None
    detail: str


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_trace(path: Path) -> list[TraceRecord]:
    records: list[TraceRecord] = []
    with path.open("r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, 1):
            text = line.strip()
            if not text or text.startswith("#"):
                continue
            parts = text.split()
            if len(parts) < 2:
                raise ValueError(f"{path}:{line_no}: expected '<frame> <tag> [key=value...]'")
            try:
                frame = int(parts[0], 0)
            except ValueError as e:
                raise ValueError(f"{path}:{line_no}: bad frame number: {parts[0]!r}") from e
            fields: dict[str, str] = {}
            for item in parts[2:]:
                if "=" not in item:
                    raise ValueError(f"{path}:{line_no}: bad field, expected key=value: {item!r}")
                key, value = item.split("=", 1)
                if not key:
                    raise ValueError(f"{path}:{line_no}: empty field name")
                fields[key] = value
            records.append(TraceRecord(path, line_no, frame, parts[1], fields, text))
    return records


def load_metadata(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError(f"{path}: metadata must be a JSON object")
    return data


def check_required_pinned_trace_hashes(metadata: dict[str, Any]) -> list[str]:
    """Require complete, syntactically valid immutable trace pins."""

    errors: list[str] = []
    for field in PINNED_TRACE_HASH_FIELDS:
        if field not in metadata:
            errors.append(f"metadata is missing required trace hash {field!r}")
            continue
        value = metadata[field]
        if not isinstance(value, str) or SHA256_PATTERN.fullmatch(value) is None:
            errors.append(f"metadata trace hash {field!r} must be exactly 64 hexadecimal digits")
    return errors


def choose_fields(
    explicit_fields: list[str],
    ignored_fields: set[str],
    golden: TraceRecord,
    candidate: TraceRecord,
) -> list[str]:
    if explicit_fields:
        return [field for field in explicit_fields if field not in ignored_fields]
    fields = sorted(set(golden.fields) | set(candidate.fields))
    return [field for field in fields if field not in ignored_fields]


def compare_records(
    golden_records: list[TraceRecord],
    candidate_records: list[TraceRecord],
    explicit_fields: list[str],
    ignored_fields: set[str],
) -> Mismatch | None:
    count = min(len(golden_records), len(candidate_records))
    for index in range(count):
        golden = golden_records[index]
        candidate = candidate_records[index]
        if golden.frame != candidate.frame:
            return Mismatch(index, "frame", golden, candidate, f"{golden.frame} != {candidate.frame}")
        if golden.tag != candidate.tag:
            return Mismatch(index, "tag", golden, candidate, f"{golden.tag!r} != {candidate.tag!r}")
        for field in choose_fields(explicit_fields, ignored_fields, golden, candidate):
            golden_value = golden.fields.get(field)
            candidate_value = candidate.fields.get(field)
            if golden_value != candidate_value:
                return Mismatch(
                    index,
                    field,
                    golden,
                    candidate,
                    f"{golden_value!r} != {candidate_value!r}",
                )

    if len(golden_records) != len(candidate_records):
        if len(candidate_records) < len(golden_records):
            return Mismatch(
                len(candidate_records),
                "record-count",
                golden_records[len(candidate_records)],
                None,
                "candidate trace ended early",
            )
        return Mismatch(
            len(golden_records),
            "record-count",
            None,
            candidate_records[len(golden_records)],
            "candidate trace has extra records",
        )
    return None


def check_required_fields(records: list[TraceRecord], fields: list[str], label: str) -> list[str]:
    errors: list[str] = []
    if not records:
        errors.append(f"{label}: empty trace")
        return errors
    for field in fields:
        for record in records:
            if field not in record.fields:
                errors.append(f"{label}: missing required field {field!r} at {record.path}:{record.line_no}")
                break
    return errors


def check_min_records(records: list[TraceRecord], minimum: int, label: str) -> list[str]:
    if minimum <= 0 or len(records) >= minimum:
        return []
    return [f"{label}: expected at least {minimum} records, got {len(records)}"]


def parse_field_budget(value: str, flag: str) -> tuple[str, int]:
    field, sep, raw_minimum = value.partition("=")
    if not sep or not field:
        raise argparse.ArgumentTypeError(f"{flag}: expected FIELD=COUNT, got {value!r}")
    try:
        minimum = int(raw_minimum, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"{flag}: bad count in {value!r}") from exc
    if minimum < 0:
        raise argparse.ArgumentTypeError(f"{flag}: count must be non-negative")
    return field, minimum


def check_min_distinct_fields(
    records: list[TraceRecord],
    budgets: list[tuple[str, int]],
    label: str,
) -> list[str]:
    errors: list[str] = []
    for field, minimum in budgets:
        values = {record.fields[field] for record in records if field in record.fields}
        if len(values) < minimum:
            errors.append(
                f"{label}: expected at least {minimum} distinct {field} values, got {len(values)}"
            )
    return errors


def check_required_tags(records: list[TraceRecord], tags: list[str], label: str) -> list[str]:
    errors: list[str] = []
    if not tags:
        return errors
    present = {record.tag for record in records}
    for tag in tags:
        if tag not in present:
            errors.append(f"{label}: missing required tag {tag!r}")
    return errors


def record_label(record: TraceRecord | None) -> str:
    if record is None:
        return "<missing>"
    return f"{record.path}:{record.line_no}: {record.raw}"


def print_context(
    title: str,
    records: list[TraceRecord],
    index: int,
    context: int,
) -> None:
    start = max(0, index - context)
    end = min(len(records), index + context + 1)
    print(title)
    if start >= end:
        print("  <no records>")
        return
    for i in range(start, end):
        marker = ">" if i == index else " "
        record = records[i]
        print(f"{marker} [{i}] {record.raw}")


def record_frame(record: TraceRecord) -> int:
    value = record.fields.get("frame")
    if value is None:
        return record.frame
    try:
        return int(value, 0)
    except ValueError:
        return record.frame


def print_frame_context(
    title: str,
    records: list[TraceRecord],
    frame: int,
    context: int,
) -> None:
    print(title)
    selected = [record for record in records if abs(record_frame(record) - frame) <= context]
    if not selected:
        print("  <no records>")
        return
    for record in selected:
        marker = ">" if record_frame(record) == frame else " "
        print(f"{marker} {record.raw}")


def mismatch_frame(mismatch: Mismatch) -> int:
    if mismatch.golden is not None:
        return record_frame(mismatch.golden)
    if mismatch.candidate is not None:
        return record_frame(mismatch.candidate)
    return 0


def print_mismatch(
    label: str,
    mismatch: Mismatch,
    golden_records: list[TraceRecord],
    candidate_records: list[TraceRecord],
    context: int,
) -> int:
    frame = mismatch_frame(mismatch)
    print("\n## FAIL")
    print(f"first {label} divergence: record={mismatch.index} frame={frame} field={mismatch.field}")
    print(f"detail: {mismatch.detail}")
    print(f"golden: {record_label(mismatch.golden)}")
    print(f"candidate: {record_label(mismatch.candidate)}")
    print_context(f"\ngolden {label} context:", golden_records, mismatch.index, context)
    print_context(f"\ncandidate {label} context:", candidate_records, mismatch.index, context)
    return frame


def write_metadata(
    path: Path,
    manifest_sha256: str | None,
    golden_path: Path,
    golden_sha256: str,
    golden_script_path: Path | None,
    golden_script_sha256: str | None,
    golden_event_path: Path | None,
    golden_event_sha256: str | None,
    frame_count: int,
    fields: list[str],
) -> None:
    data: dict[str, Any] = {
        "golden_trace": str(golden_path),
        "golden_trace_sha256": golden_sha256,
        "frames": frame_count,
        "compare_fields": fields,
    }
    if manifest_sha256 is not None:
        data["manifest_sha256"] = manifest_sha256
    if golden_script_path is not None and golden_script_sha256 is not None:
        data["golden_script_trace"] = str(golden_script_path)
        data["golden_script_trace_sha256"] = golden_script_sha256
    if golden_event_path is not None and golden_event_sha256 is not None:
        data["golden_event_trace"] = str(golden_event_path)
        data["golden_event_trace_sha256"] = golden_event_sha256
    with path.open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("golden", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--metadata", type=Path, help="JSON metadata that pins manifest/golden hashes")
    parser.add_argument(
        "--require-pinned-traces",
        action="store_true",
        help=(
            "require metadata to pin valid SHA-256 hashes for the main, script, "
            "and event golden traces"
        ),
    )
    parser.add_argument("--write-metadata", type=Path, help="write JSON metadata for this golden trace")
    parser.add_argument("--golden-script-trace", type=Path)
    parser.add_argument("--candidate-script-trace", type=Path)
    parser.add_argument("--golden-event-trace", type=Path)
    parser.add_argument("--candidate-event-trace", type=Path)
    parser.add_argument("--expect-manifest-sha256")
    parser.add_argument("--field", action="append", default=[], help="field to compare; defaults to all fields")
    parser.add_argument("--ignore-field", action="append", default=[])
    parser.add_argument("--ignore-script-field", action="append", default=[])
    parser.add_argument("--ignore-event-field", action="append", default=[])
    parser.add_argument("--require-field", action="append", default=[])
    parser.add_argument("--require-tag", action="append", default=[])
    parser.add_argument("--require-script-tag", action="append", default=[])
    parser.add_argument("--require-event-tag", action="append", default=[])
    parser.add_argument("--min-records", type=int, default=0)
    parser.add_argument("--min-script-records", type=int, default=0)
    parser.add_argument("--min-event-records", type=int, default=0)
    parser.add_argument(
        "--min-distinct-field",
        action="append",
        default=[],
        metavar="FIELD=COUNT",
        help="require each main trace to contain at least COUNT distinct values for FIELD",
    )
    parser.add_argument("--context", type=int, default=3)
    parser.add_argument("--script-context", type=int, default=3)
    args = parser.parse_args()

    try:
        golden_path = args.golden.resolve()
        candidate_path = args.candidate.resolve()
        golden_records = parse_trace(golden_path)
        candidate_records = parse_trace(candidate_path)
        golden_sha256 = sha256_file(golden_path)
        candidate_sha256 = sha256_file(candidate_path)
        manifest_sha256 = sha256_file(args.manifest.resolve()) if args.manifest is not None else None
        metadata = load_metadata(args.metadata.resolve()) if args.metadata is not None else {}
        golden_script_path = args.golden_script_trace.resolve() if args.golden_script_trace is not None else None
        candidate_script_path = args.candidate_script_trace.resolve() if args.candidate_script_trace is not None else None
        golden_event_path = args.golden_event_trace.resolve() if args.golden_event_trace is not None else None
        candidate_event_path = args.candidate_event_trace.resolve() if args.candidate_event_trace is not None else None
        golden_script_records = parse_trace(golden_script_path) if golden_script_path is not None else []
        candidate_script_records = parse_trace(candidate_script_path) if candidate_script_path is not None else []
        golden_event_records = parse_trace(golden_event_path) if golden_event_path is not None else []
        candidate_event_records = parse_trace(candidate_event_path) if candidate_event_path is not None else []
        golden_script_sha256 = sha256_file(golden_script_path) if golden_script_path is not None else None
        candidate_script_sha256 = sha256_file(candidate_script_path) if candidate_script_path is not None else None
        golden_event_sha256 = sha256_file(golden_event_path) if golden_event_path is not None else None
        candidate_event_sha256 = sha256_file(candidate_event_path) if candidate_event_path is not None else None
    except (OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    errors: list[str] = []
    if args.require_pinned_traces:
        if args.metadata is None:
            errors.append("--require-pinned-traces requires --metadata")
        else:
            errors.extend(check_required_pinned_trace_hashes(metadata))
    expected_manifest = args.expect_manifest_sha256 or metadata.get("manifest_sha256")
    if expected_manifest is not None:
        if manifest_sha256 is None:
            errors.append("metadata pins a manifest hash, but --manifest was not supplied")
        elif str(expected_manifest) != manifest_sha256:
            errors.append(f"manifest hash mismatch: expected {expected_manifest}, got {manifest_sha256}")

    expected_golden = metadata.get("golden_trace_sha256")
    if expected_golden is not None and str(expected_golden) != golden_sha256:
        errors.append(f"golden trace hash mismatch: expected {expected_golden}, got {golden_sha256}")

    expected_golden_script = metadata.get("golden_script_trace_sha256")
    if expected_golden_script is not None:
        if golden_script_sha256 is None:
            errors.append("metadata pins a golden script trace hash, but --golden-script-trace was not supplied")
        elif str(expected_golden_script) != golden_script_sha256:
            errors.append(f"golden script trace hash mismatch: expected {expected_golden_script}, got {golden_script_sha256}")

    expected_golden_event = metadata.get("golden_event_trace_sha256")
    if expected_golden_event is not None:
        if golden_event_sha256 is None:
            errors.append("metadata pins a golden event trace hash, but --golden-event-trace was not supplied")
        elif str(expected_golden_event) != golden_event_sha256:
            errors.append(f"golden event trace hash mismatch: expected {expected_golden_event}, got {golden_event_sha256}")

    errors.extend(check_required_fields(golden_records, list(args.require_field), "golden"))
    errors.extend(check_required_fields(candidate_records, list(args.require_field), "candidate"))
    errors.extend(check_required_tags(golden_records, list(args.require_tag), "golden"))
    errors.extend(check_required_tags(candidate_records, list(args.require_tag), "candidate"))
    errors.extend(check_required_tags(golden_script_records, list(args.require_script_tag), "golden script"))
    errors.extend(check_required_tags(candidate_script_records, list(args.require_script_tag), "candidate script"))
    errors.extend(check_required_tags(golden_event_records, list(args.require_event_tag), "golden event"))
    errors.extend(check_required_tags(candidate_event_records, list(args.require_event_tag), "candidate event"))
    errors.extend(check_min_records(golden_records, args.min_records, "golden"))
    errors.extend(check_min_records(candidate_records, args.min_records, "candidate"))
    errors.extend(check_min_records(golden_script_records, args.min_script_records, "golden script"))
    errors.extend(check_min_records(candidate_script_records, args.min_script_records, "candidate script"))
    errors.extend(check_min_records(golden_event_records, args.min_event_records, "golden event"))
    errors.extend(check_min_records(candidate_event_records, args.min_event_records, "candidate event"))
    distinct_budgets = [
        parse_field_budget(value, "--min-distinct-field")
        for value in args.min_distinct_field
    ]
    errors.extend(check_min_distinct_fields(golden_records, distinct_budgets, "golden"))
    errors.extend(check_min_distinct_fields(candidate_records, distinct_budgets, "candidate"))

    ignored_fields = set(args.ignore_field)
    explicit_fields = list(args.field)
    if not explicit_fields and isinstance(metadata.get("compare_fields"), list):
        explicit_fields = [str(field) for field in metadata["compare_fields"]]
    mismatch = compare_records(golden_records, candidate_records, explicit_fields, ignored_fields)
    script_mismatch = (
        compare_records(golden_script_records, candidate_script_records, [], set(args.ignore_script_field))
        if golden_script_records or candidate_script_records
        else None
    )
    event_mismatch = (
        compare_records(golden_event_records, candidate_event_records, [], set(args.ignore_event_field))
        if golden_event_records or candidate_event_records
        else None
    )

    all_field_names = sorted({field for record in golden_records for field in record.fields})
    if args.write_metadata is not None:
        try:
            write_metadata(
                args.write_metadata.resolve(),
                manifest_sha256,
                golden_path,
                golden_sha256,
                golden_script_path,
                golden_script_sha256,
                golden_event_path,
                golden_event_sha256,
                len(golden_records),
                explicit_fields or [field for field in all_field_names if field not in ignored_fields],
            )
        except OSError as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

    print("# Deterministic Trace Check")
    print(f"golden: {golden_path}")
    print(f"candidate: {candidate_path}")
    print(f"golden_trace_sha256: {golden_sha256}")
    print(f"candidate_trace_sha256: {candidate_sha256}")
    if golden_script_sha256 is not None:
        print(f"golden_script_trace_sha256: {golden_script_sha256}")
    if candidate_script_sha256 is not None:
        print(f"candidate_script_trace_sha256: {candidate_script_sha256}")
    if golden_event_sha256 is not None:
        print(f"golden_event_trace_sha256: {golden_event_sha256}")
    if candidate_event_sha256 is not None:
        print(f"candidate_event_trace_sha256: {candidate_event_sha256}")
    if manifest_sha256 is not None:
        print(f"manifest_sha256: {manifest_sha256}")
    print(f"golden_records: {len(golden_records)}")
    print(f"candidate_records: {len(candidate_records)}")

    if errors:
        print("\n## FAIL")
        for error in errors:
            print(error)
        return 1

    if script_mismatch is not None:
        frame = print_mismatch(
            "script",
            script_mismatch,
            golden_script_records,
            candidate_script_records,
            args.context,
        )
        print_frame_context("\ngolden frame context:", golden_records, frame, args.script_context)
        print_frame_context("\ncandidate frame context:", candidate_records, frame, args.script_context)
        if golden_event_records or candidate_event_records:
            print_frame_context("\ngolden event context:", golden_event_records, frame, args.script_context)
            print_frame_context("\ncandidate event context:", candidate_event_records, frame, args.script_context)
        return 1

    if event_mismatch is not None:
        frame = print_mismatch(
            "event",
            event_mismatch,
            golden_event_records,
            candidate_event_records,
            args.context,
        )
        print_frame_context("\ngolden frame context:", golden_records, frame, args.script_context)
        print_frame_context("\ncandidate frame context:", candidate_records, frame, args.script_context)
        if golden_script_records or candidate_script_records:
            print_frame_context("\ngolden script context:", golden_script_records, frame, args.script_context)
            print_frame_context("\ncandidate script context:", candidate_script_records, frame, args.script_context)
        return 1

    if mismatch is not None:
        frame = print_mismatch("frame", mismatch, golden_records, candidate_records, args.context)
        if golden_script_records or candidate_script_records:
            print_frame_context("\ngolden script context:", golden_script_records, frame, args.script_context)
            print_frame_context("\ncandidate script context:", candidate_script_records, frame, args.script_context)
        if golden_event_records or candidate_event_records:
            print_frame_context("\ngolden event context:", golden_event_records, frame, args.script_context)
            print_frame_context("\ncandidate event context:", candidate_event_records, frame, args.script_context)
        return 1

    print("\n## PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
