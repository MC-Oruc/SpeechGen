# SpeechGen Third-Party Notices

This document records the components used or redistributed by SpeechGen. It is an attribution inventory, not legal advice.

## Kokoro-82M v1.0 model and English voice weights

- Source: https://huggingface.co/hexgrad/Kokoro-82M
- License: Apache License 2.0
- Copyright and attribution: hexgrad and the Kokoro contributors.
- Training-data attribution identified by the upstream model card includes the Koniwa corpus under CC BY 3.0 and SIWIS under CC BY 4.0.

The runtime downloads a Q8 ONNX conversion pinned from:

- Source: https://huggingface.co/onnx-community/Kokoro-82M-v1.0-ONNX
- License: Apache License 2.0

Apache License 2.0: https://www.apache.org/licenses/LICENSE-2.0

## Flite 2.2 CMU letter-to-sound data

- Source: https://github.com/festvox/flite/tree/v2.2
- License file: `ThirdParty/flite/COPYING`
- Copyright (c) 1999-2017 Language Technologies Institute, Carnegie Mellon University.

SpeechGen compiles only the CMU letter-to-sound model and rules required for unknown English words. The complete upstream notice remains available in the pinned Flite submodule.

## CMU Pronouncing Dictionary

- Source: https://github.com/cmusphinx/cmudict
- Copyright (c) 1993-2015 Carnegie Mellon University.
- License: permissive CMU redistribution license supplied by the upstream project.

The dictionary is downloaded from an immutable revision and distributed as runtime data. The upstream license and project history are available at https://github.com/cmusphinx/cmudict/blob/master/LICENSE.

## ONNX Runtime

- Source: https://github.com/microsoft/onnxruntime
- License: MIT
- Copyright (c) Microsoft Corporation.

SpeechGen uses the ONNX Runtime integration supplied by Unreal Engine through `NNERuntimeORTCpu`; SpeechGen does not redistribute a separate ONNX Runtime package.

## Components intentionally not distributed

SpeechGen does not package eSpeak NG, Misaki, OpenPhonemizer, sherpa-onnx, or Python. Their licenses do not become SpeechGen runtime redistribution dependencies.
