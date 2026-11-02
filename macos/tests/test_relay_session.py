from pathlib import Path
import sys
import unittest
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from backend import (
    MOTION_STOP_GRACE_SECONDS,
    NEW_SESSION_IDLE_SECONDS,
    TRANSPORT_LOSS_FAILSAFE_SECONDS,
    OralBBridge,
)


class RelaySessionTests(unittest.TestCase):
    def test_repeated_identical_timer_packets_do_not_freeze_clock(self):
        bridge = OralBBridge(no_ble=True)
        for i in range(41):
            now = 100.0 + i * 0.5
            values = {"brushing": False, "elapsed_seconds": 2}
            with patch("backend.time.monotonic", return_value=now):
                bridge._normalize_relay_session(values, has_motion=True, now=now)
                bridge._apply_update(values, source="esp32_ble", name="relay", device_id="esp32")
                state = bridge.public_state()["brush"]
            self.assertTrue(state["brushing"])
            self.assertEqual(state["elapsed_seconds"], 2 + int(i * 0.5))

    def setUp(self) -> None:
        self.bridge = OralBBridge(no_ble=True)
        self.bridge.brush["elapsed_seconds"] = 42
        self.bridge.brush["brushing"] = True
        self.bridge.position_engine.coverage[3] = 80.0
        self.bridge._last_motion_at = 100.0
        self.bridge._timer_anchor = (42, 100.0)

    def test_short_motion_gap_keeps_running_and_drops_stale_zero(self):
        values = {"brushing": False, "elapsed_seconds": 0}
        self.bridge._normalize_relay_session(values, has_motion=False, now=103.0)
        self.assertTrue(values["brushing"])
        self.assertNotIn("elapsed_seconds", values)
        self.assertEqual(self.bridge.position_engine.coverage[3], 80.0)

    def test_motion_resume_keeps_timer_and_coverage(self):
        self.bridge.brush["brushing"] = False
        self.bridge._timer_anchor = None
        values = {"brushing": False, "elapsed_seconds": 0}
        self.bridge._normalize_relay_session(values, has_motion=True, now=110.0)
        self.assertTrue(values["brushing"])
        self.assertEqual(values["elapsed_seconds"], 42)
        self.assertEqual(self.bridge.position_engine.coverage[3], 80.0)

    def test_long_idle_starts_clean_session(self):
        self.bridge.brush["brushing"] = False
        self.bridge._timer_anchor = None
        values = {"brushing": False, "elapsed_seconds": 0}
        self.bridge._normalize_relay_session(
            values,
            has_motion=True,
            now=100.0 + NEW_SESSION_IDLE_SECONDS,
        )
        self.assertTrue(values["brushing"])
        self.assertEqual(values["elapsed_seconds"], 0)
        self.assertEqual(self.bridge.position_engine.coverage, [0.0] * 16)

    def test_sixteen_second_gatt_dropout_does_not_hide_brushing_ui(self):
        self.bridge._advance_session_timeout(116.0)

        self.assertTrue(self.bridge.brush["brushing"])
        self.assertEqual(self.bridge._effective_elapsed(116.0), 58)
        self.assertEqual(self.bridge.position_engine.coverage[3], 80.0)

    def test_repeated_disconnect_reconnect_cycles_never_rewind_timer(self):
        displayed_times = []
        for disconnected_at, reconnected_at in ((108.0, 116.0), (124.0, 139.0), (147.0, 159.0)):
            self.bridge._advance_session_timeout(disconnected_at)
            displayed_times.append(self.bridge._effective_elapsed(disconnected_at))

            values = {"brushing": False, "elapsed_seconds": 0}
            self.bridge._normalize_relay_session(
                values,
                has_motion=True,
                now=reconnected_at,
            )
            self.bridge._apply_update(
                values,
                source="esp32_ble",
                name="ESP32 BLE relay",
                device_id="esp32",
            )
            displayed_times.append(self.bridge._effective_elapsed(reconnected_at))

        self.assertTrue(self.bridge.brush["brushing"])
        self.assertEqual(displayed_times, sorted(displayed_times))
        self.assertEqual(self.bridge.position_engine.coverage[3], 80.0)

    def test_explicit_idle_packet_stops_without_rewinding(self):
        now = 100.0 + MOTION_STOP_GRACE_SECONDS + 1.0
        values = {"brushing": False, "elapsed_seconds": 0}
        self.bridge._normalize_relay_session(values, has_motion=False, now=now)
        self.bridge._apply_update(
            values,
            source="esp32_ble",
            name="ESP32 BLE relay",
            device_id="esp32",
        )

        self.assertFalse(self.bridge.brush["brushing"])
        self.assertEqual(self.bridge.brush["elapsed_seconds"], 49)
        self.assertIsNone(self.bridge._timer_anchor)
        self.assertEqual(self.bridge.position_engine.coverage[3], 80.0)

    def test_silence_ends_session_only_at_transport_failsafe(self):
        self.bridge._advance_session_timeout(
            100.0 + TRANSPORT_LOSS_FAILSAFE_SECONDS - 0.1
        )
        self.assertTrue(self.bridge.brush["brushing"])

        self.bridge._advance_session_timeout(
            100.0 + TRANSPORT_LOSS_FAILSAFE_SECONDS
        )
        self.assertFalse(self.bridge.brush["brushing"])
        self.assertEqual(
            self.bridge.brush["elapsed_seconds"],
            42 + int(TRANSPORT_LOSS_FAILSAFE_SECONDS),
        )
        self.assertEqual(self.bridge.position_engine.coverage[3], 80.0)


if __name__ == "__main__":
    unittest.main()
