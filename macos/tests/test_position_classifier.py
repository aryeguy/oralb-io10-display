from pathlib import Path
import sys
import tempfile
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from position_classifier import CALIBRATION_ORDER, PositionEngine, SURFACE_LABELS


def sample(timestamp: int, surface: int) -> dict[str, int]:
    return {
        "timestamp": timestamp & 0xFFFF,
        "gyro_x": surface * 4,
        "gyro_y": surface * -3,
        "gyro_z": surface * 2,
        "motion_x": 30 + surface * 3,
        "motion_y": -20 + surface * 2,
        "motion_z": 10 - surface * 2,
    }


class PositionCalibrationTests(unittest.TestCase):
    def test_guided_pass_trains_and_persists_all_surfaces(self):
        with tempfile.TemporaryDirectory() as directory:
            model_path = Path(directory) / "model.json"
            engine = PositionEngine(model_path)
            engine.start_calibration()

            timestamp = 0
            while engine.calibration_active:
                surface = CALIBRATION_ORDER[engine.calibration_index]
                timestamp += 37
                engine.ingest([sample(timestamp, surface)], brushing=True)

            self.assertTrue(engine.trained)
            self.assertTrue(model_path.exists())
            self.assertEqual({surface for surface, _ in engine.examples}, set(SURFACE_LABELS))
            self.assertEqual(engine.quality_score, 1.0)

            loaded = PositionEngine(model_path)
            self.assertTrue(loaded.trained)
            self.assertEqual(len(loaded.examples), len(engine.examples))

    def test_calibration_pauses_while_brush_is_off(self):
        with tempfile.TemporaryDirectory() as directory:
            engine = PositionEngine(Path(directory) / "model.json")
            engine.start_calibration()
            engine.ingest([sample(37, 1)], brushing=False)
            state = engine.calibration_state(brushing=False)
            self.assertEqual(state["stage"], "waiting_for_brush")
            self.assertEqual(state["completed_surfaces"], 0)


if __name__ == "__main__":
    unittest.main()
