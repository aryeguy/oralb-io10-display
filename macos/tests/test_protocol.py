import sys
from pathlib import Path
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from oralb_protocol import parse_advertisement, parse_gatt_value, parse_motion


class AdvertisementTests(unittest.TestCase):
    def test_decodes_io_advertisement(self):
        payload = bytes([6, 54, 82, 3, 0, 0, 42, 0, 2, 12, 6])
        result = parse_advertisement(payload)
        self.assertIsNotNone(result)
        self.assertTrue(result["brushing"])
        self.assertEqual(result["elapsed_seconds"], 42)
        self.assertEqual(result["pressure"], "normal")
        self.assertEqual(result["pacer_sector"], 2)
        self.assertEqual(result["pacer_sector_count"], 6)

    def test_accepts_company_prefix_and_high_pressure(self):
        payload = b"\xdc\x00" + bytes([6, 54, 82, 3, 0x80, 1, 2, 4, 7, 3, 6])
        result = parse_advertisement(payload)
        self.assertEqual(result["elapsed_seconds"], 62)
        self.assertEqual(result["pressure"], "high")
        self.assertEqual(result["pacer_sector"], 6)

    def test_rejects_unknown_length(self):
        self.assertIsNone(parse_advertisement(b"\x00\x01"))

    def test_idle_advertisement_does_not_expose_pacer_as_position(self):
        payload = bytes([6, 50, 107, 8, 0x72, 0, 0, 1, 3, 0, 0])
        result = parse_advertisement(payload)
        self.assertFalse(result["brushing"])
        self.assertEqual(result["pacer_sector"], 0)


class GattTests(unittest.TestCase):
    def test_decodes_direct_values(self):
        self.assertEqual(parse_gatt_value("brushing_time", b"\x01\x05")["elapsed_seconds"], 65)
        self.assertEqual(parse_gatt_value("pressure", b"\x02")["pressure"], "high")
        sector = parse_gatt_value("sector", b"\x02\x09\x06", 6)
        self.assertEqual(sector["pacer_sector"], 3)
        self.assertEqual(sector["pacer_sector_timer"], 9)

    def test_decodes_comino_motion_without_claiming_position(self):
        payload = bytes([1, 0, 255, 2, 3, 4, 5, 6, 2, 0, 7, 8, 9, 10, 11, 12, 0, 0, 0x10, 0x80])
        result = parse_motion(payload)
        self.assertEqual(result["motion_format"], "comino_gyro_motion")
        self.assertEqual(result["motion_samples"][0]["gyro_x"], -1)
        self.assertNotIn("position", result)


if __name__ == "__main__":
    unittest.main()
