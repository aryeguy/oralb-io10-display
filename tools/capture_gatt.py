#!/usr/bin/env python3
"""
Experimental Oral-B iO GATT capture tool.

This is intentionally a capture/logger, not a guessed position decoder.

It connects directly to the toothbrush, reads known characteristics and
subscribes where notifications are supported. The important target for the
future mouth UI research is the raw `motion` characteristic. It is inertial
data, not a direct mouth-zone enum.

Usage:
    python3 -m pip install -r requirements.txt
    python3 capture_gatt.py

Power off / unplug the iO Sense charger while testing because the brush can
have a single active connection occupied by another client.
"""

import asyncio
import json
import time
from bleak import BleakClient, BleakScanner

SERVICE = "a0f0ff00-5047-4d53-8208-4f72616c2d42"

CHARACTERISTICS = {
    "status": "a0f0ff04-5047-4d53-8208-4f72616c2d42",
    "battery": "a0f0ff05-5047-4d53-8208-4f72616c2d42",
    "mode": "a0f0ff07-5047-4d53-8208-4f72616c2d42",
    "brushing_time": "a0f0ff08-5047-4d53-8208-4f72616c2d42",
    "sector": "a0f0ff09-5047-4d53-8208-4f72616c2d42",
    "pressure": "a0f0ff0b-5047-4d53-8208-4f72616c2d42",
    "motion": "a0f0ff0d-5047-4d53-8208-4f72616c2d42",
    "pacer_config": "a0f0ff26-5047-4d53-8208-4f72616c2d42",
}

async def find_brush():
    print("Searching for Oral-B Toothbrush...")
    devices = await BleakScanner.discover(timeout=10, return_adv=True)

    for address, pair in devices.items():
        device, adv = pair
        name = adv.local_name or device.name or ""
        if "Oral-B" in name or "Oral B" in name:
            print(f"Found {name} at {address}")
            return device

    raise RuntimeError("No Oral-B toothbrush found")

async def main():
    device = await find_brush()
    out = open("oralb_gatt_capture.jsonl", "a", buffering=1)

    async with BleakClient(device) as client:
        print("Connected.")
        services = client.services

        char_by_uuid = {}
        for service in services:
            for char in service.characteristics:
                char_by_uuid[char.uuid.lower()] = char

        async def log_value(name, uuid, value, source):
            row = {
                "time": time.time(),
                "name": name,
                "uuid": uuid,
                "source": source,
                "hex": bytes(value).hex(),
                "bytes": list(value),
            }
            print(json.dumps(row))
            out.write(json.dumps(row) + "\n")

        # Initial reads
        for name, uuid in CHARACTERISTICS.items():
            char = char_by_uuid.get(uuid.lower())
            if not char:
                print(f"{name:14} absent")
                continue
            if "read" in char.properties:
                try:
                    value = await client.read_gatt_char(char)
                    await log_value(name, uuid, value, "read")
                except Exception as exc:
                    print(f"{name:14} read failed: {exc}")

        # Notifications
        for name, uuid in CHARACTERISTICS.items():
            char = char_by_uuid.get(uuid.lower())
            if not char:
                continue
            if "notify" not in char.properties and "indicate" not in char.properties:
                continue

            def make_handler(n, u):
                def handler(_, data):
                    row = {
                        "time": time.time(),
                        "name": n,
                        "uuid": u,
                        "source": "notify",
                        "hex": bytes(data).hex(),
                        "bytes": list(data),
                    }
                    print(json.dumps(row))
                    out.write(json.dumps(row) + "\n")
                return handler

            try:
                await client.start_notify(char, make_handler(name, uuid))
                print(f"Subscribed: {name}")
            except Exception as exc:
                print(f"{name:14} notify failed: {exc}")

        print()
        print("Brush normally and deliberately visit surfaces.")
        print("Leave this running for a complete session. Ctrl+C when done.")

        try:
            while True:
                await asyncio.sleep(1)
        finally:
            out.close()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
