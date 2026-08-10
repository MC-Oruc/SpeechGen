#pragma once

#include "CoreMinimal.h"
#include "SpeechGen/SpeechGenTypes.h"

class FKokoroLanguageFrontend;

class FKokoroPhonemizer
{
public:
	~FKokoroPhonemizer();
	bool Initialize(const FString& RuntimeDirectory, FString& OutError);
	bool Encode(ESpeechGenLanguage Language, const FString& Text, TArray<int64>& OutTokenIds,
		FString& OutPhonemes, FString& OutError) const;

private:
	static FString NormalizeEnglishText(const FString& Text);
	static FString NumberToWords(int64 Value);
	static TArray<FString> ApplyFliteLts(const FString& Word);
	static FString ArpabetToIpa(const TArray<FString>& Phones);
	static void AppendWordPhones(const FString& OriginalWord, const TMap<FString, TArray<FString>>& Dictionary,
		TArray<FString>& OutPhones);

	TMap<FString, TArray<FString>> Dictionary;
	TMap<FString, int64> Vocab;
	TUniquePtr<FKokoroLanguageFrontend> LanguageFrontend;
};
