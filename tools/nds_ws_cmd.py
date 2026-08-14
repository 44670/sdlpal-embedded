#!/usr/bin/env python3
"""Send raw commands to a running DeSmuME WS harness (shared client class)."""
import importlib.util
import json
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "nds_desmume_ws", str(Path(__file__).with_name("nds_desmume_ws.py")))
module = importlib.util.module_from_spec(spec)
sys.modules["nds_desmume_ws"] = module
spec.loader.exec_module(module)

port = int(sys.argv[1])
ws = module.WebSocket.connect(port, 10.0)
for command in sys.argv[2:]:
    reply_type, payload = ws.command(json.loads(command))
    print(reply_type, payload.decode("utf-8", "replace"))
