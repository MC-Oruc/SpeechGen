#include "Phonemizer/KokoroLanguageFrontend.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const FString SupportedPunctuation = TEXT(";:,.!?\"—…()“”");

	FString NormalizeCommon(const FString& Text)
	{
		FString Result = Text.ToLower();
		Result.ReplaceInline(TEXT("\r"), TEXT(" "));
		Result.ReplaceInline(TEXT("\n"), TEXT(" "));
		Result.ReplaceInline(TEXT("\t"), TEXT(" "));
		Result.ReplaceInline(TEXT("’"), TEXT("'"));
		Result.ReplaceInline(TEXT("–"), TEXT("—"));
		while (Result.Contains(TEXT("  ")))
		{
			Result.ReplaceInline(TEXT("  "), TEXT(" "));
		}
		return Result.TrimStartAndEnd();
	}

	FString ExpandDigits(const FString& Text, const TCHAR* const Names[10])
	{
		FString Result;
		for (const TCHAR Character : Text)
		{
			if (Character >= TEXT('0') && Character <= TEXT('9'))
			{
				Result += TEXT(" ");
				Result += Names[Character - TEXT('0')];
				Result += TEXT(" ");
			}
			else
			{
				Result.AppendChar(Character);
			}
		}
		return Result;
	}

	FString TransformWords(const FString& Text, const TFunctionRef<FString(const FString&)>& TransformWord)
	{
		const FString Normalized = NormalizeCommon(Text);
		FString Result;
		FString Word;
		auto FlushWord = [&]()
		{
			if (!Word.IsEmpty())
			{
				Result += TransformWord(Word);
				Word.Reset();
			}
		};

		for (const TCHAR Character : Normalized)
		{
			if (FChar::IsAlpha(Character) || Character == TEXT('\''))
			{
				Word.AppendChar(Character);
				continue;
			}
			FlushWord();
			if (SupportedPunctuation.Contains(FString::Chr(Character)))
			{
				Result.AppendChar(Character);
			}
			Result.AppendChar(TEXT(' '));
		}
		FlushWord();
		while (Result.Contains(TEXT("  ")))
		{
			Result.ReplaceInline(TEXT("  "), TEXT(" "));
		}
		return Result.TrimStartAndEnd();
	}

	bool IsSpanishVowel(const TCHAR Character)
	{
		return FString(TEXT("aeiouáéíóúü")).Contains(FString::Chr(Character));
	}

	bool IsItalianVowel(const TCHAR Character)
	{
		return FString(TEXT("aeiouàèéìíòóùú")).Contains(FString::Chr(Character));
	}

	bool IsPortugueseVowel(const TCHAR Character)
	{
		return FString(TEXT("aeiouáâãàéêíóôõú")).Contains(FString::Chr(Character));
	}

	int32 FindStressIndex(const FString& Word, const TFunctionRef<bool(TCHAR)>& IsVowel,
		const FString& ExplicitStress, const bool bPenultimateWhenVowelFinal)
	{
		TArray<int32> Vowels;
		for (int32 Index = 0; Index < Word.Len(); ++Index)
		{
			if (!IsVowel(Word[Index]))
			{
				continue;
			}
			Vowels.Add(Index);
			if (ExplicitStress.Contains(FString::Chr(Word[Index])))
			{
				return Index;
			}
		}
		if (Vowels.IsEmpty())
		{
			return INDEX_NONE;
		}
		const bool bPenultimate = bPenultimateWhenVowelFinal
			&& (IsVowel(Word[Word.Len() - 1]) || Word.EndsWith(TEXT("n")) || Word.EndsWith(TEXT("s")));
		return bPenultimate && Vowels.Num() > 1 ? Vowels[Vowels.Num() - 2] : Vowels.Last();
	}

	FString SpanishWord(const FString& Word)
	{
		const int32 Stress = FindStressIndex(Word, IsSpanishVowel, TEXT("áéíóú"), true);
		FString Result;
		for (int32 Index = 0; Index < Word.Len(); ++Index)
		{
			const TCHAR Character = Word[Index];
			const TCHAR Next = Index + 1 < Word.Len() ? Word[Index + 1] : 0;
			if (Index == Stress)
			{
				Result += TEXT("ˈ");
			}
			if (Character == TEXT('c') && Next == TEXT('h')) { Result += TEXT("ʧ"); ++Index; }
			else if (Character == TEXT('l') && Next == TEXT('l')) { Result += TEXT("ʝ"); ++Index; }
			else if (Character == TEXT('r') && Next == TEXT('r')) { Result += TEXT("r"); ++Index; }
			else if (Character == TEXT('q') && Next == TEXT('u')) { Result += TEXT("k"); ++Index; }
			else if (Character == TEXT('g') && Next == TEXT('u') && Index + 2 < Word.Len()
				&& FString(TEXT("eiéí")).Contains(FString::Chr(Word[Index + 2]))) { Result += TEXT("ɡ"); ++Index; }
			else
			{
				switch (Character)
				{
				case TEXT('a'): case TEXT('á'): Result += TEXT("a"); break;
				case TEXT('e'): case TEXT('é'): Result += TEXT("e"); break;
				case TEXT('i'): case TEXT('í'): Result += TEXT("i"); break;
				case TEXT('o'): case TEXT('ó'): Result += TEXT("o"); break;
				case TEXT('u'): case TEXT('ú'): case TEXT('ü'): Result += TEXT("u"); break;
				case TEXT('b'): case TEXT('v'): Result += TEXT("b"); break;
				case TEXT('c'): Result += FString(TEXT("eéií")).Contains(FString::Chr(Next)) ? TEXT("θ") : TEXT("k"); break;
				case TEXT('d'): Result += TEXT("d"); break;
				case TEXT('f'): Result += TEXT("f"); break;
				case TEXT('g'): Result += FString(TEXT("eéií")).Contains(FString::Chr(Next)) ? TEXT("x") : TEXT("ɡ"); break;
				case TEXT('h'): break;
				case TEXT('j'): Result += TEXT("x"); break;
				case TEXT('k'): Result += TEXT("k"); break;
				case TEXT('l'): Result += TEXT("l"); break;
				case TEXT('m'): Result += TEXT("m"); break;
				case TEXT('n'): Result += TEXT("n"); break;
				case TEXT('ñ'): Result += TEXT("ɲ"); break;
				case TEXT('p'): Result += TEXT("p"); break;
				case TEXT('r'): Result += Index == 0 ? TEXT("r") : TEXT("ɾ"); break;
				case TEXT('s'): Result += TEXT("s"); break;
				case TEXT('t'): Result += TEXT("t"); break;
				case TEXT('w'): Result += TEXT("w"); break;
				case TEXT('x'): Result += TEXT("ks"); break;
				case TEXT('y'): Result += IsSpanishVowel(Next) ? TEXT("ʝ") : TEXT("i"); break;
				case TEXT('z'): Result += TEXT("θ"); break;
				default: break;
				}
			}
		}
		return Result;
	}

	FString ItalianWord(const FString& Word)
	{
		const int32 Stress = FindStressIndex(Word, IsItalianVowel, TEXT("àèéìíòóùú"), true);
		FString Result;
		for (int32 Index = 0; Index < Word.Len(); ++Index)
		{
			const TCHAR Character = Word[Index];
			const TCHAR Next = Index + 1 < Word.Len() ? Word[Index + 1] : 0;
			const TCHAR After = Index + 2 < Word.Len() ? Word[Index + 2] : 0;
			if (Index == Stress) Result += TEXT("ˈ");
			if (Character == TEXT('g') && Next == TEXT('n')) { Result += TEXT("ɲ"); ++Index; }
			else if (Character == TEXT('g') && Next == TEXT('l') && After == TEXT('i')) { Result += TEXT("ʎ"); Index += 2; }
			else if (Character == TEXT('s') && Next == TEXT('c') && FString(TEXT("eiéèìí")).Contains(FString::Chr(After)))
			{ Result += TEXT("ʃ"); Index += 2; }
			else if ((Character == TEXT('c') || Character == TEXT('g')) && Next == TEXT('h')
				&& FString(TEXT("eiéèìí")).Contains(FString::Chr(After)))
			{ Result += Character == TEXT('c') ? TEXT("k") : TEXT("ɡ"); ++Index; }
			else if (Character == TEXT('q') && Next == TEXT('u')) { Result += TEXT("kw"); ++Index; }
			else
			{
				switch (Character)
				{
				case TEXT('a'): case TEXT('à'): Result += TEXT("a"); break;
				case TEXT('e'): case TEXT('é'): Result += TEXT("e"); break;
				case TEXT('è'): Result += TEXT("ɛ"); break;
				case TEXT('i'): case TEXT('ì'): case TEXT('í'): Result += TEXT("i"); break;
				case TEXT('o'): case TEXT('ó'): Result += TEXT("o"); break;
				case TEXT('ò'): Result += TEXT("ɔ"); break;
				case TEXT('u'): case TEXT('ù'): case TEXT('ú'): Result += TEXT("u"); break;
				case TEXT('b'): Result += TEXT("b"); break;
				case TEXT('c'): Result += FString(TEXT("eiéèìí")).Contains(FString::Chr(Next)) ? TEXT("ʧ") : TEXT("k"); break;
				case TEXT('d'): Result += TEXT("d"); break;
				case TEXT('f'): Result += TEXT("f"); break;
				case TEXT('g'): Result += FString(TEXT("eiéèìí")).Contains(FString::Chr(Next)) ? TEXT("ʤ") : TEXT("ɡ"); break;
				case TEXT('h'): break;
				case TEXT('j'): Result += TEXT("ʤ"); break;
				case TEXT('l'): Result += TEXT("l"); break;
				case TEXT('m'): Result += TEXT("m"); break;
				case TEXT('n'): Result += TEXT("n"); break;
				case TEXT('p'): Result += TEXT("p"); break;
				case TEXT('r'): Result += TEXT("r"); break;
				case TEXT('s'): Result += TEXT("s"); break;
				case TEXT('t'): Result += TEXT("t"); break;
				case TEXT('v'): Result += TEXT("v"); break;
				case TEXT('z'): Result += TEXT("ʦ"); break;
				default: break;
				}
			}
		}
		return Result;
	}

	FString PortugueseWord(const FString& Word)
	{
		const int32 Stress = FindStressIndex(Word, IsPortugueseVowel, TEXT("áâãàéêíóôõú"), true);
		FString Result;
		for (int32 Index = 0; Index < Word.Len(); ++Index)
		{
			const TCHAR Character = Word[Index];
			const TCHAR Next = Index + 1 < Word.Len() ? Word[Index + 1] : 0;
			if (Index == Stress) Result += TEXT("ˈ");
			if (Character == TEXT('n') && Next == TEXT('h')) { Result += TEXT("ɲ"); ++Index; }
			else if (Character == TEXT('l') && Next == TEXT('h')) { Result += TEXT("ʎ"); ++Index; }
			else if (Character == TEXT('c') && Next == TEXT('h')) { Result += TEXT("ʃ"); ++Index; }
			else if (Character == TEXT('r') && Next == TEXT('r')) { Result += TEXT("h"); ++Index; }
			else if (Character == TEXT('q') && Next == TEXT('u')) { Result += TEXT("k"); ++Index; }
			else
			{
				switch (Character)
				{
				case TEXT('a'): case TEXT('á'): case TEXT('à'): Result += TEXT("a"); break;
				case TEXT('â'): Result += TEXT("ɐ"); break;
				case TEXT('ã'): Result += TEXT("ɐ̃"); break;
				case TEXT('e'): case TEXT('é'): Result += TEXT("e"); break;
				case TEXT('ê'): Result += TEXT("ɛ"); break;
				case TEXT('i'): case TEXT('í'): Result += TEXT("i"); break;
				case TEXT('o'): case TEXT('ó'): Result += TEXT("o"); break;
				case TEXT('ô'): Result += TEXT("ɔ"); break;
				case TEXT('õ'): Result += TEXT("õ"); break;
				case TEXT('u'): case TEXT('ú'): Result += TEXT("u"); break;
				case TEXT('b'): Result += TEXT("b"); break;
				case TEXT('c'): case TEXT('ç'): Result += Character == TEXT('ç') || FString(TEXT("eiéêí")).Contains(FString::Chr(Next)) ? TEXT("s") : TEXT("k"); break;
				case TEXT('d'): Result += FString(TEXT("ií")).Contains(FString::Chr(Next)) ? TEXT("ʤ") : TEXT("d"); break;
				case TEXT('f'): Result += TEXT("f"); break;
				case TEXT('g'): Result += FString(TEXT("eiéêí")).Contains(FString::Chr(Next)) ? TEXT("ʒ") : TEXT("ɡ"); break;
				case TEXT('h'): break;
				case TEXT('j'): Result += TEXT("ʒ"); break;
				case TEXT('l'): Result += TEXT("l"); break;
				case TEXT('m'): Result += Index == Word.Len() - 1 ? TEXT("̃") : TEXT("m"); break;
				case TEXT('n'): Result += Index == Word.Len() - 1 ? TEXT("̃") : TEXT("n"); break;
				case TEXT('p'): Result += TEXT("p"); break;
				case TEXT('r'): Result += Index == 0 ? TEXT("h") : TEXT("ɾ"); break;
				case TEXT('s'): Result += Index == Word.Len() - 1 ? TEXT("s") : TEXT("z"); break;
				case TEXT('t'): Result += FString(TEXT("ií")).Contains(FString::Chr(Next)) ? TEXT("ʧ") : TEXT("t"); break;
				case TEXT('v'): Result += TEXT("v"); break;
				case TEXT('x'): Result += TEXT("ʃ"); break;
				case TEXT('z'): Result += TEXT("z"); break;
				default: break;
				}
			}
		}
		return Result;
	}

	FString HindiConsonant(const TCHAR Character)
	{
		switch (Character)
		{
		case 0x0915: return TEXT("k"); case 0x0916: return TEXT("kʰ"); case 0x0917: return TEXT("ɡ");
		case 0x0918: return TEXT("ɡʰ"); case 0x0919: return TEXT("ŋ"); case 0x091A: return TEXT("ʧ");
		case 0x091B: return TEXT("ʧʰ"); case 0x091C: return TEXT("ʤ"); case 0x091D: return TEXT("ʤʰ");
		case 0x091E: return TEXT("ɲ"); case 0x091F: return TEXT("ʈ"); case 0x0920: return TEXT("ʈʰ");
		case 0x0921: return TEXT("ɖ"); case 0x0922: return TEXT("ɖʰ"); case 0x0923: return TEXT("ɳ");
		case 0x0924: return TEXT("t"); case 0x0925: return TEXT("tʰ"); case 0x0926: return TEXT("d");
		case 0x0927: return TEXT("dʰ"); case 0x0928: return TEXT("n"); case 0x092A: return TEXT("p");
		case 0x092B: return TEXT("pʰ"); case 0x092C: return TEXT("b"); case 0x092D: return TEXT("bʰ");
		case 0x092E: return TEXT("m"); case 0x092F: return TEXT("j"); case 0x0930: return TEXT("r");
		case 0x0932: return TEXT("l"); case 0x0935: return TEXT("ʋ"); case 0x0936: return TEXT("ʃ");
		case 0x0937: return TEXT("ʂ"); case 0x0938: return TEXT("s"); case 0x0939: return TEXT("h");
		case 0x0958: return TEXT("q"); case 0x0959: return TEXT("x"); case 0x095A: return TEXT("ɣ");
		case 0x095B: return TEXT("z"); case 0x095C: return TEXT("ɽ"); case 0x095D: return TEXT("ɽʰ");
		case 0x095E: return TEXT("f"); default: return {};
		}
	}

	FString HindiVowel(const TCHAR Character)
	{
		switch (Character)
		{
		case 0x0905: case 0x093D: return TEXT("ə"); case 0x0906: case 0x093E: return TEXT("aː");
		case 0x0907: case 0x093F: return TEXT("i"); case 0x0908: case 0x0940: return TEXT("iː");
		case 0x0909: case 0x0941: return TEXT("u"); case 0x090A: case 0x0942: return TEXT("uː");
		case 0x090F: case 0x0947: return TEXT("eː"); case 0x0910: case 0x0948: return TEXT("ɛː");
		case 0x0913: case 0x094B: return TEXT("oː"); case 0x0914: case 0x094C: return TEXT("ɔː");
		case 0x090B: case 0x0943: return TEXT("ri"); default: return {};
		}
	}

	FString HindiWord(const FString& Word)
	{
		FString Result;
		for (int32 Index = 0; Index < Word.Len(); ++Index)
		{
			const TCHAR Character = Word[Index];
			if (const FString IndependentVowel = HindiVowel(Character); !IndependentVowel.IsEmpty())
			{
				Result += IndependentVowel;
				continue;
			}
			const FString Consonant = HindiConsonant(Character);
			if (Consonant.IsEmpty())
			{
				if (Character == 0x0902) Result += TEXT("n");
				else if (Character == 0x0901) Result += TEXT("̃");
				else if (Character == 0x0903) Result += TEXT("h");
				continue;
			}
			Result += Consonant;
			const TCHAR Next = Index + 1 < Word.Len() ? Word[Index + 1] : 0;
			if (Next == 0x094D)
			{
				++Index;
				continue;
			}
			if (const FString VowelSign = HindiVowel(Next); !VowelSign.IsEmpty())
			{
				Result += VowelSign;
				++Index;
				continue;
			}
			if (Index + 1 < Word.Len())
			{
				Result += TEXT("ə");
			}
		}
		return Result;
	}
}

FKokoroLanguageFrontend::FKokoroLanguageFrontend(const FString& InRuntimeDirectory)
	: RuntimeDirectory(InRuntimeDirectory)
{
}

bool FKokoroLanguageFrontend::Phonemize(const ESpeechGenLanguage Language, const FString& Text,
	FString& OutPhonemes, FString& OutError) const
{
	switch (Language)
	{
	case ESpeechGenLanguage::MandarinChinese:
		return PhonemizeMandarin(Text, OutPhonemes, OutError);
	case ESpeechGenLanguage::Spanish:
		OutPhonemes = PhonemizeSpanish(Text);
		break;
	case ESpeechGenLanguage::Hindi:
		OutPhonemes = PhonemizeHindi(Text);
		break;
	case ESpeechGenLanguage::Italian:
		OutPhonemes = PhonemizeItalian(Text);
		break;
	case ESpeechGenLanguage::BrazilianPortuguese:
		OutPhonemes = PhonemizePortuguese(Text);
		break;
	default:
		OutError = TEXT("The requested language requires the English frontend.");
		return false;
	}
	if (OutPhonemes.IsEmpty())
	{
		OutError = TEXT("Text did not contain speakable content for the selected language.");
		return false;
	}
	return true;
}

FString FKokoroLanguageFrontend::PhonemizeSpanish(const FString& Text)
{
	static const TCHAR* Digits[] = {TEXT("cero"), TEXT("uno"), TEXT("dos"), TEXT("tres"), TEXT("cuatro"),
		TEXT("cinco"), TEXT("seis"), TEXT("siete"), TEXT("ocho"), TEXT("nueve")};
	return TransformWords(ExpandDigits(Text, Digits), SpanishWord);
}

FString FKokoroLanguageFrontend::PhonemizeItalian(const FString& Text)
{
	static const TCHAR* Digits[] = {TEXT("zero"), TEXT("uno"), TEXT("due"), TEXT("tre"), TEXT("quattro"),
		TEXT("cinque"), TEXT("sei"), TEXT("sette"), TEXT("otto"), TEXT("nove")};
	return TransformWords(ExpandDigits(Text, Digits), ItalianWord);
}

FString FKokoroLanguageFrontend::PhonemizePortuguese(const FString& Text)
{
	static const TCHAR* Digits[] = {TEXT("zero"), TEXT("um"), TEXT("dois"), TEXT("três"), TEXT("quatro"),
		TEXT("cinco"), TEXT("seis"), TEXT("sete"), TEXT("oito"), TEXT("nove")};
	return TransformWords(ExpandDigits(Text, Digits), PortugueseWord);
}

FString FKokoroLanguageFrontend::PhonemizeHindi(const FString& Text)
{
	static const TCHAR* Digits[] = {TEXT("शून्य"), TEXT("एक"), TEXT("दो"), TEXT("तीन"), TEXT("चार"),
		TEXT("पाँच"), TEXT("छह"), TEXT("सात"), TEXT("आठ"), TEXT("नौ")};
	FString Normalized = NormalizeCommon(ExpandDigits(Text, Digits));
	Normalized.ReplaceInline(TEXT("।"), TEXT("."));
	Normalized.ReplaceInline(TEXT("॥"), TEXT("."));
	FString Result;
	FString Word;
	for (int32 Index = 0; Index <= Normalized.Len(); ++Index)
	{
		const TCHAR Character = Index < Normalized.Len() ? Normalized[Index] : TEXT(' ');
		if (Character >= 0x0900 && Character <= 0x097F)
		{
			Word.AppendChar(Character);
			continue;
		}
		if (!Word.IsEmpty())
		{
			Result += HindiWord(Word);
			Result.AppendChar(TEXT(' '));
			Word.Reset();
		}
		if (SupportedPunctuation.Contains(FString::Chr(Character)))
		{
			Result.AppendChar(Character);
			Result.AppendChar(TEXT(' '));
		}
	}
	while (Result.Contains(TEXT("  ")))
	{
		Result.ReplaceInline(TEXT("  "), TEXT(" "));
	}
	return Result.TrimStartAndEnd();
}

bool FKokoroLanguageFrontend::LoadMandarinDictionary(FString& OutError) const
{
	if (bMandarinDictionaryLoaded)
	{
		return true;
	}
	FString DictionaryText;
	const FString DictionaryPath = FPaths::Combine(RuntimeDirectory, TEXT("phonemizer/mandarin_ipa.dict"));
	if (!FFileHelper::LoadFileToString(DictionaryText, *DictionaryPath))
	{
		OutError = FString::Printf(TEXT("Unable to load Mandarin pronunciation dictionary: %s"), *DictionaryPath);
		return false;
	}
	TArray<FString> Lines;
	DictionaryText.ParseIntoArrayLines(Lines, true);
	for (const FString& Line : Lines)
	{
		FString Key;
		FString Value;
		if (!Line.Split(TEXT("\t"), &Key, &Value) || Key.IsEmpty() || Value.IsEmpty())
		{
			continue;
		}
		MandarinMaxKeyLength = FMath::Max(MandarinMaxKeyLength, Key.Len());
		MandarinDictionary.Add(MoveTemp(Key), MoveTemp(Value));
	}
	bMandarinDictionaryLoaded = !MandarinDictionary.IsEmpty();
	if (!bMandarinDictionaryLoaded)
	{
		OutError = TEXT("Mandarin pronunciation dictionary is empty.");
	}
	return bMandarinDictionaryLoaded;
}

bool FKokoroLanguageFrontend::PhonemizeMandarin(const FString& Text, FString& OutPhonemes,
	FString& OutError) const
{
	if (!LoadMandarinDictionary(OutError))
	{
		return false;
	}
	static const TCHAR* Digits[] = {TEXT("零"), TEXT("一"), TEXT("二"), TEXT("三"), TEXT("四"),
		TEXT("五"), TEXT("六"), TEXT("七"), TEXT("八"), TEXT("九")};
	FString Normalized = NormalizeCommon(ExpandDigits(Text, Digits));
	Normalized.ReplaceInline(TEXT("、"), TEXT(",")); Normalized.ReplaceInline(TEXT("，"), TEXT(","));
	Normalized.ReplaceInline(TEXT("。"), TEXT(".")); Normalized.ReplaceInline(TEXT("！"), TEXT("!"));
	Normalized.ReplaceInline(TEXT("？"), TEXT("?")); Normalized.ReplaceInline(TEXT("："), TEXT(":"));
	Normalized.ReplaceInline(TEXT("；"), TEXT(";"));

	OutPhonemes.Reset();
	for (int32 Index = 0; Index < Normalized.Len();)
	{
		const TCHAR Character = Normalized[Index];
		if (SupportedPunctuation.Contains(FString::Chr(Character)))
		{
			OutPhonemes.AppendChar(Character);
			OutPhonemes.AppendChar(TEXT(' '));
			++Index;
			continue;
		}
		if (FChar::IsWhitespace(Character))
		{
			OutPhonemes.AppendChar(TEXT(' '));
			++Index;
			continue;
		}

		const int32 Remaining = Normalized.Len() - Index;
		const int32 SearchLength = FMath::Min(MandarinMaxKeyLength, Remaining);
		const FString* Pronunciation = nullptr;
		int32 MatchedLength = 0;
		for (int32 Length = SearchLength; Length > 0; --Length)
		{
			if (const FString* Found = MandarinDictionary.Find(Normalized.Mid(Index, Length)))
			{
				Pronunciation = Found;
				MatchedLength = Length;
				break;
			}
		}
		if (!Pronunciation)
		{
			OutError = FString::Printf(TEXT("No Mandarin pronunciation for U+%04X at character %d."), Character, Index);
			return false;
		}
		OutPhonemes += *Pronunciation;
		OutPhonemes.AppendChar(TEXT(' '));
		Index += MatchedLength;
	}
	OutPhonemes = OutPhonemes.TrimStartAndEnd();
	return !OutPhonemes.IsEmpty();
}
