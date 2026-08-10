#pragma once

#include "CoreMinimal.h"
#include "SpeechGenTypes.generated.h"

UENUM(BlueprintType)
enum class ESpeechGenRuntimeState : uint8
{
	Unavailable,
	Loading,
	Ready,
	Failed
};

UENUM(BlueprintType)
enum class ESpeechGenStyleMode : uint8
{
	Native,
	Directed
};

UENUM(BlueprintType)
enum class ESpeechGenLanguage : uint8
{
	English,
	MandarinChinese,
	Spanish,
	Hindi,
	Italian,
	BrazilianPortuguese,
	TurkishExperimental UMETA(DisplayName = "Turkish (Experimental)"),
	AzerbaijaniExperimental UMETA(DisplayName = "Azerbaijani (Experimental)"),
	GermanExperimental UMETA(DisplayName = "German (Experimental)"),
	DutchExperimental UMETA(DisplayName = "Dutch (Experimental)")
};

USTRUCT(BlueprintType)
struct SPEECHGEN_API FSpeechGenVoiceWeight
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeechGen|Voice")
	FName VoiceId = TEXT("af_heart");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeechGen|Voice", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;
};

USTRUCT(BlueprintType)
struct SPEECHGEN_API FSpeechGenVoiceVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeechGen|Voice")
	FName VariantId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeechGen|Voice")
	TArray<FSpeechGenVoiceWeight> VoiceBlend;
};

USTRUCT(BlueprintType)
struct SPEECHGEN_API FSpeechGenStyleDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeechGen|Style")
	FName StyleId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeechGen|Style")
	TArray<FSpeechGenVoiceWeight> VoiceBlend;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeechGen|Style")
	TArray<FSpeechGenVoiceVariant> VoiceVariants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeechGen|Style", meta = (ClampMin = "0.7", ClampMax = "1.3"))
	float Speed = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeechGen|Style", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Gain = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeechGen|Style", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PauseScale = 1.0f;
};

USTRUCT(BlueprintType)
struct SPEECHGEN_API FSpeechGenRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SpeechGen")
	FGuid TurnId;

	UPROPERTY(BlueprintReadWrite, Category = "SpeechGen")
	FGuid SegmentId;

	UPROPERTY(BlueprintReadWrite, Category = "SpeechGen")
	FString Text;

	UPROPERTY(BlueprintReadWrite, Category = "SpeechGen")
	ESpeechGenLanguage Language = ESpeechGenLanguage::English;

	UPROPERTY(BlueprintReadWrite, Category = "SpeechGen")
	TArray<FSpeechGenVoiceWeight> VoiceBlend;

	UPROPERTY(BlueprintReadWrite, Category = "SpeechGen", meta = (ClampMin = "0.7", ClampMax = "1.3"))
	float Speed = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category = "SpeechGen", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Gain = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category = "SpeechGen", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PauseScale = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category = "SpeechGen", AdvancedDisplay)
	bool bEnableDiagnostics = false;
};

struct SPEECHGEN_API FSpeechGenResult
{
	FGuid TurnId;

	FGuid SegmentId;

	FString Phonemes;

	TArray<int16> PcmSamples;

	int32 SampleRate = 24000;

	float DurationSeconds = 0.0f;
};

USTRUCT(BlueprintType)
struct SPEECHGEN_API FSpeechGenRuntimeStatus
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpeechGen")
	ESpeechGenRuntimeState State = ESpeechGenRuntimeState::Unavailable;

	UPROPERTY(BlueprintReadOnly, Category = "SpeechGen")
	FString Model = TEXT("Kokoro-82M v1.0 CPU Mixed");

	UPROPERTY(BlueprintReadOnly, Category = "SpeechGen")
	FString Backend = TEXT("ONNX Runtime CPU");

	UPROPERTY(BlueprintReadOnly, Category = "SpeechGen")
	FString Phonemizer = TEXT("Native multilingual G2P");

	UPROPERTY(BlueprintReadOnly, Category = "SpeechGen")
	FString RuntimePath;

	UPROPERTY(BlueprintReadOnly, Category = "SpeechGen")
	FString Error;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FSpeechGenSegmentReady, const FSpeechGenResult&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FSpeechGenRequestFailed, FGuid, FGuid, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FSpeechGenRuntimeStateChanged, const FSpeechGenRuntimeStatus&);
