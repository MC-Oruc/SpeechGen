#pragma once

#include "CoreMinimal.h"
#include "SpeechGen/SpeechGenTypes.h"

class FKokoroLanguageFrontend
{
public:
	explicit FKokoroLanguageFrontend(const FString& InRuntimeDirectory);

	bool Phonemize(ESpeechGenLanguage Language, const FString& Text, FString& OutPhonemes, FString& OutError) const;

private:
	bool PhonemizeMandarin(const FString& Text, FString& OutPhonemes, FString& OutError) const;
	bool LoadMandarinDictionary(FString& OutError) const;

	static FString PhonemizeSpanish(const FString& Text);
	static FString PhonemizeItalian(const FString& Text);
	static FString PhonemizePortuguese(const FString& Text);
	static FString PhonemizeHindi(const FString& Text);

	FString RuntimeDirectory;
	mutable TMap<FString, FString> MandarinDictionary;
	mutable int32 MandarinMaxKeyLength = 0;
	mutable bool bMandarinDictionaryLoaded = false;
};
