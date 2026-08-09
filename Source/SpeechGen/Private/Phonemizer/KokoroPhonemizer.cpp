#include "Phonemizer/KokoroPhonemizer.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

THIRD_PARTY_INCLUDES_START
extern "C"
{
#include "cst_lts.h"
const char* const* SpeechGen_GetCmuLtsPhoneTable();
const cst_lts_addr* SpeechGen_GetCmuLtsLetterIndex();
const cst_lts_model* SpeechGen_GetCmuLtsModel();
}
THIRD_PARTY_INCLUDES_END

namespace
{
	constexpr int32 LtsRuleSize = 6;
	constexpr int32 LtsContextSize = 4;

	struct FFliteLtsRule
	{
		uint8 Feature;
		uint8 Value;
		uint16 TrueState;
		uint16 FalseState;
	};

	uint8 ApplyLtsModel(const TArray<uint8>& Features, uint16 StateIndex)
	{
		const cst_lts_model* Model = SpeechGen_GetCmuLtsModel();
		FFliteLtsRule Rule{};
		for (;;)
		{
			FMemory::Memcpy(&Rule, &Model[StateIndex * LtsRuleSize], LtsRuleSize);
			if (Rule.Feature == CST_LTS_EOR)
			{
				return Rule.Value;
			}
			StateIndex = Features[Rule.Feature] == Rule.Value ? Rule.TrueState : Rule.FalseState;
		}
	}

	const TMap<FString, FString>& GetArpabetMap()
	{
		static const TMap<FString, FString> Map = {
			{TEXT("AA"), TEXT("ɑ")}, {TEXT("AE"), TEXT("æ")}, {TEXT("AH"), TEXT("ʌ")},
			{TEXT("AO"), TEXT("ɔ")}, {TEXT("AW"), TEXT("aʊ")}, {TEXT("AX"), TEXT("ə")},
			{TEXT("AY"), TEXT("aɪ")}, {TEXT("EH"), TEXT("ɛ")}, {TEXT("ER"), TEXT("ɜɹ")},
			{TEXT("EY"), TEXT("eɪ")}, {TEXT("IH"), TEXT("ɪ")}, {TEXT("IY"), TEXT("i")},
			{TEXT("OW"), TEXT("oʊ")}, {TEXT("OY"), TEXT("ɔɪ")}, {TEXT("UH"), TEXT("ʊ")},
			{TEXT("UW"), TEXT("u")}, {TEXT("B"), TEXT("b")}, {TEXT("CH"), TEXT("ʧ")},
			{TEXT("D"), TEXT("d")}, {TEXT("DH"), TEXT("ð")}, {TEXT("F"), TEXT("f")},
			{TEXT("G"), TEXT("ɡ")}, {TEXT("HH"), TEXT("h")}, {TEXT("JH"), TEXT("ʤ")},
			{TEXT("K"), TEXT("k")}, {TEXT("L"), TEXT("l")}, {TEXT("M"), TEXT("m")},
			{TEXT("N"), TEXT("n")}, {TEXT("NG"), TEXT("ŋ")}, {TEXT("P"), TEXT("p")},
			{TEXT("R"), TEXT("ɹ")}, {TEXT("S"), TEXT("s")}, {TEXT("SH"), TEXT("ʃ")},
			{TEXT("T"), TEXT("t")}, {TEXT("TH"), TEXT("θ")}, {TEXT("V"), TEXT("v")},
			{TEXT("W"), TEXT("w")}, {TEXT("Y"), TEXT("j")}, {TEXT("Z"), TEXT("z")},
			{TEXT("ZH"), TEXT("ʒ")}
		};
		return Map;
	}

	bool IsWordCharacter(const TCHAR Character)
	{
		return FChar::IsAlpha(Character) || Character == TEXT('\'');
	}
}

bool FKokoroPhonemizer::Initialize(const FString& RuntimeDirectory, FString& OutError)
{
	Dictionary.Reset();
	Vocab.Reset();

	FString DictionaryText;
	const FString DictionaryPath = FPaths::Combine(RuntimeDirectory, TEXT("phonemizer/cmudict.dict"));
	if (!FFileHelper::LoadFileToString(DictionaryText, *DictionaryPath))
	{
		OutError = FString::Printf(TEXT("Unable to load CMUdict: %s"), *DictionaryPath);
		return false;
	}

	TArray<FString> Lines;
	DictionaryText.ParseIntoArrayLines(Lines, true);
	for (const FString& Line : Lines)
	{
		int32 Separator = INDEX_NONE;
		if (!Line.FindChar(TEXT(' '), Separator) || Separator <= 0)
		{
			continue;
		}

		FString Word = Line.Left(Separator).ToLower();
		int32 VariantStart = INDEX_NONE;
		if (Word.FindLastChar(TEXT('('), VariantStart))
		{
			Word.LeftInline(VariantStart);
		}
		if (Dictionary.Contains(Word))
		{
			continue;
		}

		TArray<FString> Phones;
		Line.Mid(Separator + 1).ParseIntoArrayWS(Phones);
		if (!Phones.IsEmpty())
		{
			Dictionary.Add(MoveTemp(Word), MoveTemp(Phones));
		}
	}

	FString ConfigText;
	const FString ConfigPath = FPaths::Combine(RuntimeDirectory, TEXT("kokoro_config.json"));
	if (!FFileHelper::LoadFileToString(ConfigText, *ConfigPath))
	{
		OutError = FString::Printf(TEXT("Unable to load Kokoro vocabulary: %s"), *ConfigPath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ConfigText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("Kokoro vocabulary JSON is invalid.");
		return false;
	}

	const TSharedPtr<FJsonObject>* VocabObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("vocab"), VocabObject) || !VocabObject || !VocabObject->IsValid())
	{
		OutError = TEXT("Kokoro vocabulary field is missing.");
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*VocabObject)->Values)
	{
		Vocab.Add(Pair.Key, static_cast<int64>(Pair.Value->AsNumber()));
	}

	return Dictionary.Num() > 100000 && Vocab.Num() > 100;
}

bool FKokoroPhonemizer::Encode(const FString& Text, TArray<int64>& OutTokenIds, FString& OutPhonemes,
	FString& OutError) const
{
	OutTokenIds.Reset();
	OutPhonemes.Reset();

	const FString Normalized = NormalizeText(Text);
	FString Word;
	TArray<FString> Phones;
	for (int32 Index = 0; Index <= Normalized.Len(); ++Index)
	{
		const TCHAR Character = Index < Normalized.Len() ? Normalized[Index] : TEXT(' ');
		if (IsWordCharacter(Character))
		{
			Word.AppendChar(Character);
			continue;
		}

		if (!Word.IsEmpty())
		{
			AppendWordPhones(Word, Dictionary, Phones);
			Word.Reset();
			Phones.Add(TEXT(" "));
		}

		if (FString(TEXT(";:,.!?\"—…()\u201c\u201d")).Contains(FString::Chr(Character)))
		{
			Phones.Add(FString::Chr(Character));
			Phones.Add(TEXT(" "));
		}
	}

	OutPhonemes = ArpabetToIpa(Phones).TrimStartAndEnd();
	OutTokenIds.Add(0);
	for (int32 Index = 0; Index < OutPhonemes.Len(); ++Index)
	{
		const FString Token = FString::Chr(OutPhonemes[Index]);
		const int64* TokenId = Vocab.Find(Token);
		if (!TokenId)
		{
			OutError = FString::Printf(TEXT("Unsupported Kokoro phoneme token U+%04X."), OutPhonemes[Index]);
			return false;
		}
		OutTokenIds.Add(*TokenId);
	}
	OutTokenIds.Add(0);

	if (OutTokenIds.Num() < 3)
	{
		OutError = TEXT("Text did not contain speakable English content.");
		return false;
	}
	if (OutTokenIds.Num() > 512)
	{
		OutError = TEXT("Kokoro segment exceeds the 510-token model limit.");
		return false;
	}
	return true;
}

FString FKokoroPhonemizer::NormalizeText(const FString& Text)
{
	FString Result = Text;
	Result.ReplaceInline(TEXT("\r"), TEXT(" "));
	Result.ReplaceInline(TEXT("\n"), TEXT(" "));
	Result.ReplaceInline(TEXT("\t"), TEXT(" "));
	Result.ReplaceInline(TEXT("’"), TEXT("'"));
	Result.ReplaceInline(TEXT("–"), TEXT("—"));

	FString Expanded;
	for (int32 Index = 0; Index < Result.Len();)
	{
		if (!FChar::IsDigit(Result[Index]))
		{
			Expanded.AppendChar(Result[Index++]);
			continue;
		}

		const int32 NumberStart = Index;
		while (Index < Result.Len() && FChar::IsDigit(Result[Index]))
		{
			++Index;
		}
		const FString Digits = Result.Mid(NumberStart, Index - NumberStart);
		int64 Number = 0;
		if (LexTryParseString(Number, *Digits))
		{
			Expanded += TEXT(" ") + NumberToWords(Number) + TEXT(" ");
		}
	}
	while (Expanded.Contains(TEXT("  ")))
	{
		Expanded.ReplaceInline(TEXT("  "), TEXT(" "));
	}
	return Expanded.TrimStartAndEnd();
}

FString FKokoroPhonemizer::NumberToWords(const int64 Value)
{
	static const TCHAR* Small[] = {TEXT("zero"), TEXT("one"), TEXT("two"), TEXT("three"), TEXT("four"),
		TEXT("five"), TEXT("six"), TEXT("seven"), TEXT("eight"), TEXT("nine"), TEXT("ten"), TEXT("eleven"),
		TEXT("twelve"), TEXT("thirteen"), TEXT("fourteen"), TEXT("fifteen"), TEXT("sixteen"),
		TEXT("seventeen"), TEXT("eighteen"), TEXT("nineteen")};
	static const TCHAR* Tens[] = {TEXT(""), TEXT(""), TEXT("twenty"), TEXT("thirty"), TEXT("forty"),
		TEXT("fifty"), TEXT("sixty"), TEXT("seventy"), TEXT("eighty"), TEXT("ninety")};
	if (Value < 0)
	{
		return TEXT("minus ") + NumberToWords(-Value);
	}
	if (Value < 20)
	{
		return Small[Value];
	}
	if (Value < 100)
	{
		return FString(Tens[Value / 10]) + (Value % 10 ? TEXT(" ") + NumberToWords(Value % 10) : TEXT(""));
	}
	if (Value < 1000)
	{
		return NumberToWords(Value / 100) + TEXT(" hundred")
			+ (Value % 100 ? TEXT(" ") + NumberToWords(Value % 100) : TEXT(""));
	}
	for (const TPair<int64, FString>& Scale : TArray<TPair<int64, FString>>{
		{1000000000LL, TEXT("billion")}, {1000000LL, TEXT("million")}, {1000LL, TEXT("thousand")}})
	{
		if (Value >= Scale.Key)
		{
			return NumberToWords(Value / Scale.Key) + TEXT(" ") + Scale.Value
				+ (Value % Scale.Key ? TEXT(" ") + NumberToWords(Value % Scale.Key) : TEXT(""));
		}
	}
	return FString::Printf(TEXT("%lld"), Value);
}

TArray<FString> FKokoroPhonemizer::ApplyFliteLts(const FString& Word)
{
	const FString Lower = Word.ToLower();
	const FTCHARToUTF8 Utf8(*Lower);
	const FString Context = TEXT("000#") + Lower + TEXT("#000");
	const FTCHARToUTF8 ContextUtf8(*Context);
	const ANSICHAR* Data = ContextUtf8.Get();
	TArray<FString> Result;

	for (int32 Position = LtsContextSize + Utf8.Length() - 1; Position >= LtsContextSize; --Position)
	{
		const uint8 Letter = static_cast<uint8>(Data[Position]);
		if (Letter < 'a' || Letter > 'z')
		{
			continue;
		}

		TArray<uint8> Features;
		Features.AddZeroed(9);
		for (int32 Index = 0; Index < LtsContextSize; ++Index)
		{
			Features[Index] = static_cast<uint8>(Data[Position - LtsContextSize + Index]);
			Features[LtsContextSize + Index] = static_cast<uint8>(Data[Position + Index + 1]);
		}
		Features[8] = '0';

		const uint8 PhoneIndex = ApplyLtsModel(Features, SpeechGen_GetCmuLtsLetterIndex()[Letter - 'a']);
		FString Phone = UTF8_TO_TCHAR(SpeechGen_GetCmuLtsPhoneTable()[PhoneIndex]);
		if (Phone == TEXT("epsilon"))
		{
			continue;
		}

		TArray<FString> Parts;
		Phone.ParseIntoArray(Parts, TEXT("-"), true);
		for (int32 PartIndex = Parts.Num() - 1; PartIndex >= 0; --PartIndex)
		{
			Result.Insert(Parts[PartIndex].ToUpper(), 0);
		}
	}
	return Result;
}

void FKokoroPhonemizer::AppendWordPhones(const FString& OriginalWord,
	const TMap<FString, TArray<FString>>& InDictionary, TArray<FString>& OutPhones)
{
	const FString Word = OriginalWord.ToLower();
	if (const TArray<FString>* Found = InDictionary.Find(Word))
	{
		OutPhones.Append(*Found);
		return;
	}
	OutPhones.Append(ApplyFliteLts(Word));
}

FString FKokoroPhonemizer::ArpabetToIpa(const TArray<FString>& Phones)
{
	FString Result;
	for (const FString& RawPhone : Phones)
	{
		if (RawPhone == TEXT(" ") || (RawPhone.Len() == 1
			&& FString(TEXT(";:,.!?\"—…()\u201c\u201d")).Contains(RawPhone)))
		{
			Result += RawPhone;
			continue;
		}

		FString Phone = RawPhone.ToUpper();
		int32 Stress = 0;
		if (!Phone.IsEmpty() && FChar::IsDigit(Phone[Phone.Len() - 1]))
		{
			Stress = Phone[Phone.Len() - 1] - TEXT('0');
			Phone.LeftChopInline(1);
		}

		const FString* Ipa = GetArpabetMap().Find(Phone);
		if (!Ipa)
		{
			continue;
		}
		if (Phone == TEXT("AH") && Stress == 0)
		{
			Result += TEXT("ə");
			continue;
		}
		if (Phone == TEXT("ER") && Stress == 0)
		{
			Result += TEXT("ɚ");
			continue;
		}
		if (Stress == 1)
		{
			Result += TEXT("ˈ");
		}
		else if (Stress == 2)
		{
			Result += TEXT("ˌ");
		}
		Result += *Ipa;
	}
	return Result;
}
