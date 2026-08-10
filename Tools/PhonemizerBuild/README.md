# Mandarin pronunciation dictionary recipe

This developer-only recipe converts pinned `pypinyin` pronunciation data through pinned `pinyin-to-ipa`
rules into the compact UTF-8 dictionary loaded by SpeechGen. Python and the source packages are not loaded or
distributed by the game.

```powershell
uv run .\Tools\PhonemizerBuild\build_mandarin_dictionary.py `
  --vocab "..\..\Saved\SpeechGen\Runtimes\Kokoro\Win64\v1.0-cpu-mixed.2\kokoro_config.json" `
  --output "..\..\Saved\SpeechGen\Runtimes\Kokoro\Win64\v1.0-cpu-mixed.2\phonemizer\mandarin_ipa.dict"
```

The output contains only Kokoro-supported IPA tokens. Phrase pronunciations take precedence over single-character
readings at runtime. Regenerate and re-run the multilingual phonemizer and inference tests when either pinned source
changes.
