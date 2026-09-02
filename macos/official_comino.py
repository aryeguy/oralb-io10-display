"""Local runner for Oral-B's Comino position model shipped in the APK.

The APK supplies a recurrent TensorFlow Lite model.  This module mirrors the
app's live preprocessing (26 Hz, six IMU channels, vendor normalization) and
keeps the two recurrent states alive between windows.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Iterable

import numpy as np
from ai_edge_litert.interpreter import Interpreter


ZONE_NAMES = (
    "OUT_OF_MOUTH", "TOP_RIGHT_OUTSIDE", "TOP_RIGHT_ONSIDE", "BOTTOM_RIGHT_ONSIDE",
    "TOP_RIGHT_INSIDE", "TOP_LEFT_OUTSIDE", "TOP_LEFT_ONSIDE", "BOTTOM_LEFT_ONSIDE",
    "TOP_LEFT_INSIDE", "TOP_CENTER_OUTSIDE", "BOTTOM_CENTER_OUTSIDE",
    "TOP_CENTER_INSIDE", "BOTTOM_CENTER_INSIDE", "BOTTOM_RIGHT_OUTSIDE",
    "BOTTOM_RIGHT_INSIDE", "BOTTOM_LEFT_OUTSIDE", "BOTTOM_LEFT_INSIDE",
    "CENTER_OUTSIDE", "RIGHT_OUTSIDE", "LEFT_OUTSIDE",
)

# CominoZone -> the project's 16 displayed surfaces.
ZONE_TO_SURFACE = {1: 8, 2: 7, 4: 6, 13: 16, 3: 15, 14: 14,
                   5: 1, 6: 2, 8: 3, 15: 9, 7: 10, 16: 11,
                   9: 4, 11: 5, 10: 12, 12: 13}

MEAN = np.array([0.48881329, 0.0, -0.02702209, 0.0, -1.72588759, 0.0], dtype=np.float32)
SCALE = np.array([0.31810252, 0.56983057, 0.59904806, 67.53917287, 22.98771858, 27.34702038], dtype=np.float32)


class OfficialComino:
    def __init__(self, root: Path):
        model = root / "20210420-102833.tflite"
        state = root / "20210420-102833.json"
        self.interpreter = Interpreter(model_path=str(model), num_threads=2)
        self.interpreter.allocate_tensors()
        self.input_details = self.interpreter.get_input_details()
        self.output_details = self.interpreter.get_output_details()
        payload = json.loads(state.read_text())
        self.initial = [np.asarray(payload["init_1_in"], dtype=np.float32),
                        np.asarray(payload["init_2_in"], dtype=np.float32)]
        self.hidden = [a.copy() for a in self.initial]
        self.ready = True

    def reset(self):
        self.hidden = [a.copy() for a in self.initial]

    @staticmethod
    def _vector(sample: dict[str, int]) -> np.ndarray:
        # App order is calibrated accelerometer XYZ followed by gyro XYZ.
        accel = [float(sample[k]) * 0.03137255 for k in ("motion_x", "motion_y", "motion_z")]
        gyro = [float(sample[k]) * 4.48 for k in ("gyro_x", "gyro_y", "gyro_z")]
        return (np.asarray(accel + gyro, dtype=np.float32) - MEAN) / SCALE

    def predict(self, samples: Iterable[dict[str, int]]) -> tuple[int, float, str]:
        matrix = np.stack([self._vector(s) for s in samples]).astype(np.float32)[None, :, :]
        # Names in the APK are h0_2 then h0_1; this ordering is what the TFLite
        # signature exposes, while the JSON files use init_1/init_2 naming.
        self.interpreter.set_tensor(self.input_details[0]["index"], matrix)
        self.interpreter.set_tensor(self.input_details[1]["index"], self.hidden[0][None, :])
        self.interpreter.set_tensor(self.input_details[2]["index"], self.hidden[1][None, :])
        self.interpreter.invoke()
        outputs = [self.interpreter.get_tensor(d["index"]) for d in self.output_details]
        probs = outputs[0][0]
        self.hidden = [outputs[1][0].copy(), outputs[2][0].copy()]
        # Android sums each model's probabilities over all 26 timesteps and
        # chooses the strongest zone (CompositeNativeCominoPrediction.g()).
        scores = probs.sum(axis=0)
        zone = int(np.argmax(scores))
        confidence = float(scores[zone] / max(scores.sum(), 1e-9))
        return zone, confidence, ZONE_NAMES[zone]
