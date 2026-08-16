#!/usr/bin/env python3
"""Verify the explicit Cardputer extreme RIX-music resource-pack profile."""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

import check_cardputer_extreme as common


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))
import pal_pack_build as builder  # noqa: E402


PROFILE = "rix-music"
TRACK_IDS = set(range(1, 88)) - {29}
SOURCE_SLOT_IDS = set(range(88))
EMPTY_TRACK_IDS = {0, 29}
MUS_CHUNK_COUNT = 88
MUS_PAYLOAD_BYTES = 330928
MUS_MAX_TRACK_BYTES = 10108


def archive_payloads(path: Path, archive_id: int) -> list[tuple[bytes, int]]:
    data = path.read_bytes()
    archive_count = struct.unpack_from("<H", data, 8)[0]
    archive_table = struct.unpack_from("<I", data, 12)[0]
    for archive_index in range(archive_count):
        entry = archive_table + archive_index * common.ARCHIVE_ENTRY_BYTES
        candidate, chunk_count = struct.unpack_from("<HH", data, entry)
        if candidate != archive_id:
            continue
        chunk_table = struct.unpack_from("<I", data, entry + 4)[0]
        result: list[tuple[bytes, int]] = []
        for chunk_id in range(chunk_count):
            chunk = chunk_table + chunk_id * common.CHUNK_ENTRY_BYTES
            offset, size, fmt = struct.unpack_from("<IIH", data, chunk)
            result.append((data[offset : offset + size], fmt))
        return result
    return []


def find_archive_summary(manifest_pack: object, name: str) -> dict[str, object]:
    if not isinstance(manifest_pack, dict):
        return {}
    summaries = manifest_pack.get("archive_summaries")
    if not isinstance(summaries, list):
        return {}
    for item in summaries:
        if isinstance(item, dict) and item.get("name") == name:
            return item
    return {}


def check_profile(
    nor_path: Path,
    tf_path: Path,
    manifest_path: Path,
    layout_path: Path,
) -> tuple[list[str], dict[str, int]]:
    errors: list[str] = []
    for path in (
        nor_path,
        tf_path,
        manifest_path,
        layout_path,
    ):
        if not path.is_file():
            errors.append(f"missing required artifact: {path}")
    if errors:
        return errors, {}

    try:
        layout_json = json.loads(layout_path.read_text(encoding="utf-8"))
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        base_layout = builder.load_pack_layout(layout_path)
        music_layout = builder.load_pack_layout(layout_path, PROFILE)
    except (OSError, json.JSONDecodeError, SystemExit) as exc:
        return [f"cannot load music pack policy: {exc}"], {}

    if nor_path.stat().st_size > common.NOR_BYTES:
        errors.append(
            f"NOR pack {nor_path.stat().st_size} exceeds {common.NOR_BYTES}"
        )
    if nor_path.stat().st_size > common.MUSIC_MAX_NOR_BYTES:
        errors.append(
            f"NOR pack {nor_path.stat().st_size} leaves less than "
            f"{common.MUSIC_MIN_NOR_RESERVE} bytes reserved"
        )
    if tf_path.stat().st_size == 0:
        errors.append("TF pack is empty")

    nor_toc, nor = common.parse_pack(nor_path, errors)
    tf_toc, tf = common.parse_pack(tf_path, errors)
    if tf_toc > common.TF_TOC_BYTES:
        errors.append(
            f"TF TOC {tf_toc} exceeds SRAM capacity {common.TF_TOC_BYTES}"
        )

    nor_header = nor_path.read_bytes()[: common.PACK_HEADER_BYTES]
    tf_header = tf_path.read_bytes()[: common.PACK_HEADER_BYTES]
    nor_set_id = struct.unpack_from("<I", nor_header, 20)[0]
    tf_set_id = struct.unpack_from("<I", tf_header, 20)[0]
    if nor_set_id == 0 or nor_set_id != tf_set_id:
        errors.append(
            f"NOR/TF pack-set IDs differ: {nor_set_id:#010x} != {tf_set_id:#010x}"
        )

    expected_nor_ids = {
        builder.ARCHIVE_IDS[name] for name in music_layout.pack_names["nor"]
    }
    expected_tf_ids = {
        builder.ARCHIVE_IDS[name] for name in music_layout.pack_names["tf"]
    }
    if set(nor) != expected_nor_ids:
        errors.append("NOR archives do not match the rix-music profile")
    if set(tf) != expected_tf_ids:
        errors.append("TF archives do not match the rix-music profile")

    if common.nonempty(nor, common.ARCHIVE["FBP"]) != {0, 60}:
        errors.append("unexpected NOR FBP chapter selection")
    if common.nonempty(tf, common.ARCHIVE["FBP"]) != {1, 3, 6, 8, 21}:
        errors.append("unexpected TF FBP chapter selection")
    if common.nonempty(tf, common.ARCHIVE["RNG"]) != {1}:
        errors.append("unexpected TF RNG chapter selection")
    font_chunks = nor.get(common.ARCHIVE["FONT"], {})
    if (
        font_chunks.get(1, (0, 0))[0] <= 0
        or font_chunks.get(1, (0, 0))[1] != builder.FORMAT_FONT10
    ):
        errors.append("NOR FONT chunk 1 is not a non-empty FONT10 payload")
    if common.nonempty(nor, common.ARCHIVE["FONT"]) != {1}:
        errors.append("NOR FONT must contain only the FONT10 chunk 1")
    if common.ARCHIVE["FONT"] in tf:
        errors.append("FONT10 must be mapped from NOR, not TF")
    if common.nonempty(nor, common.ARCHIVE["MAP"]) != common.nonempty(
        nor, common.ARCHIVE["GOP"]
    ):
        errors.append("MAP/GOP chapter selections differ")
    event_bytes = nor.get(common.ARCHIVE["SSS"], {}).get(0, (0, 0))[0]
    if (
        event_bytes <= 0
        or event_bytes % common.EVENT_RECORD_BYTES != 0
        or event_bytes
        > common.EVENT_RECORD_CAPACITY * common.EVENT_RECORD_BYTES
    ):
        errors.append("SSS event-object chunk has invalid bounded geometry")
    for archive_id in set(nor) & set(tf):
        overlap = common.nonempty(nor, archive_id) & common.nonempty(
            tf, archive_id
        )
        if overlap:
            errors.append(
                f"archive {archive_id} has non-empty chunks in both packs: "
                f"{sorted(overlap)}"
            )

    base_archives = set(base_layout.pack_names["nor"]) | set(
        base_layout.pack_names["tf"]
    )
    if {"MIDI", "MUS", "VOC", "SFX"} & base_archives:
        errors.append("base Cardputer extreme layout is no longer no-audio")
    effective_archives = set(music_layout.pack_names["nor"]) | set(
        music_layout.pack_names["tf"]
    )
    if effective_archives - base_archives != {"MUS"}:
        errors.append("rix-music profile must add only MUS")
    for name in ("MIDI", "VOC", "SFX"):
        archive_id = common.ARCHIVE[name]
        if archive_id in nor or archive_id in tf:
            errors.append(
                f"{name} must remain excluded from the legacy sparse pack pair"
            )

    mus_id = common.ARCHIVE["MUS"]
    mus = nor.get(mus_id, {})
    if len(mus) != MUS_CHUNK_COUNT:
        errors.append(
            f"MUS archive has {len(mus)} chunks, expected {MUS_CHUNK_COUNT}"
        )
    if common.nonempty(nor, mus_id) != TRACK_IDS:
        errors.append("MUS does not contain every non-empty source track")
    if mus_id in tf:
        errors.append("MUS must be NOR-mapped, not TF-backed")
    mus_payloads = archive_payloads(nor_path, mus_id)
    for chunk_id in TRACK_IDS:
        if chunk_id >= len(mus_payloads):
            continue
        payload, fmt = mus_payloads[chunk_id]
        if fmt != builder.FORMAT_NATIVE:
            errors.append(f"MUS chunk {chunk_id} is not NATIVE")
        if len(payload) < 16 or payload[:2] != b"\xaa\x55":
            errors.append(f"MUS chunk {chunk_id} is not a valid RIX payload")

    profile_json = layout_json.get("profiles", {}).get(PROFILE, {})
    audit = profile_json.get("music_audit", {})
    if set(audit.get("selected_track_ids", [])) != TRACK_IDS:
        errors.append("layout music audit track closure mismatch")
    if set(audit.get("empty_track_ids", [])) != EMPTY_TRACK_IDS:
        errors.append("layout music audit empty-slot mismatch")
    if audit.get("source_chunk_count") != MUS_CHUNK_COUNT:
        errors.append("layout music audit source chunk count mismatch")
    if audit.get("selected_payload_bytes") != MUS_PAYLOAD_BYTES:
        errors.append("layout music audit payload-byte total mismatch")
    if audit.get("max_selected_track_bytes") != MUS_MAX_TRACK_BYTES:
        errors.append("layout music audit maximum track size mismatch")

    if manifest.get("schema") != "sdlpal-embedded-pack-manifest":
        errors.append("unexpected manifest schema")
    pack_layout = manifest.get("pack_layout", {})
    if pack_layout.get("profile") != PROFILE:
        errors.append("manifest does not identify the rix-music profile")
    if pack_layout.get("sha256") != common.sha256_file(layout_path):
        errors.append("manifest layout SHA-256 mismatch")
    if pack_layout.get("packs") != music_layout.pack_names:
        errors.append("manifest effective archive placement mismatch")
    if pack_layout.get("overrides") != {"nor": False, "tf": False}:
        errors.append("music pack must use layout rules without CLI overrides")

    runtime = manifest.get("runtime", {})
    if runtime != {
        "heap_required": False,
        "payloads_are_runtime_native": True,
        "runtime_decompression_required": False,
    }:
        errors.append(f"manifest runtime contract mismatch: {runtime!r}")
    manifest_font10 = manifest.get("font10")
    if not isinstance(manifest_font10, dict):
        errors.append("manifest is missing mandatory FONT10 metadata")
    else:
        font10_summary = manifest_font10.get("font10")
        chunk_summary = manifest_font10.get("pack_chunk")
        if not isinstance(font10_summary, dict):
            errors.append("manifest FONT10 metadata is invalid")
        else:
            metrics = font10_summary.get("metrics", {})
            if metrics.get("cell_width") != 10 or metrics.get("cell_height") != 10:
                errors.append("manifest FONT10 cell geometry is not 10x10")
            if font_chunks.get(1, (0, 0))[0] != font10_summary.get("bytes"):
                errors.append("NOR FONT10 size differs from its manifest")
        if chunk_summary != {
            "archive": "FONT",
            "chunk_id": 1,
            "format": "FONT10",
            "format_id": builder.FORMAT_FONT10,
        }:
            errors.append("manifest FONT10 chunk placement/format mismatch")

    for key, path in (("nor", nor_path), ("tf", tf_path)):
        pack_manifest = manifest.get("packs", {}).get(key, {})
        if pack_manifest.get("size") != path.stat().st_size:
            errors.append(f"manifest {key} pack size mismatch")
        header = path.read_bytes()[: common.PACK_HEADER_BYTES]
        if pack_manifest.get("pack_set_id") != struct.unpack_from(
            "<I", header, 20
        )[0]:
            errors.append(f"manifest {key} pack-set ID mismatch")
        if pack_manifest.get("crc32") != struct.unpack_from("<I", header, 28)[0]:
            errors.append(f"manifest {key} CRC32 mismatch")
        if pack_manifest.get("archives") != music_layout.pack_names[key]:
            errors.append(f"manifest {key} archive list mismatch")

    nor_manifest = manifest.get("packs", {}).get("nor", {})
    mus_summary = find_archive_summary(nor_manifest, "MUS")
    if mus_summary.get("chunk_count") != MUS_CHUNK_COUNT:
        errors.append("manifest MUS chunk count mismatch")
    if mus_summary.get("payload_bytes") != MUS_PAYLOAD_BYTES:
        errors.append("manifest MUS payload-byte total mismatch")
    if mus_summary.get("max_payload_bytes") != MUS_MAX_TRACK_BYTES:
        errors.append("manifest MUS maximum track size mismatch")
    selection = nor_manifest.get("chunk_selection", {}).get("MUS", {})
    if set(selection.get("present_chunk_ids", [])) != SOURCE_SLOT_IDS:
        errors.append("manifest MUS full-slot selection mismatch")
    if selection.get("absent_chunk_count") != 0:
        errors.append("manifest MUS archive is not complete")
    if selection.get("source_chunk_count") != MUS_CHUNK_COUNT:
        errors.append("manifest MUS source chunk count mismatch")
    if selection.get("absent_payload_bytes_are_zero") is not True:
        errors.append("manifest does not confirm zero-sized absent MUS chunks")

    data_dir = Path(manifest.get("data_dir", ""))
    for source in manifest.get("source_files", []):
        source_path = data_dir / str(source.get("path", ""))
        if (
            not source_path.is_file()
            or source_path.stat().st_size != source.get("size")
            or common.sha256_file(source_path) != source.get("sha256")
        ):
            errors.append(f"manifest source mismatch: {source_path}")

    metrics = {
        "nor_bytes": nor_path.stat().st_size,
        "nor_headroom": common.NOR_BYTES - nor_path.stat().st_size,
        "nor_toc_bytes": nor_toc,
        "tf_bytes": tf_path.stat().st_size,
        "tf_toc_bytes": tf_toc,
        "pack_set_id": nor_set_id,
        "mus_payload_bytes": sum(size for size, _fmt in mus.values()),
        "mus_max_track_bytes": max((size for size, _fmt in mus.values()), default=0),
    }
    return errors, metrics


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nor-pack", required=True, type=Path)
    parser.add_argument("--tf-pack", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--layout", required=True, type=Path)
    args = parser.parse_args()

    errors, metrics = check_profile(
        args.nor_pack.resolve(),
        args.tf_pack.resolve(),
        args.manifest.resolve(),
        args.layout.resolve(),
    )
    if metrics:
        print("Cardputer ADV extreme RIX-music pack contract")
        print(
            f"  NOR={metrics['nor_bytes']}/{common.NOR_BYTES}, "
            f"headroom={metrics['nor_headroom']}, TOC={metrics['nor_toc_bytes']}"
        )
        print(
            f"  TF={metrics['tf_bytes']}, TOC={metrics['tf_toc_bytes']}/"
            f"{common.TF_TOC_BYTES}, pack_set_id=0x{metrics['pack_set_id']:08x}"
        )
        print(
            f"  MUS={MUS_CHUNK_COUNT} original slots, tracks={len(TRACK_IDS)}, "
            f"payload={metrics['mus_payload_bytes']}, "
            f"max_track={metrics['mus_max_track_bytes']}"
        )
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print(
        "PASS: legacy sparse RIX pair; MIDI/VOC/SFX remain excluded from "
        "NOR/pal_tf.pak (the separate complete mirror owns PCM8 SFX)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
