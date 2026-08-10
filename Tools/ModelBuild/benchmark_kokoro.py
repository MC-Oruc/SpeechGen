# /// script
# requires-python = ">=3.11,<3.14"
# dependencies = [
#   "numpy==2.2.2",
#   "onnxruntime==1.20.1",
#   "psutil==6.1.1",
# ]
# ///

"""Compare a Kokoro candidate with its FP32 source before runtime adoption."""

from __future__ import annotations

import argparse
import json
import math
import statistics
import time
import wave
from pathlib import Path

import numpy as np
import onnxruntime as ort
import psutil


TOOL_DIR = Path(__file__).resolve().parent
SAMPLE_RATE = 24_000


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--voices-dir", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--candidate-wav", type=Path)
    parser.add_argument("--recipe", type=Path, default=TOOL_DIR / "kokoro_cpu_mixed.json")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--threads", type=int, default=4)
    return parser.parse_args()


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def inputs_for(entry: dict, index: int, recipe: dict, voices_dir: Path, vocab: dict[str, int]) -> dict:
    phonemes = entry["phonemes"]
    tokens = np.asarray([[0, *(vocab[character] for character in phonemes), 0]], dtype=np.int64)
    voice = recipe["calibration"]["voices"][index % len(recipe["calibration"]["voices"])]
    values = np.fromfile(voices_dir / f"{voice}.bin", dtype=np.float32).reshape(510, 256)
    style = values[len(phonemes) - 1].reshape(1, 256)
    speed = recipe["calibration"]["speeds"][index % len(recipe["calibration"]["speeds"])]
    return {"tokens": tokens, "style": style, "speed": np.asarray([speed], dtype=np.float32)}


def create_session(path: Path, threads: int) -> tuple[ort.InferenceSession, float, int]:
    options = ort.SessionOptions()
    options.intra_op_num_threads = threads
    options.inter_op_num_threads = 1
    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    process = psutil.Process()
    before = process.memory_info().rss
    started = time.perf_counter()
    session = ort.InferenceSession(str(path), options, providers=["CPUExecutionProvider"])
    return session, (time.perf_counter() - started) * 1000.0, process.memory_info().rss - before


def run_session(session: ort.InferenceSession, feed: dict, runs: int) -> tuple[np.ndarray, list[float]]:
    timings: list[float] = []
    audio: np.ndarray | None = None
    for _ in range(runs):
        started = time.perf_counter()
        audio = np.asarray(session.run(None, feed)[0], dtype=np.float32).reshape(-1)
        timings.append((time.perf_counter() - started) * 1000.0)
    assert audio is not None
    return audio, timings


def normalized_feature_time(feature: np.ndarray, frame_count: int = 128) -> np.ndarray:
    source = np.linspace(0.0, 1.0, feature.shape[0])
    target = np.linspace(0.0, 1.0, frame_count)
    return np.stack([np.interp(target, source, feature[:, index]) for index in range(feature.shape[1])], axis=1)


def spectral_features(audio: np.ndarray) -> tuple[np.ndarray, np.ndarray, float]:
    frame_size = 1024
    hop_size = 256
    padded = np.pad(audio, (frame_size // 2, frame_size // 2))
    frames = np.lib.stride_tricks.sliding_window_view(padded, frame_size)[::hop_size]
    windowed = frames * np.hanning(frame_size)
    power = np.abs(np.fft.rfft(windowed, axis=1)) ** 2
    frequencies = np.fft.rfftfreq(frame_size, 1.0 / SAMPLE_RATE)
    mel_edges = np.linspace(2595.0 * np.log10(1.0 + 40.0 / 700.0), 2595.0 * np.log10(1.0 + 12_000.0 / 700.0), 82)
    edge_hz = 700.0 * (10.0 ** (mel_edges / 2595.0) - 1.0)
    filters = np.zeros((frequencies.size, 80), dtype=np.float64)
    for index in range(80):
        left, center, right = edge_hz[index:index + 3]
        filters[:, index] = np.maximum(
            0.0,
            np.minimum((frequencies - left) / (center - left), (right - frequencies) / (right - center)),
        )
    mel_db = 10.0 * np.log10(np.maximum(power @ filters, 1e-12))
    mel_db -= mel_db.max()
    envelope = np.sqrt(np.mean(frames * frames, axis=1, keepdims=True))
    active = envelope[:, 0] > max(float(envelope.max()) * 0.02, 1e-5)
    flatness = np.exp(np.mean(np.log(np.maximum(power[active], 1e-12)), axis=1)) / np.maximum(np.mean(power[active], axis=1), 1e-12)
    return normalized_feature_time(mel_db), normalized_feature_time(envelope), float(np.median(flatness))


def quality_metrics(reference: np.ndarray, candidate: np.ndarray) -> dict[str, float | bool]:
    count = min(reference.size, candidate.size)
    left = reference[:count].astype(np.float64)
    right = candidate[:count].astype(np.float64)
    left_centered = left - left.mean()
    right_centered = right - right.mean()
    correlation = float(np.corrcoef(left_centered, right_centered)[0, 1]) if count > 1 else 0.0
    scale = float(np.dot(right_centered, left_centered) / max(np.dot(left_centered, left_centered), 1e-12))
    target = scale * left_centered
    noise = right_centered - target
    si_sdr = 10.0 * math.log10(max(np.dot(target, target), 1e-12) / max(np.dot(noise, noise), 1e-12))
    reference_mel, reference_envelope, reference_flatness = spectral_features(reference)
    candidate_mel, candidate_envelope, candidate_flatness = spectral_features(candidate)
    mel_rmse_db = float(np.sqrt(np.mean((reference_mel - candidate_mel) ** 2)))
    envelope_correlation = float(np.corrcoef(reference_envelope[:, 0], candidate_envelope[:, 0])[0, 1])
    return {
        "finite": bool(np.isfinite(candidate).all()),
        "duration_ratio": float(candidate.size / max(reference.size, 1)),
        "rms_ratio": float(np.sqrt(np.mean(right * right)) / max(np.sqrt(np.mean(left * left)), 1e-12)),
        "peak": float(np.max(np.abs(right))),
        "correlation": correlation,
        "si_sdr_db": float(si_sdr),
        "mel_rmse_db": mel_rmse_db,
        "envelope_correlation": envelope_correlation,
        "spectral_flatness_delta": abs(reference_flatness - candidate_flatness),
    }


def write_audio(path: Path, audio: np.ndarray) -> None:
    pcm = np.clip(audio, -1.0, 1.0)
    pcm = (pcm * 32767.0).astype("<i2")
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(pcm.tobytes())


def main() -> None:
    args = parse_args()
    recipe = load_json(args.recipe)
    vocab = load_json(args.config)["vocab"]
    reference_session, reference_load_ms, reference_rss = create_session(args.reference, args.threads)
    candidate_session, candidate_load_ms, candidate_rss = create_session(args.candidate, args.threads)

    results = []
    reference_total_ms = 0.0
    candidate_total_ms = 0.0
    first_pair: tuple[np.ndarray, np.ndarray] | None = None
    for index, entry in enumerate(recipe["calibration"]["samples"]):
        feed = inputs_for(entry, index, recipe, args.voices_dir, vocab)
        reference_audio, reference_times = run_session(reference_session, feed, args.runs)
        candidate_audio, candidate_times = run_session(candidate_session, feed, args.runs)
        reference_warm_ms = statistics.median(reference_times[1:])
        candidate_warm_ms = statistics.median(candidate_times[1:])
        reference_total_ms += reference_warm_ms
        candidate_total_ms += candidate_warm_ms
        metrics = quality_metrics(reference_audio, candidate_audio)
        results.append({
            "name": entry["name"],
            "audio_seconds": reference_audio.size / SAMPLE_RATE,
            "reference_first_ms": reference_times[0],
            "candidate_first_ms": candidate_times[0],
            "reference_warm_median_ms": reference_warm_ms,
            "candidate_warm_median_ms": candidate_warm_ms,
            "quality": metrics,
        })
        if first_pair is None:
            first_pair = (reference_audio, candidate_audio)

    thresholds = recipe["quality_thresholds"]
    quality_passed = all(
        item["quality"]["finite"]
        and thresholds["duration_ratio_min"] <= item["quality"]["duration_ratio"] <= thresholds["duration_ratio_max"]
        and thresholds["rms_ratio_min"] <= item["quality"]["rms_ratio"] <= thresholds["rms_ratio_max"]
        and item["quality"]["peak"] <= thresholds["peak_max"]
        and item["quality"]["mel_rmse_db"] <= thresholds["mel_rmse_db_max"]
        and item["quality"]["envelope_correlation"] >= thresholds["envelope_correlation_min"]
        and item["quality"]["spectral_flatness_delta"] <= thresholds["spectral_flatness_delta_max"]
        for item in results
    )
    report = {
        "quality_passed": quality_passed,
        "reference": {"load_ms": reference_load_ms, "shared_process_incremental_rss_bytes": reference_rss, "bytes": args.reference.stat().st_size},
        "candidate": {"load_ms": candidate_load_ms, "shared_process_incremental_rss_bytes": candidate_rss, "bytes": args.candidate.stat().st_size},
        "warm_speedup": reference_total_ms / candidate_total_ms,
        "samples": results,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2), encoding="utf-8")
    if args.candidate_wav and first_pair:
        write_audio(args.candidate_wav, first_pair[1])
    print(json.dumps({"quality_passed": quality_passed, "warm_speedup": report["warm_speedup"]}))
    if not quality_passed:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
