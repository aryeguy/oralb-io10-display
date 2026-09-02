#!/usr/bin/env python3
"""Train a leakage-resistant 16-surface model from the Macquarie dataset.

Dataset:
  Z. Hussain et al., "Dataset for Toothbrushing Activity Using
  Brush-Attached and Wearable Sensors", CC BY 4.0.
  DOI: 10.17632/hx5kkkbr3j.1

The GitHub release contains precise labels for the continuous (setting 1)
sessions.  We use only the brush-attached accelerometer and gyroscope, resample
them to the iO stream rate, and split evaluation by person.  Adjacent windows
from a session therefore never appear on both sides of a validation fold.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import gzip
import json
from pathlib import Path
import re
import sys
from typing import Any

import numpy as np
from sklearn.metrics import accuracy_score, balanced_accuracy_score, confusion_matrix


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "macos"))

from position_classifier import AXES, WINDOW_SIZE  # noqa: E402


DATASET_DOI = "10.17632/hx5kkkbr3j.1"
DATASET_URL = "https://github.com/icl-mq/toothbrushing-dataset"
SAMPLE_RATE = 27.0
WINDOW_STEP = 13
EDGE_MARGIN_SECONDS = 0.75
MODEL_VERSION = 2

LABEL_TO_SURFACE = {
    "Left Upper Jaw Front": 1,
    "Left Upper Jaw Top": 2,
    "Left Upper Jaw Back": 3,
    "Upper Incisors Front": 4,
    "Upper Incisors Back": 5,
    "Right Upper Jaw Back": 6,
    "Right Upper Jaw Top": 7,
    "Right Upper Jaw Front": 8,
    "Left Lower Jaw Front": 9,
    "Left Lower Jaw Top": 10,
    "Left Lower Jaw Back": 11,
    "Lower Incisors Front": 12,
    "Lower Incisors Back": 13,
    "Right Lower Jaw Back": 14,
    "Right Lower Jaw Top": 15,
    "Right Lower Jaw Front": 16,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--dataset",
        type=Path,
        required=True,
        help="Path to the icl-mq/toothbrushing-dataset checkout",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "data" / "public_position_model.json.gz",
    )
    parser.add_argument(
        "--cache",
        type=Path,
        default=ROOT / "data" / "public_position_windows.npz",
    )
    parser.add_argument("--rebuild-cache", action="store_true")
    return parser.parse_args()


def _numeric_csv(path: Path) -> tuple[np.ndarray, np.ndarray]:
    """Return epoch seconds and three numeric sensor axes."""
    rows = np.genfromtxt(
        path,
        delimiter=",",
        skip_header=1,
        usecols=(0, 3, 4, 5),
        dtype=np.float64,
    )
    if rows.ndim != 2 or rows.shape[1] != 4:
        raise ValueError(f"unexpected sensor CSV layout: {path}")
    keep = np.all(np.isfinite(rows), axis=1)
    rows = rows[keep]
    return rows[:, 0] / 1000.0, rows[:, 1:]


def _timestamp(value: str) -> float:
    return datetime.strptime(value, "%Y-%m-%dT%H:%M:%S.%f%z").timestamp()


def _feature_vector(window: np.ndarray) -> np.ndarray:
    """Match the runtime's 40 statistical features for six IMU axes."""
    means = window.mean(axis=0)
    stds = window.std(axis=0)
    mins = window.min(axis=0)
    maxs = window.max(axis=0)
    mean_abs = np.abs(window).mean(axis=0)
    mean_abs_diff = np.abs(np.diff(window, axis=0)).mean(axis=0)
    per_axis = np.stack((means, stds, mins, maxs, mean_abs, mean_abs_diff), axis=1).reshape(-1)
    magnitudes = []
    for start in (0, 3):
        magnitude = np.linalg.norm(window[:, start : start + 3], axis=1)
        magnitudes.extend((magnitude.mean(), magnitude.std()))
    return np.concatenate((per_axis, np.asarray(magnitudes)))


def _session_identity(folder: Path) -> tuple[int, str] | None:
    match = re.match(r"S(?P<subject>\d+)-S(?P<session>\d+)-", folder.name)
    if not match:
        return None
    return int(match.group("subject")), folder.name


def _sensor_file(folder: Path, suffix: str) -> Path:
    matches = sorted(folder.glob(f"*-A-*-{suffix}.csv"))
    if len(matches) != 1:
        raise ValueError(f"expected one brush-attached {suffix} file in {folder}")
    return matches[0]


def extract_windows(dataset: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    data_root = dataset / "data"
    features: list[np.ndarray] = []
    labels: list[int] = []
    subjects: list[int] = []
    sessions: list[str] = []

    for folder in sorted(data_root.iterdir()):
        identity = _session_identity(folder)
        labels_path = folder / "labels.json"
        if not folder.is_dir() or identity is None or not labels_path.exists():
            continue
        subject, session = identity
        annotations = json.loads(labels_path.read_text())
        if set(annotations) != set(LABEL_TO_SURFACE):
            # The repository deliberately leaves unreliable sessions unlabelled;
            # a partial label set is not safe for a 16-way model.
            continue

        accel_t, accel = _numeric_csv(_sensor_file(folder, "A"))
        gyro_t, gyro = _numeric_csv(_sensor_file(folder, "G"))

        for label_name, (start_text, end_text) in annotations.items():
            start = max(
                _timestamp(start_text) + EDGE_MARGIN_SECONDS,
                accel_t[0],
                gyro_t[0],
            )
            end = min(
                _timestamp(end_text) - EDGE_MARGIN_SECONDS,
                accel_t[-1],
                gyro_t[-1],
            )
            if end - start < WINDOW_SIZE / SAMPLE_RATE:
                continue
            sample_times = np.arange(start, end, 1.0 / SAMPLE_RATE)
            axes = np.column_stack(
                [
                    np.interp(sample_times, gyro_t, gyro[:, axis])
                    for axis in range(3)
                ]
                + [
                    np.interp(sample_times, accel_t, accel[:, axis])
                    for axis in range(3)
                ]
            )
            for offset in range(0, len(axes) - WINDOW_SIZE + 1, WINDOW_STEP):
                features.append(_feature_vector(axes[offset : offset + WINDOW_SIZE]))
                labels.append(LABEL_TO_SURFACE[label_name])
                subjects.append(subject)
                sessions.append(session)

    if not features:
        raise ValueError(f"no labeled sessions found under {data_root}")
    return (
        np.stack(features),
        np.asarray(labels, dtype=np.int16),
        np.asarray(subjects, dtype=np.int16),
        np.asarray(sessions),
    )


def load_or_extract(
    dataset: Path, cache: Path, rebuild: bool
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    if cache.exists() and not rebuild:
        payload = np.load(cache)
        return payload["features"], payload["labels"], payload["subjects"], payload["sessions"]
    result = extract_windows(dataset)
    cache.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        cache,
        features=result[0],
        labels=result[1],
        subjects=result[2],
        sessions=result[3],
    )
    return result


def _centroids(features: np.ndarray, labels: np.ndarray) -> np.ndarray:
    return np.stack(
        [np.median(features[labels == surface], axis=0) for surface in range(1, 17)]
    )


def _prior(features: np.ndarray, labels: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    centroids = _centroids(features, labels)
    residuals = features - centroids[labels - 1]
    residual_scale = np.maximum(np.std(residuals, axis=0), 1e-6)
    between_class_scale = np.maximum(np.std(centroids, axis=0), 1e-6)
    return centroids, residual_scale, between_class_scale


def _distances(
    features: np.ndarray, centroids: np.ndarray, residual_scale: np.ndarray
) -> np.ndarray:
    return np.mean(
        ((features[:, None, :] - centroids[None, :, :]) / residual_scale) ** 2,
        axis=2,
    )


def _predict_with_confidence(
    features: np.ndarray, centroids: np.ndarray, residual_scale: np.ndarray
) -> tuple[np.ndarray, np.ndarray]:
    distances = _distances(features, centroids, residual_scale)
    order = np.argsort(distances, axis=1)
    nearest = distances[np.arange(len(features)), order[:, 0]]
    competing = distances[np.arange(len(features)), order[:, 1]]
    confidence = competing / (nearest + competing + 1e-9)
    return order[:, 0] + 1, confidence


def evaluate_uncalibrated(
    features: np.ndarray,
    labels: np.ndarray,
    subjects: np.ndarray,
) -> dict[str, Any]:
    predictions = np.zeros_like(labels)
    rows = []
    for subject in sorted(np.unique(subjects)):
        train = subjects != subject
        test = subjects == subject
        centroids, residual_scale, _ = _prior(features[train], labels[train])
        predictions[test], _ = _predict_with_confidence(
            features[test], centroids, residual_scale
        )
        rows.append(
            {
                "subject": int(subject),
                "accuracy": float(accuracy_score(labels[test], predictions[test])),
            }
        )
    return {
        "accuracy": float(accuracy_score(labels, predictions)),
        "balanced_accuracy": float(balanced_accuracy_score(labels, predictions)),
        "subjects": rows,
        "confusion_matrix": confusion_matrix(labels, predictions, labels=range(1, 17)).tolist(),
    }


def evaluate_personalized(
    features: np.ndarray,
    labels: np.ndarray,
    subjects: np.ndarray,
    sessions: np.ndarray,
    acceptance_confidence: float,
) -> dict[str, Any]:
    actual: list[np.ndarray] = []
    predicted: list[np.ndarray] = []
    confidences: list[np.ndarray] = []
    rows = []
    for subject in sorted(np.unique(subjects)):
        subject_sessions = sorted(np.unique(sessions[subjects == subject]))
        if len(subject_sessions) < 2:
            continue
        calibration = sessions == subject_sessions[0]
        test = (subjects == subject) & ~calibration
        source = subjects != subject

        _, source_residual_scale, source_between_scale = _prior(
            features[source], labels[source]
        )
        target_centroids = _centroids(features[calibration], labels[calibration])
        target_between_scale = np.maximum(np.std(target_centroids, axis=0), 1e-6)
        scale_ratio = np.clip(target_between_scale / source_between_scale, 0.05, 20.0)
        adapted_residual_scale = np.maximum(source_residual_scale * scale_ratio, 1e-6)
        subject_prediction, subject_confidence = _predict_with_confidence(
            features[test], target_centroids, adapted_residual_scale
        )
        rows.append(
            {
                "subject": int(subject),
                "calibration_session": str(subject_sessions[0]),
                "test_sessions": len(subject_sessions) - 1,
                "accuracy": float(accuracy_score(labels[test], subject_prediction)),
            }
        )
        actual.append(labels[test])
        predicted.append(subject_prediction)
        confidences.append(subject_confidence)

    actual_array = np.concatenate(actual)
    predicted_array = np.concatenate(predicted)
    confidence_array = np.concatenate(confidences)
    accepted = confidence_array >= acceptance_confidence
    return {
        "accuracy": float(accuracy_score(actual_array, predicted_array)),
        "balanced_accuracy": float(
            balanced_accuracy_score(actual_array, predicted_array)
        ),
        "accepted_accuracy": float(
            accuracy_score(actual_array[accepted], predicted_array[accepted])
        ),
        "accepted_fraction": float(np.mean(accepted)),
        "acceptance_confidence": acceptance_confidence,
        "subjects": rows,
        "confusion_matrix": confusion_matrix(
            actual_array, predicted_array, labels=range(1, 17)
        ).tolist(),
    }


def save_model(
    output: Path,
    public_centroids: np.ndarray,
    public_residual_scale: np.ndarray,
    public_between_class_scale: np.ndarray,
    uncalibrated_evaluation: dict[str, Any],
    personalized_evaluation: dict[str, Any],
    feature_count: int,
    training_rows: int,
    subjects: np.ndarray,
    sessions: np.ndarray,
) -> None:
    payload = {
        "version": MODEL_VERSION,
        "kind": "personalized_diagonal_mahalanobis",
        "classes": list(range(1, 17)),
        "feature_count": feature_count,
        "window_size": WINDOW_SIZE,
        "window_step": WINDOW_STEP,
        "sample_rate_hz": SAMPLE_RATE,
        "axes": list(AXES),
        "training": {
            "dataset_doi": DATASET_DOI,
            "dataset_url": DATASET_URL,
            "license": "CC BY 4.0",
            "rows": training_rows,
            "subjects": sorted(int(value) for value in set(subjects)),
            "session_count": len(set(sessions)),
            "uncalibrated_evaluation": uncalibrated_evaluation,
            "personalized_evaluation": personalized_evaluation,
        },
        "acceptance_confidence": personalized_evaluation["acceptance_confidence"],
        "public_centroids": np.round(public_centroids, 9).tolist(),
        "public_residual_scale": np.round(public_residual_scale, 9).tolist(),
        "public_between_class_scale": np.round(public_between_class_scale, 9).tolist(),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with gzip.open(output, "wt", encoding="utf-8", compresslevel=9) as handle:
        json.dump(payload, handle, separators=(",", ":"))


def main() -> int:
    args = parse_args()
    features, labels, subjects, sessions = load_or_extract(
        args.dataset.resolve(), args.cache.resolve(), args.rebuild_cache
    )
    print(
        f"windows={len(labels)} sessions={len(np.unique(sessions))} "
        f"subjects={len(np.unique(subjects))} features={features.shape[1]}"
    )
    uncalibrated = evaluate_uncalibrated(features, labels, subjects)
    personalized = evaluate_personalized(
        features, labels, subjects, sessions, acceptance_confidence=0.62
    )
    print(
        f"uncalibrated held-out-subject accuracy={uncalibrated['accuracy']:.3f} "
        f"balanced={uncalibrated['balanced_accuracy']:.3f}"
    )
    print(
        f"one-session-personalized accuracy={personalized['accuracy']:.3f}; "
        f"accepted={personalized['accepted_accuracy']:.3f} at "
        f"{personalized['accepted_fraction']:.3f} coverage"
    )
    public_centroids, public_residual_scale, public_between_class_scale = _prior(
        features, labels
    )
    save_model(
        args.output.resolve(),
        public_centroids,
        public_residual_scale,
        public_between_class_scale,
        uncalibrated,
        personalized,
        features.shape[1],
        len(labels),
        subjects,
        sessions,
    )
    print(f"saved {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
