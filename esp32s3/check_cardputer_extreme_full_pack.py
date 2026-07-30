#!/usr/bin/env python3
"""Verify the complete decoded TF mirror beside the active sparse pack."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))
import pal_pack_build as builder  # noqa: E402


ACTIVE_TF_TOC_BYTES = 2048
EXPECTED_RUNTIME_ARCHIVES = set(builder.ARCHIVE_IDS) - {"VOC"}


def pack_archives(
    data: bytes,
) -> tuple[int, list[str], dict[str, list[tuple[bytes, int]]]]:
    archive_count = builder.u16(data, 8)
    archive_table = builder.u32(data, 12)
    toc_bytes = builder.u32(data, 16)
    id_to_name = {value: name for name, value in builder.ARCHIVE_IDS.items()}
    names: list[str] = []
    result: dict[str, list[tuple[bytes, int]]] = {}

    for archive_index in range(archive_count):
        entry = archive_table + archive_index * builder.ARCHIVE_ENTRY_SIZE
        archive_id, chunk_count = struct.unpack_from("<HH", data, entry)
        name = id_to_name.get(archive_id, f"#{archive_id}")
        chunk_table = builder.u32(data, entry + 4)
        chunks: list[tuple[bytes, int]] = []
        for chunk_id in range(chunk_count):
            chunk_entry = chunk_table + chunk_id * builder.CHUNK_ENTRY_SIZE
            offset, size, fmt = struct.unpack_from("<IIH", data, chunk_entry)
            chunks.append((data[offset : offset + size], fmt))
        names.append(name)
        result[name] = chunks
    return toc_bytes, names, result


def archive_summaries(pack_summary: object) -> dict[str, dict[str, object]]:
    if not isinstance(pack_summary, dict):
        return {}
    raw = pack_summary.get("archive_summaries")
    if not isinstance(raw, list):
        return {}
    return {
        str(item.get("name")): item
        for item in raw
        if isinstance(item, dict) and isinstance(item.get("name"), str)
    }


def check_complete_mirror(
    nor_path: Path,
    tf_path: Path,
    full_path: Path,
    manifest_path: Path,
    layout_path: Path,
) -> tuple[list[str], dict[str, int]]:
    errors: list[str] = []
    for path in (nor_path, tf_path, full_path, manifest_path, layout_path):
        if not path.is_file():
            errors.append(f"missing required artifact: {path}")
    if errors:
        return errors, {}

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        profile = manifest.get("pack_layout", {}).get("profile")
        if profile is not None and not isinstance(profile, str):
            raise ValueError("manifest pack profile is not a string")
        layout = builder.load_pack_layout(layout_path, profile)
    except (OSError, ValueError, json.JSONDecodeError, SystemExit) as exc:
        return [f"cannot load complete-mirror policy: {exc}"], {}

    mirror = layout.tf_complete_mirror
    if mirror is None:
        return ["layout has no tf_complete_mirror policy"], {}
    if set(mirror.archives) != EXPECTED_RUNTIME_ARCHIVES:
        errors.append(
            "complete mirror is not the exact runtime archive set "
            "(all archive IDs except raw VOC)"
        )
    if "SFX" not in mirror.archives or "VOC" in mirror.archives:
        errors.append("complete mirror must replace raw VOC with converted SFX")

    pack_bytes: dict[str, bytes] = {}
    invalid_pack = False
    for label, path in (
        ("nor", nor_path),
        ("tf", tf_path),
        ("tf_complete", full_path),
    ):
        data = path.read_bytes()
        pack_bytes[label] = data
        try:
            builder.verify_pack(data)
        except ValueError as exc:
            errors.append(f"{label} pack verification failed: {exc}")
            invalid_pack = True
    if invalid_pack:
        return errors, {}

    set_ids = {
        label: builder.u32(data, builder.PACK_SET_ID_OFFSET)
        for label, data in pack_bytes.items()
        if len(data) >= builder.HEADER_SIZE
    }
    if (
        len(set(set_ids.values())) != 1
        or not set_ids
        or next(iter(set_ids.values())) == 0
    ):
        errors.append(f"bundle pack-set IDs differ or are zero: {set_ids}")

    active_toc = builder.u32(pack_bytes["tf"], 16)
    full_toc, packed_names, packed = pack_archives(pack_bytes["tf_complete"])
    if active_toc > ACTIVE_TF_TOC_BYTES:
        errors.append(
            f"active pal_tf.pak TOC {active_toc} exceeds "
            f"{ACTIVE_TF_TOC_BYTES}"
        )
    if full_toc <= ACTIVE_TF_TOC_BYTES:
        errors.append(
            "complete mirror unexpectedly fits the active 2KB TOC budget; "
            "re-audit its completeness"
        )

    expected_order = sorted(
        mirror.archives,
        key=lambda name: builder.ARCHIVE_IDS[name],
    )
    if packed_names != expected_order:
        errors.append(
            f"complete mirror archives differ: {packed_names} != {expected_order}"
        )

    pack_layout = manifest.get("pack_layout")
    if not isinstance(pack_layout, dict):
        errors.append("manifest has no pack_layout object")
        pack_layout = {}
    if pack_layout.get("sha256") != builder.hash_file(layout_path):
        errors.append("manifest layout SHA-256 mismatch")
    expected_policy = {
        "archives": list(mirror.archives),
        "target_filename": mirror.target_filename,
        "runtime_active": False,
        "all_chunks": True,
        "allow_overlap": True,
        "index_strategy": mirror.index_strategy,
    }
    if pack_layout.get("tf_complete_mirror") != expected_policy:
        errors.append("manifest complete-mirror policy mismatch")

    packs_manifest = manifest.get("packs")
    if not isinstance(packs_manifest, dict):
        errors.append("manifest has no packs object")
        packs_manifest = {}
    full_summary = packs_manifest.get("tf_complete")
    if not isinstance(full_summary, dict):
        errors.append("manifest has no tf_complete pack summary")
        full_summary = {}
    if full_summary.get("path") != str(full_path.resolve()):
        errors.append("manifest complete-pack path mismatch")
    if full_summary.get("size") != len(pack_bytes["tf_complete"]):
        errors.append("manifest complete-pack size mismatch")
    if full_summary.get("sha256") != hashlib.sha256(
        pack_bytes["tf_complete"]
    ).hexdigest():
        errors.append("manifest complete-pack SHA-256 mismatch")
    if full_summary.get("crc32") != builder.u32(
        pack_bytes["tf_complete"],
        builder.PACK_CRC32_OFFSET,
    ):
        errors.append("manifest complete-pack CRC32 mismatch")
    if full_summary.get("pack_set_id") != set_ids.get("tf_complete"):
        errors.append("manifest complete-pack set ID mismatch")
    if full_summary.get("toc_bytes") != full_toc:
        errors.append("manifest complete-pack TOC size mismatch")
    if full_summary.get("archives") != list(mirror.archives):
        errors.append("manifest complete-pack archive list mismatch")

    data_dir_value = manifest.get("data_dir")
    if not isinstance(data_dir_value, str) or not data_dir_value:
        errors.append("manifest has no PAL data directory")
        return errors, {}
    data_dir = Path(data_dir_value)
    if not data_dir.is_dir():
        errors.append(f"manifest PAL data directory is unavailable: {data_dir}")
        return errors, {}
    expected_sources = builder.source_file_manifest(
        data_dir,
        list(mirror.archives),
    )
    if manifest.get("source_files") != expected_sources:
        errors.append("manifest source-file hash inventory is incomplete or stale")

    summaries = archive_summaries(full_summary)
    total_chunks = 0
    total_payload = 0
    for name in expected_order:
        expected_chunks = builder.load_archive(data_dir, name)
        actual_chunks = packed.get(name, [])
        total_chunks += len(expected_chunks)
        total_payload += sum(len(chunk.payload) for chunk in expected_chunks)
        if len(actual_chunks) != len(expected_chunks):
            errors.append(
                f"{name}: complete chunk count {len(actual_chunks)} != "
                f"{len(expected_chunks)}"
            )
            continue

        summary = summaries.get(name, {})
        manifest_chunks = summary.get("chunks")
        if not isinstance(manifest_chunks, list):
            errors.append(f"{name}: manifest has no per-chunk hash inventory")
            manifest_chunks = []
        if summary.get("chunk_count") != len(expected_chunks):
            errors.append(f"{name}: manifest chunk count mismatch")

        for chunk_id, (expected, actual) in enumerate(
            zip(expected_chunks, actual_chunks)
        ):
            payload, fmt = actual
            if fmt != expected.fmt:
                errors.append(
                    f"{name}#{chunk_id}: format {fmt} != {expected.fmt}"
                )
            if payload != expected.payload:
                errors.append(
                    f"{name}#{chunk_id}: payload differs "
                    f"({len(payload)} != {len(expected.payload)} bytes)"
                )
            if payload.startswith(b"YJ_1"):
                errors.append(f"{name}#{chunk_id}: runtime YJ1 payload remains")

            if chunk_id >= len(manifest_chunks):
                errors.append(f"{name}#{chunk_id}: missing manifest chunk entry")
                continue
            chunk_manifest = manifest_chunks[chunk_id]
            expected_manifest = {
                "id": chunk_id,
                "format": builder.FORMAT_NAMES.get(expected.fmt, str(expected.fmt)),
                "payload_bytes": len(expected.payload),
                "sha256": hashlib.sha256(expected.payload).hexdigest(),
            }
            if chunk_manifest != expected_manifest:
                errors.append(f"{name}#{chunk_id}: manifest metadata mismatch")

    if full_summary.get("chunk_count") != total_chunks:
        errors.append("manifest complete total chunk count mismatch")
    if full_summary.get("payload_bytes") != total_payload:
        errors.append("manifest complete payload-byte total mismatch")

    metrics = {
        "active_tf_bytes": len(pack_bytes["tf"]),
        "active_tf_toc_bytes": active_toc,
        "full_bytes": len(pack_bytes["tf_complete"]),
        "full_toc_bytes": full_toc,
        "archive_count": len(expected_order),
        "chunk_count": total_chunks,
        "payload_bytes": total_payload,
        "pack_set_id": set_ids.get("tf_complete", 0),
    }
    return errors, metrics


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nor-pack", required=True, type=Path)
    parser.add_argument("--tf-pack", required=True, type=Path)
    parser.add_argument("--tf-complete-pack", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--layout", required=True, type=Path)
    args = parser.parse_args()

    errors, metrics = check_complete_mirror(
        args.nor_pack.resolve(),
        args.tf_pack.resolve(),
        args.tf_complete_pack.resolve(),
        args.manifest.resolve(),
        args.layout.resolve(),
    )
    if metrics:
        print("Cardputer ADV complete decoded TF mirror")
        print(
            f"  active TF={metrics['active_tf_bytes']} bytes, "
            f"TOC={metrics['active_tf_toc_bytes']}/{ACTIVE_TF_TOC_BYTES}"
        )
        print(
            f"  pal_full.pak={metrics['full_bytes']} bytes, "
            f"TOC={metrics['full_toc_bytes']}, "
            f"archives={metrics['archive_count']}, "
            f"chunks={metrics['chunk_count']}, "
            f"payload={metrics['payload_bytes']}"
        )
        print(f"  pack_set_id=0x{metrics['pack_set_id']:08x}")
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print("PASS: complete host-decoded mirror; active runtime pack remains sparse")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
