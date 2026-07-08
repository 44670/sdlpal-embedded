#!/usr/bin/env python3
"""Contract checks for the CoreS3 SE full-engine host artifact."""

from __future__ import annotations

import argparse
import ast
import csv
import json
import re
import subprocess
from collections import Counter
from pathlib import Path


FLASH_BYTES = 16 * 1024 * 1024
PAL_NOR_OFFSET = 0x310000
PAL_NOR_PARTITION_BYTES = 0xB00000
PAL_NOR_SOFT_LIMIT_PERCENT = 97
PAL_NOR_SOFT_LIMIT_BYTES = PAL_NOR_PARTITION_BYTES * PAL_NOR_SOFT_LIMIT_PERCENT // 100
ENGINE_BUFFER_SYMBOL_REGISTRY = Path("esp32s3/engine_buffer_symbols.json")
FACTORY_APP_OFFSET = 0x10000
FACTORY_APP_BYTES = 0x300000
FORBIDDEN_UNDEFINED = re.compile(
    r"(?:\b("
    r"malloc|calloc|realloc|free|"
    r"_malloc_r|_calloc_r|_realloc_r|_free_r|"
    r"_Znwm|_Znam|_ZdlPv|_ZdaPv|_ZdlPvm|_ZdaPvm|"
    r"PAL_MKFDecompressChunk|Decompress|YJ1|YJ2"
    r")\b)|(?:operator\s+(?:new|delete))"
)
FORBIDDEN_SYMBOL_PREFIXES = (
    "pal_audio_",
    "pal_sfx_",
    "pal_music_",
    "pal_sram_audio_",
    "pal_psram_sfx_",
)
FORBIDDEN_LINKED_SYMBOLS = (
    "PAL_MKFDecompressChunk",
    "PAL_MKFGetDecompressedSize",
    "YJ1_Decompress",
    "YJ2_Decompress",
    "Decompress",
    "_Znwm",
    "_Znam",
    "_ZdlPv",
    "_ZdaPv",
    "_ZdlPvm",
    "_ZdaPvm",
)
REQUIRED_LINKED_SYMBOLS = (
    "__wrap_PAL_MKFOpenPackArchive",
    "__wrap_PAL_MKFIsPackArchive",
    "__wrap_PAL_MKFMapChunk",
    "__wrap_PAL_MKFGetChunkCount",
    "__wrap_PAL_MKFGetChunkSize",
    "__wrap_PAL_MKFReadChunk",
    "__wrap_fopen",
    "__wrap_fclose",
    "__wrap_fread",
    "__wrap_fwrite",
    "__wrap_fputs",
    "__wrap_fflush",
    "CoreS3Se_FlushArgb8888Texture",
    "CoreS3Se_MountTf",
    "CoreS3Se_PrepareTfAccess",
    "CoreS3Se_TouchPoint",
    "PalEngineBridge_Delay",
    "PalEngineBridge_GetTicks",
    "PalEngineBridge_PollEvent",
    "PalEngineBridge_RenderPresent",
    "PalEngineBridge_IsPackFile",
    "PalEngineBridge_TargetInitPacks",
    "pal_engine_fatfs_stdio_slots",
    "access",
)
REQUIRED_OBJECT_SYMBOLS = (
    "__wrap_fseek",
    "__wrap_ftell",
    "__wrap_fgets",
    "__wrap_feof",
    "__wrap_ferror",
    "__wrap_rewind",
)
REQUIRED_SUPPORT_SOURCES = (
    "overlay.c",
    "paldebug.c",
    "sdl_compat/sdl_compat.c",
    "unix/embedded_contract_stubs.c",
    "unix/contract_noaudio.c",
    "embedded/pal_pack.c",
    "embedded/pal_text_cache.c",
    "embedded/pal_font_cache.c",
    "esp32s3/engine_bridge/pal_engine_app_main.c",
    "esp32s3/engine_bridge/pal_engine_fatfs_stdio.c",
    "esp32s3/engine_bridge/pal_engine_pack_provider.c",
    "esp32s3/engine_bridge/pal_engine_posix_shims.c",
    "esp32s3/engine_bridge/pal_engine_target_input.c",
    "esp32s3/engine_bridge/pal_engine_target_packs.c",
    "esp32s3/engine_bridge/pal_engine_target_time.c",
    "esp32s3/engine_bridge/pal_engine_target_video.c",
    "esp32s3/native_engine_shim/sdl_shim.c",
    "esp32s3/main/cores3se_board.c",
    "esp32s3/main/cores3se_memory.c",
)
REQUIRED_OBJECT_RELOCS = {
    "pal_engine_app_main.c.obj": (
        "CoreS3Se_Begin",
        "PalEngineBridge_TargetInitPacks",
        "PAL_EngineMain",
    ),
    "pal_engine_fatfs_stdio.c.obj": (
        "CoreS3Se_PrepareTfAccess",
        "f_open",
        "f_close",
        "f_read",
        "f_write",
        "f_lseek",
    ),
    "pal_engine_target_input.c.obj": (
        "CoreS3Se_TouchPoint",
    ),
    "pal_engine_target_packs.c.obj": (
        "CoreS3Se_MountTf",
        "CoreS3Se_PrepareTfAccess",
        "PalEngineBridge_SetNorPackConst",
        "PalEngineBridge_SetTfPackReadAt",
        "esp_partition_mmap",
        "f_open",
        "f_lseek",
        "f_read",
    ),
    "pal_engine_target_video.c.obj": (
        "CoreS3Se_FlushArgb8888Texture",
    ),
    "pal_engine_target_time.c.obj": (
        "esp_timer_get_time",
        "vTaskDelay",
    ),
    "sdl_shim.c.obj": (
        "PalEngineBridge_PollEvent",
        "PalEngineBridge_RenderPresent",
        "PalEngineBridge_GetTicks",
        "PalEngineBridge_Delay",
    ),
}
FORBIDDEN_PAL_MKF_EXPORTS = re.compile(
    r"\b[Tt]\s+PAL_MKF("
    r"OpenPackArchive|IsPackArchive|MapChunk|GetChunkCount|GetChunkSize|ReadChunk"
    r")$"
)
MMAP_CALL_PATTERN = re.compile(r"\bmmap\s*\([^;]*;", re.S)
READ_ONLY_PACK_MMAP_SOURCES = (
    "esp32s3/native_shim/cores3se_native_shim.c",
    "esp32s3/engine_bridge/pal_engine_native_packs.c",
    "unix/embedded_contract_stubs.c",
)
FORBIDDEN_ENGINE_HOST_SOURCE_ENTRIES = (
    "../../audio.c",
    "../../midi.c",
    "../../embedded/pal_audio_static.c",
    "../../embedded/pal_sfx_cache.c",
    "app_main.c",
    "pal_save_fatfs.c",
)
REQUIRED_ENGINE_HOST_SOURCE_ENTRIES = (
    "../../unix/contract_noaudio.c",
    "../engine_bridge/pal_engine_app_main.c",
    "../engine_bridge/pal_engine_fatfs_stdio.c",
    "../engine_bridge/pal_engine_pack_provider.c",
    "../native_engine_shim/sdl_shim.c",
)
PROTECTED_MISSING_EXCEPTIONS = {
    "audio.c",
}
STACK_STATIC_FRAME_BUDGET = 8192
REQUIRED_SDKCONFIG_VALUES = {
    "CONFIG_IDF_TARGET": "esp32s3",
    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB": "y",
    "CONFIG_ESPTOOLPY_FLASHSIZE": "16MB",
    "CONFIG_PARTITION_TABLE_CUSTOM": "y",
    "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME": "partitions.csv",
    "CONFIG_PARTITION_TABLE_FILENAME": "partitions.csv",
    "CONFIG_SPIRAM": "y",
    "CONFIG_SPIRAM_USE_MEMMAP": "y",
    "CONFIG_SPIRAM_USE_CAPS_ALLOC": "n",
    "CONFIG_SPIRAM_USE_MALLOC": "n",
    "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY": "y",
    "CONFIG_FATFS_VOLUME_COUNT": "1",
    "CONFIG_FATFS_LFN_NONE": "y",
    "CONFIG_FATFS_USE_DYN_BUFFERS": "n",
    "CONFIG_FATFS_ALLOC_PREFER_EXTRAM": "n",
}
REQUIRED_SDKCONFIG_MIN_VALUES = {
    "CONFIG_ESP_MAIN_TASK_STACK_SIZE": 32768,
    "CONFIG_MAIN_TASK_STACK_SIZE": 32768,
}
UNSUPPORTED_FATFS_STDIO_CALLS = {
    "clearerr",
    "fgetpos",
    "fsetpos",
    "fscanf",
    "vfscanf",
    "fgetc",
    "getc",
    "getchar",
    "ungetc",
    "fputc",
    "putc",
    "putchar",
    "fprintf",
    "vfprintf",
    "freopen",
    "remove",
    "rename",
    "setbuf",
    "setvbuf",
    "tmpfile",
    "tmpnam",
}


def run_text(args: list[str], cwd: Path | None = None) -> str:
    return subprocess.check_output(args, cwd=cwd, text=True, stderr=subprocess.STDOUT)


def section_sizes(objdump_output: str) -> dict[str, int]:
    sections: dict[str, int] = {}
    for line in objdump_output.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0].isdigit():
            try:
                sections[parts[1]] = int(parts[2], 16)
            except ValueError:
                pass
    return sections


def section_ranges(objdump_output: str) -> dict[str, tuple[int, int]]:
    ranges: dict[str, tuple[int, int]] = {}
    for line in objdump_output.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[0].isdigit():
            try:
                size = int(parts[2], 16)
                start = int(parts[3], 16)
            except ValueError:
                continue
            ranges[parts[1]] = (start, start + size)
    return ranges


def symbol_sizes(nm_output: str) -> dict[str, int]:
    sizes: dict[str, int] = {}
    for line in nm_output.splitlines():
        parts = line.split()
        if len(parts) >= 4:
            try:
                sizes[parts[3]] = int(parts[1], 16)
            except ValueError:
                pass
        elif len(parts) >= 3:
            sizes.setdefault(parts[2], 0)
    return sizes


def symbol_rows(nm_output: str) -> list[tuple[str, int, int]]:
    rows: list[tuple[str, int, int]] = []
    for line in nm_output.splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        try:
            address = int(parts[0], 16)
            size = int(parts[1], 16)
        except ValueError:
            continue
        rows.append((parts[3], address, size))
    return rows


def load_protected_sources(manifest: Path) -> list[str]:
    with manifest.open("r", encoding="utf-8") as fp:
        data = json.load(fp)
    return [
        str(entry["path"])
        for entry in data.get("files", [])
        if str(entry.get("path", "")).endswith(".c")
    ]


def parse_int_token(value: str) -> int:
    text = value.strip().strip('"').lower()
    multiplier = 1
    if text.endswith("k"):
        multiplier = 1024
        text = text[:-1]
    elif text.endswith("m"):
        multiplier = 1024 * 1024
        text = text[:-1]
    return int(text, 0) * multiplier


def parse_c_uint_expr(expr: str) -> int:
    cleaned = expr.strip()
    cleaned = re.sub(r"/\*.*?\*/", "", cleaned)
    cleaned = re.sub(r"//.*", "", cleaned)
    cleaned = cleaned.replace("u", "").replace("U", "")

    def eval_node(node: ast.AST) -> int:
        if isinstance(node, ast.Expression):
            return eval_node(node.body)
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return int(node.value)
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            value = eval_node(node.operand)
            return value if isinstance(node.op, ast.UAdd) else -value
        if isinstance(node, ast.BinOp):
            left = eval_node(node.left)
            right = eval_node(node.right)
            if isinstance(node.op, ast.Add):
                return left + right
            if isinstance(node.op, ast.Sub):
                return left - right
            if isinstance(node.op, ast.Mult):
                return left * right
            if isinstance(node.op, ast.FloorDiv):
                return left // right
            if isinstance(node.op, ast.LShift):
                return left << right
            if isinstance(node.op, ast.RShift):
                return left >> right
        raise ValueError(expr)

    return eval_node(ast.parse(cleaned, mode="eval"))


def parse_c_define_uint(path: Path, name: str) -> int | None:
    pattern = re.compile(rf"^\s*#\s*define\s+{re.escape(name)}\s+(.+?)\s*$", re.M)
    match = pattern.search(path.read_text(errors="replace"))
    if match is None:
        return None
    return parse_c_uint_expr(match.group(1))


def parse_partitions(path: Path) -> dict[str, dict[str, object]]:
    rows: dict[str, dict[str, object]] = {}
    with path.open("r", encoding="utf-8") as fp:
        for raw in csv.reader(fp):
            if not raw:
                continue
            cells = [item.strip() for item in raw]
            if not cells[0] or cells[0].startswith("#"):
                continue
            if len(cells) < 5:
                continue
            name, ptype, subtype, offset, size = cells[:5]
            rows[name] = {
                "type": ptype,
                "subtype": subtype,
                "offset": parse_int_token(offset),
                "size": parse_int_token(size),
            }
    return rows


def parse_make_default(path: Path, name: str) -> str | None:
    pattern = re.compile(rf"^{re.escape(name)}\s*\?=\s*(\S+)", re.M)
    match = pattern.search(path.read_text(errors="replace"))
    return match.group(1) if match is not None else None


def check_partition_contract(root: Path, build_dir: Path, nor_pack: Path | None, tf_pack: Path | None) -> list[str]:
    errors: list[str] = []
    partitions_path = root / "esp32s3" / "partitions.csv"
    makefile_path = root / "esp32s3" / "Makefile"

    try:
        partitions = parse_partitions(partitions_path)
    except (OSError, ValueError) as exc:
        return [f"{partitions_path}: cannot parse partition table: {exc}"]

    factory = partitions.get("factory")
    pal_nor = partitions.get("pal_nor")
    storage = partitions.get("storage")
    if factory is None:
        errors.append("partition table missing factory app partition")
    else:
        if factory.get("type") != "app" or factory.get("subtype") != "factory":
            errors.append(f"factory partition type/subtype is {factory.get('type')}/{factory.get('subtype')}, expected app/factory")
        if factory.get("offset") != FACTORY_APP_OFFSET:
            errors.append(f"factory partition offset is {factory.get('offset')}, expected {FACTORY_APP_OFFSET}")
        if factory.get("size") != FACTORY_APP_BYTES:
            errors.append(f"factory partition size is {factory.get('size')}, expected {FACTORY_APP_BYTES}")

    if pal_nor is None:
        errors.append("partition table missing pal_nor data partition")
    else:
        if pal_nor.get("type") != "data" or pal_nor.get("subtype") != "0x40":
            errors.append(f"pal_nor partition type/subtype is {pal_nor.get('type')}/{pal_nor.get('subtype')}, expected data/0x40")
        if pal_nor.get("offset") != PAL_NOR_OFFSET:
            errors.append(f"pal_nor partition offset is {pal_nor.get('offset')}, expected {PAL_NOR_OFFSET}")
        if pal_nor.get("size") != PAL_NOR_PARTITION_BYTES:
            errors.append(f"pal_nor partition size is {pal_nor.get('size')}, expected {PAL_NOR_PARTITION_BYTES}")

    if storage is None:
        errors.append("partition table missing storage FatFS partition")
    elif storage.get("type") != "data" or storage.get("subtype") != "fat":
        errors.append(f"storage partition type/subtype is {storage.get('type')}/{storage.get('subtype')}, expected data/fat")

    for name, item in partitions.items():
        offset = int(item.get("offset", 0))
        size = int(item.get("size", 0))
        if offset + size > FLASH_BYTES:
            errors.append(f"{name} partition ends at 0x{offset + size:X}, beyond 16MB flash")

    default_offset = parse_make_default(makefile_path, "PAL_NOR_OFFSET")
    if default_offset is None:
        errors.append("Makefile missing PAL_NOR_OFFSET default")
    elif parse_int_token(default_offset) != PAL_NOR_OFFSET:
        errors.append(f"Makefile PAL_NOR_OFFSET is {default_offset}, expected 0x{PAL_NOR_OFFSET:X}")

    default_flash_size = parse_make_default(makefile_path, "FLASH_SIZE")
    if default_flash_size != "16MB":
        errors.append(f"Makefile FLASH_SIZE is {default_flash_size}, expected 16MB")

    app_bin = build_dir / "sdlpal_cores3se.bin"
    if not app_bin.is_file():
        errors.append(f"missing app binary for partition size check: {app_bin}")
    elif app_bin.stat().st_size > FACTORY_APP_BYTES:
        errors.append(f"app binary is {app_bin.stat().st_size} bytes, over factory partition {FACTORY_APP_BYTES}")

    if nor_pack is not None:
        if not nor_pack.is_file():
            errors.append(f"missing NOR pack for partition size check: {nor_pack}")
        else:
            nor_size = nor_pack.stat().st_size
            if nor_size > PAL_NOR_PARTITION_BYTES:
                errors.append(f"NOR pack is {nor_size} bytes, over pal_nor partition {PAL_NOR_PARTITION_BYTES}")
            if nor_size > PAL_NOR_SOFT_LIMIT_BYTES:
                errors.append(
                    f"NOR pack is {nor_size} bytes, over soft limit "
                    f"{PAL_NOR_SOFT_LIMIT_BYTES} ({PAL_NOR_SOFT_LIMIT_PERCENT}% of pal_nor partition)"
                )
    if tf_pack is not None:
        if not tf_pack.is_file():
            errors.append(f"missing TF pack for runtime path check: {tf_pack}")
        elif tf_pack.stat().st_size == 0:
            errors.append(f"TF pack is empty: {tf_pack}")

    return errors


def max_manifest_payload(manifest: Path, pack_name: str) -> int | None:
    try:
        data = json.loads(manifest.read_text(errors="replace"))
    except (OSError, json.JSONDecodeError):
        return None

    pack = data.get("packs", {}).get(pack_name)
    if not isinstance(pack, dict):
        return None

    max_payload = 0
    for archive in pack.get("archive_summaries", []):
        if not isinstance(archive, dict):
            continue
        value = archive.get("max_payload_bytes")
        if isinstance(value, int):
            max_payload = max(max_payload, value)
        for chunk in archive.get("chunks", []):
            if isinstance(chunk, dict) and isinstance(chunk.get("payload_bytes"), int):
                max_payload = max(max_payload, chunk["payload_bytes"])
    return max_payload


def check_tf_map_capacity(root: Path, manifest: Path | None) -> list[str]:
    if manifest is None:
        return []

    errors: list[str] = []
    provider = root / "esp32s3" / "engine_bridge" / "pal_engine_pack_provider.c"
    try:
        tf_map_bytes = parse_c_define_uint(provider, "PAL_ENGINE_TF_MAP_BYTES")
    except (OSError, ValueError) as exc:
        return [f"{provider}: cannot parse PAL_ENGINE_TF_MAP_BYTES: {exc}"]
    if tf_map_bytes is None:
        return [f"{provider}: missing PAL_ENGINE_TF_MAP_BYTES"]

    max_tf_payload = max_manifest_payload(manifest, "tf")
    if max_tf_payload is None:
        return [f"{manifest}: cannot read TF max payload"]
    if max_tf_payload > tf_map_bytes:
        errors.append(
            f"largest TF payload {max_tf_payload} exceeds PAL_ENGINE_TF_MAP_BYTES {tf_map_bytes}"
        )
    return errors


def parse_sdkconfig(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(errors="replace").splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("# ") and stripped.endswith(" is not set"):
            values[stripped[2:-11]] = "n"
            continue
        if stripped.startswith("#") or "=" not in stripped:
            continue
        key, value = stripped.split("=", 1)
        values[key] = value.strip('"')
    return values


def check_sdkconfig(root: Path, build_dir: Path) -> list[str]:
    errors: list[str] = []
    sdkconfig = build_dir / "sdkconfig"
    if not sdkconfig.is_file():
        sdkconfig = root / "esp32s3" / "sdkconfig"
    if not sdkconfig.is_file():
        sdkconfig = root / "esp32s3" / "sdkconfig.defaults"
    values = parse_sdkconfig(sdkconfig)

    for key, expected in REQUIRED_SDKCONFIG_VALUES.items():
        actual = values.get(key, "n")
        if actual != expected:
            errors.append(f"{sdkconfig}: {key}={actual}, expected {expected}")
    for key, minimum in REQUIRED_SDKCONFIG_MIN_VALUES.items():
        actual_text = values.get(key)
        try:
            actual = int(actual_text or "0", 0)
        except ValueError:
            actual = 0
        if actual < minimum:
            errors.append(f"{sdkconfig}: {key}={actual_text}, expected at least {minimum}")
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

    for func in ("CoreS3Se_FlushPalFramebuffer", "CoreS3Se_FlushArgb8888Texture"):
        body = find_function_body(board, func)
        if "CoreS3Se_PrepareLcdAccess();" not in body or "CoreS3Se_PrepareTfAccess();" not in body:
            errors.append(f"{board_rel}: {func} must bracket LCD transfers and restore TF input mode")
        if "esp_lcd_panel_io_tx_param(lcd_io, -1, NULL, 0)" not in body:
            errors.append(f"{board_rel}: {func} must wait for LCD idle before restoring TF input mode")

    for rel in (
        "esp32s3/main/cores3se_board.c",
        "esp32s3/engine_bridge/pal_engine_target_packs.c",
        "esp32s3/engine_bridge/pal_engine_fatfs_stdio.c",
    ):
        path = root / rel
        if not path.is_file():
            continue
        for line_no, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
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


def strip_c_comments_and_strings(text: str) -> str:
    out: list[str] = []
    i = 0
    state = "normal"
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "normal":
            if ch == "/" and nxt == "/":
                out.extend((" ", " "))
                i += 2
                state = "line_comment"
                continue
            if ch == "/" and nxt == "*":
                out.extend((" ", " "))
                i += 2
                state = "block_comment"
                continue
            if ch == '"':
                out.append(" ")
                i += 1
                state = "string"
                continue
            if ch == "'":
                out.append(" ")
                i += 1
                state = "char"
                continue
            out.append(ch)
            i += 1
            continue
        if state == "line_comment":
            out.append("\n" if ch == "\n" else " ")
            i += 1
            if ch == "\n":
                state = "normal"
            continue
        if state == "block_comment":
            if ch == "*" and nxt == "/":
                out.extend((" ", " "))
                i += 2
                state = "normal"
                continue
            out.append("\n" if ch == "\n" else " ")
            i += 1
            continue
        if state in ("string", "char"):
            if ch == "\\" and nxt:
                out.extend((" ", "\n" if nxt == "\n" else " "))
                i += 2
                continue
            terminator = '"' if state == "string" else "'"
            out.append("\n" if ch == "\n" else " ")
            i += 1
            if ch == terminator:
                state = "normal"
            continue
    return "".join(out)


def check_target_stdio_surface(root: Path, protected_sources: list[str]) -> list[str]:
    hits: list[str] = []
    sources = sorted(
        {source for source in protected_sources if source not in PROTECTED_MISSING_EXCEPTIONS}
        | set(REQUIRED_SUPPORT_SOURCES)
    )
    call_re = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")

    for rel in sources:
        path = root / rel
        if not path.is_file():
            continue
        original_text = path.read_text(errors="replace")
        text = strip_c_comments_and_strings(original_text)
        original_lines = original_text.splitlines()
        for line_no, line in enumerate(text.splitlines(), 1):
            for match in call_re.finditer(line):
                name = match.group(1)
                if name not in UNSUPPORTED_FATFS_STDIO_CALLS:
                    continue
                if name in {"fprintf", "vfprintf"}:
                    args = line[match.end() :].lstrip()
                    if args.startswith("stderr"):
                        continue
                original = original_lines[line_no - 1].strip() if line_no <= len(original_lines) else line.strip()
                hits.append(f"{rel}:{line_no}: unsupported target stdio call {name}: {original}")
    return hits


def check_fatfs_stdio_contract(root: Path) -> list[str]:
    rel = "esp32s3/engine_bridge/pal_engine_fatfs_stdio.c"
    text = (root / rel).read_text(errors="replace")
    fopen_body = find_function_body(text, "__wrap_fopen")
    fclose_body = find_function_body(text, "__wrap_fclose")
    errors: list[str] = []

    if 'path[0] == \'0\' && path[1] == \':\' && path[2] == \'/\'' not in text:
        errors.append(f"{rel}: FatFS stdio must only accept short 0:/ paths")
    if "return __real_fopen(path, mode);" in fopen_body:
        errors.append(f"{rel}: __wrap_fopen must not fall back to target VFS for non-0:/ paths")
    if "errno = ENOENT;" not in fopen_body:
        errors.append(f"{rel}: __wrap_fopen must reject non-0:/ paths with ENOENT")
    if "PalEngineBridge_IsPackFile(stream)" not in fclose_body:
        errors.append(f"{rel}: __wrap_fclose must no-op generated-pack pseudo FILE handles")
    return errors


def check_pack_provider_dynamic_layout(root: Path) -> list[str]:
    rel = "esp32s3/engine_bridge/pal_engine_pack_provider.c"
    text = (root / rel).read_text(errors="replace")
    errors: list[str] = []

    if "archive_is_tf" in text:
        errors.append(f"{rel}: pack provider must not hard-code TF archive IDs")
    if "find_archive_store" not in text:
        errors.append(f"{rel}: pack provider must choose NOR/TF placement from pack archive tables")
    if "PalPack_GetChunkCount(&pal_engine_nor_pack" not in text:
        errors.append(f"{rel}: pack provider must probe NOR archive table")
    if "PalPackToc_GetChunkCount(&pal_engine_tf_toc" not in text:
        errors.append(f"{rel}: pack provider must probe TF archive table")
    if "PalEngineBridge_IsPackFile" not in text or "__wrap_fclose" not in text:
        errors.append(f"{rel}: pack provider must expose/no-op pack pseudo FILE fclose handling")
    if "pal_engine_tf_map_valid" not in text or "pal_engine_tf_map_archive" not in text:
        errors.append(f"{rel}: TF PAL_MKFMapChunk path must cache repeated mapped chunks")
    if re.search(r"\bcase\s+PAL_PACK_ARCHIVE_", text):
        errors.append(f"{rel}: pack provider contains hard-coded archive placement switch")
    return errors


def check_read_only_pack_mmaps(root: Path) -> list[str]:
    errors: list[str] = []

    for rel in READ_ONLY_PACK_MMAP_SOURCES:
        path = root / rel
        if not path.is_file():
            errors.append(f"{rel}: missing read-only pack mmap source")
            continue
        text = path.read_text(errors="replace")
        calls = list(MMAP_CALL_PATTERN.finditer(text))
        if not calls:
            errors.append(f"{rel}: missing mmap() read-only pack view")
            continue
        for match in calls:
            call = match.group(0)
            line_no = text.count("\n", 0, match.start()) + 1
            if "PROT_READ" not in call:
                errors.append(f"{rel}:{line_no}: mmap pack view must use PROT_READ")
            if "MAP_PRIVATE" not in call:
                errors.append(f"{rel}:{line_no}: mmap pack view must use MAP_PRIVATE")
            if "PROT_WRITE" in call:
                errors.append(f"{rel}:{line_no}: mmap pack view must not use PROT_WRITE")
            if "MAP_SHARED" in call:
                errors.append(f"{rel}:{line_no}: mmap pack view must not use MAP_SHARED")

    native_shim = (root / "esp32s3/native_shim/cores3se_native_shim.c").read_text(errors="replace")
    esp_mmap_body = find_function_body(native_shim, "esp_partition_mmap")
    if "open(path, O_RDONLY)" not in esp_mmap_body:
        errors.append("esp32s3/native_shim/cores3se_native_shim.c: esp_partition_mmap must open NOR pack O_RDONLY")
    if "PAL_CORES3SE_NATIVE_NOR_PACK" not in esp_mmap_body:
        errors.append("esp32s3/native_shim/cores3se_native_shim.c: esp_partition_mmap must use the CoreS3 SE NOR pack path")

    native_packs = (root / "esp32s3/engine_bridge/pal_engine_native_packs.c").read_text(errors="replace")
    map_nor_body = find_function_body(native_packs, "map_nor_pack")
    if "open(path, O_RDONLY)" not in map_nor_body:
        errors.append("esp32s3/engine_bridge/pal_engine_native_packs.c: native NOR pack mapper must open O_RDONLY")
    if "const uint8_t *image" not in map_nor_body:
        errors.append("esp32s3/engine_bridge/pal_engine_native_packs.c: native NOR pack mapper must expose a const byte view")

    return errors


def check_target_video_contract(root: Path) -> list[str]:
    rel = "esp32s3/engine_bridge/pal_engine_target_video.c"
    text = (root / rel).read_text(errors="replace")
    body = find_function_body(text, "PalEngineBridge_RenderPresent")
    errors: list[str] = []

    if "(void)CoreS3Se_FlushArgb8888Texture" in body:
        errors.append(f"{rel}: engine present must not ignore LCD flush failures")
    if "if (!CoreS3Se_FlushArgb8888Texture" not in body:
        errors.append(f"{rel}: engine present must check CoreS3Se_FlushArgb8888Texture result")
    if "ESP_LOGE" not in body:
        errors.append(f"{rel}: engine present must log LCD flush failures")
    if "engine first present" not in text:
        errors.append(f"{rel}: engine present must log first successful frame for hardware smoke verification")
    if "engine present stats: frames=%lu" not in text:
        errors.append(f"{rel}: engine present must log continuing frame stats for hardware smoke verification")
    if "pal_engine_present_count == 60u" not in text:
        errors.append(f"{rel}: engine present stats must include a 60-frame hardware smoke checkpoint")
    return errors


def check_hardware_smoke_contract(root: Path) -> list[str]:
    rel = "esp32s3/Makefile"
    text = (root / rel).read_text(errors="replace")
    errors: list[str] = []

    if re.search(r"^prepare-tf:", text, re.MULTILINE) is None:
        errors.append(f"{rel}: missing prepare-tf target for staging 0:/pal_tf.pak")
    if "TF_MOUNT ?=" not in text:
        errors.append(f"{rel}: prepare-tf must require an explicit TF_MOUNT")
    if 'cp -f "$(PACK_TF)" "$(TF_MOUNT)/pal_tf.pak"' not in text:
        errors.append(f"{rel}: prepare-tf must copy the generated TF pack as pal_tf.pak")
    if 'sync "$(TF_MOUNT)/pal_tf.pak"' not in text:
        errors.append(f"{rel}: prepare-tf must sync the staged TF pack")
    if re.search(r"^engine-hardware-smoke:", text, re.MULTILINE) is None:
        errors.append(f"{rel}: missing engine-hardware-smoke target")
    flash_port_body = make_target_body(text, "flash-engine-port")
    if "TF_MOUNT=/media/... is required" not in flash_port_body:
        errors.append(f"{rel}: flash-engine-port must require an explicit TF_MOUNT")
    if '$(MAKE) TF_MOUNT="$(TF_MOUNT)" prepare-tf' not in flash_port_body:
        errors.append(f"{rel}: flash-engine-port must stage the generated TF pack")
    if "$(MAKE) flash-nor" not in flash_port_body:
        errors.append(f"{rel}: flash-engine-port must flash the generated NOR pack")
    if "$(MAKE) flash-engine-host" not in flash_port_body:
        errors.append(f"{rel}: flash-engine-port must flash the full engine-host app")
    hardware_body = make_target_body(text, "engine-hardware-smoke")
    if "TF_MOUNT=/media/... is required" not in hardware_body:
        errors.append(f"{rel}: hardware smoke must require an explicit TF_MOUNT")
    if '$(MAKE) TF_MOUNT="$(TF_MOUNT)" prepare-tf' not in hardware_body:
        errors.append(f"{rel}: hardware smoke must stage the generated TF pack")
    if "engine-port-check" not in hardware_body:
        errors.append(f"{rel}: hardware smoke must run the full native/IDF engine port gate first")
    if '$(MAKE) PORT="$(PORT)" flash-nor' not in hardware_body:
        errors.append(f"{rel}: hardware smoke must flash the generated NOR pack")
    if '"$(ENGINE_HOST_BUILD_DIR)" -p "$(PORT)" -b "$(BAUD)" flash monitor' not in hardware_body:
        errors.append(f"{rel}: hardware smoke must flash and monitor the full engine-host build")
    if "flash monitor" not in text:
        errors.append(f"{rel}: hardware smoke must flash the engine host and monitor the same reset")
    if "engine packs ready" not in text:
        errors.append(f"{rel}: hardware smoke must require pack-provider startup log")
    if "engine first present" not in text:
        errors.append(f"{rel}: hardware smoke must require first engine LCD-present log")
    if "engine present stats: frames=60" not in text:
        errors.append(f"{rel}: hardware smoke must require 60-frame engine-present log")
    if "PORT=/dev/ttyACM0 is required" not in text:
        errors.append(f"{rel}: hardware smoke must require an explicit serial PORT")
    return errors


def make_target_body(text: str, target: str) -> str:
    match = re.search(
        rf"^{re.escape(target)}:[^\n]*\n(?P<body>(?:\t[^\n]*(?:\n|$))*)",
        text,
        re.MULTILINE,
    )
    return match.group("body") if match is not None else ""


def make_target_rule(text: str, target: str) -> str:
    match = re.search(
        rf"^{re.escape(target)}:[^\n]*(?:\n(?:\t[^\n]*(?:\n|$))*)",
        text,
        re.MULTILINE,
    )
    return match.group(0) if match is not None else ""


def cmake_set_body(text: str, name: str) -> str:
    match = re.search(rf"\bset\s*\(\s*{re.escape(name)}\b", text)
    if match is None:
        return ""

    depth = 0
    for index in range(match.start(), len(text)):
        char = text[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return text[match.end() : index]
    return ""


def check_engine_host_source_set_contract(root: Path) -> list[str]:
    rel = "esp32s3/main/CMakeLists.txt"
    text = (root / rel).read_text(errors="replace")
    sources = cmake_set_body(text, "PAL_ENGINE_HOST_SOURCES")
    errors: list[str] = []

    if not sources:
        return [f"{rel}: missing PAL_ENGINE_HOST_SOURCES source set"]
    for entry in REQUIRED_ENGINE_HOST_SOURCE_ENTRIES:
        if entry not in sources:
            errors.append(f"{rel}: engine host source set must include {entry}")
    for entry in FORBIDDEN_ENGINE_HOST_SOURCE_ENTRIES:
        if re.search(rf"(?<![\w./-]){re.escape(entry)}(?![\w./-])", sources):
            errors.append(f"{rel}: engine host source set must not include {entry}")
    if "PAL_CONTRACT_NO_AUDIO=1" not in text:
        errors.append(f"{rel}: engine host must compile with PAL_CONTRACT_NO_AUDIO=1")
    if "PAL_ESP_CORES3SE_NO_AUDIO=1" not in text or "PAL_ESP_CORES3SE_NO_SFX=1" not in text:
        errors.append(f"{rel}: engine host must compile with CoreS3 SE no-audio/no-SFX definitions")
    return errors


def check_core_no_audio_power_contract(root: Path) -> list[str]:
    rel = "esp32s3/main/cores3se_board.c"
    board = (root / rel).read_text(errors="replace")
    errors: list[str] = []

    if re.search(r"\boutput0_mask\s*=.*CORES3SE_AW9523_SPEAKER_ENABLE_MASK", board):
        errors.append(f"{rel}: CoreS3 SE no-audio target must not seed speaker power in output0_mask")
    if re.search(r"\boutput0_mask\s*=.*\|\s*CORES3SE_AW9523_SPEAKER_ENABLE_MASK", board):
        errors.append(f"{rel}: CoreS3 SE no-audio target must not OR speaker power into output0_mask")
    required_speaker_clear = (
        "aw_update(CORES3SE_AW9523_REG_OUTPUT0, output0_mask, "
        "CORES3SE_AW9523_SPEAKER_ENABLE_MASK)"
    )
    if required_speaker_clear not in board:
        errors.append(f"{rel}: CoreS3 SE no-audio target must explicitly clear AW9523 speaker power")
    return errors


def check_native_host_parity_contract(root: Path) -> list[str]:
    rel = "esp32s3/Makefile"
    text = (root / rel).read_text(errors="replace")
    engine_port_body = make_target_body(text, "engine-port-check")
    parity_body = make_target_body(text, "native-engine-host-parity-check")
    save_parity_body = make_target_body(text, "native-engine-host-save-parity-check")
    errors: list[str] = []

    if not parity_body:
        errors.append(f"{rel}: missing native-engine-host-parity-check target")
        return errors
    if not save_parity_body:
        errors.append(f"{rel}: missing native-engine-host-save-parity-check target")
        return errors
    if "native-engine-host-parity-check" not in engine_port_body:
        errors.append(f"{rel}: engine-port-check must run native-engine-host-parity-check")
    if "native-engine-host-save-parity-check" not in engine_port_body:
        errors.append(f"{rel}: engine-port-check must run native-engine-host-save-parity-check")
    if "native-engine-parity-check" not in engine_port_body:
        errors.append(f"{rel}: engine-port-check must run the Unix native contract parity gate")
    if "native-pack-provider-cache-smoke" not in engine_port_body:
        errors.append(f"{rel}: engine-port-check must run the TF pack-provider cache smoke")
    cache_smoke = root / "esp32s3" / "native_shim" / "pack_provider_cache_smoke.c"
    cache_text = cache_smoke.read_text(errors="replace") if cache_smoke.is_file() else ""
    if "fclose(rng)" not in cache_text:
        errors.append("esp32s3/native_shim/pack_provider_cache_smoke.c: cache smoke must verify pseudo pack fclose")
    deterministic = root / "unix" / "deterministic.c"
    deterministic_text = deterministic.read_text(errors="replace") if deterministic.is_file() else ""
    if "pal_deterministic_maybe_save_game" not in deterministic_text or "PAL_DETERMINISTIC_SAVE_FRAME" not in deterministic_text:
        errors.append("unix/deterministic.c: deterministic harness must support save-injection parity")
    if "pal_deterministic_maybe_reload_game" not in deterministic_text or "PAL_DETERMINISTIC_RELOAD_FRAME" not in deterministic_text:
        errors.append("unix/deterministic.c: deterministic harness must support save-load parity")
    if "pal_deterministic_have_last_battle" not in deterministic_text or 'pal_deterministic_emit_event("battle"' not in deterministic_text:
        errors.append("unix/deterministic.c: deterministic harness must emit battle transition events")
    if "viewport_x=%d" not in deterministic_text or "party_x=%d" not in deterministic_text:
        errors.append("unix/deterministic.c: deterministic frame traces must include scene position checkpoints")
    if "NATIVE_ENGINE_HOST_DETERMINISTIC_TARGET" not in parity_body:
        errors.append(f"{rel}: native host parity must run the deterministic app_main host binary")
    if "deterministic_trace_check.py" not in parity_body:
        errors.append(f"{rel}: native host parity must compare deterministic traces")
    for field in ("viewport_x", "viewport_y", "party_x", "party_y", "battle", "battle_result", "battle_enemies", "cash", "keys"):
        if f"--require-field {field}" not in parity_body:
            errors.append(f"{rel}: native host parity must require deterministic {field} checkpoints")
        if f"--require-field {field}" not in save_parity_body:
            errors.append(f"{rel}: native host save parity must require deterministic {field} checkpoints")
    if "native_shim/check_png.py" not in parity_body:
        errors.append(f"{rel}: native host parity must verify a CoreS3 SE PNG screenshot")
    if 'cmp -s "$$tmpd/stock.png" "$$tmpd/host-gpscreen.png"' not in parity_body:
        errors.append(f"{rel}: native host parity must compare stock and host gpScreen PNGs")
    if 'cmp -s "$$tmpd/stock.png" "$(abspath $(NATIVE_ENGINE_HOST_DETERMINISTIC_SCREENSHOT))"' not in parity_body:
        errors.append(f"{rel}: native host parity must compare stock PNG with target-flushed PNG")
    if "PAL_CORES3SE_NATIVE_NOR_PACK" not in parity_body or "PAL_CORES3SE_NATIVE_TF_PACK" not in parity_body:
        errors.append(f"{rel}: native host parity must use target NOR/TF pack shim paths")
    if "PAL_DETERMINISTIC_REPLAY" not in parity_body:
        errors.append(f"{rel}: native host parity must replay a deterministic route")
    if "PAL_DETERMINISTIC_SCREENSHOT" not in parity_body:
        errors.append(f"{rel}: native host parity must emit deterministic PNG screenshots")
    if "PAL_DETERMINISTIC_MAX_PRESENTS=$(NATIVE_ENGINE_BRIDGE_FRAMES)" not in parity_body:
        errors.append(f"{rel}: native host parity must run the full configured route length")
    if "NATIVE_ENGINE_HOST_DETERMINISTIC_CFLAGS" not in text or "-DPAL_DETERMINISTIC=1" not in text:
        errors.append(f"{rel}: deterministic native host must compile with PAL_DETERMINISTIC=1")
    if "PAL_CORES3SE_NATIVE_ENGINE_HOST=1" not in text:
        errors.append(f"{rel}: native host parity must use the native app_main host compile profile")
    if "engine_bridge/pal_engine_posix_shims.c" not in text:
        errors.append(f"{rel}: native host must link the target access() shim so FatFS save paths are verified")
    if "PAL_DETERMINISTIC_SAVE_FRAME" not in save_parity_body:
        errors.append(f"{rel}: native host save parity must inject a deterministic save")
    if "PAL_DETERMINISTIC_RELOAD_FRAME" not in save_parity_body:
        errors.append(f"{rel}: native host save parity must reload the deterministic save through target FatFS")
    if "PAL_CORES3SE_NATIVE_SAVE_DIR" not in save_parity_body:
        errors.append(f"{rel}: native host save parity must use the target FatFS save-directory shim")
    if "--require-event-tag save" not in save_parity_body:
        errors.append(f"{rel}: native host save parity must compare save events")
    if "--ignore-event-field save" not in save_parity_body:
        errors.append(f"{rel}: native host save parity must leave raw save bytes to pal_save_compare.py")
    if "! -iname '*.rpg'" not in save_parity_body:
        errors.append(f"{rel}: native host save parity must not symlink original RPG saves into the stock write dir")
    if "pal_save_compare.py" not in save_parity_body:
        errors.append(f"{rel}: native host save parity must compare canonical stock and FatFS-shim save files")
    if "reload frame=$(NATIVE_ENGINE_RELOAD_FRAME)" not in save_parity_body:
        errors.append(f"{rel}: native host save parity must assert the injected reload event")
    if "PAL_CORES3SE_NATIVE_SCREENSHOT_FRAME=$$(($(NATIVE_ENGINE_BRIDGE_FRAMES) + 100))" not in save_parity_body:
        errors.append(f"{rel}: native host save parity must disable early native screenshot exit")
    return errors


def check_default_engine_build_contract(root: Path) -> list[str]:
    make_rel = "esp32s3/Makefile"
    cmake_rel = "esp32s3/CMakeLists.txt"
    make_text = (root / make_rel).read_text(errors="replace")
    cmake_text = (root / cmake_rel).read_text(errors="replace")
    build_rule = make_target_rule(make_text, "build")
    check_rule = make_target_rule(make_text, "check")
    errors: list[str] = []

    if "set(PAL_CORES3SE_ENGINE_HOST ON CACHE BOOL" not in cmake_text:
        errors.append(f"{cmake_rel}: default ESP-IDF build must be the full-engine host")
    if "SCAFFOLD_BUILD_DIR ?= ../build-cores3se-scaffold" not in make_text:
        errors.append(f"{make_rel}: scaffold build must use a separate build directory")
    if "BUILD_DIR ?= $(SCAFFOLD_BUILD_DIR)" not in make_text:
        errors.append(f"{make_rel}: scaffold contract BUILD_DIR must default to SCAFFOLD_BUILD_DIR")
    if "engine-host-build" not in build_rule:
        errors.append(f"{make_rel}: build target must build the full-engine host")
    if "scaffold-build" not in check_rule:
        errors.append(f"{make_rel}: scaffold contract check must request the scaffold build explicitly")
    if "-DPAL_CORES3SE_ENGINE_HOST=0" not in make_text:
        errors.append(f"{make_rel}: scaffold build must explicitly opt out of the full-engine host")
    return errors


def address_is_in_section(address: int, size: int, ranges: dict[str, tuple[int, int]], names: tuple[str, ...]) -> bool:
    end = address + max(size, 1)
    for name in names:
        section = ranges.get(name)
        if section is None:
            continue
        if address >= section[0] and end <= section[1]:
            return True
    return False


def check_named_buffer_placement(
    rows: list[tuple[str, int, int]],
    ranges: dict[str, tuple[int, int]],
) -> tuple[list[str], int, int]:
    errors: list[str] = []
    sram_total = 0
    psram_total = 0

    for name, address, size in rows:
        if name.startswith("pal_sram_"):
            sram_total += size
            if not address_is_in_section(address, size, ranges, (".dram0.bss", ".dram0.data")):
                errors.append(f"{name} size={size} is not in DRAM SRAM sections")
        elif name.startswith("pal_psram_"):
            psram_total += size
            if not address_is_in_section(address, size, ranges, (".ext_ram.bss",)):
                errors.append(f"{name} size={size} is not in external PSRAM section")

    return errors, sram_total, psram_total


def check_engine_buffer_symbol_registry(
    root: Path,
    rows: list[tuple[str, int, int]],
) -> list[str]:
    registry_path = root / ENGINE_BUFFER_SYMBOL_REGISTRY
    errors: list[str] = []
    if not registry_path.is_file():
        return [f"engine buffer symbol registry missing: {registry_path}"]

    data = json.loads(registry_path.read_text(errors="replace"))
    entries = data.get("symbols", [])
    if not isinstance(entries, list):
        return [f"engine buffer symbol registry has no symbols list: {registry_path}"]

    actual = Counter(
        (name, size)
        for name, _address, size in rows
        if name.startswith("pal_sram_") or name.startswith("pal_psram_")
    )
    expected: Counter[tuple[str, int]] = Counter()
    for entry in entries:
        if not isinstance(entry, dict):
            errors.append(f"bad engine buffer registry entry: {entry!r}")
            continue
        name = entry.get("name")
        size = entry.get("size")
        region = entry.get("region")
        owner = entry.get("owner")
        if not isinstance(name, str) or not isinstance(size, int):
            errors.append(f"bad engine buffer registry entry: {entry!r}")
            continue
        if region not in {"SRAM", "PSRAM"}:
            errors.append(f"engine buffer registry entry {name} has bad region {region!r}")
        elif name.startswith("pal_sram_") and region != "SRAM":
            errors.append(f"engine buffer registry entry {name} must be in SRAM")
        elif name.startswith("pal_psram_") and region != "PSRAM":
            errors.append(f"engine buffer registry entry {name} must be in PSRAM")
        if not isinstance(owner, str) or not owner:
            errors.append(f"engine buffer registry entry {name} must have an owner")
        expected[(name, size)] += 1

    for key, count in sorted(expected.items()):
        actual_count = actual.get(key, 0)
        if actual_count < count:
            name, size = key
            errors.append(f"registered engine buffer symbol missing: {name} size={size}")
    for key, count in sorted(actual.items()):
        expected_count = expected.get(key, 0)
        if count > expected_count:
            name, size = key
            errors.append(f"unregistered engine buffer symbol: {name} size={size}")
    return errors


def has_object_for_source(obj_dir: Path, source: str) -> bool:
    suffix = Path(source).name + ".obj"
    return any(path.name == suffix for path in obj_dir.rglob(suffix))


def object_for_name(obj_dir: Path, name: str) -> Path | None:
    matches = sorted(path for path in obj_dir.rglob(name) if path.name == name)
    if len(matches) == 1:
        return matches[0]
    return None


def parse_stack_usage(build_dir: Path) -> tuple[list[tuple[int, str, str]], list[str]]:
    rows: list[tuple[int, str, str]] = []
    errors: list[str] = []
    stack_files = sorted((build_dir / "esp-idf" / "main").rglob("*.su"))

    if not stack_files:
        return rows, [f"no stack-usage files found under {build_dir / 'esp-idf' / 'main'}"]

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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default="..")
    parser.add_argument("--build-dir", default="../build-cores3se-engine-host")
    parser.add_argument("--tool-prefix", default="xtensa-esp32s3-elf-")
    parser.add_argument("--max-dram-bss", type=lambda s: int(s, 0), default=307200)
    parser.add_argument("--max-ext-ram-bss", type=lambda s: int(s, 0), default=8388608)
    parser.add_argument("--nor-pack", type=Path)
    parser.add_argument("--tf-pack", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    root = Path(args.root).resolve()
    build_dir = Path(args.build_dir).resolve()
    elf = build_dir / "sdlpal_cores3se.elf"
    obj_dir = build_dir / "esp-idf" / "main" / "CMakeFiles" / "__idf_main.dir"
    errors: list[str] = []

    if not elf.exists():
        errors.append(f"missing engine-host ELF: {elf}")
    if not obj_dir.exists():
        errors.append(f"missing engine-host object dir: {obj_dir}")
    if errors:
        for error in errors:
            print(error)
        return 1

    divergence_script = root / "tools" / "engine_divergence_check.py"
    divergence_manifest = root / "tools" / "engine_divergence_manifest.json"
    try:
        print(run_text(["python3", "-B", str(divergence_script), "--root", str(root), "--manifest", str(divergence_manifest)]), end="")
    except subprocess.CalledProcessError as exc:
        print(exc.output, end="")
        errors.append("engine divergence check failed")

    try:
        protected_sources = load_protected_sources(divergence_manifest)
    except (OSError, json.JSONDecodeError, KeyError) as exc:
        protected_sources = []
        errors.append(f"failed to load protected source manifest: {exc}")

    nm = args.tool_prefix + "nm"
    objdump = args.tool_prefix + "objdump"
    size = args.tool_prefix + "size"

    size_output = run_text([size, str(elf)])
    objdump_h = run_text([objdump, "-h", str(elf)])
    nm_sorted = run_text([nm, "-S", "--size-sort", str(elf)])
    nm_plain = run_text([nm, str(elf)])
    main_lib = build_dir / "esp-idf" / "main" / "libmain.a"
    main_lib_nm = run_text([nm, str(main_lib)])
    symbols = symbol_sizes(nm_sorted + "\n" + nm_plain)
    object_symbols = symbol_sizes(main_lib_nm)
    sections = section_sizes(objdump_h)
    ranges = section_ranges(objdump_h)
    rows = symbol_rows(nm_sorted)

    print("\n# Engine Host Check")
    print(f"elf: {elf}")
    print("\n## size")
    print(size_output, end="")
    print("\n## sections")
    for name in (".iram0.text", ".dram0.data", ".dram0.bss", ".flash.text", ".flash.rodata", ".ext_ram.bss"):
        print(f"{name:16s} {sections.get(name, 0)}")

    if sections.get(".dram0.bss", 0) > args.max_dram_bss:
        errors.append(f".dram0.bss {sections.get('.dram0.bss', 0)} > {args.max_dram_bss}")
    if sections.get(".ext_ram.bss", 0) > args.max_ext_ram_bss:
        errors.append(f".ext_ram.bss {sections.get('.ext_ram.bss', 0)} > {args.max_ext_ram_bss}")

    partition_hits = check_partition_contract(
        root,
        build_dir,
        args.nor_pack.resolve() if args.nor_pack is not None else None,
        args.tf_pack.resolve() if args.tf_pack is not None else None,
    )
    print(f"\npartition/pack layout hits: {len(partition_hits)}")
    for hit in partition_hits:
        print(hit)
    errors.extend(partition_hits)

    sdkconfig_hits = check_sdkconfig(root, build_dir)
    print(f"\nsdkconfig target hits: {len(sdkconfig_hits)}")
    for hit in sdkconfig_hits:
        print(hit)
    errors.extend(sdkconfig_hits)

    shared_spi_hits = check_shared_spi_contract(root)
    print(f"\nshared SPI/SDSPI hits: {len(shared_spi_hits)}")
    for hit in shared_spi_hits:
        print(hit)
    errors.extend(shared_spi_hits)

    board_init_hits = check_cores3se_board_init_contract(root)
    print(f"\nCoreS3 SE board init hits: {len(board_init_hits)}")
    for hit in board_init_hits:
        print(hit)
    errors.extend(board_init_hits)

    stdio_hits = check_target_stdio_surface(root, protected_sources)
    print(f"\ntarget stdio surface hits: {len(stdio_hits)}")
    for hit in stdio_hits:
        print(hit)
    errors.extend(stdio_hits)

    fatfs_stdio_hits = check_fatfs_stdio_contract(root)
    print(f"\nFatFS stdio contract hits: {len(fatfs_stdio_hits)}")
    for hit in fatfs_stdio_hits:
        print(hit)
    errors.extend(fatfs_stdio_hits)

    pack_provider_hits = check_pack_provider_dynamic_layout(root)
    print(f"\npack provider layout hits: {len(pack_provider_hits)}")
    for hit in pack_provider_hits:
        print(hit)
    errors.extend(pack_provider_hits)

    read_only_mmap_hits = check_read_only_pack_mmaps(root)
    print(f"\nread-only pack mmap hits: {len(read_only_mmap_hits)}")
    for hit in read_only_mmap_hits:
        print(hit)
    errors.extend(read_only_mmap_hits)

    target_video_hits = check_target_video_contract(root)
    print(f"\ntarget video bridge hits: {len(target_video_hits)}")
    for hit in target_video_hits:
        print(hit)
    errors.extend(target_video_hits)

    hardware_smoke_hits = check_hardware_smoke_contract(root)
    print(f"\nhardware smoke target hits: {len(hardware_smoke_hits)}")
    for hit in hardware_smoke_hits:
        print(hit)
    errors.extend(hardware_smoke_hits)

    native_host_parity_hits = check_native_host_parity_contract(root)
    print(f"\nnative host parity target hits: {len(native_host_parity_hits)}")
    for hit in native_host_parity_hits:
        print(hit)
    errors.extend(native_host_parity_hits)

    engine_host_source_hits = check_engine_host_source_set_contract(root)
    print(f"\nengine host source-set hits: {len(engine_host_source_hits)}")
    for hit in engine_host_source_hits:
        print(hit)
    errors.extend(engine_host_source_hits)

    no_audio_power_hits = check_core_no_audio_power_contract(root)
    print(f"\nCoreS3 SE no-audio power hits: {len(no_audio_power_hits)}")
    for hit in no_audio_power_hits:
        print(hit)
    errors.extend(no_audio_power_hits)

    default_build_hits = check_default_engine_build_contract(root)
    print(f"\ndefault engine build hits: {len(default_build_hits)}")
    for hit in default_build_hits:
        print(hit)
    errors.extend(default_build_hits)

    tf_map_hits = check_tf_map_capacity(root, args.manifest.resolve() if args.manifest is not None else None)
    print(f"\nTF map capacity hits: {len(tf_map_hits)}")
    for hit in tf_map_hits:
        print(hit)
    errors.extend(tf_map_hits)

    placement_hits, sram_total, psram_total = check_named_buffer_placement(rows, ranges)
    print(f"\nnamed buffer placement hits: {len(placement_hits)}")
    print(f"pal_sram_ total={sram_total} / {args.max_dram_bss}")
    print(f"pal_psram_ total={psram_total} / {args.max_ext_ram_bss}")
    for hit in placement_hits:
        print(hit)
    errors.extend(placement_hits)
    registry_hits = check_engine_buffer_symbol_registry(root, rows)
    print(f"\nengine buffer registry hits: {len(registry_hits)}")
    for hit in registry_hits:
        print(hit)
    errors.extend(registry_hits)

    missing_protected = [
        source
        for source in protected_sources
        if source not in PROTECTED_MISSING_EXCEPTIONS and not has_object_for_source(obj_dir, source)
    ]
    print(f"\nprotected engine object coverage: {len(protected_sources) - len(PROTECTED_MISSING_EXCEPTIONS) - len(missing_protected)}/{len(protected_sources) - len(PROTECTED_MISSING_EXCEPTIONS)}")
    for source in missing_protected:
        print(f"missing protected object: {source}")
    if missing_protected:
        errors.append("missing protected engine objects")

    missing_support = [
        source for source in REQUIRED_SUPPORT_SOURCES
        if not has_object_for_source(obj_dir, source)
    ]
    print(f"\nrequired support object coverage: {len(REQUIRED_SUPPORT_SOURCES) - len(missing_support)}/{len(REQUIRED_SUPPORT_SOURCES)}")
    for source in missing_support:
        print(f"missing support object: {source}")
    if missing_support:
        errors.append("missing required support objects")

    required_reloc_errors: list[str] = []
    required_reloc_count = 0
    required_reloc_hits = 0
    for object_name, targets in sorted(REQUIRED_OBJECT_RELOCS.items()):
        obj = object_for_name(obj_dir, object_name)
        required_reloc_count += len(targets)
        if obj is None:
            required_reloc_errors.append(f"missing object for required relocation scan: {object_name}")
            continue
        relocs = run_text([objdump, "-dr", str(obj)])
        for target in targets:
            if re.search(rf"\b{re.escape(target)}\b", relocs):
                required_reloc_hits += 1
            else:
                required_reloc_errors.append(f"{object_name}: missing relocation/call target {target}")
    print(f"\nrequired bridge relocation coverage: {required_reloc_hits}/{required_reloc_count}")
    for error in required_reloc_errors:
        print(error)
    if required_reloc_errors:
        errors.append("missing required bridge relocations")

    stack_rows, stack_errors = parse_stack_usage(build_dir)
    print("\n## stack usage")
    print(f"stack usage files: {len(list((build_dir / 'esp-idf' / 'main').rglob('*.su')))}")
    for size_value, location, qualifier in stack_rows[:10]:
        print(f"{size_value:8d} {location} {qualifier}".rstrip())
    for size_value, location, _qualifier in stack_rows:
        if size_value > STACK_STATIC_FRAME_BUDGET:
            stack_errors.append(
                f"static stack frame {location} uses {size_value}, over limit {STACK_STATIC_FRAME_BUDGET}"
            )
    if stack_errors:
        errors.extend(stack_errors)

    for symbol in REQUIRED_LINKED_SYMBOLS:
        if symbol not in symbols:
            errors.append(f"missing required engine-host symbol: {symbol}")
    for symbol in REQUIRED_OBJECT_SYMBOLS:
        if symbol not in object_symbols:
            errors.append(f"missing required engine-host object symbol: {symbol}")

    for line in nm_plain.splitlines():
        if FORBIDDEN_PAL_MKF_EXPORTS.search(line):
            errors.append(f"original PAL_MKF export linked: {line}")

    linked_forbidden = [
        symbol for symbol in FORBIDDEN_LINKED_SYMBOLS
        if symbol in symbols
    ]
    print(f"\nlinked runtime decompressor hits: {len(linked_forbidden)}")
    for symbol in linked_forbidden:
        print(symbol)
    if linked_forbidden:
        errors.append("linked runtime decompressor symbols")

    forbidden_prefix_hits = []
    for symbol in sorted(symbols):
        if symbol.startswith(FORBIDDEN_SYMBOL_PREFIXES):
            forbidden_prefix_hits.append(symbol)
    print(f"\nforbidden audio/sfx symbol hits: {len(forbidden_prefix_hits)}")
    for hit in forbidden_prefix_hits[:20]:
        print(hit)
    if forbidden_prefix_hits:
        errors.append("forbidden audio/sfx symbols linked")

    object_hits = []
    for obj in sorted(obj_dir.rglob("*.obj")):
        try:
            undefined = run_text([nm, "-u", str(obj)])
        except subprocess.CalledProcessError as exc:
            undefined = exc.output
        for line in undefined.splitlines():
            if FORBIDDEN_UNDEFINED.search(line):
                object_hits.append(f"{obj.relative_to(build_dir)}: {line.strip()}")
    print(f"\nforbidden project object undefined hits: {len(object_hits)}")
    for hit in object_hits[:20]:
        print(hit)
    if object_hits:
        errors.append("forbidden project object undefined calls")

    print(f"\nfatfs stdio slots: {symbols.get('pal_engine_fatfs_stdio_slots', 0)} bytes")

    if errors:
        print("\n## FAIL")
        for error in errors:
            print(error)
        return 1
    print("\n## PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
