#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    shim = root / "esp32s3" / "native_engine_shim"
    if "--cflags" in sys.argv:
        print(
            f"-I{shim / 'include'} "
            "-DPAL_HEADLESS_SDL_SHIM=1 "
            "-DPAL_HAS_JOYSTICKS=0 "
            "-DPAL_HAS_TOUCH=0"
        )
        return 0
    if "--libs" in sys.argv:
        print(str(shim / "build" / "libsdlshim.a"))
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
