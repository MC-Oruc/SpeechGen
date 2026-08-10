# SpeechGen

SpeechGen is an Unreal Engine 5.7 plugin for fully local multilingual speech synthesis. It runs the single supported SpeechGen Kokoro-82M v1.0 CPU mixed model through Unreal's `NNERuntimeORTCpu`, keeping GPU memory available for the language model.

## Runtime lifecycle

- Opening an editor project with SpeechGen enabled starts runtime preparation automatically.
- Project Settings > Plugins > SpeechGen exposes install/update and reinstall actions.
- The editor downloads only `v1.0-cpu-mixed.2` from the public SpeechGen runtime release and atomically promotes it into `Saved/SpeechGen/Runtimes/Kokoro/Win64/v1.0-cpu-mixed.2` after required file sizes are complete.
- The runtime contains one model, 48 supported voice/style tables, English pronunciation data, and the generated Mandarin IPA dictionary. It contains no benchmark audio, reports, rejected models, full-FP16 model, community mixed model, French voices, or Japanese voices.
- Game and Shipping builds require a complete installed runtime. Packaging stages it beside the executable as NonUFS data; shipped players never download model files.
- Runtime files and generated binaries are deliberately excluded from Git and Git LFS.

## Speech pipeline

SpeechGen accepts a fully resolved request language, voice blend, and synthesis controls. `USpeechGenVoiceProfile` is an authoring preset for voice identity, named acoustic variants, and directed styles; selection and language policy belong to the caller. Cross-language voice blends are rejected before synthesis. Styles may replace the blend and adjust speed, gain, and terminal pause without changing the model.

Supported profile languages and voice IDs:

- English: the existing `af_*`, `am_*`, `bf_*`, and `bm_*` voices.
- Spanish: `ef_dora`, `em_alex`, `em_santa`.
- Hindi: `hf_alpha`, `hf_beta`, `hm_omega`, `hm_psi`.
- Italian: `if_sara`, `im_nicola`.
- Brazilian Portuguese: `pf_dora`, `pm_alex`, `pm_santa`.
- Mandarin Chinese: `zf_xiaobei`, `zf_xiaoni`, `zf_xiaoxiao`, `zf_xiaoyi`, `zm_yunjian`, `zm_yunxi`, `zm_yunxia`, `zm_yunyang`.

Experimental phoneme-guidance languages require no additional model or voice data:

- Turkish and Azerbaijani use native Turkic letter, soft-consonant, vowel, and final-stress rules. The inference tests use `if_sara` as the carrier voice.
- German uses native digraph, umlaut, initial-cluster, and final-devoicing rules. The inference test uses `bf_emma` as the carrier voice.
- Dutch uses native digraph, long-vowel, and consonant rules. The inference test uses `bf_emma` as the carrier voice.

Experimental profiles may select any installed supported voice as a timbre carrier. This does not claim native voice
training or native prosody; the frontend constrains text to valid Kokoro IPA so unsupported graphemes are never sent
to the model. The enum display names retain the `Experimental` label in authored assets and UI.

Language selection is explicit; SpeechGen does not guess from the input text. English uses native CMUdict plus Flite CMU letter-to-sound rules. Spanish, Hindi, Italian, and Brazilian Portuguese use native deterministic language rules. Mandarin uses a phrase-first IPA dictionary generated from pinned permissive sources and loaded only when Mandarin is requested. ASCII digits are pronounced digit by digit in the selected language. No Python, eSpeak, Misaki, OpenPhonemizer, or external process is loaded at runtime.

### Misaki difference

The official Kokoro Python pipeline uses Misaki and eSpeak for richer contextual normalization and pronunciation. SpeechGen instead uses native deterministic frontends so it can ship as an offline Unreal runtime with no interpreter or copyleft runtime dependency. Diagnostics expose phonemization, unsupported-token, voice-language, and inference failures explicitly.

## Requirements

- Unreal Engine 5.7
- Windows x64
- Engine plugins `NNE` and `NNERuntimeORT`
- Internet access in the editor only while preparing or updating the runtime

## CPU performance decision record

The runtime model and thread policy were selected from local Windows benchmarks on an Intel Core i5-11400H using ONNX Runtime 1.20.1 CPU EP. Times exclude phonemization, request queueing, playback, and Unreal startup.

- The pinned FP32 graph reduced warm synthesis latency by about 2.2x versus the previous dynamic Q8 graph. The evaluated full-FP16 graph preserved FP32 throughput while reducing its measured process-memory delta from about 710 MB to 426-438 MB and its model file from 310 MB to 156 MB.
- Four intra-op threads provided the useful latency/CPU balance. For the same short input, FP32 measured 1312 ms at one thread, 833 ms at two threads, and 589 ms at four threads. Six and eight threads produced only marginal further gains on this six-core CPU, while twelve threads regressed.
- In three fresh-process full-FP16 trials, ORT model/session creation took 1075-1124 ms and the first short synthesis took 549-691 ms. Model loading starts before dialogue use, so synthesis latency is the player-facing figure once the runtime reports Ready.
- In a paired ten-run benchmark, full FP16 and FP32 measured 2.32x and 2.31x realtime respectively. Earlier in-engine dynamic Q8 measurements were 0.87-0.94x realtime.
- FP16 and FP32 waveforms have identical sample counts but are not numerically identical. Runtime validation proves graph compatibility, not perceptual equivalence; representative voices must be auditioned after a model revision.
- A full static INT8 experiment was rejected because aggressive vocoder quantization produced noise. The community selective U8/S8 QOperator graph sounded equivalent in manual A/B but omitted a reproducible calibration/build recipe. It is retained only as an attributed research reference and is never downloaded by SpeechGen.
- `Tools/ModelBuild` records the adopted selective, quality-gated fused `QGemm` recipe. In a clean three-round local benchmark, the community reference reached 3.16x realtime, the adopted reproducible graph 3.00x, and full FP16 2.59x. Against the same FP32 reference, median mel-spectral error was 1.82 dB for the adopted graph, 2.37 dB for full FP16, and 8.41 dB for the community graph; energy-envelope correlation was 0.9990, 0.9997, and 0.9352 respectively. These metrics guide regression detection and do not replace listening tests.
- OpenVINO EP trials could not create a real provider session with the tested Windows package/ABI combination and fell back to CPU EP. The project does not carry an unproven OpenVINO dependency.
- This CPU exposes AVX2 and AVX-512F but not AVX-VNNI or AVX-512 VNNI. VNNI-specific speedup claims therefore do not apply to the measured machine.

Re-benchmark these decisions when the target CPU, ONNX Runtime version, or Kokoro graph changes. Measure session creation, first synthesis, warm synthesis, and process memory separately; a warm-only result is not sufficient for player-facing latency decisions.

See `ThirdParty-LICENSES.md` before redistribution.
