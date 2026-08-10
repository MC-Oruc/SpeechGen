# SpeechGen

SpeechGen is an Unreal Engine 5.7 plugin for fully local English speech synthesis. It runs Kokoro-82M v1.0 FP16 through Unreal's `NNERuntimeORTCpu`, keeping GPU memory available for the language model.

## Runtime lifecycle

- Opening an editor project with SpeechGen enabled starts runtime preparation automatically.
- Project Settings > Plugins > SpeechGen exposes install/update and reinstall actions.
- Downloads are pinned to immutable upstream revisions and atomically promoted into `Saved/SpeechGen/Runtimes/Kokoro/Win64/v1.0-fp16` after required file sizes are complete.
- Game and Shipping builds require a complete installed runtime. Packaging stages it beside the executable as NonUFS data; shipped players never download model files.
- Runtime files and generated binaries are deliberately excluded from Git and Git LFS.

## Speech pipeline

SpeechGen accepts a `USpeechGenVoiceProfile`, text, and an optional directed style. Profiles can blend official English Kokoro voices. Styles may replace that blend and adjust speed, gain, and terminal pause without changing the model.

English grapheme-to-phoneme conversion is native C++. Known words use CMUdict; unknown words use Flite's CMU letter-to-sound model. No Python, eSpeak, Misaki, OpenPhonemizer, or external process is loaded at runtime.

### Misaki difference

The official Kokoro Python pipeline uses Misaki for richer contextual normalization and pronunciation. SpeechGen instead uses deterministic CMUdict plus Flite G2P so it can ship as a native, offline Unreal runtime with no interpreter. Diagnostics expose phonemization and inference failures explicitly. English prose and ordinary numbers are supported; language switching and author-defined pronunciation overrides are intentionally outside this version.

## Requirements

- Unreal Engine 5.7
- Windows x64
- Engine plugins `NNE` and `NNERuntimeORT`
- Internet access in the editor only while preparing or updating the runtime

## CPU performance decision record

The runtime model and thread policy were selected from local Windows benchmarks on an Intel Core i5-11400H using ONNX Runtime 1.20.1 CPU EP. Times exclude phonemization, request queueing, playback, and Unreal startup.

- The pinned FP32 graph reduced warm synthesis latency by about 2.2x versus the previous dynamic Q8 graph. The production FP16 graph preserved that FP32 throughput while reducing its measured process-memory delta from about 710 MB to 426-438 MB and its model file from 310 MB to 156 MB.
- Four intra-op threads provided the useful latency/CPU balance. For the same short input, FP32 measured 1312 ms at one thread, 833 ms at two threads, and 589 ms at four threads. Six and eight threads produced only marginal further gains on this six-core CPU, while twelve threads regressed.
- In three fresh-process FP16 trials, ORT model/session creation took 1075-1124 ms and the first short synthesis took 549-691 ms. Model loading starts before dialogue use, so synthesis latency is the player-facing figure once the runtime reports Ready.
- In a paired ten-run benchmark, FP16 and FP32 measured 2.32x and 2.31x realtime respectively. Earlier in-engine dynamic Q8 measurements were 0.87-0.94x realtime, so FP16 preserves the roughly 2.6x production-throughput improvement while materially reducing memory.
- FP16 and FP32 waveforms have identical sample counts but are not numerically identical. Runtime validation proves graph compatibility, not perceptual equivalence; representative voices must be auditioned after a model revision.
- A full static INT8 experiment was rejected because aggressive vocoder quantization produced noise. The community selective U8/S8 QOperator graph sounded equivalent in manual A/B but omitted a reproducible calibration/build recipe. `Tools/ModelBuild` records a selective, quality-gated fused `QGemm` recipe. In a clean three-round local benchmark, the community graph reached 3.16x realtime, the reproducible graph 3.00x, and production full FP16 2.59x. Neither mixed graph is adopted by the runtime without Unreal compatibility and listening acceptance.
- OpenVINO EP trials could not create a real provider session with the tested Windows package/ABI combination and fell back to CPU EP. The project does not carry an unproven OpenVINO dependency.
- This CPU exposes AVX2 and AVX-512F but not AVX-VNNI or AVX-512 VNNI. VNNI-specific speedup claims therefore do not apply to the measured machine.

Re-benchmark these decisions when the target CPU, ONNX Runtime version, or Kokoro graph changes. Measure session creation, first synthesis, warm synthesis, and process memory separately; a warm-only result is not sufficient for player-facing latency decisions.

See `ThirdParty-LICENSES.md` before redistribution.
