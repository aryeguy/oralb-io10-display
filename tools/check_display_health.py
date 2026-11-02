#!/usr/bin/env python3
"""Read-only sustained hardware check; no USB attachment or device reset.

Run after flashing. This checks reported firmware/LVGL progress, not whether
the physical pixels look correct. Keep the brush on to also test live input.
"""
import argparse
from datetime import datetime, timezone
import json
import time
from urllib.request import urlopen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://192.168.0.139:8765")
    parser.add_argument("--seconds", type=int, default=180)
    args = parser.parse_args()
    deadline = time.monotonic() + args.seconds
    previous = None
    samples = 0
    while time.monotonic() < deadline:
        with urlopen(args.url + "/api/state", timeout=5) as response:
            state = json.load(response)
        diagnostic = state.get("display_diagnostics", {})
        if not diagnostic:
            raise RuntimeError("No firmware diagnostics received")
        age = (datetime.now(timezone.utc) -
               datetime.fromisoformat(diagnostic["received_at"])).total_seconds()
        if age > 5:
            raise RuntimeError(f"Display heartbeat is {age:.1f}s old")
        if diagnostic["lock_failures"]:
            raise RuntimeError(f"Display lock failures: {diagnostic}")
        if diagnostic["render_brushing"] and diagnostic["brightness"] == 0:
            raise RuntimeError("Display went dark during an active rendered session")
        if diagnostic["stack_free"] < 512:
            raise RuntimeError(f"Low main-task stack headroom: {diagnostic}")
        if previous:
            if diagnostic["uptime_ms"] <= previous["uptime_ms"]:
                raise RuntimeError("Device rebooted or stopped reporting")
            for key in ("render_count", "refresh_count"):
                if diagnostic[key] <= previous[key]:
                    raise RuntimeError(f"{key} stopped advancing")
        print(json.dumps(diagnostic), flush=True)
        previous = diagnostic
        samples += 1
        time.sleep(min(5, max(0, deadline - time.monotonic())))
    print(f"PASS: {samples} samples over {args.seconds}s; no reboot or render stall.")


if __name__ == "__main__":
    main()
