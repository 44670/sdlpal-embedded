#!/usr/bin/env python3
"""Integration test for the Cardputer extreme tagged/atomic save path."""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile
import zlib


MAGIC = b"PALXSAVE"
HEADER_BYTES = 48
VERSION = 1
PROFILE = 0x32435A53  # "SZC2"
FORMAT_DOS = 1
EVENT_COUNT = 424  # 423 contiguous chapter events + sparse event 5334
EVENT_BYTES = EVENT_COUNT * 32


def put_u16(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", data, offset, value)


def put_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value)


def refresh_checksums(data: bytearray, payload: bool) -> None:
    if payload:
        put_u32(data, 36, zlib.crc32(data[HEADER_BYTES:]) & 0xFFFFFFFF)
    put_u32(data, 44, zlib.crc32(data[:44]) & 0xFFFFFFFF)


def validate_save(data: bytes) -> dict[str, int]:
    if len(data) < HEADER_BYTES or data[:8] != MAGIC:
        raise AssertionError("missing Cardputer extreme save magic")
    (
        version,
        header_bytes,
        profile,
        save_format,
        flags,
        fixed_bytes,
        event_count,
        event_bytes,
        total_bytes,
        payload_crc,
        scene,
        party,
        header_crc,
    ) = struct.unpack_from("<HHIHHIIIIIHHI", data, 8)
    expected = {
        "version": VERSION,
        "header_bytes": HEADER_BYTES,
        "profile": PROFILE,
        "save_format": FORMAT_DOS,
        "flags": 0,
        "event_count": EVENT_COUNT,
        "event_bytes": EVENT_BYTES,
        "total_bytes": len(data),
        "payload_crc": zlib.crc32(data[HEADER_BYTES:]) & 0xFFFFFFFF,
        "header_crc": zlib.crc32(data[:44]) & 0xFFFFFFFF,
    }
    actual = {
        "version": version,
        "header_bytes": header_bytes,
        "profile": profile,
        "save_format": save_format,
        "flags": flags,
        "event_count": event_count,
        "event_bytes": event_bytes,
        "total_bytes": total_bytes,
        "payload_crc": payload_crc,
        "header_crc": header_crc,
    }
    for key, value in expected.items():
        if actual[key] != value:
            raise AssertionError(
                f"{key}: expected {value:#x}, got {actual[key]:#x}"
            )
    if fixed_bytes + event_bytes + header_bytes != total_bytes:
        raise AssertionError("header lengths do not sum to exact file length")
    if not (scene == 22 or 1 <= scene <= 20):
        raise AssertionError(f"scene {scene} is outside the SZC1 profile")
    if party >= 3:
        raise AssertionError(f"party index {party} is outside 0..2")
    return {
        "fixed_bytes": fixed_bytes,
        "scene": scene,
        "party": party,
        "total_bytes": total_bytes,
    }


INIT_RE = re.compile(r"\binit\b.*\bscene=(\d+)\b.*\bstate=([0-9a-f]+)\b.*\bslot=5\b")


class Runner:
    def __init__(
        self,
        binary: pathlib.Path,
        nor: pathlib.Path,
        tf: pathlib.Path,
        data_dir: pathlib.Path,
        route: pathlib.Path,
    ) -> None:
        self.binary = binary.resolve()
        self.nor = nor.resolve()
        self.tf = tf.resolve()
        self.data_dir = data_dir.resolve()
        self.route = route.resolve()
        self.sequence = 0
        self.last_event_path: pathlib.Path | None = None

    def run(
        self,
        save_dir: pathlib.Path,
        *,
        save: bool = False,
        save_times: int = 77,
        sparse_state: int | None = None,
        reload: bool = False,
        failure: str | None = None,
    ) -> tuple[int | None, str | None]:
        self.sequence += 1
        label = f"run-{self.sequence:02d}"
        event_path = save_dir / f"{label}.event"
        self.last_event_path = event_path
        env = os.environ.copy()
        env.update(
            {
                "PAL_CORES3SE_NATIVE_NOR_PACK": str(self.nor),
                "PAL_CORES3SE_NATIVE_TF_PACK": str(self.tf),
                "PAL_CORES3SE_NATIVE_SAVE_DIR": str(save_dir),
                "PAL_CORES3SE_NATIVE_SCREENSHOT_FRAME": "999999",
                "PAL_CORES3SE_NATIVE_MAX_PRESENTS": "0",
                "PAL_DETERMINISTIC_REPLAY": str(self.route),
                "PAL_DETERMINISTIC_CHECKPOINTS": str(save_dir / f"{label}.trace"),
                "PAL_DETERMINISTIC_SCRIPT_TRACE": str(save_dir / f"{label}.script"),
                "PAL_DETERMINISTIC_EVENT_TRACE": str(event_path),
                "PAL_DETERMINISTIC_MAX_PRESENTS": "900" if reload else "720",
            }
        )
        for key in (
            "PAL_DETERMINISTIC_SAVE_FRAME",
            "PAL_DETERMINISTIC_SAVE_SLOT",
            "PAL_DETERMINISTIC_SAVE_TIMES",
            "PAL_DETERMINISTIC_SPARSE_EVENT_5334_STATE",
            "PAL_DETERMINISTIC_RELOAD_FRAME",
            "PAL_DETERMINISTIC_RELOAD_SLOT",
            "PAL_CARDPUTER_NATIVE_SAVE_FAIL",
        ):
            env.pop(key, None)
        if save:
            env.update(
                {
                    "PAL_DETERMINISTIC_SAVE_FRAME": "650",
                    "PAL_DETERMINISTIC_SAVE_SLOT": "5",
                    "PAL_DETERMINISTIC_SAVE_TIMES": str(save_times),
                }
            )
            if sparse_state is not None:
                env["PAL_DETERMINISTIC_SPARSE_EVENT_5334_STATE"] = str(
                    sparse_state
                )
        if reload:
            env.update(
                {
                    "PAL_DETERMINISTIC_RELOAD_FRAME": "650",
                    "PAL_DETERMINISTIC_RELOAD_SLOT": "5",
                }
            )
        if failure is not None:
            env["PAL_CARDPUTER_NATIVE_SAVE_FAIL"] = failure

        result = subprocess.run(
            [str(self.binary)],
            cwd=self.data_dir,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=15,
            check=False,
        )
        if result.returncode not in (0, 1):
            raise AssertionError(
                f"{label}: engine returned {result.returncode}\n{result.stdout}"
            )

        if not reload:
            return None, None
        matches = [
            INIT_RE.search(line)
            for line in event_path.read_text(encoding="utf-8").splitlines()
        ]
        matches = [match for match in matches if match is not None]
        if not matches:
            raise AssertionError(f"{label}: no slot-5 init event")
        return int(matches[-1].group(1)), matches[-1].group(2)


def write_variant(directory: pathlib.Path, source: bytes, mutate) -> None:
    directory.mkdir()
    data = bytearray(source)
    mutate(data)
    (directory / "5.rpg").write_bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=pathlib.Path, required=True)
    parser.add_argument("--nor", type=pathlib.Path, required=True)
    parser.add_argument("--tf", type=pathlib.Path, required=True)
    parser.add_argument(
        "--data-dir", type=pathlib.Path, default=pathlib.Path("/mnt/hgfs/deb13/PAL")
    )
    parser.add_argument(
        "--route",
        type=pathlib.Path,
        default=pathlib.Path(__file__).parents[2]
        / "tools"
        / "deterministic_routes"
        / "newgame_walk.route",
    )
    args = parser.parse_args()
    runner = Runner(args.binary, args.nor, args.tf, args.data_dir, args.route)

    with tempfile.TemporaryDirectory(prefix="pal-extreme-save-test-") as temp_text:
        temp = pathlib.Path(temp_text)
        generated = temp / "generated"
        generated.mkdir()
        runner.run(generated, save=True, sparse_state=9)
        if runner.last_event_path is None:
            raise AssertionError("save run did not produce an event trace path")
        first_save_events = runner.last_event_path.read_text(encoding="utf-8")
        if (
            "saved_times=77 menu_saved_times=77 sparse5334=9"
            not in first_save_events
        ):
            raise AssertionError("menu save counter did not decode the tagged envelope")
        save_path = generated / "5.rpg"
        original = save_path.read_bytes()
        info = validate_save(original)
        if (generated / "5.tmp").exists() or (generated / "5.bak").exists():
            raise AssertionError("successful save left a transaction sidecar")

        valid_scene, valid_state = runner.run(generated, reload=True)
        if valid_scene != info["scene"]:
            raise AssertionError("tagged save did not restore its scene")
        if runner.last_event_path is None or (
            "init" not in runner.last_event_path.read_text(encoding="utf-8")
            or "sparse5334=9"
            not in runner.last_event_path.read_text(encoding="utf-8")
        ):
            raise AssertionError("sparse event 5334 did not survive restart/reload")

        corrupt = temp / "corrupt"
        write_variant(corrupt, original, lambda data: data.__setitem__(12, data[12] ^ 1))
        fallback_scene, fallback_state = runner.run(corrupt, reload=True)
        if fallback_state == valid_state:
            raise AssertionError("corrupt profile was accepted")

        def mutate_u16(offset: int, value: int, payload: bool = False):
            def apply(data: bytearray) -> None:
                put_u16(data, offset, value)
                refresh_checksums(data, payload)

            return apply

        def mutate_u32(offset: int, value: int):
            def apply(data: bytearray) -> None:
                put_u32(data, offset, value)
                refresh_checksums(data, False)

            return apply

        def mutate_scene(data: bytearray) -> None:
            put_u16(data, 40, 21)
            put_u16(data, HEADER_BYTES + 8, 21)
            refresh_checksums(data, True)

        def mutate_party(data: bytearray) -> None:
            put_u16(data, 42, 3)
            put_u16(data, HEADER_BYTES + 6, 3)
            refresh_checksums(data, True)

        def mutate_role(data: bytearray) -> None:
            put_u16(data, HEADER_BYTES + 44, 6)
            refresh_checksums(data, True)

        def mutate_payload(data: bytearray) -> None:
            data[HEADER_BYTES + 100] ^= 1

        def mutate_trailing(data: bytearray) -> None:
            data.append(0)

        variants = {
            "version": mutate_u16(8, VERSION + 1),
            "profile": mutate_u32(12, PROFILE ^ 1),
            "format": mutate_u16(16, 2),
            "event-count": mutate_u32(24, EVENT_COUNT - 1),
            "trailing-byte": mutate_trailing,
            "payload-crc": mutate_payload,
            "scene-bound": mutate_scene,
            "party-bound": mutate_party,
            "role-bound": mutate_role,
        }
        for name, mutate in variants.items():
            directory = temp / name
            write_variant(directory, original, mutate)
            _, state = runner.run(directory, reload=True)
            if state != fallback_state:
                raise AssertionError(f"{name} variant was accepted")

        recovery = temp / "recovery"
        recovery.mkdir()
        (recovery / "5.rpg").write_bytes((temp / "corrupt" / "5.rpg").read_bytes())
        (recovery / "5.bak").write_bytes(original)
        recovered_scene, recovered_state = runner.run(recovery, reload=True)
        if (recovered_scene, recovered_state) != (valid_scene, valid_state):
            raise AssertionError("valid backup did not recover a corrupt final")

        recovery_failure = temp / "recovery-save-failure"
        recovery_failure.mkdir()
        (recovery_failure / "5.rpg").write_bytes(
            (temp / "corrupt" / "5.rpg").read_bytes()
        )
        (recovery_failure / "5.bak").write_bytes(original)
        runner.run(
            recovery_failure,
            save=True,
            save_times=88,
            failure="rename_temp_final",
        )
        if not (recovery_failure / "5.bak").is_file():
            raise AssertionError("failed recovery save discarded the only valid backup")
        recovered_scene_2, recovered_state_2 = runner.run(
            recovery_failure, reload=True
        )
        if (recovered_scene_2, recovered_state_2) != (valid_scene, valid_state):
            raise AssertionError("failed recovery save lost the valid backup state")

        overwrite = temp / "overwrite"
        overwrite.mkdir()
        (overwrite / "5.rpg").write_bytes(original)
        runner.run(overwrite, save=True, save_times=88)
        overwritten = (overwrite / "5.rpg").read_bytes()
        validate_save(overwritten)
        if struct.unpack_from("<H", overwritten, HEADER_BYTES)[0] != 88:
            raise AssertionError("overwrite reused old scratch state instead of new save data")
        if (overwrite / "5.bak").read_bytes() != original:
            raise AssertionError("successful overwrite did not retain the prior valid save")
        if runner.last_event_path is None or (
            "saved_times=88 menu_saved_times=88"
            not in runner.last_event_path.read_text(encoding="utf-8")
        ):
            raise AssertionError("menu save counter did not observe overwritten save")

        for failure in ("rename_final_backup", "rename_temp_final"):
            directory = temp / failure
            directory.mkdir()
            (directory / "5.rpg").write_bytes(original)
            before = (directory / "5.rpg").read_bytes()
            runner.run(directory, save=True, save_times=88, failure=failure)
            if (directory / "5.rpg").read_bytes() != before:
                raise AssertionError(f"{failure} changed the committed save")
            if (directory / "5.tmp").exists():
                raise AssertionError(f"{failure} left a temporary file")

        write_open_failure = temp / "write_open_failure"
        write_open_failure.mkdir()
        (write_open_failure / "5.rpg").write_bytes(original)
        (write_open_failure / "5.tmp").mkdir()
        runner.run(write_open_failure, save=True, save_times=88)
        if (write_open_failure / "5.rpg").read_bytes() != original:
            raise AssertionError("temporary-file open failure changed the committed save")

        unlink_failure = temp / "unlink_backup"
        unlink_failure.mkdir()
        (unlink_failure / "5.rpg").write_bytes(original)
        (unlink_failure / "5.bak").write_bytes(original)
        runner.run(
            unlink_failure,
            save=True,
            save_times=88,
            failure="unlink_backup",
        )
        if (unlink_failure / "5.rpg").read_bytes() != original:
            raise AssertionError("unlink_backup changed the committed save")
        if (unlink_failure / "5.bak").read_bytes() != original:
            raise AssertionError("unlink_backup discarded the recovery save")
        if (unlink_failure / "5.tmp").exists():
            raise AssertionError("unlink_backup left a temporary file")

        legacy_results: list[tuple[str, int, str]] = []
        first_legacy: pathlib.Path | None = None
        for legacy_name in ("1.rpg", "2.rpg"):
            legacy_source = args.data_dir / legacy_name
            if not legacy_source.is_file():
                continue
            if first_legacy is None:
                first_legacy = legacy_source
            directory = temp / f"legacy-{legacy_name[0]}"
            directory.mkdir()
            shutil.copyfile(legacy_source, directory / "5.rpg")
            scene, state = runner.run(directory, reload=True)
            if state == fallback_state:
                raise AssertionError(f"legacy DOS {legacy_name} was not imported")
            legacy_results.append((legacy_name, scene or 0, state or ""))

        if first_legacy is not None:
            legacy_recovery = temp / "legacy-recovery-save-failure"
            legacy_recovery.mkdir()
            shutil.copyfile(first_legacy, legacy_recovery / "5.rpg")
            (legacy_recovery / "5.bak").write_bytes(original)
            runner.run(
                legacy_recovery,
                save=True,
                save_times=91,
                failure="rename_temp_final",
            )
            if (legacy_recovery / "5.bak").read_bytes() != original:
                raise AssertionError(
                    "legacy conversion replaced a checksum-protected backup"
                )
            recovered_scene_3, recovered_state_3 = runner.run(
                legacy_recovery, reload=True
            )
            if (recovered_scene_3, recovered_state_3) != (
                valid_scene,
                valid_state,
            ):
                raise AssertionError(
                    "failed legacy conversion lost the tagged recovery save"
                )

        print(
            "PASS cardputer extreme save:",
            f"bytes={info['total_bytes']}",
            f"fixed={info['fixed_bytes']}",
            f"events={EVENT_COUNT}",
            f"scene={valid_scene}",
            f"state={valid_state}",
            f"fallback={fallback_state}",
            f"mutations={len(variants)}",
            "atomic_failures=6",
            f"legacy={legacy_results}",
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
