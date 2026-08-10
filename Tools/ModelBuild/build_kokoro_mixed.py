# /// script
# requires-python = ">=3.11,<3.14"
# dependencies = [
#   "numpy==2.2.2",
#   "onnx==1.22.0",
#   "onnxruntime==1.23.2",
# ]
# ///

"""Build SpeechGen's selective Kokoro CPU quantization candidate."""

from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path

import numpy as np
import onnx
from onnxruntime.quantization import (
    CalibrationDataReader,
    CalibrationMethod,
    QuantFormat,
    QuantType,
    quantize_static,
)
from onnxruntime.quantization.shape_inference import quant_pre_process


TOOL_DIR = Path(__file__).resolve().parent


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True, type=Path, help="Kokoro v1.0 FP32 ONNX graph")
    parser.add_argument("--voices-dir", required=True, type=Path, help="Directory containing raw voice .bin files")
    parser.add_argument("--config", required=True, type=Path, help="Kokoro config containing the vocabulary")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--recipe", type=Path, default=TOOL_DIR / "kokoro_cpu_mixed.json")
    return parser.parse_args()


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def encode_phonemes(phonemes: str, vocab: dict[str, int]) -> np.ndarray:
    unknown = sorted({character for character in phonemes if character not in vocab})
    if unknown:
        raise ValueError(f"Calibration phonemes contain unsupported characters: {unknown}")
    return np.asarray([[0, *(vocab[character] for character in phonemes), 0]], dtype=np.int64)


def load_style(voices_dir: Path, voice: str, phoneme_count: int) -> np.ndarray:
    values = np.fromfile(voices_dir / f"{voice}.bin", dtype=np.float32)
    expected_values = 510 * 256
    if values.size != expected_values:
        raise ValueError(f"Voice {voice} contains {values.size} floats; expected {expected_values}")
    return values.reshape(510, 256)[phoneme_count - 1].reshape(1, 256)


class KokoroCalibrationReader(CalibrationDataReader):
    def __init__(self, recipe: dict, voices_dir: Path, vocab: dict[str, int]) -> None:
        self._samples: list[dict[str, np.ndarray]] = []
        voices = recipe["calibration"]["voices"]
        speeds = recipe["calibration"]["speeds"]
        for index, entry in enumerate(recipe["calibration"]["samples"]):
            tokens = encode_phonemes(entry["phonemes"], vocab)
            phoneme_count = tokens.shape[1] - 2
            voice = voices[index % len(voices)]
            speed = speeds[index % len(speeds)]
            self._samples.append(
                {
                    "tokens": tokens,
                    "style": load_style(voices_dir, voice, phoneme_count),
                    "speed": np.asarray([speed], dtype=np.float32),
                }
            )
        self._iterator = iter(self._samples)

    def get_next(self) -> dict[str, np.ndarray] | None:
        return next(self._iterator, None)

    def rewind(self) -> None:
        self._iterator = iter(self._samples)


def select_nodes(model_path: Path, recipe: dict) -> list[str]:
    graph = onnx.load(str(model_path), load_external_data=False).graph
    rules = recipe["selection"]
    selected: list[str] = []
    for node in graph.node:
        rule = rules.get(node.op_type)
        if not rule:
            continue
        included = any(node.name.startswith(prefix) for prefix in rule.get("include_prefixes", []))
        excluded = any(node.name.startswith(prefix) for prefix in rule.get("exclude_prefixes", []))
        exact = node.name in rule.get("include_exact", [])
        if (included or exact) and not excluded:
            selected.append(node.name)
    expected = recipe["expected_quantized_nodes"]
    if len(selected) != expected:
        raise ValueError(f"Recipe selected {len(selected)} nodes; expected {expected}. Source graph changed.")
    return selected


def main() -> None:
    args = parse_args()
    recipe = load_json(args.recipe)
    expected_source_bytes = recipe["source"]["model_bytes"]
    if args.model.stat().st_size != expected_source_bytes:
        raise ValueError(
            f"Source model is {args.model.stat().st_size} bytes; expected {expected_source_bytes}. Source graph changed."
        )
    vocab = load_json(args.config)["vocab"]
    selected_nodes = select_nodes(args.model, recipe)

    args.work_dir.mkdir(parents=True, exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    preprocessed = args.work_dir / "kokoro-v1.0.preprocessed.onnx"
    quant_pre_process(
        str(args.model),
        str(preprocessed),
        skip_optimization=False,
        skip_onnx_shape=False,
        skip_symbolic_shape=True,
        auto_merge=True,
    )

    quantize_static(
        str(preprocessed),
        str(args.output),
        KokoroCalibrationReader(recipe, args.voices_dir, vocab),
        quant_format=QuantFormat.QOperator,
        activation_type=QuantType.QUInt8,
        weight_type=QuantType.QInt8,
        per_channel=True,
        reduce_range=True,
        nodes_to_quantize=selected_nodes,
        calibrate_method=CalibrationMethod.MinMax,
        extra_options={"ActivationSymmetric": False, "WeightSymmetric": True},
    )
    output_model = onnx.load(str(args.output), load_external_data=False)
    onnx.checker.check_model(output_model, full_check=True)
    operator_counts = collections.Counter(node.op_type for node in output_model.graph.node)
    actual_quantized_operators = {
        name: operator_counts[name] for name in recipe["expected_quantized_operators"]
    }
    if actual_quantized_operators != recipe["expected_quantized_operators"]:
        raise ValueError(
            f"Quantized operator audit failed: {actual_quantized_operators}; "
            f"expected {recipe['expected_quantized_operators']}"
        )
    print(json.dumps({
        "output": str(args.output),
        "bytes": args.output.stat().st_size,
        "selected_nodes": len(selected_nodes),
        "quantized_operators": actual_quantized_operators,
    }))


if __name__ == "__main__":
    main()
