# Kokoro CPU model recipe

This developer-only toolset reproduces and evaluates selective Kokoro v1.0 CPU quantization. It is not compiled, loaded, or staged by SpeechGen at runtime.

## Why this recipe exists

The community file commonly named `kokoro-v1.0.fp16.onnx` is not a full FP16 graph. Inspection shows a static mixed graph with 51 `QLinearConv` and 39 `QLinearMatMul` operations, U8 activations, per-channel S8 weights, and reduced-range weights. The main generator residual blocks, transposed convolutions, final convolution, and STFT remain floating point. This explains its useful speed without the noise produced by quantizing the complete vocoder.

The published artifact does not include its calibration corpus, exact build command, dependency lock, or acceptance gates. This directory records those missing decisions. The recipe intentionally selects graph nodes by stable ownership paths and fails if the expected graph shape changes.

The local recipe keeps 37 affine layers as fused `QGemm` operations instead of expanding them into quantized matrix multiplication and add chains. Its audited graph contains 51 `QLinearConv`, 37 `QGemm`, and two `QLinearMatMul` operations. On the reference i5-11400H it passed the signal gate and rejected the known noise-only full-static model. A clean three-round benchmark with rotated model order measured 3.00x realtime for this graph, 3.16x for the community mixed graph, and 2.59x for the production full-FP16 graph. The fused recipe is reproducible and quality-gated, but it does not outperform the community graph on this CPU.

## Build

Install `uv`, then obtain the pinned FP32 graph, `kokoro_config.json`, and SpeechGen raw voice files. Keep all generated files outside the plugin repository.

```powershell
uv run .\Plugins\SpeechGen\Tools\ModelBuild\build_kokoro_mixed.py `
  --model "$env:TEMP\SpeechGenModelBuild\kokoro-v1.0.onnx" `
  --voices-dir ".\Saved\SpeechGen\Runtimes\Kokoro\Win64\v1.0-cpu-mixed.1\voices" `
  --config ".\Saved\SpeechGen\Runtimes\Kokoro\Win64\v1.0-cpu-mixed.1\kokoro_config.json" `
  --work-dir "$env:TEMP\SpeechGenModelBuild\work" `
  --output "$env:TEMP\SpeechGenModelBuild\kokoro-v1.0.cpu-mixed.onnx"
```

PEP 723 metadata pins the Python build dependencies. The recipe pins the upstream source revision, expected file size, node policy, calibration phonemes, voices, speeds, and quantization parameters.

## Acceptance

Run the candidate against the exact FP32 source with the ONNX Runtime version used by Unreal:

```powershell
uv run .\Plugins\SpeechGen\Tools\ModelBuild\benchmark_kokoro.py `
  --reference "$env:TEMP\SpeechGenModelBuild\kokoro-v1.0.onnx" `
  --candidate "$env:TEMP\SpeechGenModelBuild\kokoro-v1.0.cpu-mixed.onnx" `
  --voices-dir ".\Saved\SpeechGen\Runtimes\Kokoro\Win64\v1.0-cpu-mixed.1\voices" `
  --config ".\Saved\SpeechGen\Runtimes\Kokoro\Win64\v1.0-cpu-mixed.1\kokoro_config.json" `
  --report ".\Saved\SpeechGen\Benchmarks\Kokoro_CPU_Mixed.json" `
  --candidate-wav ".\Saved\SpeechGen\Benchmarks\Kokoro_CPU_Mixed.wav"
```

The generated WAV contains the candidate only. Compare it with a separately named reference file; concatenating both takes into one unlabelled WAV makes the sentence appear to repeat.

Adoption requires all automated signal gates, lower representative warm latency, a runtime compatibility test through `NNERuntimeORTCpu`, and human A/B listening across representative voices. File size or tensor validity alone is never a quality result.

## Safe extension path

Profile first. Add one cohesive node group at a time and retain the floating-point vocoder boundary unless listening and signal gates prove otherwise. Do not blanket-convert the mixed graph to FP16: generic converters can clamp semantic constants such as the 24000 Hz sample rate. Do not quantize `ConvTranspose`, STFT, `generator/resblocks.*`, or `generator/conv_post` as a bulk operation; the rejected full-static experiment produced pure noise there.
