#!/usr/bin/env python3
"""Check the CoreS3 SE bring-up artifact against the embedded contract."""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
import struct
import subprocess
import sys
from pathlib import Path


PACK_MAGIC = 0x4B504C50
PACK_HEADER_SIZE = 32
PACK_ARCHIVE_ENTRY_SIZE = 12
PACK_CHUNK_ENTRY_SIZE = 16
PACK_CHUNK_F_COMPRESSED = 0x0001
PAL_NOR_PARTITION_BYTES = 0xB00000
PAL_NOR_SOFT_LIMIT_PERCENT = 97
PAL_NOR_SOFT_LIMIT_BYTES = PAL_NOR_PARTITION_BYTES * PAL_NOR_SOFT_LIMIT_PERCENT // 100
SRAM_BUDGET = 300 * 1024
PSRAM_BUDGET = 8 * 1024 * 1024
DRAM_STATIC_BUDGET = 300 * 1024
STACK_STATIC_FRAME_BUDGET = 4096
BUFFER_SYMBOL_REGISTRY = Path("esp32s3/buffer_symbols.json")
FORBIDDEN_TARGET_SOURCE_NAMES = {
    "pal_audio_static.c",
    "pal_script_static.c",
    "pal_sfx_cache.c",
}
FORBIDDEN_TARGET_AUDIO_PREFIXES = (
    "pal_audio_",
    "pal_sfx_",
)
FORBIDDEN_TARGET_AUDIO_SYMBOLS = {
    "pal_sram_audio",
    "pal_psram_sfx_bank",
}
REQUIRED_CMAKE_DEFINITIONS = {
    "PAL_ESP_CORES3SE_NO_AUDIO=1",
    "PAL_ESP_CORES3SE_NO_SFX=1",
}
SCAFFOLD_SOURCE_FILES = (
    "esp32s3/main/app_main.c",
    "esp32s3/main/pal_save_fatfs.c",
    "esp32s3/main/pal_save_fatfs.h",
)
FORBIDDEN_SCAFFOLD_SOURCE = (
    ("target-side trigger script execution", re.compile(r"\bPAL_RunTriggerScript\s*\(")),
    ("target-side auto script execution", re.compile(r"\bPAL_RunAutoScript\s*\(")),
    ("target-side dialog runtime", re.compile(r"\bPAL_(?:StartDialog|ShowDialogText)\s*\(")),
    ("target-side battle runtime", re.compile(r"\bPAL_(?:StartBattle|Battle)\s*\(")),
    ("target-side gameplay movement", re.compile(r"\b(?:move_party_direction|update_demo_viewport)\s*\(")),
    ("target-side gameplay collision", re.compile(r"\b(?:map_tile_blocked|map_position_blocked|event_position_blocked|viewport_party_position_blocked)\s*\(")),
    ("target-side walking state machine", re.compile(r"\bpal_player_walking\s*=\s*true\b")),
    ("target-side movement constants", re.compile(r"\bDEMO_(?:STEP|TOUCH_DEADZONE|PARTY_SCREEN_)\w*\b")),
    ("target scaffold save write", re.compile(r"\bPalSaveFatFs_WriteFile\s*\(")),
    ("target scaffold full-save read", re.compile(r"\bPalSaveFatFs_ReadFile\s*\(")),
    ("target scaffold full-save slot state", re.compile(r"\bPalFatFsSaveSlot\b")),
    ("target scaffold save player roles", re.compile(r"\bSAVE_PLAYER_ROLES_OFFSET\b")),
    ("target scaffold save scene table", re.compile(r"\bSAVE_SCENES_OFFSET\b")),
    ("target scaffold save event objects", re.compile(r"\bSAVE_EVENT_OBJECTS_OFFSET\b")),
    ("target scaffold FatFS write", re.compile(r"\bf_write\s*\(")),
    ("target scaffold FatFS write-open flag", re.compile(r"\bFA_(?:WRITE|CREATE_ALWAYS|CREATE_NEW|_OPEN_APPEND)\b")),
    ("target scaffold PAL data write helper", re.compile(r"\bwrite_le(?:16|32)\s*\(")),
    ("target scaffold mutable event-object cast", re.compile(r"\(uint8_t\s*\*\)\s*pal_scene_event_objects")),
)

SOURCE_FILES = (
    "esp32s3/main/app_main.c",
    "esp32s3/main/cores3se_board.c",
    "esp32s3/main/cores3se_board.h",
    "esp32s3/main/cores3se_hw.h",
    "esp32s3/main/cores3se_memory.c",
    "esp32s3/main/cores3se_memory.h",
    "esp32s3/main/pal_save_fatfs.c",
    "esp32s3/main/pal_save_fatfs.h",
    "embedded/pal_battle_cache.c",
    "embedded/pal_battle_cache.h",
    "embedded/pal_dialog_static.c",
    "embedded/pal_dialog_static.h",
    "embedded/pal_ending_static.c",
    "embedded/pal_ending_static.h",
    "embedded/pal_font_cache.c",
    "embedded/pal_font_cache.h",
    "embedded/pal_global_cache.c",
    "embedded/pal_global_cache.h",
    "embedded/pal_menu_static.c",
    "embedded/pal_menu_static.h",
    "embedded/pal_music_cache.c",
    "embedded/pal_music_cache.h",
    "embedded/pal_memory.h",
    "embedded/pal_pack.c",
    "embedded/pal_pack.h",
    "embedded/pal_palette_static.c",
    "embedded/pal_palette_static.h",
    "embedded/pal_rng_cache.c",
    "embedded/pal_rng_cache.h",
    "embedded/pal_scene_cache.c",
    "embedded/pal_scene_cache.h",
    "embedded/pal_text_cache.c",
    "embedded/pal_text_cache.h",
    "embedded/pal_ui_cache.c",
    "embedded/pal_ui_cache.h",
    "embedded/pal_video_static.c",
    "embedded/pal_video_static.h",
)

TARGET_CMAKE = Path("esp32s3/main/CMakeLists.txt")

FORBIDDEN_SOURCE = (
    re.compile(r"\bmalloc\s*\("),
    re.compile(r"\bcalloc\s*\("),
    re.compile(r"\brealloc\s*\("),
    re.compile(r"\bfree\s*\("),
    re.compile(r"\bUTIL_malloc\s*\("),
    re.compile(r"\bUTIL_calloc\s*\("),
    re.compile(r"\bnew\b"),
    re.compile(r"\bdelete\b"),
    re.compile(r"\bPAL_MKFDecompressChunk\s*\("),
    re.compile(r"\bPAL_MKFGetDecompressedSize\s*\("),
    re.compile(r"\bYJ1_Decompress\s*\("),
    re.compile(r"\bYJ2_Decompress\s*\("),
    re.compile(r"\bDecompress\s*\("),
)

FORBIDDEN_LINKED_SYMBOLS = (
    "PAL_MKFDecompressChunk",
    "PAL_MKFGetDecompressedSize",
    "YJ1_Decompress",
    "YJ2_Decompress",
    "Decompress",
)

FORBIDDEN_PROJECT_UNDEFINED_SYMBOLS = FORBIDDEN_LINKED_SYMBOLS + (
    "malloc",
    "calloc",
    "realloc",
    "free",
    "_malloc_r",
    "_calloc_r",
    "_realloc_r",
    "_free_r",
    "__wrap_malloc",
    "__wrap_calloc",
    "__wrap_realloc",
    "__wrap_free",
    "_Znwm",
    "_Znam",
    "_ZdlPv",
    "_ZdaPv",
    "_ZdlPvm",
    "_ZdaPvm",
    "operator new",
    "operator delete",
)

FORBIDDEN_PROJECT_RELOC_SYMBOLS = set(FORBIDDEN_PROJECT_UNDEFINED_SYMBOLS)

KEY_SECTIONS = (
    ".iram0.text",
    ".dram0.data",
    ".dram0.bss",
    ".flash.text",
    ".flash.rodata",
    ".ext_ram.bss",
)

REQUIRED_CONFIG_VALUES = {
    "CONFIG_SPIRAM": "y",
    "CONFIG_SPIRAM_USE_MEMMAP": "y",
    "CONFIG_SPIRAM_USE_CAPS_ALLOC": "n",
    "CONFIG_SPIRAM_USE_MALLOC": "n",
    "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY": "y",
    "CONFIG_FATFS_LFN_NONE": "y",
    "CONFIG_FATFS_USE_DYN_BUFFERS": "n",
    "CONFIG_FATFS_ALLOC_PREFER_EXTRAM": "n",
}

REQUIRED_CONFIG_MIN_VALUES = {
    "CONFIG_ESP_MAIN_TASK_STACK_SIZE": 32768,
    "CONFIG_MAIN_TASK_STACK_SIZE": 32768,
}


def run(cmd: list[str]) -> str:
    return subprocess.check_output(cmd, text=True)


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def scan_sources(root: Path) -> list[str]:
    hits: list[str] = []
    for rel in SOURCE_FILES:
        path = root / rel
        text = path.read_text(errors="replace")
        for line_no, line in enumerate(text.splitlines(), 1):
            if any(pattern.search(line) for pattern in FORBIDDEN_SOURCE):
                hits.append(f"{rel}:{line_no}: {line.strip()}")
    return hits


def check_scaffold_freeze(root: Path) -> list[str]:
    hits: list[str] = []
    for rel in SCAFFOLD_SOURCE_FILES:
        path = root / rel
        text = path.read_text(errors="replace")
        for line_no, line in enumerate(text.splitlines(), 1):
            for label, pattern in FORBIDDEN_SCAFFOLD_SOURCE:
                if pattern.search(line):
                    hits.append(f"{rel}:{line_no}: {label}: {line.strip()}")
    return hits


def normalize_component_source(root: Path, raw: str) -> str:
    source = (root / "esp32s3/main" / raw).resolve()
    try:
        return source.relative_to(root).as_posix()
    except ValueError:
        return str(source)


def parse_component_sources(root: Path) -> list[str]:
    cmake = (root / TARGET_CMAKE).read_text(errors="replace")
    match = re.search(r"\bSRCS\b(?P<body>.*?)\bINCLUDE_DIRS\b", cmake, re.S)
    if match is None:
        return []
    return [
        normalize_component_source(root, item)
        for item in re.findall(r'"([^"]+\.(?:c|cpp|cc|cxx))"', match.group("body"))
    ]


def check_cmake_source_coverage(root: Path) -> list[str]:
    errors: list[str] = []
    expected = {rel for rel in SOURCE_FILES if rel.endswith((".c", ".cpp", ".cc", ".cxx"))}
    actual = set(parse_component_sources(root))

    if not actual:
        return [f"no target sources parsed from {TARGET_CMAKE}"]
    for rel in sorted(actual - expected):
        errors.append(f"target source linked but not contract-scanned: {rel}")
    for rel in sorted(expected - actual):
        errors.append(f"contract-scanned source missing from target CMake: {rel}")
    return errors


def find_function_body(text: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", text)
    if match is None:
        return ""

    depth = 0
    for index in range(match.end() - 1, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[match.start() : index + 1]
    return ""


def check_shared_spi_contract(root: Path) -> list[str]:
    errors: list[str] = []
    board_rel = "esp32s3/main/cores3se_board.c"
    hw_rel = "esp32s3/main/cores3se_hw.h"
    board = (root / board_rel).read_text(errors="replace")
    hw = (root / hw_rel).read_text(errors="replace")

    clock_match = re.search(r"\bTF_SPI_CLOCK_KHZ\s*=\s*(\d+)u?\s*;", board)
    if clock_match is None:
        errors.append(f"{board_rel}: TF_SPI_CLOCK_KHZ definition missing")
    elif int(clock_match.group(1)) > 20000:
        errors.append(f"{board_rel}: TF_SPI_CLOCK_KHZ={clock_match.group(1)}, expected <= 20000")

    required_board_snippets = (
        ("SDSPI host default", "SDSPI_HOST_DEFAULT()"),
        ("SDSPI device config default", "sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT()"),
        ("FatFS SDSPI mount", "esp_vfs_fat_sdspi_mount("),
        ("SDSPI owns TF CS", "slot_config.gpio_cs = CORES3SE_PIN_TF_CS"),
        ("SDSPI uses shared SPI host", "slot_config.host_id = LCD_HOST"),
        ("SDSPI host uses shared SPI bus", "host.slot = LCD_HOST"),
        ("LCD bus MISO is shared D/C pin", "bus_cfg.miso_io_num = CORES3SE_PIN_LCD_DC"),
    )
    for label, snippet in required_board_snippets:
        if snippet not in board:
            errors.append(f"{board_rel}: missing {label}: {snippet}")

    required_hw_snippets = (
        ("CoreS3 SE shared D/C-MISO pin", "#define CORES3SE_PIN_LCD_DC GPIO_NUM_35"),
        ("CoreS3 SE TF CS pin", "#define CORES3SE_PIN_TF_CS GPIO_NUM_4"),
    )
    for label, snippet in required_hw_snippets:
        if snippet not in hw:
            errors.append(f"{hw_rel}: missing {label}: {snippet}")

    tf_body = find_function_body(board, "CoreS3Se_PrepareTfAccess")
    if "gpio_set_direction(CORES3SE_PIN_LCD_DC, GPIO_MODE_INPUT)" not in tf_body:
        errors.append(f"{board_rel}: CoreS3Se_PrepareTfAccess must return shared D/C-MISO to input mode")

    lcd_body = find_function_body(board, "CoreS3Se_PrepareLcdAccess")
    if "gpio_set_direction(CORES3SE_PIN_LCD_DC, GPIO_MODE_OUTPUT)" not in lcd_body:
        errors.append(f"{board_rel}: CoreS3Se_PrepareLcdAccess must set shared D/C-MISO to LCD output mode")

    mount_body = find_function_body(board, "CoreS3Se_MountTf")
    if "CoreS3Se_PrepareTfAccess();" not in mount_body:
        errors.append(f"{board_rel}: CoreS3Se_MountTf must put shared pin in TF input mode before mount")
    if "CoreS3Se_PrepareLcdAccess();" in mount_body:
        errors.append(f"{board_rel}: CoreS3Se_MountTf must not leave the shared pin in LCD output mode")

    flush_body = find_function_body(board, "CoreS3Se_FlushPalFramebuffer")
    if "CoreS3Se_PrepareLcdAccess();" not in flush_body or "CoreS3Se_PrepareTfAccess();" not in flush_body:
        errors.append(f"{board_rel}: framebuffer flush must bracket LCD transfers and restore TF input mode")
    if "esp_lcd_panel_io_tx_param(lcd_io, -1, NULL, 0)" not in flush_body:
        errors.append(f"{board_rel}: framebuffer flush must wait for LCD idle before restoring TF input mode")

    for rel in SOURCE_FILES:
        path = root / rel
        if not path.is_file():
            continue
        text = path.read_text(errors="replace")
        for line_no, line in enumerate(text.splitlines(), 1):
            if re.search(r"\bgpio_set_level\s*\(\s*CORES3SE_PIN_TF_CS\b", line):
                errors.append(f"{rel}:{line_no}: manual TF CS toggle: {line.strip()}")
    return errors


def check_cores3se_board_init_contract(root: Path) -> list[str]:
    errors: list[str] = []
    board_rel = "esp32s3/main/cores3se_board.c"
    hw_rel = "esp32s3/main/cores3se_hw.h"
    board = (root / board_rel).read_text(errors="replace")
    hw = (root / hw_rel).read_text(errors="replace")

    required_hw_snippets = (
        ("AW9523 address", "#define CORES3SE_AW9523_ADDR 0x58u"),
        ("AW9523 id", "#define CORES3SE_AW9523_EXPECTED_ID 0x23u"),
        ("AXP2101 address", "#define CORES3SE_AXP2101_ADDR 0x34u"),
        ("FT6336 address", "#define CORES3SE_TOUCH_ADDR 0x38u"),
        ("touch INT pin", "#define CORES3SE_PIN_TOUCH_INT GPIO_NUM_21"),
    )
    for label, snippet in required_hw_snippets:
        if snippet not in hw:
            errors.append(f"{hw_rel}: missing CoreS3 SE {label}: {snippet}")

    required_board_snippets = (
        ("AW9523 add/read", "add_i2c_device(CORES3SE_AW9523_ADDR"),
        ("AW9523 id validation", "id != CORES3SE_AW9523_EXPECTED_ID"),
        ("AXP2101 add", "add_i2c_device(CORES3SE_AXP2101_ADDR"),
        ("FT6336 add", "add_i2c_device(CORES3SE_TOUCH_ADDR"),
        ("FT6336 mode init", "touch_write(TOUCH_REG_DEV_MODE, 0x00u)"),
        ("FT6336 interrupt mode", "touch_write(TOUCH_REG_INT_MODE, 0x00u)"),
        ("LCD CoreS3 SE SETEXTC", "static const uint8_t LCD_SETEXTC[] = {0xFFu, 0x93u, 0x42u}"),
        ("LCD RGB565 color mode", "static const uint8_t LCD_COLMOD = 0x55u"),
        ("LCD BGR MADCTL", "static const uint8_t LCD_MADCTL = LCD_CMD_BGR_BIT"),
        ("LCD display function", "static const uint8_t LCD_DFUNCTR[] = {0x08u, 0x82u, 0x1Du, 0x04u}"),
    )
    for label, snippet in required_board_snippets:
        if snippet not in board:
            errors.append(f"{board_rel}: missing CoreS3 SE {label}: {snippet}")

    return errors


def parse_size(output: str) -> dict[str, int]:
    lines = [line.split() for line in output.splitlines() if line.strip()]
    if len(lines) < 2:
        return {}
    values: dict[str, int] = {}
    for key, value in zip(lines[0], lines[1][: len(lines[0])]):
        base = 16 if key == "hex" else 10
        try:
            values[key] = int(value, base)
        except ValueError:
            pass
    return values


def parse_sdkconfig(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(errors="replace").splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("# ") and stripped.endswith(" is not set"):
            values[stripped[2:-11]] = "n"
            continue
        if stripped.startswith("#"):
            continue
        if "=" not in stripped:
            continue
        key, value = stripped.split("=", 1)
        values[key] = value.strip('"')
    return values


def check_sdkconfig(root: Path) -> list[str]:
    errors: list[str] = []
    values = parse_sdkconfig(root / "esp32s3/sdkconfig")

    for key, expected in REQUIRED_CONFIG_VALUES.items():
        actual = values.get(key, "n")
        if actual != expected:
            errors.append(f"{key}={actual}, expected {expected}")
    for key, minimum in REQUIRED_CONFIG_MIN_VALUES.items():
        actual_text = values.get(key)
        try:
            actual = int(actual_text or "0", 0)
        except ValueError:
            actual = 0
        if actual < minimum:
            errors.append(f"{key}={actual_text}, expected at least {minimum}")
    return errors


def check_engine_divergence(root: Path) -> list[str]:
    script = root / "tools/engine_divergence_check.py"
    manifest = root / "tools/engine_divergence_manifest.json"
    if not script.is_file() or not manifest.is_file():
        return [f"engine divergence checker missing: {script} / {manifest}"]
    try:
        print("\n" + run([sys.executable, "-B", str(script), "--root", str(root), "--manifest", str(manifest)]).rstrip())
    except subprocess.CalledProcessError as exc:
        if exc.output:
            print("\n" + exc.output.rstrip())
        return ["engine divergence check failed"]
    return []


def load_embedded_contract_checker(root: Path):
    checker = root / "tools/embedded_contract_check.py"
    if not checker.is_file():
        raise RuntimeError(f"embedded contract checker missing: {checker}")
    spec = importlib.util.spec_from_file_location("embedded_contract_check", checker)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import embedded contract checker: {checker}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def check_pack_manifest(root: Path, manifest_path: Path, nor_pack: Path, tf_pack: Path) -> tuple[list[str], str]:
    errors: list[str] = []
    try:
        manifest = json.loads(manifest_path.read_text())
    except OSError as exc:
        return [f"{manifest_path}: cannot read manifest: {exc}"], ""
    except json.JSONDecodeError as exc:
        return [f"{manifest_path}: invalid JSON: {exc}"], ""

    packs = manifest.get("packs")
    if not isinstance(packs, dict):
        errors.append(f"{manifest_path}: packs is not an object")
        packs = {}

    for label, expected_path in (("nor", nor_pack.resolve()), ("tf", tf_pack.resolve())):
        item = packs.get(label)
        if not isinstance(item, dict):
            errors.append(f"{manifest_path}: missing pack manifest for {label}")
            continue
        raw_path = item.get("path")
        if not isinstance(raw_path, str):
            errors.append(f"{manifest_path}: pack {label} has no path")
            continue
        actual_path = Path(raw_path).resolve()
        if actual_path != expected_path:
            errors.append(f"{manifest_path}: pack {label} path {actual_path} does not match checked pack {expected_path}")

    try:
        checker = load_embedded_contract_checker(root)
        manifest_errors, manifest_report = checker.check_manifest(manifest_path.resolve())
    except Exception as exc:  # pragma: no cover - defensive import/check wrapper.
        return [*errors, f"{manifest_path}: manifest checker failed: {exc}"], ""
    errors.extend(manifest_errors)
    return errors, manifest_report


def parse_stack_usage(build_dir: Path) -> tuple[list[tuple[int, str, str]], list[str]]:
    rows: list[tuple[int, str, str]] = []
    errors: list[str] = []
    stack_files = sorted((build_dir / "esp-idf/main").rglob("*.su"))

    if not stack_files:
        return rows, [f"no stack-usage files found under {build_dir / 'esp-idf/main'}"]

    for path in stack_files:
        for line_no, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            parts = line.split()
            if len(parts) < 2:
                continue
            try:
                size = int(parts[1], 0)
            except ValueError:
                errors.append(f"{path}:{line_no}: bad stack usage line: {line}")
                continue
            rows.append((size, parts[0], " ".join(parts[2:])))
    return sorted(rows, reverse=True), errors


def symbol_size_map(nm_output: str, prefixes: tuple[str, ...]) -> dict[str, int]:
    symbols: dict[str, int] = {}
    for line in nm_output.splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        try:
            size = int(parts[1], 16)
        except ValueError:
            continue
        name = parts[3]
        if any(name.startswith(prefix) for prefix in prefixes):
            symbols[name] = size
    return symbols


def check_buffer_symbol_registry(root: Path, nm_output: str) -> list[str]:
    registry_path = root / BUFFER_SYMBOL_REGISTRY
    errors: list[str] = []
    if not registry_path.is_file():
        return [f"buffer symbol registry missing: {registry_path}"]

    data = json.loads(registry_path.read_text(errors="replace"))
    expected_entries = data.get("symbols", [])
    if not isinstance(expected_entries, list):
        return [f"buffer symbol registry has no symbols list: {registry_path}"]

    actual = symbol_size_map(nm_output, ("pal_sram_", "pal_psram_"))
    expected: dict[str, int] = {}
    for entry in expected_entries:
        if not isinstance(entry, dict):
            errors.append(f"bad buffer registry entry: {entry!r}")
            continue
        name = entry.get("name")
        size = entry.get("size")
        if not isinstance(name, str) or not isinstance(size, int):
            errors.append(f"bad buffer registry entry: {entry!r}")
            continue
        expected[name] = size

    for name, size in sorted(expected.items()):
        if name not in actual:
            errors.append(f"registered buffer symbol missing: {name}")
        elif actual[name] != size:
            errors.append(f"registered buffer symbol {name} is {actual[name]}, expected {size}")
    for name, size in sorted(actual.items()):
        if name not in expected:
            errors.append(f"unregistered buffer symbol: {name} size={size}")
    return errors


def check_target_disabled_sources(root: Path, nm_output: str) -> list[str]:
    errors: list[str] = []
    cmake = (root / "esp32s3/main/CMakeLists.txt").read_text(errors="replace")

    for source_name in sorted(FORBIDDEN_TARGET_SOURCE_NAMES):
        if source_name in cmake:
            errors.append(f"CoreS3 SE target links forbidden source: {source_name}")
    for definition in sorted(REQUIRED_CMAKE_DEFINITIONS):
        if definition not in cmake:
            errors.append(f"CoreS3 SE target missing compile definition: {definition}")

    board = (root / "esp32s3/main/cores3se_board.c").read_text(errors="replace")
    if re.search(r"\boutput0_mask\s*=.*CORES3SE_AW9523_SPEAKER_ENABLE_MASK", board):
        errors.append("CoreS3 SE target includes speaker power in AW9523 output0 set mask")
    if re.search(r"\boutput0_mask\s*=.*\|\s*CORES3SE_AW9523_SPEAKER_ENABLE_MASK", board):
        errors.append("CoreS3 SE target ORs speaker power into AW9523 output0 set mask")
    required_speaker_clear = (
        "aw_update(CORES3SE_AW9523_REG_OUTPUT0, output0_mask, "
        "CORES3SE_AW9523_SPEAKER_ENABLE_MASK)"
    )
    if required_speaker_clear not in board:
        errors.append("CoreS3 SE target must explicitly clear speaker power while PAL_ESP_CORES3SE_NO_AUDIO=1")

    symbols = symbol_size_map(nm_output, FORBIDDEN_TARGET_AUDIO_PREFIXES + tuple(FORBIDDEN_TARGET_AUDIO_SYMBOLS))
    for name, size in sorted(symbols.items()):
        errors.append(f"CoreS3 SE target contains audio/SFX symbol: {name} size={size}")
    return errors


def symbol_prefix_total(nm_output: str, prefix: str) -> tuple[int, list[tuple[int, str]]]:
    total = 0
    rows: list[tuple[int, str]] = []
    for line in nm_output.splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        try:
            size = int(parts[1], 16)
        except ValueError:
            continue
        name = parts[3]
        if name.startswith(prefix):
            total += size
            rows.append((size, name))
    return total, sorted(rows, reverse=True)


def parse_objdump_sections(output: str) -> dict[str, int]:
    sections: dict[str, int] = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        if not parts[0].isdigit():
            continue
        try:
            sections[parts[1]] = int(parts[2], 16)
        except ValueError:
            pass
    return sections


def linked_forbidden_symbols(nm_output: str) -> list[str]:
    hits: list[str] = []
    names = set()
    for line in nm_output.splitlines():
        parts = line.split()
        if parts:
            names.add(parts[-1])
    for name in FORBIDDEN_LINKED_SYMBOLS:
        if name in names:
            hits.append(name)
    return hits


def project_forbidden_undefined_symbols(output: str) -> list[str]:
    hits: list[str] = []
    current_object = ""
    forbidden = set(FORBIDDEN_PROJECT_UNDEFINED_SYMBOLS)

    for line in output.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.endswith(":"):
            current_object = stripped[:-1]
            continue
        parts = stripped.split()
        if len(parts) == 2 and parts[0] == "U" and parts[1] in forbidden:
            hits.append(f"{current_object}: {parts[1]}")
    return hits


def project_forbidden_reloc_symbols(output: str) -> list[str]:
    hits: list[str] = []
    current_object = ""

    for line in output.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.endswith(":"):
            current_object = stripped[:-1]
            continue
        if "R_XTENSA_" not in stripped:
            continue
        target = stripped.rsplit(None, 1)[-1]
        target = target.split("+", 1)[0]
        target = target.split("@", 1)[0]
        if target in FORBIDDEN_PROJECT_RELOC_SYMBOLS:
            hits.append(f"{current_object}: {target}")
    return hits


def check_pack(path: Path, label: str, max_size: int | None) -> list[str]:
    errors: list[str] = []
    data = path.read_bytes()
    if max_size is not None and len(data) > max_size:
        errors.append(f"{label} pack is {len(data)} bytes, over limit {max_size}")
        return errors
    if len(data) < PACK_HEADER_SIZE or u32(data, 0) != PACK_MAGIC:
        errors.append(f"{label} pack header is invalid")
        return errors

    archive_count = u16(data, 8)
    archive_table = u32(data, 12)
    pack_size = u32(data, 24)
    if pack_size != len(data):
        errors.append(f"{label} pack declares {pack_size} bytes, file has {len(data)}")
        return errors
    if archive_table + archive_count * PACK_ARCHIVE_ENTRY_SIZE > len(data):
        errors.append(f"{label} pack archive table is out of range")
        return errors

    for archive_index in range(archive_count):
        archive = archive_table + archive_index * PACK_ARCHIVE_ENTRY_SIZE
        chunk_count = u16(data, archive + 2)
        chunk_table = u32(data, archive + 4)
        if chunk_table + chunk_count * PACK_CHUNK_ENTRY_SIZE > len(data):
            errors.append(f"{label} pack chunk table {archive_index} is out of range")
            continue
        for chunk_id in range(chunk_count):
            chunk = chunk_table + chunk_id * PACK_CHUNK_ENTRY_SIZE
            offset = u32(data, chunk)
            size = u32(data, chunk + 4)
            flags = u16(data, chunk + 10)
            if flags & PACK_CHUNK_F_COMPRESSED:
                errors.append(f"{label} pack archive {archive_index} chunk {chunk_id} is compressed")
            if offset + size > len(data):
                errors.append(f"{label} pack archive {archive_index} chunk {chunk_id} is out of range")
            if data[offset : offset + 4] == b"YJ_1":
                errors.append(f"{label} pack archive {archive_index} chunk {chunk_id} still contains YJ1")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--nor-pack", type=Path, required=True)
    parser.add_argument("--tf-pack", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    build_dir = (Path.cwd() / args.build_dir).resolve()
    elf = build_dir / "sdlpal_cores3se.elf"
    errors: list[str] = []

    print("# CoreS3 SE Contract Check")
    print(f"elf: {elf}")
    print(f"nor_pack: {args.nor_pack}")
    print(f"tf_pack: {args.tf_pack}")
    print(f"manifest: {args.manifest}")

    source_hits = scan_sources(root)
    print(f"\nsource forbidden hits: {len(source_hits)}")
    if source_hits:
        errors.extend(source_hits)
        for hit in source_hits:
            print(hit)

    scaffold_hits = check_scaffold_freeze(root)
    print(f"\nscaffold freeze hits: {len(scaffold_hits)}")
    if scaffold_hits:
        errors.extend(scaffold_hits)
        for hit in scaffold_hits:
            print(hit)

    source_coverage_hits = check_cmake_source_coverage(root)
    print(f"\ntarget source coverage hits: {len(source_coverage_hits)}")
    if source_coverage_hits:
        errors.extend(source_coverage_hits)
        for hit in source_coverage_hits:
            print(hit)

    shared_spi_hits = check_shared_spi_contract(root)
    print(f"\nshared SPI/SDSPI hits: {len(shared_spi_hits)}")
    if shared_spi_hits:
        errors.extend(shared_spi_hits)
        for hit in shared_spi_hits:
            print(hit)

    board_init_hits = check_cores3se_board_init_contract(root)
    print(f"\nCoreS3 SE board init hits: {len(board_init_hits)}")
    if board_init_hits:
        errors.extend(board_init_hits)
        for hit in board_init_hits:
            print(hit)

    sdkconfig_hits = check_sdkconfig(root)
    print(f"\nsdkconfig storage hits: {len(sdkconfig_hits)}")
    if sdkconfig_hits:
        errors.extend(sdkconfig_hits)
        for hit in sdkconfig_hits:
            print(hit)

    errors.extend(check_engine_divergence(root))

    errors.extend(check_pack(args.nor_pack, "NOR", PAL_NOR_PARTITION_BYTES))
    errors.extend(check_pack(args.tf_pack, "TF", None))
    manifest_hits, manifest_report = check_pack_manifest(root, args.manifest, args.nor_pack, args.tf_pack)
    if manifest_report:
        print()
        print(manifest_report)
    if manifest_hits:
        errors.extend(manifest_hits)
    nor_pack_size = args.nor_pack.stat().st_size
    if nor_pack_size > PAL_NOR_SOFT_LIMIT_BYTES:
        errors.append(
            f"NOR pack is {nor_pack_size} bytes, over soft limit "
            f"{PAL_NOR_SOFT_LIMIT_BYTES} ({PAL_NOR_SOFT_LIMIT_PERCENT}% of pal_nor partition)"
        )
    print(
        f"\nNOR pack bytes: {nor_pack_size} / {PAL_NOR_PARTITION_BYTES} "
        f"(soft {PAL_NOR_SOFT_LIMIT_BYTES}, {PAL_NOR_SOFT_LIMIT_PERCENT}%)"
    )
    print(f"TF pack bytes: {args.tf_pack.stat().st_size}")

    size_output = run(["xtensa-esp32s3-elf-size", str(elf)])
    size_values = parse_size(size_output)
    print("\n## size")
    print(size_output.rstrip())

    objdump_sections = parse_objdump_sections(run(["xtensa-esp32s3-elf-objdump", "-h", str(elf)]))
    print("\n## sections")
    for section in KEY_SECTIONS:
        print(f"{section:16s} {objdump_sections.get(section, 0):8d}")

    stack_rows, stack_errors = parse_stack_usage(build_dir)
    print("\n## stack usage")
    print(f"stack usage files: {len(list((build_dir / 'esp-idf/main').rglob('*.su')))}")
    for size, location, qualifier in stack_rows[:10]:
        print(f"{size:8d} {location} {qualifier}".rstrip())
    for size, location, _qualifier in stack_rows:
        if size > STACK_STATIC_FRAME_BUDGET:
            stack_errors.append(f"static stack frame {location} uses {size}, over limit {STACK_STATIC_FRAME_BUDGET}")
    if stack_errors:
        errors.extend(stack_errors)

    nm_output = run(["xtensa-esp32s3-elf-nm", "-S", "--size-sort", str(elf)])
    main_lib = build_dir / "esp-idf/main/libmain.a"
    project_undef_output = run(["xtensa-esp32s3-elf-nm", "-u", str(main_lib)])
    project_reloc_output = run(["xtensa-esp32s3-elf-objdump", "-dr", str(main_lib)])
    sram_total, sram_rows = symbol_prefix_total(nm_output, "pal_sram_")
    psram_total, psram_rows = symbol_prefix_total(nm_output, "pal_psram_")
    linked_hits = linked_forbidden_symbols(nm_output)
    project_undef_hits = project_forbidden_undefined_symbols(project_undef_output)
    project_reloc_hits = project_forbidden_reloc_symbols(project_reloc_output)
    print(f"\nproject forbidden undefined hits: {len(project_undef_hits)}")
    for hit in project_undef_hits:
        print(hit)
    print(f"\nproject forbidden relocation hits: {len(project_reloc_hits)}")
    for hit in project_reloc_hits:
        print(hit)
    print(f"\npal_sram_ total={sram_total} / {SRAM_BUDGET}")
    for size, name in sram_rows:
        print(f"{size:8d} {name}")
    print(f"\npal_psram_ total={psram_total} / {PSRAM_BUDGET}")
    for size, name in psram_rows:
        print(f"{size:8d} {name}")

    buffer_registry_hits = check_buffer_symbol_registry(root, nm_output)
    print(f"\nbuffer registry hits: {len(buffer_registry_hits)}")
    for hit in buffer_registry_hits:
        print(hit)
    if buffer_registry_hits:
        errors.extend(buffer_registry_hits)

    disabled_source_hits = check_target_disabled_sources(root, nm_output)
    print(f"\ntarget disabled-source/audio hits: {len(disabled_source_hits)}")
    for hit in disabled_source_hits:
        print(hit)
    if disabled_source_hits:
        errors.extend(disabled_source_hits)

    if sram_total > SRAM_BUDGET:
        errors.append(f"pal_sram_ total {sram_total} exceeds {SRAM_BUDGET}")
    if psram_total > PSRAM_BUDGET:
        errors.append(f"pal_psram_ total {psram_total} exceeds {PSRAM_BUDGET}")
    dram_static = objdump_sections.get(".dram0.data", 0) + objdump_sections.get(".dram0.bss", 0)
    if dram_static > DRAM_STATIC_BUDGET:
        errors.append(f"DRAM static sections {dram_static} exceed {DRAM_STATIC_BUDGET}")
    if objdump_sections.get(".ext_ram.bss", 0) > PSRAM_BUDGET:
        errors.append(f".ext_ram.bss {objdump_sections.get('.ext_ram.bss', 0)} exceeds {PSRAM_BUDGET}")
    if objdump_sections.get(".ext_ram.bss", 0) < psram_total:
        errors.append(f".ext_ram.bss {objdump_sections.get('.ext_ram.bss', 0)} is smaller than pal_psram_ total {psram_total}")
    if linked_hits:
        errors.extend(f"linked forbidden symbol: {name}" for name in linked_hits)
    if project_undef_hits:
        errors.extend(f"project object forbidden undefined symbol: {hit}" for hit in project_undef_hits)
    if project_reloc_hits:
        errors.extend(f"project object forbidden relocation symbol: {hit}" for hit in project_reloc_hits)
    if size_values.get("bss", 0) > 9000000:
        errors.append(f"bss exceeds broad target limit: {size_values.get('bss')}")

    if errors:
        print("\n## FAIL")
        for error in errors:
            print(error)
        return 1
    print("\n## PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
