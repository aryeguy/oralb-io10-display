"""Mouth-surface classifier for Oral-B Comino IMU samples.

The brush does not transmit a zone enum. It streams timestamped gyroscope and
motion axes through FF0D. When the private APK model is present, it is used
directly; otherwise this module falls back to the public prior and optional
local calibration.
"""

from __future__ import annotations

from collections import Counter, defaultdict, deque
from datetime import datetime, timezone
import gzip
import json
import math
from pathlib import Path
import statistics
from typing import Any, Iterable

try:
    from official_comino import OfficialComino, ZONE_TO_SURFACE
except ImportError:  # optional until the vendor model is installed
    try:
        from .official_comino import OfficialComino, ZONE_TO_SURFACE
    except ImportError:
        OfficialComino = None  # type: ignore[misc,assignment]
        ZONE_TO_SURFACE = {}


SURFACE_LABELS = {
    1: "Upper left · Outside",
    2: "Upper left · Chewing",
    3: "Upper left · Inside",
    4: "Upper middle · Outside",
    5: "Upper middle · Inside",
    6: "Upper right · Inside",
    7: "Upper right · Chewing",
    8: "Upper right · Outside",
    9: "Lower left · Outside",
    10: "Lower left · Chewing",
    11: "Lower left · Inside",
    12: "Lower middle · Outside",
    13: "Lower middle · Inside",
    14: "Lower right · Inside",
    15: "Lower right · Chewing",
    16: "Lower right · Outside",
}

# A continuous path that minimizes large jumps while still sampling every
# visual surface independently.
CALIBRATION_ORDER = (1, 4, 8, 7, 6, 5, 3, 2, 10, 11, 13, 14, 15, 16, 12, 9)

AXES = ("gyro_x", "gyro_y", "gyro_z", "motion_x", "motion_y", "motion_z")
WINDOW_SIZE = 26
WINDOW_STEP = 13
SAMPLE_RATE = 27
MOVE_SAMPLES = SAMPLE_RATE * 2
COLLECT_SAMPLES = SAMPLE_RATE * 4
MODEL_VERSION = 1


def _mean(values: list[float]) -> float:
    return sum(values) / len(values)


def feature_vector(samples: Iterable[dict[str, int]]) -> list[float]:
    """Summarize one 26-sample motion window without external dependencies."""
    window = list(samples)
    if len(window) != WINDOW_SIZE:
        raise ValueError(f"position window must contain {WINDOW_SIZE} samples")

    features: list[float] = []
    for axis in AXES:
        values = [float(sample[axis]) for sample in window]
        mean = _mean(values)
        variance = _mean([(value - mean) ** 2 for value in values])
        diffs = [values[index] - values[index - 1] for index in range(1, len(values))]
        features.extend(
            (
                mean,
                math.sqrt(variance),
                min(values),
                max(values),
                _mean([abs(value) for value in values]),
                _mean([abs(value) for value in diffs]),
            )
        )

    for group in (AXES[:3], AXES[3:]):
        magnitudes = [
            math.sqrt(sum(float(sample[axis]) ** 2 for axis in group))
            for sample in window
        ]
        magnitude_mean = _mean(magnitudes)
        magnitude_variance = _mean(
            [(value - magnitude_mean) ** 2 for value in magnitudes]
        )
        features.extend((magnitude_mean, math.sqrt(magnitude_variance)))
    return features


def _distance(left: list[float], right: list[float]) -> float:
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(left, right)) / len(left))


class PositionEngine:
    """Deduplicate FF0D samples, guide calibration, classify, and track coverage."""

    def __init__(self, model_path: Path, public_model_path: Path | None = None) -> None:
        self.model_path = model_path
        self.public_model_path = public_model_path or (
            Path(__file__).resolve().parents[1] / "data" / "public_position_model.json.gz"
        )
        self.examples: list[tuple[int, list[float]]] = []
        self.normalization_mean: list[float] = []
        self.normalization_scale: list[float] = []
        self.distance_threshold = 0.0
        self.trained_at: str | None = None
        self.quality_score: float | None = None
        self.public_model_loaded = False
        self.public_acceptance_confidence = 0.62
        self.public_centroids: list[list[float]] = []
        self.public_residual_scale: list[float] = []
        self.public_between_class_scale: list[float] = []
        self._adapted_centroids: list[list[float]] = []
        self._adapted_scale: list[float] = []

        self.coverage = [0.0] * 16
        self.active_surface: int | None = None
        self.confidence = 0.0
        self.nearest_distance: float | None = None
        self._was_brushing = False
        self._last_timestamp: int | None = None
        self._prediction_samples: deque[dict[str, int]] = deque(maxlen=WINDOW_SIZE)
        self._prediction_stride = 0
        self._prediction_history: deque[int] = deque(maxlen=3)
        self.official_model = None
        if OfficialComino is not None:
            try:
                self.official_model = OfficialComino(
                    Path(__file__).resolve().parents[1] / "data" / "comino_models"
                )
            except (OSError, ValueError, RuntimeError, ImportError):
                self.official_model = None

        self.calibration_active = False
        self.calibration_stage = "not_started"
        self.calibration_index = 0
        self.calibration_move_remaining = MOVE_SAMPLES
        self.calibration_samples: list[dict[str, int]] = []
        self.calibration_examples: dict[int, list[list[float]]] = defaultdict(list)
        self.calibration_error: str | None = None

        self._load_public_prior()
        self._load()

    @property
    def trained(self) -> bool:
        return bool(self.examples and self.normalization_mean and self.normalization_scale)

    def _load(self) -> None:
        if not self.model_path.exists():
            return
        try:
            payload = json.loads(self.model_path.read_text())
            if payload.get("version") != MODEL_VERSION:
                return
            examples = [
                (int(item["surface"]), [float(value) for value in item["features"]])
                for item in payload["examples"]
            ]
            means = [float(value) for value in payload["normalization_mean"]]
            scales = [float(value) for value in payload["normalization_scale"]]
            if not examples or not means or len(means) != len(scales):
                return
            if any(len(vector) != len(means) for _, vector in examples):
                return
            self.examples = examples
            self.normalization_mean = means
            self.normalization_scale = scales
            self.distance_threshold = float(payload["distance_threshold"])
            self.trained_at = payload.get("trained_at")
            self.quality_score = float(
                payload.get("quality_score", self._leave_one_out_quality(examples))
            )
            self._build_adapted_model()
        except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError):
            self.examples = []

    def _load_public_prior(self) -> None:
        """Load the public brush-IMU prior, if the optional artifact is present."""
        if not self.public_model_path.exists():
            return
        try:
            with gzip.open(self.public_model_path, "rt", encoding="utf-8") as handle:
                payload = json.load(handle)
            if payload.get("kind") != "personalized_diagonal_mahalanobis":
                return
            centroids = payload["public_centroids"]
            residual_scale = payload["public_residual_scale"]
            between_scale = payload["public_between_class_scale"]
            if len(centroids) != 16 or any(len(row) != 40 for row in centroids):
                return
            if len(residual_scale) != 40 or len(between_scale) != 40:
                return
            self.public_centroids = [[float(value) for value in row] for row in centroids]
            self.public_residual_scale = [max(float(value), 1e-6) for value in residual_scale]
            self.public_between_class_scale = [max(float(value), 1e-6) for value in between_scale]
            self.public_acceptance_confidence = float(payload.get("acceptance_confidence", 0.62))
            self.public_model_loaded = True
        except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError):
            self.public_model_loaded = False

    def _build_adapted_model(self) -> None:
        """Align public feature-space centroids to this brush's local calibration."""
        if not (self.public_model_loaded and self.examples):
            self._adapted_centroids = []
            self._adapted_scale = []
            return
        target_centroids = []
        for surface in range(1, 17):
            vectors = [vector for label, vector in self.examples if label == surface]
            if not vectors:
                return
            # Local examples are normalized with the calibration-wide statistics.
            target_centroids.append([
                statistics.median(vector[index] for vector in vectors)
                * self.normalization_scale[index] + self.normalization_mean[index]
                for index in range(40)
            ])
        target_between = [
            max(statistics.pstdev([row[index] for row in target_centroids]), 1e-6)
            for index in range(40)
        ]
        self._adapted_centroids = target_centroids
        self._adapted_scale = [
            max(
                self.public_residual_scale[index]
                * min(20.0, max(0.05, target_between[index] / self.public_between_class_scale[index])),
                1e-6,
            )
            for index in range(40)
        ]

    def start_calibration(self) -> None:
        self.calibration_active = True
        self.calibration_stage = "waiting_for_brush"
        self.calibration_index = 0
        self.calibration_move_remaining = MOVE_SAMPLES
        self.calibration_samples = []
        self.calibration_examples = defaultdict(list)
        self.calibration_error = None
        self.active_surface = None
        self.confidence = 0.0
        self._reset_stream()

    def cancel_calibration(self) -> None:
        self.calibration_active = False
        self.calibration_stage = "cancelled"
        self.calibration_samples = []
        self.calibration_examples = defaultdict(list)
        self._reset_stream()

    def set_brushing(self, brushing: bool) -> None:
        if brushing and not self._was_brushing:
            if not self.calibration_active:
                self.coverage = [0.0] * 16
            self._reset_stream(keep_timestamp=True)
        elif not brushing and self._was_brushing:
            self.active_surface = None
            self.confidence = 0.0
            self._prediction_history.clear()
        self._was_brushing = brushing

    def _reset_stream(self, keep_timestamp: bool = False) -> None:
        if not keep_timestamp:
            self._last_timestamp = None
        self._prediction_samples.clear()
        self._prediction_stride = 0
        self._prediction_history.clear()
        if self.official_model is not None:
            self.official_model.reset()

    def ingest(self, samples: Iterable[dict[str, int]], brushing: bool) -> None:
        self.set_brushing(brushing)
        for sample in self._unique_samples(samples):
            if not brushing:
                continue
            if self.calibration_active:
                self._ingest_calibration(sample)
            elif self.official_model is not None:
                self._ingest_official(sample)
            elif self.trained:
                self._ingest_prediction(sample)

    def _unique_samples(
        self, samples: Iterable[dict[str, int]]
    ) -> Iterable[dict[str, int]]:
        for sample in samples:
            timestamp = int(sample["timestamp"])
            if self._last_timestamp == timestamp:
                continue
            if self._last_timestamp is not None:
                delta = (timestamp - self._last_timestamp) & 0xFFFF
                if delta == 0:
                    continue
                if delta > 1000:
                    self._reset_stream(keep_timestamp=True)
                    if self.calibration_active and self.calibration_stage == "collecting":
                        self.calibration_samples = []
            self._last_timestamp = timestamp
            yield sample

    def _ingest_calibration(self, sample: dict[str, int]) -> None:
        if self.calibration_stage == "waiting_for_brush":
            self.calibration_stage = "move"
            self.calibration_move_remaining = MOVE_SAMPLES

        if self.calibration_stage == "move":
            self.calibration_move_remaining -= 1
            if self.calibration_move_remaining <= 0:
                self.calibration_stage = "collecting"
                self.calibration_samples = []
            return

        if self.calibration_stage != "collecting":
            return
        self.calibration_samples.append(sample)
        if len(self.calibration_samples) < COLLECT_SAMPLES:
            return

        surface = CALIBRATION_ORDER[self.calibration_index]
        for start in range(0, COLLECT_SAMPLES - WINDOW_SIZE + 1, WINDOW_STEP):
            window = self.calibration_samples[start : start + WINDOW_SIZE]
            self.calibration_examples[surface].append(feature_vector(window))

        self.calibration_index += 1
        self.calibration_samples = []
        if self.calibration_index >= len(CALIBRATION_ORDER):
            try:
                self._train(self.calibration_examples)
                self.calibration_stage = "complete"
            except (ValueError, OSError) as exc:
                self.calibration_error = str(exc)
                self.calibration_stage = "error"
            self.calibration_active = False
            self._reset_stream(keep_timestamp=True)
            return

        self.calibration_stage = "move"
        self.calibration_move_remaining = MOVE_SAMPLES

    def _train(self, examples_by_surface: dict[int, list[list[float]]]) -> None:
        if set(examples_by_surface) != set(SURFACE_LABELS):
            raise ValueError("calibration did not capture every surface")
        raw_examples = [
            (surface, vector)
            for surface, vectors in examples_by_surface.items()
            for vector in vectors
        ]
        feature_count = len(raw_examples[0][1])
        columns = [[vector[index] for _, vector in raw_examples] for index in range(feature_count)]
        means = [_mean(column) for column in columns]
        scales = [max(statistics.pstdev(column), 1.0) for column in columns]

        normalized = [
            (surface, self._normalize(vector, means, scales))
            for surface, vector in raw_examples
        ]
        same_class_nearest = []
        for index, (surface, vector) in enumerate(normalized):
            distances = [
                _distance(vector, other)
                for other_index, (other_surface, other) in enumerate(normalized)
                if other_index != index and other_surface == surface
            ]
            if distances:
                same_class_nearest.append(min(distances))
        if not same_class_nearest:
            raise ValueError("not enough calibration windows")
        same_class_nearest.sort()
        percentile = same_class_nearest[min(len(same_class_nearest) - 1, int(len(same_class_nearest) * 0.95))]

        self.normalization_mean = means
        self.normalization_scale = scales
        self.examples = normalized
        self.distance_threshold = max(0.8, percentile * 3.0)
        self.quality_score = self._leave_one_out_quality(normalized)
        self.trained_at = datetime.now(timezone.utc).isoformat(timespec="seconds")
        self._build_adapted_model()
        self._save()

    @staticmethod
    def _leave_one_out_quality(
        examples: list[tuple[int, list[float]]]
    ) -> float:
        if len(examples) < 2:
            return 0.0
        correct = 0
        for index, (surface, vector) in enumerate(examples):
            nearest_surface = min(
                (
                    (_distance(vector, other), other_surface)
                    for other_index, (other_surface, other) in enumerate(examples)
                    if other_index != index
                ),
                key=lambda item: item[0],
            )[1]
            correct += nearest_surface == surface
        return correct / len(examples)

    @staticmethod
    def _normalize(
        vector: list[float], means: list[float], scales: list[float]
    ) -> list[float]:
        return [
            (value - means[index]) / scales[index]
            for index, value in enumerate(vector)
        ]

    def _save(self) -> None:
        self.model_path.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "version": MODEL_VERSION,
            "trained_at": self.trained_at,
            "distance_threshold": self.distance_threshold,
            "quality_score": self.quality_score,
            "normalization_mean": self.normalization_mean,
            "normalization_scale": self.normalization_scale,
            "examples": [
                {"surface": surface, "features": vector}
                for surface, vector in self.examples
            ],
        }
        temporary = self.model_path.with_suffix(".tmp")
        temporary.write_text(json.dumps(payload, separators=(",", ":")))
        temporary.replace(self.model_path)

    def _ingest_prediction(self, sample: dict[str, int]) -> None:
        self._prediction_samples.append(sample)
        if len(self._prediction_samples) < WINDOW_SIZE:
            return
        self._prediction_stride += 1
        if self._prediction_stride < WINDOW_STEP:
            return
        self._prediction_stride = 0

        surface, confidence, nearest = self.predict(feature_vector(self._prediction_samples))
        # The public benchmark's 0.62 gate is intentionally conservative for
        # offline scoring. For live feedback, 0.50 avoids a permanently blank
        # mouth while the UI still exposes the confidence value.
        confidence_floor = max(0.50, self.public_acceptance_confidence - 0.12) if self._adapted_centroids else 0.55
        accepted = surface if confidence >= confidence_floor and nearest <= self.distance_threshold else 0
        self._prediction_history.append(accepted)
        counts = Counter(value for value in self._prediction_history if value)
        if not counts:
            self.active_surface = None
            self.confidence = 0.0
            self.nearest_distance = nearest
            return
        winner, count = counts.most_common(1)[0]
        required = 2 if len(self._prediction_history) >= 2 else 1
        if count < required:
            self.active_surface = None
            self.confidence = confidence
            self.nearest_distance = nearest
            return
        self.active_surface = winner
        self.confidence = confidence
        self.nearest_distance = nearest
        dwell_seconds = WINDOW_STEP / SAMPLE_RATE
        target_seconds = 120 / 16
        self.coverage[winner - 1] = min(
            100.0,
            self.coverage[winner - 1] + 100 * dwell_seconds / target_seconds,
        )

    def _ingest_official(self, sample: dict[str, int]) -> None:
        self._prediction_samples.append(sample)
        if len(self._prediction_samples) < WINDOW_SIZE:
            return
        self._prediction_stride += 1
        if self._prediction_stride < WINDOW_STEP:
            return
        self._prediction_stride = 0
        zone, confidence, _ = self.official_model.predict(self._prediction_samples)
        surface = ZONE_TO_SURFACE.get(zone)
        # The vendor model explicitly reports out-of-mouth and aggregate
        # classes; those must never light up a detailed surface.
        accepted = surface if surface is not None and confidence >= 0.30 else 0
        self._prediction_history.append(accepted)
        counts = Counter(value for value in self._prediction_history if value)
        self.confidence = confidence
        self.nearest_distance = None
        if not counts:
            self.active_surface = None
            return
        winner, count = counts.most_common(1)[0]
        if count < (2 if len(self._prediction_history) >= 2 else 1):
            self.active_surface = None
            return
        self.active_surface = winner
        dwell_seconds = WINDOW_STEP / SAMPLE_RATE
        target_seconds = 120 / 16
        self.coverage[winner - 1] = min(100.0, self.coverage[winner - 1] + 100 * dwell_seconds / target_seconds)

    def predict(self, raw_vector: list[float]) -> tuple[int, float, float]:
        if not self.trained:
            raise ValueError("position model is not calibrated")
        if self._adapted_centroids:
            distances = [
                math.sqrt(
                    sum(
                        ((raw_vector[index] - centroid[index]) / self._adapted_scale[index]) ** 2
                        for index in range(len(raw_vector))
                    )
                    / len(raw_vector)
                )
                for centroid in self._adapted_centroids
            ]
            order = sorted(range(16), key=lambda index: distances[index])
            nearest_distance = distances[order[0]]
            other_distance = distances[order[1]]
            confidence = other_distance / (nearest_distance + other_distance + 0.0001)
            return order[0] + 1, confidence, nearest_distance

        vector = self._normalize(raw_vector, self.normalization_mean, self.normalization_scale)
        neighbors = sorted(
            (_distance(vector, example), surface)
            for surface, example in self.examples
        )
        nearest_distance, surface = neighbors[0]
        other_distance = next(
            distance
            for distance, other_surface in neighbors
            if other_surface != surface
        )
        confidence = other_distance / (nearest_distance + other_distance + 0.0001)
        return surface, confidence, nearest_distance

    def calibration_state(self, brushing: bool) -> dict[str, Any]:
        current_surface = (
            CALIBRATION_ORDER[self.calibration_index]
            if self.calibration_index < len(CALIBRATION_ORDER)
            else None
        )
        if self.calibration_stage == "move":
            remaining = self.calibration_move_remaining / SAMPLE_RATE
        elif self.calibration_stage == "collecting":
            remaining = (COLLECT_SAMPLES - len(self.calibration_samples)) / SAMPLE_RATE
        else:
            remaining = 0.0
        return {
            "active": self.calibration_active,
            "stage": self.calibration_stage,
            "brushing": brushing,
            "current_surface": current_surface,
            "current_label": SURFACE_LABELS.get(current_surface),
            "completed_surfaces": self.calibration_index,
            "total_surfaces": len(CALIBRATION_ORDER),
            "seconds_remaining": round(max(0.0, remaining), 1),
            "trained": self.trained,
            "trained_at": self.trained_at,
            "quality_score": (
                round(self.quality_score, 3)
                if self.quality_score is not None
                else None
            ),
            "model_source": "official_comino_apk" if self.official_model is not None else ("public_prior_plus_local_calibration" if self._adapted_centroids else "local_calibration"),
            "error": self.calibration_error,
        }

    def position_state(self) -> dict[str, Any]:
        if self.calibration_active:
            status = "calibrating"
        elif self.official_model is None and not self.trained:
            status = "needs_calibration"
        elif self.active_surface:
            status = "classified"
        elif self._was_brushing:
            status = "uncertain"
        else:
            status = "ready"
        return {
            "status": status,
            "active_surface": self.active_surface,
            "confidence": round(self.confidence, 3),
            "nearest_distance": (
                round(self.nearest_distance, 3)
                if self.nearest_distance is not None
                else None
            ),
            "coverage": list(self.coverage),
            "calibration_target": (
                CALIBRATION_ORDER[self.calibration_index]
                if self.calibration_active
                else None
            ),
            "model_source": "official_comino_apk" if self.official_model is not None else ("public_prior_plus_local_calibration" if self._adapted_centroids else ("local_calibration" if self.trained else None)),
        }
