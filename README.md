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

See `ThirdParty-LICENSES.md` before redistribution.
