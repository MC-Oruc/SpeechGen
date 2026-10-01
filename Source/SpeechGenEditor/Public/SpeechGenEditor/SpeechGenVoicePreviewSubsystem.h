#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "SpeechGen/SpeechGenTypes.h"
#include "SpeechGenVoicePreviewSubsystem.generated.h"

class UAudioComponent;
class UGameInstance;
class USoundWaveProcedural;
class USpeechGenSubsystem;
class USpeechGenVoiceProfile;

UCLASS()
class SPEECHGENEDITOR_API USpeechGenVoicePreviewSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	void Preview(const USpeechGenVoiceProfile& Profile, FName VariantId, const FString& Text);
	void StopPreview();
	FText GetStatusText() const;
	FText GetMetricsText() const;
	bool IsBusy() const { return bLoading || bSynthesizing; }

private:
	void SubmitRequest();
	void HandleRuntimeStateChanged(const FSpeechGenRuntimeStatus& Status);
	void HandleSegmentReady(const FSpeechGenResult& Result);
	void HandleRequestFailed(FGuid TurnId, FGuid SegmentId, const FString& Error);

	UPROPERTY(Transient)
	TObjectPtr<USpeechGenSubsystem> SpeechGen;

	UPROPERTY(Transient)
	TObjectPtr<UGameInstance> PreviewGameInstance;

	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> PreviewWave;

	TWeakObjectPtr<UAudioComponent> PreviewAudio;
	FSpeechGenRequest PendingRequest;
	FGuid ActiveTurnId;
	FGuid ActiveSegmentId;
	FString LastError;
	ESpeechGenResultSource LastSource = ESpeechGenResultSource::Synthesized;
	double LoadStartedAtSeconds = 0.0;
	double RequestStartedAtSeconds = 0.0;
	float LastLoadMilliseconds = 0.0f;
	float LastRequestMilliseconds = 0.0f;
	float LastAudioSeconds = 0.0f;
	int32 LastSampleRate = 0;
	bool bLoading = false;
	bool bSynthesizing = false;
	bool bHasMetrics = false;
};
