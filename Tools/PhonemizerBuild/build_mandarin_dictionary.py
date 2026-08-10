# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "pinyin-to-ipa==1.0.0",
#   "pypinyin==0.55.0",
# ]
# ///

"""Build the native SpeechGen Mandarin IPA dictionary from pinned permissive sources."""

import argparse
import json
from pathlib import Path

import pypinyin
from pinyin_to_ipa import pinyin_to_ipa


def retone(value: str) -> str:
    return (
        value.replace("˧˩˧", "↓")
        .replace("˧˥", "↗")
        .replace("˥˩", "↘")
        .replace("˥", "→")
        .replace("ɻ̩", "ɨ")
        .replace("ɹ̩", "ɨ")
        .replace("ʐ̩", "ɨ")
        .replace("z̩", "ɨ")
        .replace("̯", "")
    )


def to_ipa(pinyin: str) -> str:
    variants = pinyin_to_ipa(pinyin)
    if not variants:
        return ""
    return retone("".join(next(iter(variants))))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--vocab", type=Path, required=True)
    args = parser.parse_args()

    package_root = Path(pypinyin.__file__).parent
    characters = json.loads((package_root / "pinyin_dict.json").read_text(encoding="utf-8"))
    phrases = json.loads((package_root / "phrases_dict.json").read_text(encoding="utf-8"))
    vocab = set(json.loads(args.vocab.read_text(encoding="utf-8"))["vocab"])

    entries: dict[str, str] = {}
    for phrase, syllables in phrases.items():
        pronunciation = "".join(to_ipa(options[0]) for options in syllables if options)
        if pronunciation and set(pronunciation) <= vocab:
            entries[phrase] = pronunciation

    for codepoint, alternatives in characters.items():
        character = chr(int(codepoint))
        pronunciation = to_ipa(alternatives.split(",", 1)[0])
        if pronunciation and set(pronunciation) <= vocab:
            entries.setdefault(character, pronunciation)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    ordered = sorted(entries.items(), key=lambda item: (-len(item[0]), item[0]))
    args.output.write_text("".join(f"{key}\t{value}\n" for key, value in ordered), encoding="utf-8")
    print(f"Wrote {len(ordered)} Mandarin pronunciations to {args.output}")


if __name__ == "__main__":
    main()
