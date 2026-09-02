"""Pure parsers for the Oral-B values used by the macOS bridge.

The physical-position-looking FF0D characteristic is deliberately exposed as
motion data. It is not decoded into mouth surfaces without the proprietary
classifier used by the Oral-B application.
"""

from __future__ import annotations

from typing import Any


ORALB_COMPANY_ID = 0x00DC

STATES = {
    0: "unknown",
    1: "initializing",
    2: "idle",
    3: "running",
    4: "charging",
    5: "setup",
    6: "flight_menu",
    7: "charge_forbidden",
    8: "selection_menu",
    9: "session_summary",
    10: "post_brushing_summary",
    113: "final_test",
    114: "pcb_test",
    115: "sleeping",
    116: "transport",
    117: "calibration_test",
}

MODES = {
    0: "daily_clean",
    1: "sensitive",
    2: "gum_care",
    3: "whiten",
    4: "intense",
    5: "super_sensitive",
    6: "tongue_clean",
    7: "off",
    8: "settings",
    9: "off",
    11: "smart_adapt",
    12: "gentle_white",
}

PRESSURES = {0: "low", 1: "normal", 2: "high"}


def _signed_byte(value: int) -> int:
    return value - 256 if value > 127 else value


def parse_advertisement(payload: bytes | bytearray) -> dict[str, Any] | None:
    """Decode the documented 9/11-byte Oral-B manufacturer value."""
    raw = bytes(payload)
    if len(raw) >= 2 and raw[:2] == b"\xdc\x00":
        raw = raw[2:]
    if len(raw) not in (9, 11):
        return None

    state_raw = raw[3]
    pressure_raw = raw[4]
    sector_raw = raw[8]
    sector_hint = raw[10] if len(raw) == 11 else 0
    sector = sector_raw & 0x07
    if sector == 7:
        sector = (sector_hint & 0x07) or 4

    return {
        "protocol_version": raw[0],
        "model_id": raw[1],
        "firmware_revision": raw[2],
        "state_raw": state_raw,
        "state": STATES.get(state_raw, f"unknown_state_{state_raw}"),
        "brushing": state_raw == 3,
        "pressure_raw": pressure_raw,
        "pressure": "high" if pressure_raw & 0x80 else "normal",
        "elapsed_seconds": raw[5] * 60 + raw[6],
        "mode_raw": raw[7],
        "mode": MODES.get(raw[7], f"mode_{raw[7]}"),
        "pacer_sector_raw": sector_raw,
        "pacer_sector": sector if state_raw == 3 else 0,
        "pacer_sector_timer": raw[9] if len(raw) == 11 else None,
        "pacer_sector_count": sector_hint & 0x07 if sector_hint else None,
        "payload_hex": raw.hex(),
    }


def parse_motion(payload: bytes | bytearray) -> dict[str, Any]:
    """Decode FF0D samples while keeping their meaning strictly inertial."""
    raw = bytes(payload)
    result: dict[str, Any] = {
        "motion_payload_hex": raw.hex(),
        "motion_payload_length": len(raw),
        "motion_format": "unknown",
        "motion_samples": [],
    }

    if len(raw) == 20 and raw[18:20] == b"\x10\x80":
        result["motion_format"] = "comino_gyro_motion"
        for offset in (0, 8):
            sample = raw[offset : offset + 8]
            result["motion_samples"].append(
                {
                    "timestamp": int.from_bytes(sample[0:2], "little"),
                    "gyro_x": _signed_byte(sample[2]),
                    "gyro_y": _signed_byte(sample[3]),
                    "gyro_z": _signed_byte(sample[4]),
                    "motion_x": _signed_byte(sample[5]),
                    "motion_y": _signed_byte(sample[6]),
                    "motion_z": _signed_byte(sample[7]),
                }
            )
    elif len(raw) == 20:
        result["motion_format"] = "four_axis_samples"
        for offset in range(0, 20, 5):
            sample = raw[offset : offset + 5]
            result["motion_samples"].append(
                {
                    "timestamp": int.from_bytes(sample[0:2], "little"),
                    "motion_x": _signed_byte(sample[2]),
                    "motion_y": _signed_byte(sample[3]),
                    "motion_z": _signed_byte(sample[4]),
                }
            )
    return result


def parse_gatt_value(
    name: str,
    payload: bytes | bytearray,
    configured_sector_count: int | None = None,
) -> dict[str, Any]:
    """Decode one known read or notification into browser-facing fields."""
    raw = bytes(payload)
    if name == "device_info" and len(raw) >= 3:
        return {
            "model_id": raw[0],
            "protocol_version": raw[1],
            "firmware_revision": raw[2],
        }
    if name == "state" and raw:
        return {
            "state_raw": raw[0],
            "state": STATES.get(raw[0], f"unknown_state_{raw[0]}"),
            "brushing": raw[0] == 3,
        }
    if name == "battery" and raw:
        result: dict[str, Any] = {"battery": raw[0]} if raw[0] <= 100 else {}
        if len(raw) >= 3:
            remaining = int.from_bytes(raw[1:3], "little")
            if remaining != 0xFFFF:
                result["battery_time_remaining"] = remaining
        if len(raw) >= 5:
            result["battery_voltage"] = int.from_bytes(raw[3:5], "little") / 1000
        if len(raw) >= 7:
            current = int.from_bytes(raw[5:7], "little", signed=True)
            if current != -1:
                result["battery_current"] = current
        if len(raw) >= 8:
            result["battery_temperature"] = _signed_byte(raw[7])
        return result
    if name == "mode" and raw:
        return {"mode_raw": raw[0], "mode": MODES.get(raw[0], f"mode_{raw[0]}")}
    if name == "brushing_time" and len(raw) >= 2:
        return {"elapsed_seconds": raw[0] * 60 + raw[1]}
    if name == "sector" and raw:
        total = configured_sector_count
        if total is None and len(raw) >= 3 and 1 <= raw[2] <= 8:
            total = raw[2]
        if raw[0] == 0xF0:
            sector = 0
        elif raw[0] == 0xFF:
            sector = total or 4
        else:
            sector = (raw[0] & 0x07) + 1
        return {
            "pacer_sector_raw": raw[0],
            "pacer_sector": sector,
            "pacer_sector_timer": raw[1] if len(raw) >= 2 else None,
            "pacer_sector_count": total,
        }
    if name == "pressure" and raw:
        result = {
            "pressure_raw": raw[0],
            "pressure": PRESSURES.get(raw[0], f"unknown_{raw[0]}"),
        }
        if len(raw) >= 5:
            result["pressure_force"] = int.from_bytes(raw[3:5], "little", signed=True)
        return result
    if name == "pacer_config":
        times = [value for value in raw if 0 < value < 0xFF]
        return {
            "pacer_sector_times": times,
            "pacer_sector_count": len(times) or configured_sector_count,
            "target_seconds": sum(times) if times else None,
        }
    if name == "motion":
        return parse_motion(raw)
    return {"payload_hex": raw.hex()}
