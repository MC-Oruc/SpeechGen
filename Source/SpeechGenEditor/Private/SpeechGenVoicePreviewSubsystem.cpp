#include "SpeechGenEditor/SpeechGenVoicePreviewSubsystem.h"

#include "Components/AudioComponent.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Sound/SoundWaveProcedural.h"
#include "SpeechGen/SpeechGenSubsystem.h"
#include "SpeechGen/SpeechGenVoiceProfile.h"

#define LOCTEXT_NAMESPACE "SpeechGenVoicePreview"

void USpeechGenVoicePreviewSubsystem::Preview(
	const USpeechGenVoiceProfile& Profile, const FName VariantId, const FString& Text)
{
	StopPreview();
	LastError.Reset();
	bHasMetrics = false;
	LastLoadMilliseconds = 0.0f;
	if (Text.TrimStartAndEnd().IsEmpty() || Text.Len() > 240)
	{
		LastError = LOCTEXT("InvalidText", "Enter a preview sentence of 1-240 characters.").ToString();
		return;
	}

	float Speed = 1.0f;
	float Gain = 1.0f;
	float PauseScale = 1.0f;
	if (!Profile.Resolve(VariantId, ESpeechGenStyleMode::Native, NAME_None,
		PendingRequest.VoiceBlend, Speed, Gain, PauseScale, LastError))
	{
		return;
	}
	const TOptional<ESpeechGenLanguage> Language = USpeechGenVoiceProfile::GetVoiceLanguage(
		PendingRequest.VoiceBlend[0].VoiceId);
	if (!Language.IsSet())
	{
		LastError = LOCTEXT("UnsupportedVoice", "The selected voice is not supported by SpeechGen.").ToString();
		return;
	}
	if (!USpeechGenVoiceProfile::ValidateVoiceBlend(
		Language.GetValue(), PendingRequest.VoiceBlend, LastError))
	{
		return;
	}

	PendingRequest.TurnId = FGuid::NewGuid();
	PendingRequest.SegmentId = FGuid::NewGuid();
	PendingRequest.Text = Text;
	PendingRequest.Language = Language.GetValue();
	PendingRequest.Speed = Speed;
	PendingRequest.Gain = Gain;
	PendingRequest.PauseScale = PauseScale;
	ActiveTurnId = PendingRequest.TurnId;
	ActiveSegmentId = PendingRequest.SegmentId;

	if (!SpeechGen)
	{
		PreviewGameInstance = NewObject<UGameInstance>(this);
		SpeechGen = USpeechGenSubsystem::CreateEditorPreviewRuntime(*PreviewGameInstance);
		SpeechGen->OnRuntimeStateChanged.AddUObject(this,
			&USpeechGenVoicePreviewSubsystem::HandleRuntimeStateChanged);
		SpeechGen->OnSegmentReady.AddUObject(this,
			&USpeechGenVoicePreviewSubsystem::HandleSegmentReady);
		SpeechGen->OnRequestFailed.AddUObject(this,
			&USpeechGenVoicePreviewSubsystem::HandleRequestFailed);
	}

	USpeechGenSubsystem* Subsystem = SpeechGen;
	if (!Subsystem)
	{
		LastError = LOCTEXT("NoSubsystem", "SpeechGen could not be initialized.").ToString();
		return;
	}
	if (Subsystem->GetRuntimeStatus().State == ESpeechGenRuntimeState::Ready)
	{
		SubmitRequest();
		return;
	}

	bLoading = true;
	LoadStartedAtSeconds = FPlatformTime::Seconds();
	if (!Subsystem->StartRuntime())
	{
		bLoading = false;
		LastError = Subsystem->GetRuntimeStatus().Error;
	}
}

void USpeechGenVoicePreviewSubsystem::SubmitRequest()
{
	USpeechGenSubsystem* Subsystem = SpeechGen;
	if (!Subsystem || !ActiveTurnId.IsValid())
	{
		return;
	}
	bLoading = false;
	bSynthesizing = true;
	RequestStartedAtSeconds = FPlatformTime::Seconds();
	Subsystem->SynthesizeAsync(PendingRequest);
}

void USpeechGenVoicePreviewSubsystem::HandleRuntimeStateChanged(const FSpeechGenRuntimeStatus& Status)
{
	if (!bLoading)
	{
		return;
	}
	if (Status.State == ESpeechGenRuntimeState::Ready)
	{
		LastLoadMilliseconds = static_cast<float>((FPlatformTime::Seconds() - LoadStartedAtSeconds) * 1000.0);
		SubmitRequest();
	}
	else if (Status.State == ESpeechGenRuntimeState::Failed || Status.State == ESpeechGenRuntimeState::Unavailable)
	{
		bLoading = false;
		LastError = Status.Error;
	}
}

void USpeechGenVoicePreviewSubsystem::HandleSegmentReady(const FSpeechGenResult& Result)
{
	if (Result.TurnId != ActiveTurnId || Result.SegmentId != ActiveSegmentId)
	{
		return;
	}
	bSynthesizing = false;
	LastRequestMilliseconds = static_cast<float>((FPlatformTime::Seconds() - RequestStartedAtSeconds) * 1000.0);
	LastAudioSeconds = Result.DurationSeconds;
	LastSampleRate = Result.SampleRate;
	LastSource = Result.Source;
	bHasMetrics = true;
	ActiveTurnId.Invalidate();
	ActiveSegmentId.Invalidate();
	if (!GEditor || Result.PcmSamples.IsEmpty() || Result.SampleRate <= 0)
	{
		LastError = LOCTEXT("NoAudio", "The preview produced no playable audio.").ToString();
		return;
	}

	PreviewWave = NewObject<USoundWaveProcedural>(this);
	PreviewWave->SetSampleRate(Result.SampleRate);
	PreviewWave->NumChannels = 1;
	PreviewWave->Duration = Result.DurationSeconds;
	PreviewWave->SoundGroup = SOUNDGROUP_Voice;
	PreviewWave->QueueAudio(reinterpret_cast<const uint8*>(Result.PcmSamples.GetData()),
		Result.PcmSamples.Num() * sizeof(int16));
	PreviewAudio = GEditor->PlayPreviewSound(PreviewWave);
	if (!PreviewAudio.IsValid())
	{
		LastError = LOCTEXT("AudioDeviceUnavailable", "The editor audio device is unavailable.").ToString();
	}
}

void USpeechGenVoicePreviewSubsystem::HandleRequestFailed(
	const FGuid TurnId, const FGuid SegmentId, const FString& Error)
{
	if (TurnId == ActiveTurnId && SegmentId == ActiveSegmentId)
	{
		bSynthesizing = false;
		ActiveTurnId.Invalidate();
		ActiveSegmentId.Invalidate();
		LastError = Error;
	}
}

void USpeechGenVoicePreviewSubsystem::StopPreview()
{
	if (USpeechGenSubsystem* Subsystem = SpeechGen; Subsystem && ActiveTurnId.IsValid())
	{
		Subsystem->CancelTurn(ActiveTurnId);
	}
	ActiveTurnId.Invalidate();
	ActiveSegmentId.Invalidate();
	bLoading = false;
	bSynthesizing = false;
	if (PreviewAudio.IsValid())
	{
		PreviewAudio->Stop();
		PreviewAudio.Reset();
	}
	PreviewWave = nullptr;
}

FText USpeechGenVoicePreviewSubsystem::GetStatusText() const
{
	if (!LastError.IsEmpty())
	{
		return FText::FromString(LastError);
	}
	if (bLoading)
	{
		return LOCTEXT("Loading", "Loading speech model...");
	}
	if (bSynthesizing)
	{
		return LOCTEXT("Synthesizing", "Preparing voice preview...");
	}
	if (PreviewAudio.IsValid() && PreviewAudio->IsPlaying())
	{
		return LOCTEXT("Playing", "Playing voice preview");
	}
	return bHasMetrics ? LOCTEXT("Ready", "Preview ready") : LOCTEXT("Idle", "Ready to preview");
}

FText USpeechGenVoicePreviewSubsystem::GetMetricsText() const
{
	if (!bHasMetrics)
	{
		return FText::GetEmpty();
	}
	const FText Source = LastSource == ESpeechGenResultSource::AuthoredCache
		? LOCTEXT("AuthoredCache", "Authored cache")
		: LastSource == ESpeechGenResultSource::GeneratedCache
			? LOCTEXT("GeneratedCache", "Generated cache")
			: LOCTEXT("Synthesized", "Synthesized");
	return FText::Format(LOCTEXT("Metrics",
		"{0} | Model load: {1} ms | Request: {2} ms | Audio: {3} s | RTF: {4}x | {5} Hz"),
		Source, FText::AsNumber(FMath::RoundToInt(LastLoadMilliseconds)),
		FText::AsNumber(FMath::RoundToInt(LastRequestMilliseconds)),
		FText::AsNumber(LastAudioSeconds),
		FText::AsNumber(LastAudioSeconds > 0.0f
			? LastRequestMilliseconds / (LastAudioSeconds * 1000.0f) : 0.0f),
		FText::AsNumber(LastSampleRate));
}

void USpeechGenVoicePreviewSubsystem::Deinitialize()
{
	StopPreview();
	if (USpeechGenSubsystem* Subsystem = SpeechGen)
	{
		Subsystem->OnRuntimeStateChanged.RemoveAll(this);
		Subsystem->OnSegmentReady.RemoveAll(this);
		Subsystem->OnRequestFailed.RemoveAll(this);
		Subsystem->StopRuntime();
	}
	SpeechGen = nullptr;
	PreviewGameInstance = nullptr;
	Super::Deinitialize();
}

#undef LOCTEXT_NAMESPACE
