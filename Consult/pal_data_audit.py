#!/usr/bin/env python3
"""Deeper PAL dataset audit for embedded memory planning."""

from __future__ import annotations

import argparse
import collections
import struct
from pathlib import Path


EVENTOBJECT_SIZE = 32
SCENE_SIZE = 8
OBJECT_DOS_SIZE = 12
PALMAP_RUNTIME_SIZE = 65552


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def read_mkf(path: Path):
    data = path.read_bytes()
    first = u32(data, 0)
    count = (first - 4) // 4
    offsets = [u32(data, i * 4) for i in range(count + 1)]
    chunks = []
    for index in range(count):
        chunk = data[offsets[index] : offsets[index + 1]]
        decomp = None
        kind = "raw"
        if len(chunk) >= 8 and chunk[:4] == b"YJ_1":
            kind = "YJ1"
            decomp = u32(chunk, 4)
        chunks.append({"index": index, "data": chunk, "size": len(chunk), "kind": kind, "decomp": decomp})
    return chunks


def chunk_runtime_size(chunks, index: int) -> int:
    if index < 0 or index >= len(chunks):
        return 0
    chunk = chunks[index]
    return chunk["decomp"] or chunk["size"]


def scene_rows(sss):
    scenes = sss[1]["data"]
    return [(u16(scenes, i * SCENE_SIZE), u16(scenes, i * SCENE_SIZE + 6)) for i in range(len(scenes) // SCENE_SIZE)]


def analyze_scenes(sss, mgo, gop):
    events = sss[0]["data"]
    rows = scene_rows(sss)
    results = []
    for scene_index in range(len(rows) - 1):
        map_num, start = rows[scene_index]
        end = rows[scene_index + 1][1]
        sprite_ids = []
        for event_index in range(start, end):
            offset = event_index * EVENTOBJECT_SIZE
            if offset + 18 > len(events):
                continue
            sprite = u16(events, offset + 16)
            if sprite:
                sprite_ids.append(sprite)

        all_sum = sum(chunk_runtime_size(mgo, sprite) for sprite in sprite_ids)
        unique_ids = sorted(set(sprite_ids))
        unique_sum = sum(chunk_runtime_size(mgo, sprite) for sprite in unique_ids)
        gop_size = chunk_runtime_size(gop, map_num)
        results.append(
            {
                "scene": scene_index + 1,
                "map": map_num,
                "events": end - start,
                "sprite_refs": len(sprite_ids),
                "unique_sprites": len(unique_ids),
                "all_sprite_bytes": all_sum,
                "unique_sprite_bytes": unique_sum,
                "duplicate_savings": all_sum - unique_sum,
                "gop_bytes": gop_size,
                "resident_current": PALMAP_RUNTIME_SIZE + gop_size + all_sum,
                "resident_dedup": PALMAP_RUNTIME_SIZE + gop_size + unique_sum,
            }
        )
    return results


def analyze_battles(sss, data_mkf, abc):
    object_data = sss[2]["data"]
    teams = data_mkf[2]["data"]
    results = []
    for team_index in range(len(teams) // 10):
        object_ids = [u16(teams, team_index * 10 + i * 2) for i in range(5)]
        enemy_ids = []
        for object_id in object_ids:
            offset = object_id * OBJECT_DOS_SIZE
            if object_id and offset + 2 <= len(object_data):
                enemy_ids.append(u16(object_data, offset))
        all_sum = sum(chunk_runtime_size(abc, enemy_id) for enemy_id in enemy_ids)
        unique_sum = sum(chunk_runtime_size(abc, enemy_id) for enemy_id in sorted(set(enemy_ids)))
        if all_sum:
            results.append(
                {
                    "team": team_index,
                    "enemy_refs": len(enemy_ids),
                    "unique_enemy_sprites": len(set(enemy_ids)),
                    "all_enemy_sprite_bytes": all_sum,
                    "unique_enemy_sprite_bytes": unique_sum,
                    "duplicate_savings": all_sum - unique_sum,
                    "enemy_ids": enemy_ids,
                }
            )
    return results


def analyze_rng(rng):
    results = []
    for movie in rng:
        data = movie["data"]
        if len(data) < 4:
            continue
        frame_count = (u32(data, 0) - 4) // 4
        if frame_count <= 0:
            continue
        offsets = [u32(data, i * 4) for i in range(frame_count + 1)]
        frames = []
        for frame in range(frame_count):
            chunk = data[offsets[frame] : offsets[frame + 1]]
            decomp = u32(chunk, 4) if len(chunk) >= 8 and chunk[:4] == b"YJ_1" else None
            frames.append((len(chunk), decomp))
        results.append(
            {
                "movie": movie["index"],
                "outer_bytes": movie["size"],
                "frames": len(frames),
                "max_frame_comp": max(size for size, _ in frames),
                "max_frame_decomp": max((decomp or 0) for _, decomp in frames),
                "total_frame_comp": sum(size for size, _ in frames),
                "total_frame_decomp": sum((decomp or 0) for _, decomp in frames),
            }
        )
    return results


def analyze_text(data_dir: Path, sss):
    word_path = data_dir / "WORD.DAT"
    msg_path = data_dir / "M.MSG"
    if not word_path.exists() or not msg_path.exists():
        return {}

    word = word_path.read_bytes()
    msg = msg_path.read_bytes()
    offsets_data = sss[3]["data"]
    offsets = [u32(offsets_data, i * 4) for i in range(len(offsets_data) // 4)]

    combined = word + msg
    enc_stats = {}
    for encoding in ["cp950", "big5", "gbk"]:
        decoded = combined.decode(encoding, errors="replace")
        enc_stats[encoding] = decoded.count("\ufffd")
    encoding = min(enc_stats, key=enc_stats.get)

    chars = []
    word_count = 0
    for offset in range(0, len(word), 10):
        raw = word[offset : offset + 10].rstrip(b" \x00")
        if raw:
            word_count += 1
            chars.extend(raw.decode(encoding, errors="ignore"))

    msg_count = 0
    for index in range(len(offsets) - 1):
        start, end = offsets[index], offsets[index + 1]
        if 0 <= start <= end <= len(msg):
            msg_count += 1
            chars.extend(msg[start:end].decode(encoding, errors="ignore"))

    unique = {char for char in chars if char not in "\x00\r\n"}
    counter = collections.Counter(chars)
    return {
        "encoding": encoding,
        "encoding_errors": enc_stats,
        "word_entries": len(word) // 10,
        "nonempty_word_entries": word_count,
        "message_entries": msg_count,
        "unique_chars": len(unique),
        "unique_chars_no_space": len(unique - {" "}),
        "estimated_glyph_bytes_32": len(unique) * 32,
        "top_chars": counter.most_common(12),
    }


def analyze_audio_files(data_dir: Path):
    extensions = [".ogg", ".opus", ".mp3", ".wav", ".mid", ".avi"]
    loose = []
    for path in data_dir.rglob("*"):
        if path.is_file() and path.suffix.lower() in extensions:
            loose.append((path.name, path.stat().st_size))
    containers = []
    for name in ["MUS.MKF", "MIDI.MKF", "VOC.MKF", "SOUNDS.MKF"]:
        matches = list(data_dir.glob(name))
        matches.extend(data_dir.glob(name.lower()))
        for path in matches:
            containers.append((path.name, path.stat().st_size))
    return {"loose_media": sorted(set(loose)), "containers": sorted(set(containers))}


def print_table(title, rows, columns):
    print(f"\n## {title}\n")
    print("| " + " | ".join(label for label, _ in columns) + " |")
    print("| " + " | ".join("---" for _ in columns) + " |")
    for row in rows:
        print("| " + " | ".join(str(row[key]) for _, key in columns) + " |")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data_dir", type=Path)
    parser.add_argument("--top", type=int, default=10)
    args = parser.parse_args()

    data_dir = args.data_dir
    sss = read_mkf(data_dir / "SSS.MKF")
    data_mkf = read_mkf(data_dir / "DATA.MKF")
    mgo = read_mkf(data_dir / "MGO.MKF")
    gop = read_mkf(data_dir / "GOP.MKF")
    abc = read_mkf(data_dir / "ABC.MKF")
    f = read_mkf(data_dir / "F.MKF")
    fire = read_mkf(data_dir / "FIRE.MKF")
    rng = read_mkf(data_dir / "RNG.MKF")

    print(f"# PAL Real Data Audit\n\nData path: `{data_dir}`")
    print("\n## Structured Data\n")
    print(f"- Scenes: {len(sss[1]['data']) // SCENE_SIZE}")
    print(f"- Event objects: {len(sss[0]['data']) // EVENTOBJECT_SIZE}")
    print(f"- DOS object records: {len(sss[2]['data']) // OBJECT_DOS_SIZE}")
    print(f"- Script entries: {len(sss[4]['data']) // 8}")
    print(f"- Global allocated table bytes: {sum(len(sss[i]['data']) for i in [0, 4]) + sum(len(data_mkf[i]['data']) for i in [0, 1, 2, 4, 5, 6])}")
    print(f"- DATA.MKF #9 UI sprite bytes: {len(data_mkf[9]['data'])}")
    print(f"- DATA.MKF #10 battle effect sprite bytes: {len(data_mkf[10]['data'])}")

    scenes = analyze_scenes(sss, mgo, gop)
    print_table(
        "Worst Current Scene Residency",
        sorted(scenes, key=lambda row: row["resident_current"], reverse=True)[: args.top],
        [
            ("scene", "scene"),
            ("map", "map"),
            ("events", "events"),
            ("sprite refs", "sprite_refs"),
            ("unique sprites", "unique_sprites"),
            ("all sprite bytes", "all_sprite_bytes"),
            ("dedup sprite bytes", "unique_sprite_bytes"),
            ("savings", "duplicate_savings"),
            ("current resident", "resident_current"),
            ("dedup resident", "resident_dedup"),
        ],
    )

    battles = analyze_battles(sss, data_mkf, abc)
    print_table(
        "Worst Battle Enemy Sprite Residency",
        sorted(battles, key=lambda row: row["all_enemy_sprite_bytes"], reverse=True)[: args.top],
        [
            ("team", "team"),
            ("enemy refs", "enemy_refs"),
            ("unique enemy sprites", "unique_enemy_sprites"),
            ("all sprite bytes", "all_enemy_sprite_bytes"),
            ("dedup sprite bytes", "unique_enemy_sprite_bytes"),
            ("savings", "duplicate_savings"),
            ("enemy ids", "enemy_ids"),
        ],
    )

    rng_rows = analyze_rng(rng)
    print_table(
        "RNG Movie Frame Stats",
        sorted(rng_rows, key=lambda row: row["max_frame_comp"], reverse=True),
        [
            ("movie", "movie"),
            ("outer bytes", "outer_bytes"),
            ("frames", "frames"),
            ("max frame comp", "max_frame_comp"),
            ("max frame decomp", "max_frame_decomp"),
            ("total frame comp", "total_frame_comp"),
            ("total frame decomp", "total_frame_decomp"),
        ],
    )

    text = analyze_text(data_dir, sss)
    print("\n## Text and Glyphs\n")
    for key in ["encoding", "encoding_errors", "word_entries", "nonempty_word_entries", "message_entries", "unique_chars", "unique_chars_no_space", "estimated_glyph_bytes_32"]:
        print(f"- {key}: {text[key]}")
    print("- top_chars: " + ", ".join(f"{repr(char)}:{count}" for char, count in text["top_chars"]))

    print("\n## Largest Single Runtime Chunks\n")
    for name, chunks in [("MGO", mgo), ("ABC", abc), ("F", f), ("FIRE", fire)]:
        chunk = max(chunks, key=lambda row: row["decomp"] or row["size"])
        print(f"- {name}: #{chunk['index']} comp={chunk['size']} runtime={chunk['decomp'] or chunk['size']} kind={chunk['kind']}")

    audio = analyze_audio_files(data_dir)
    print("\n## Audio and Video Files\n")
    print(f"- loose .ogg/.opus/.mp3/.wav/.mid/.avi files: {audio['loose_media']}")
    print(f"- present audio containers: {audio['containers']}")


if __name__ == "__main__":
    main()
