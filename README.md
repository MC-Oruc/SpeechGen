# SpeechGen

SpeechGen is an Unreal Engine 5.7 plugin for fully local English speech synthesis. It runs Kokoro-82M v1.0 FP32 through Unreal's `NNERuntimeORTCpu`, keeping GPU memory available for the language model.

## Runtime lifecycle

- Opening an editor project with SpeechGen enabled starts runtime preparation automatically.
- Project Settings > Plugins > SpeechGen exposes install/update and reinstall actions.
- Downloads are pinned to immutable upstream revisions and atomically promoted into `Saved/SpeechGen/Runtimes/Kokoro/Win64/v1.0-fp32` after required file sizes are complete.
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

- The pinned FP32 graph reduced warm synthesis latency by about 2.2x versus the previous dynamic Q8 graph. Its measured process memory after one inference was about 682 MB versus 234 MB for Q8.
- Four intra-op threads provided the useful latency/CPU balance. For the same short input, FP32 measured 1312 ms at one thread, 833 ms at two threads, and 589 ms at four threads. Six and eight threads produced only marginal further gains on this six-core CPU, while twelve threads regressed.
- In three fresh-process trials, ORT model/session creation took 854-911 ms, the first short synthesis took 588-738 ms, and ready-to-first-waveform time was 1.50-1.59 seconds. The following synthesis took 568-675 ms.
- With four threads, representative FP32 segments measured an RTF of 0.414, or about 2.41x realtime. Earlier in-engine dynamic Q8 measurements were RTF 1.06-1.15, or 0.87-0.94x realtime. The measured production throughput therefore improved by roughly 2.6-2.8x.
- An experimental calibrated static INT8 QOperator graph improved only about 3-4% over dynamic Q8 and the stock quantizer did not complete cleanly because of an unresolved graph initializer. It was rejected instead of adding a custom quantization pipeline.
- OpenVINO EP trials could not create a real provider session with the tested Windows package/ABI combination and fell back to CPU EP. The project does not carry an unproven OpenVINO dependency.
- This CPU exposes AVX2 and AVX-512F but not AVX-VNNI or AVX-512 VNNI. VNNI-specific speedup claims therefore do not apply to the measured machine.

Re-benchmark these decisions when the target CPU, ONNX Runtime version, or Kokoro graph changes. Measure session creation, first synthesis, warm synthesis, and process memory separately; a warm-only result is not sufficient for player-facing latency decisions.

See `ThirdParty-LICENSES.md` before redistribution.
