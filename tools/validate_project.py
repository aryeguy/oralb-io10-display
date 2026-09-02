#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

required = [
    "README.md",
    "firmware/CMakeLists.txt",
    "firmware/main/idf_component.yml",
    "firmware/main/app_main.cpp",
    "firmware/main/ui.cpp",
    "firmware/main/ble_source.cpp",
    "firmware/main/oralb_adv_parser.cpp",
    "simulator/index.html",
    "tools/capture_gatt.py",
    "macos/backend.py",
    "macos/oralb_protocol.py",
    "macos/position_classifier.py",
    "macos/requirements.txt",
    "simulator/app.js",
    "simulator/styles.css",
    "simulator/og.png",
    "start_macos.command",
]

missing = [p for p in required if not (ROOT / p).exists()]
if missing:
    raise SystemExit("Missing files: " + ", ".join(missing))

html = (ROOT / "simulator/index.html").read_text()
surface_count = len(re.findall(r'data-z="\d+"', html))
if surface_count != 16:
    raise SystemExit(f"Expected 16 simulator surfaces, got {surface_count}")

parser = (ROOT / "firmware/main/oralb_adv_parser.cpp").read_text()
for token in ["data[3]", "data[4]", "data[5]", "data[6]", "data[7]", "data[8]"]:
    if token not in parser:
        raise SystemExit(f"Parser missing expected field {token}")

print("Project structure OK")
print("Simulator surfaces:", surface_count)
