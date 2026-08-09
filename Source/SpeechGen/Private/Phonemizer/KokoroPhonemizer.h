#pragma once

#include "CoreMinimal.h"

class FKokoroPhonemizer
{
public:
	bool Initialize(const FString& RuntimeDirectory, FString& OutError);
	bool Encode(const FString& Text, TArray<int64>& OutTokenIds, FString& OutPhonemes, FString& OutError) const;

private:
	static FString NormalizeText(const FString& Text);
	static FString NumberToWords(int64 Value);
	static TArray<FString> ApplyFliteLts(const FString& Word);
	static FString ArpabetToIpa(const TArray<FString>& Phones);
	static void AppendWordPhones(const FString& OriginalWord, const TMap<FString, TArray<FString>>& Dictionary,
		TArray<FString>& OutPhones);

	TMap<FString, TArray<FString>> Dictionary;
	TMap<FString, int64> Vocab;
};
