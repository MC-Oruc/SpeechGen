#include "SpeechGen/SpeechGenSubsystem.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "Phonemizer/KokoroPhonemizer.h"
#include "SpeechGen/SpeechGenVoiceProfile.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpeechGen, Log, All);

namespace
{
	constexpr int32 StyleDimension = 256;
	constexpr int32 VoiceRows = 510;
	constexpr int32 MaxSamplesPerToken = 8192;

	struct FResolvedVoiceWeight
	{
		FName VoiceId;
		float Weight = 0.0f;
	};

	bool LoadVoiceStyle(const FString& RuntimeDirectory, const TArray<FResolvedVoiceWeight>& Blend,
		const int32 PhonemeCount, TArray<float>& OutStyle, FString& OutError)
	{
		if (PhonemeCount < 1 || PhonemeCount > VoiceRows)
		{
			OutError = TEXT("Voice style row is outside the Kokoro context window.");
			return false;
		}

		float WeightSum = 0.0f;
		for (const FResolvedVoiceWeight& Voice : Blend)
		{
			WeightSum += FMath::Max(0.0f, Voice.Weight);
		}
		if (WeightSum <= UE_SMALL_NUMBER)
		{
			OutError = TEXT("Voice profile has no positive blend weights.");
			return false;
		}

		OutStyle.Init(0.0f, StyleDimension);
		for (const FResolvedVoiceWeight& Voice : Blend)
		{
			if (Voice.Weight <= 0.0f)
			{
				continue;
			}

			const FString VoicePath = FPaths::Combine(RuntimeDirectory, TEXT("voices"),
				Voice.VoiceId.ToString() + TEXT(".bin"));
			TArray64<uint8> VoiceBytes;
			if (!FFileHelper::LoadFileToArray(VoiceBytes, *VoicePath)
				|| VoiceBytes.Num() != static_cast<int64>(VoiceRows * StyleDimension * sizeof(float)))
			{
				OutError = FString::Printf(TEXT("Voice data is missing or invalid: %s"), *Voice.VoiceId.ToString());
				return false;
			}

			const float* VoiceData = reinterpret_cast<const float*>(VoiceBytes.GetData());
			const float* Row = VoiceData + (PhonemeCount - 1) * StyleDimension;
			const float NormalizedWeight = Voice.Weight / WeightSum;
			for (int32 Index = 0; Index < StyleDimension; ++Index)
			{
				OutStyle[Index] += Row[Index] * NormalizedWeight;
			}
		}
		return true;
	}
}

struct USpeechGenSubsystem::FResolvedRequest
{
	FGuid TurnId;
	FGuid SegmentId;
	FString Text;
	TArray<FResolvedVoiceWeight> VoiceBlend;
	float Speed = 1.0f;
	float Gain = 1.0f;
	float PauseScale = 1.0f;
	bool bEnableDiagnostics = false;
	double EnqueuedAtSeconds = 0.0;
	int32 QueueDepthAtSubmission = 0;
	TSharedPtr<std::atomic_bool, ESPMode::ThreadSafe> CancellationFlag;
};

void USpeechGenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bShuttingDown = false;
	RuntimeStatus.RuntimePath = ResolveRuntimeDirectory();
	WorkerPool.Reset(FQueuedThreadPool::Allocate());
	if (!WorkerPool || !WorkerPool->Create(1, 256 * 1024, TPri_Normal, TEXT("SpeechGenWorker")))
	{
		WorkerPool.Reset();
		SetRuntimeState(ESpeechGenRuntimeState::Failed, TEXT("Unable to create SpeechGen worker thread."));
		return;
	}
	BeginRuntimeLoad();
}

void USpeechGenSubsystem::Deinitialize()
{
	bShuttingDown = true;
	CancelAll();
	if (WorkerPool)
	{
		WorkerPool->Destroy();
		WorkerPool.Reset();
	}
	Phonemizer.Reset();
	ModelInstance.Reset();
	Model.Reset();
	ModelData = nullptr;
	Super::Deinitialize();
}

FString USpeechGenSubsystem::ResolveRuntimeDirectory()
{
#if WITH_EDITOR
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SpeechGen/Runtimes/Kokoro/Win64"), RuntimeVersion);
#else
	return FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("SpeechGen/Runtimes/Kokoro/Win64"), RuntimeVersion);
#endif
}

void USpeechGenSubsystem::BeginRuntimeLoad()
{
	FModuleManager::Get().LoadModule(TEXT("NNERuntimeORT"));

	const FString RuntimeDirectory = ResolveRuntimeDirectory();
	const FString ModelPath = FPaths::Combine(RuntimeDirectory, TEXT("onnx/model_quantized.onnx"));
	TArray64<uint8> ModelBytes;
	if (!FFileHelper::LoadFileToArray(ModelBytes, *ModelPath))
	{
		SetRuntimeState(ESpeechGenRuntimeState::Unavailable,
			FString::Printf(TEXT("SpeechGen runtime is not installed: %s"), *ModelPath));
		return;
	}

	SetRuntimeState(ESpeechGenRuntimeState::Loading);
	ModelData = NewObject<UNNEModelData>(this);
	ModelData->Init(TEXT("onnx"), ModelBytes);

	TWeakObjectPtr<USpeechGenSubsystem> WeakThis(this);
	TObjectPtr<UNNEModelData> LoadedModelData = ModelData;
	AsyncPool(*WorkerPool, [WeakThis, LoadedModelData, RuntimeDirectory]() mutable
	{
		TSharedPtr<UE::NNE::IModelCPU> LoadedModel;
		TSharedPtr<UE::NNE::IModelInstanceCPU> LoadedInstance;
		TSharedPtr<FKokoroPhonemizer> LoadedPhonemizer = MakeShared<FKokoroPhonemizer>();
		FString Error;

		const TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeORTCpu"));
		if (!Runtime.IsValid())
		{
			Error = TEXT("NNERuntimeORTCpu is unavailable.");
		}
		else
		{
			LoadedModel = Runtime->CreateModelCPU(LoadedModelData);
			if (!LoadedModel)
			{
				Error = TEXT("NNERuntimeORTCpu rejected the Kokoro Q8 model.");
			}
			else
			{
				LoadedInstance = LoadedModel->CreateModelInstanceCPU();
				if (!LoadedInstance)
				{
					Error = TEXT("Unable to create the Kokoro CPU model instance.");
				}
			}
		}

		if (Error.IsEmpty() && !LoadedPhonemizer->Initialize(RuntimeDirectory, Error))
		{
			LoadedInstance.Reset();
			LoadedModel.Reset();
		}

		AsyncTask(ENamedThreads::GameThread,
			[WeakThis, LoadedModel = MoveTemp(LoadedModel), LoadedInstance = MoveTemp(LoadedInstance),
				LoadedPhonemizer = MoveTemp(LoadedPhonemizer), Error]() mutable
			{
				if (USpeechGenSubsystem* Subsystem = WeakThis.Get())
				{
					Subsystem->CompleteRuntimeLoad(MoveTemp(LoadedModel), MoveTemp(LoadedInstance),
						MoveTemp(LoadedPhonemizer), Error);
				}
			});
	});
}

void USpeechGenSubsystem::CompleteRuntimeLoad(TSharedPtr<UE::NNE::IModelCPU> InModel,
	TSharedPtr<UE::NNE::IModelInstanceCPU> InInstance, TSharedPtr<FKokoroPhonemizer> InPhonemizer,
	const FString& Error)
{
	if (!Error.IsEmpty())
	{
		SetRuntimeState(ESpeechGenRuntimeState::Failed, Error);
		return;
	}
	Model = MoveTemp(InModel);
	ModelInstance = MoveTemp(InInstance);
	Phonemizer = MoveTemp(InPhonemizer);
	SetRuntimeState(ESpeechGenRuntimeState::Ready);
}

FGuid USpeechGenSubsystem::SynthesizeAsync(const FSpeechGenRequest& Request)
{
	FResolvedRequest Resolved;
	FString Error;
	if (RuntimeStatus.State != ESpeechGenRuntimeState::Ready || !ResolveRequest(Request, Resolved, Error))
	{
		const FGuid SegmentId = Request.SegmentId.IsValid() ? Request.SegmentId : FGuid::NewGuid();
		UE_LOG(LogSpeechGen, Warning, TEXT("Rejected speech request Turn=%s Segment=%s: %s"),
			*Request.TurnId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*SegmentId.ToString(EGuidFormats::DigitsWithHyphensLower),
			Error.IsEmpty() ? TEXT("SpeechGen runtime is not ready.") : *Error);
		OnRequestFailed.Broadcast(Request.TurnId, SegmentId,
			Error.IsEmpty() ? TEXT("SpeechGen runtime is not ready.") : Error);
		return SegmentId;
	}

	{
		FScopeLock Lock(&CancellationMutex);
		Resolved.CancellationFlag = MakeShared<std::atomic_bool, ESPMode::ThreadSafe>(false);
		ActiveCancellationFlags.FindOrAdd(Resolved.TurnId).Add(Resolved.CancellationFlag);
	}
	Resolved.EnqueuedAtSeconds = FPlatformTime::Seconds();
	Resolved.QueueDepthAtSubmission = PendingRequestCount.fetch_add(1, std::memory_order_relaxed) + 1;
	if (Resolved.bEnableDiagnostics)
	{
		UE_LOG(LogSpeechGen, Log,
			TEXT("Queued request Turn=%s Segment=%s Chars=%d QueueDepth=%d Speed=%.2f Voices=%d"),
			*Resolved.TurnId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*Resolved.SegmentId.ToString(EGuidFormats::DigitsWithHyphensLower), Resolved.Text.Len(),
			Resolved.QueueDepthAtSubmission, Resolved.Speed, Resolved.VoiceBlend.Num());
	}
	const FGuid SegmentId = Resolved.SegmentId;
	TWeakObjectPtr<USpeechGenSubsystem> WeakThis(this);
	AsyncPool(*WorkerPool, [WeakThis, Resolved = MoveTemp(Resolved)]() mutable
	{
		if (USpeechGenSubsystem* Subsystem = WeakThis.Get())
		{
			Subsystem->ExecuteRequest(MoveTemp(Resolved));
		}
	});
	return SegmentId;
}

bool USpeechGenSubsystem::ResolveRequest(const FSpeechGenRequest& Request, FResolvedRequest& OutRequest,
	FString& OutError) const
{
	if (!Request.VoiceProfile)
	{
		OutError = TEXT("Speech request has no voice profile.");
		return false;
	}
	if (Request.Text.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("Speech request text is empty.");
		return false;
	}

	TArray<FSpeechGenVoiceWeight> Blend;
	if (!Request.VoiceProfile->ResolveStyle(Request.StyleMode, Request.StyleId, Blend,
		OutRequest.Speed, OutRequest.Gain, OutRequest.PauseScale))
	{
		OutError = Request.StyleMode == ESpeechGenStyleMode::Directed
			? FString::Printf(TEXT("Voice profile does not define style '%s'."), *Request.StyleId.ToString())
			: TEXT("Voice profile has no native voice blend.");
		return false;
	}

	OutRequest.TurnId = Request.TurnId.IsValid() ? Request.TurnId : FGuid::NewGuid();
	OutRequest.SegmentId = Request.SegmentId.IsValid() ? Request.SegmentId : FGuid::NewGuid();
	OutRequest.Text = Request.Text;
	OutRequest.bEnableDiagnostics = Request.bEnableDiagnostics;
	for (const FSpeechGenVoiceWeight& Voice : Blend)
	{
		OutRequest.VoiceBlend.Add({Voice.VoiceId, Voice.Weight});
	}
	return true;
}

void USpeechGenSubsystem::ExecuteRequest(FResolvedRequest Request)
{
	const double ExecutionStartedAtSeconds = FPlatformTime::Seconds();
	const double QueueWaitMilliseconds = (ExecutionStartedAtSeconds - Request.EnqueuedAtSeconds) * 1000.0;
	if (bShuttingDown || IsRequestCancelled(Request) || !ModelInstance || !Phonemizer)
	{
		if (Request.bEnableDiagnostics)
		{
			UE_LOG(LogSpeechGen, Log,
				TEXT("Discarded request before inference Turn=%s Segment=%s QueueWaitMs=%.2f ShuttingDown=%s Cancelled=%s RuntimeReady=%s"),
				*Request.TurnId.ToString(EGuidFormats::DigitsWithHyphensLower),
				*Request.SegmentId.ToString(EGuidFormats::DigitsWithHyphensLower), QueueWaitMilliseconds,
				bShuttingDown ? TEXT("true") : TEXT("false"),
				IsRequestCancelled(Request) ? TEXT("true") : TEXT("false"),
				ModelInstance && Phonemizer ? TEXT("true") : TEXT("false"));
		}
		MarkRequestFinished(Request.TurnId, Request.CancellationFlag);
		return;
	}

	TArray<int64> TokenIds;
	FString Phonemes;
	FString Error;
	const double PhonemizeStartedAtSeconds = FPlatformTime::Seconds();
	if (!Phonemizer->Encode(Request.Text, TokenIds, Phonemes, Error))
	{
		UE_LOG(LogSpeechGen, Warning, TEXT("Phonemization failed Turn=%s Segment=%s: %s"),
			*Request.TurnId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*Request.SegmentId.ToString(EGuidFormats::DigitsWithHyphensLower), *Error);
		AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<USpeechGenSubsystem>(this), Request, Error]()
		{
			if (USpeechGenSubsystem* Subsystem = WeakThis.Get())
			{
				if (!IsRequestCancelled(Request))
				{
					Subsystem->OnRequestFailed.Broadcast(Request.TurnId, Request.SegmentId, Error);
				}
				Subsystem->MarkRequestFinished(Request.TurnId, Request.CancellationFlag);
			}
		});
		return;
	}
	const double PhonemizeMilliseconds = (FPlatformTime::Seconds() - PhonemizeStartedAtSeconds) * 1000.0;

	const int32 PhonemeCount = TokenIds.Num() - 2;
	TArray<float> Style;
	const double StyleStartedAtSeconds = FPlatformTime::Seconds();
	if (!LoadVoiceStyle(ResolveRuntimeDirectory(), Request.VoiceBlend, PhonemeCount, Style, Error))
	{
		UE_LOG(LogSpeechGen, Warning, TEXT("Voice style load failed Turn=%s Segment=%s: %s"),
			*Request.TurnId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*Request.SegmentId.ToString(EGuidFormats::DigitsWithHyphensLower), *Error);
		AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<USpeechGenSubsystem>(this), Request, Error]()
		{
			if (USpeechGenSubsystem* Subsystem = WeakThis.Get())
			{
				if (!IsRequestCancelled(Request))
				{
					Subsystem->OnRequestFailed.Broadcast(Request.TurnId, Request.SegmentId, Error);
				}
				Subsystem->MarkRequestFinished(Request.TurnId, Request.CancellationFlag);
			}
		});
		return;
	}
	const double StyleMilliseconds = (FPlatformTime::Seconds() - StyleStartedAtSeconds) * 1000.0;

	TArray<UE::NNE::FTensorShape> InputShapes;
	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	for (const UE::NNE::FTensorDesc& Desc : ModelInstance->GetInputTensorDescs())
	{
		if (Desc.GetName() == TEXT("input_ids"))
		{
			InputShapes.Add(UE::NNE::FTensorShape::Make({1, static_cast<uint32>(TokenIds.Num())}));
			InputBindings.Add({TokenIds.GetData(), static_cast<uint64>(TokenIds.Num() * sizeof(int64))});
		}
		else if (Desc.GetName() == TEXT("style"))
		{
			InputShapes.Add(UE::NNE::FTensorShape::Make({1, StyleDimension}));
			InputBindings.Add({Style.GetData(), static_cast<uint64>(Style.Num() * sizeof(float))});
		}
		else if (Desc.GetName() == TEXT("speed"))
		{
			InputShapes.Add(UE::NNE::FTensorShape::Make({1}));
			InputBindings.Add({&Request.Speed, sizeof(float)});
		}
		else
		{
			Error = FString::Printf(TEXT("Unexpected Kokoro input tensor: %s"), *Desc.GetName());
			break;
		}
	}

	TArray<float> Waveform;
	Waveform.SetNumUninitialized(TokenIds.Num() * MaxSamplesPerToken);
	const UE::NNE::FTensorBindingCPU OutputBinding{Waveform.GetData(),
		static_cast<uint64>(Waveform.Num() * sizeof(float))};
	const double InferenceStartedAtSeconds = FPlatformTime::Seconds();
	if (Error.IsEmpty()
		&& ModelInstance->SetInputTensorShapes(InputShapes) == UE::NNE::IModelInstanceCPU::ESetInputTensorShapesStatus::Ok
		&& ModelInstance->RunSync(InputBindings, {OutputBinding}) == UE::NNE::IModelInstanceCPU::ERunSyncStatus::Ok)
	{
		const TConstArrayView<UE::NNE::FTensorShape> OutputShapes = ModelInstance->GetOutputTensorShapes();
		if (OutputShapes.Num() != 1 || OutputShapes[0].Volume() > static_cast<uint64>(Waveform.Num()))
		{
			Error = TEXT("Kokoro output exceeded the deterministic waveform buffer.");
		}
		else
		{
			Waveform.SetNum(static_cast<int32>(OutputShapes[0].Volume()), EAllowShrinking::No);
		}
	}
	else if (Error.IsEmpty())
	{
		Error = TEXT("Kokoro ONNX inference failed.");
	}
	const double InferenceMilliseconds = (FPlatformTime::Seconds() - InferenceStartedAtSeconds) * 1000.0;

	if (!Error.IsEmpty())
	{
		UE_LOG(LogSpeechGen, Warning, TEXT("Inference failed Turn=%s Segment=%s Tokens=%d InferenceMs=%.2f: %s"),
			*Request.TurnId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*Request.SegmentId.ToString(EGuidFormats::DigitsWithHyphensLower), TokenIds.Num(),
			InferenceMilliseconds, *Error);
		AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<USpeechGenSubsystem>(this), Request, Error]()
		{
			if (USpeechGenSubsystem* Subsystem = WeakThis.Get())
			{
				if (!IsRequestCancelled(Request))
				{
					Subsystem->OnRequestFailed.Broadcast(Request.TurnId, Request.SegmentId, Error);
				}
				Subsystem->MarkRequestFinished(Request.TurnId, Request.CancellationFlag);
			}
		});
		return;
	}
	if (IsRequestCancelled(Request))
	{
		if (Request.bEnableDiagnostics)
		{
			UE_LOG(LogSpeechGen, Log,
				TEXT("Discarded completed inference after cancellation Turn=%s Segment=%s InferenceMs=%.2f"),
				*Request.TurnId.ToString(EGuidFormats::DigitsWithHyphensLower),
				*Request.SegmentId.ToString(EGuidFormats::DigitsWithHyphensLower), InferenceMilliseconds);
		}
		MarkRequestFinished(Request.TurnId, Request.CancellationFlag);
		return;
	}

	const double PcmStartedAtSeconds = FPlatformTime::Seconds();
	FSpeechGenResult Result;
	Result.TurnId = Request.TurnId;
	Result.SegmentId = Request.SegmentId;
	Result.SampleRate = SampleRate;
	Result.PcmSamples.Reserve(Waveform.Num() + SampleRate / 2);
	for (const float Sample : Waveform)
	{
		Result.PcmSamples.Add(static_cast<int16>(FMath::Clamp(Sample * Request.Gain, -1.0f, 1.0f) * 32767.0f));
	}
	const float BasePause = Request.Text.EndsWith(TEXT("?")) || Request.Text.EndsWith(TEXT("!")) ? 0.18f : 0.12f;
	Result.PcmSamples.AddZeroed(FMath::RoundToInt(SampleRate * BasePause * Request.PauseScale));
	Result.DurationSeconds = static_cast<float>(Result.PcmSamples.Num()) / SampleRate;
	const double PcmMilliseconds = (FPlatformTime::Seconds() - PcmStartedAtSeconds) * 1000.0;
	if (Request.bEnableDiagnostics)
	{
		const double TotalMilliseconds = (FPlatformTime::Seconds() - Request.EnqueuedAtSeconds) * 1000.0;
		const double RealTimeFactor = Result.DurationSeconds > UE_SMALL_NUMBER
			? (InferenceMilliseconds / 1000.0) / Result.DurationSeconds
			: 0.0;
		UE_LOG(LogSpeechGen, Log,
			TEXT("Completed request Turn=%s Segment=%s Chars=%d Tokens=%d QueueWaitMs=%.2f PhonemizeMs=%.2f StyleMs=%.2f InferenceMs=%.2f PcmMs=%.2f AudioSec=%.2f RTF=%.3f TotalMs=%.2f"),
			*Request.TurnId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*Request.SegmentId.ToString(EGuidFormats::DigitsWithHyphensLower), Request.Text.Len(),
			TokenIds.Num(), QueueWaitMilliseconds, PhonemizeMilliseconds, StyleMilliseconds,
			InferenceMilliseconds, PcmMilliseconds, Result.DurationSeconds, RealTimeFactor, TotalMilliseconds);
	}

	AsyncTask(ENamedThreads::GameThread,
		[WeakThis = TWeakObjectPtr<USpeechGenSubsystem>(this), Request, Result = MoveTemp(Result)]() mutable
		{
			if (USpeechGenSubsystem* Subsystem = WeakThis.Get())
			{
				if (!IsRequestCancelled(Request))
				{
					Subsystem->OnSegmentReady.Broadcast(Result);
				}
				Subsystem->MarkRequestFinished(Request.TurnId, Request.CancellationFlag);
			}
		});
}

void USpeechGenSubsystem::CancelTurn(const FGuid TurnId)
{
	if (!TurnId.IsValid())
	{
		return;
	}
	FScopeLock Lock(&CancellationMutex);
	if (TArray<TSharedPtr<std::atomic_bool, ESPMode::ThreadSafe>>* Flags = ActiveCancellationFlags.Find(TurnId))
	{
		UE_LOG(LogSpeechGen, Log, TEXT("Cancelling speech turn Turn=%s Requests=%d"),
			*TurnId.ToString(EGuidFormats::DigitsWithHyphensLower), Flags->Num());
		for (const TSharedPtr<std::atomic_bool, ESPMode::ThreadSafe>& Flag : *Flags)
		{
			Flag->store(true, std::memory_order_release);
		}
	}
}

void USpeechGenSubsystem::CancelAll()
{
	FScopeLock Lock(&CancellationMutex);
	for (const TPair<FGuid, TArray<TSharedPtr<std::atomic_bool, ESPMode::ThreadSafe>>>& Turn : ActiveCancellationFlags)
	{
		for (const TSharedPtr<std::atomic_bool, ESPMode::ThreadSafe>& Flag : Turn.Value)
		{
			Flag->store(true, std::memory_order_release);
		}
	}
}

bool USpeechGenSubsystem::IsRequestCancelled(const FResolvedRequest& Request)
{
	return !Request.CancellationFlag || Request.CancellationFlag->load(std::memory_order_acquire);
}

void USpeechGenSubsystem::MarkRequestFinished(const FGuid TurnId,
	const TSharedPtr<std::atomic_bool, ESPMode::ThreadSafe>& CancellationFlag)
{
	PendingRequestCount.fetch_sub(1, std::memory_order_relaxed);
	FScopeLock Lock(&CancellationMutex);
	TArray<TSharedPtr<std::atomic_bool, ESPMode::ThreadSafe>>* Flags = ActiveCancellationFlags.Find(TurnId);
	if (!Flags)
	{
		return;
	}
	Flags->RemoveSingleSwap(CancellationFlag, EAllowShrinking::No);
	if (Flags->IsEmpty())
	{
		ActiveCancellationFlags.Remove(TurnId);
	}
}

void USpeechGenSubsystem::SetRuntimeState(const ESpeechGenRuntimeState State, const FString& Error)
{
	RuntimeStatus.State = State;
	RuntimeStatus.Error = Error;
	OnRuntimeStateChanged.Broadcast(RuntimeStatus);
	if (State == ESpeechGenRuntimeState::Failed)
	{
		UE_LOG(LogSpeechGen, Error, TEXT("%s"), *Error);
	}
	else if (State == ESpeechGenRuntimeState::Unavailable)
	{
		UE_LOG(LogSpeechGen, Warning, TEXT("%s"), *Error);
	}
	else if (State == ESpeechGenRuntimeState::Ready)
	{
		UE_LOG(LogSpeechGen, Log, TEXT("SpeechGen runtime ready: %s (%s)."),
			*RuntimeStatus.Model, *RuntimeStatus.Backend);
	}
}
