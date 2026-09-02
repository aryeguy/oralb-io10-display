#!/usr/bin/env python3
"""
Capture Oral-B BLE manufacturer advertisements as JSONL.

Useful before the ESP32 arrives:
    python3 -m pip install -r requirements.txt
    python3 capture_advertisements.py io10_advertisements.jsonl
"""

import asyncio
import json
import sys
import time
from bleak import BleakScanner

ORALB_COMPANY_ID = 0x00DC

async def main(path: str) -> None:
    print("Scanning for Oral-B advertisements. Ctrl+C to stop.")
    print(f"Writing: {path}")

    f = open(path, "a", buffering=1)

    def callback(device, adv):
        payload = adv.manufacturer_data.get(ORALB_COMPANY_ID)
        if payload is None:
            return

        row = {
            "time": time.time(),
            "address": device.address,
            "name": adv.local_name or device.name,
            "rssi": adv.rssi,
            "manufacturer_id": ORALB_COMPANY_ID,
            "payload_hex": bytes(payload).hex(),
        }
        f.write(json.dumps(row) + "\n")
        print(row)

    scanner = BleakScanner(callback)
    await scanner.start()

    try:
        while True:
            await asyncio.sleep(1)
    finally:
        await scanner.stop()
        f.close()

if __name__ == "__main__":
    output = sys.argv[1] if len(sys.argv) > 1 else "oralb_advertisements.jsonl"
    try:
        asyncio.run(main(output))
    except KeyboardInterrupt:
        pass
