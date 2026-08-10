#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SpeechGen/SpeechGenTypes.h"
#include "SpeechGenVoiceProfile.generated.h"

UCLASS(BlueprintType)
class SPEECHGEN_API USpeechGenVoiceProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	USpeechGenVoiceProfile();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeechGen|Voice")
	ESpeechGenLanguage Language = ESpeechGenLanguage::English;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeechGen|Voice")
	TArray<FSpeechGenVoiceWeight> NativeVoiceBlend;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeechGen|Voice")
	TArray<FSpeechGenStyleDefinition> DirectedStyles;

	bool ResolveStyle(ESpeechGenStyleMode StyleMode, FName StyleId,
		TArray<FSpeechGenVoiceWeight>& OutBlend, float& OutSpeed, float& OutGain, float& OutPauseScale) const;
	bool ValidateVoiceBlend(const TArray<FSpeechGenVoiceWeight>& Blend, FString& OutError) const;
};
