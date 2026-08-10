# SpeechGen Third-Party Notices

This document records the components used or redistributed by SpeechGen. It is an attribution inventory, not legal advice.

## Kokoro-82M v1.0 model and English voice weights

- Source: https://huggingface.co/hexgrad/Kokoro-82M
- License: Apache License 2.0
- Copyright and attribution: hexgrad and the Kokoro contributors.
- Training-data attribution identified by the upstream model card includes the Koniwa corpus under CC BY 3.0 and SIWIS under CC BY 4.0.

The runtime downloads the SpeechGen selective CPU mixed conversion and English voice tables from:

- Source: https://github.com/MC-Oruc/SpeechGen-Runtimes/releases/tag/kokoro-v1.0-cpu-mixed.1
- License: Apache License 2.0

The runtime does not download a community alternative model. The following projects are retained as conversion references and attribution only:

- https://github.com/taylorchu/kokoro-onnx
- https://github.com/thewh1teagle/kokoro-onnx
- https://huggingface.co/onnx-community/Kokoro-82M-v1.0-ONNX

The complete Apache License 2.0 text is distributed beside this notice as `Apache-2.0.txt`.

## Flite 2.2 CMU letter-to-sound data

- Source: https://github.com/festvox/flite/tree/v2.2
- License file: `Flite-COPYING.txt` in packaged distributions and `ThirdParty/flite/COPYING` in source form.
- Copyright (c) 1999-2017 Language Technologies Institute, Carnegie Mellon University.

SpeechGen compiles only the CMU letter-to-sound model and rules required for unknown English words. The complete upstream notice remains available in the pinned Flite submodule.

## CMU Pronouncing Dictionary

- Source: https://github.com/cmusphinx/cmudict
- Copyright (c) 1993-2015 Carnegie Mellon University.
- License: permissive CMU redistribution license supplied by the upstream project. The complete terms are
  distributed beside this notice as `CMUdict-LICENSE.txt`.

The dictionary is downloaded from an immutable revision and distributed as runtime data. The upstream license and project history are available at https://github.com/cmusphinx/cmudict/blob/master/LICENSE.

## ONNX Runtime

- Source: https://github.com/microsoft/onnxruntime
- License: MIT
- Copyright (c) Microsoft Corporation.

SpeechGen uses the ONNX Runtime integration supplied by Unreal Engine through `NNERuntimeORTCpu`; SpeechGen does not redistribute a separate ONNX Runtime package.

The developer-only model recipe under `Tools/ModelBuild` uses pinned Python packages for ONNX graph authoring and benchmarking. These tools are not loaded or redistributed by the game. Its FP32 build input is the Apache-2.0 Kokoro conversion published by `thewh1teagle/kokoro-onnx`; the selective policy was reconstructed from the mixed artifact originating from `taylorchu/kokoro-onnx`. No ONNX model is stored in this Git repository or Git LFS.

## Components intentionally not distributed

SpeechGen does not package eSpeak NG, Misaki, OpenPhonemizer, sherpa-onnx, or Python. Their licenses do not become SpeechGen runtime redistribution dependencies.
