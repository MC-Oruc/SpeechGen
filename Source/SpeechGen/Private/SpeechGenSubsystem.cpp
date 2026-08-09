#include "SpeechGen/SpeechGenSubsystem.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
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
};

void USpeechGenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RuntimeStatus.RuntimePath = ResolveRuntimeDirectory();
	WorkerPool.Reset(FQueuedThreadPool::Allocate());
	if (!WorkerPool || !WorkerPool->Create(1, 256 * 1024, TPri_BelowNormal, TEXT("SpeechGenWorker")))
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
		OnRequestFailed.Broadcast(Request.TurnId, SegmentId,
			Error.IsEmpty() ? TEXT("SpeechGen runtime is not ready.") : Error);
		return SegmentId;
	}

	{
		FScopeLock Lock(&CancellationMutex);
		ActiveRequestCounts.FindOrAdd(Resolved.TurnId)++;
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
	for (const FSpeechGenVoiceWeight& Voice : Blend)
	{
		OutRequest.VoiceBlend.Add({Voice.VoiceId, Voice.Weight});
	}
	return true;
}

void USpeechGenSubsystem::ExecuteRequest(FResolvedRequest Request)
{
	ON_SCOPE_EXIT
	{
		MarkRequestFinished(Request.TurnId);
	};

	if (bShuttingDown || IsTurnCancelled(Request.TurnId) || !ModelInstance || !Phonemizer)
	{
		return;
	}

	TArray<int64> TokenIds;
	FString Phonemes;
	FString Error;
	if (!Phonemizer->Encode(Request.Text, TokenIds, Phonemes, Error))
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<USpeechGenSubsystem>(this), Request, Error]()
		{
			if (USpeechGenSubsystem* Subsystem = WeakThis.Get(); Subsystem && !Subsystem->IsTurnCancelled(Request.TurnId))
			{
				Subsystem->OnRequestFailed.Broadcast(Request.TurnId, Request.SegmentId, Error);
			}
		});
		return;
	}

	const int32 PhonemeCount = TokenIds.Num() - 2;
	TArray<float> Style;
	if (!LoadVoiceStyle(ResolveRuntimeDirectory(), Request.VoiceBlend, PhonemeCount, Style, Error))
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<USpeechGenSubsystem>(this), Request, Error]()
		{
			if (USpeechGenSubsystem* Subsystem = WeakThis.Get(); Subsystem && !Subsystem->IsTurnCancelled(Request.TurnId))
			{
				Subsystem->OnRequestFailed.Broadcast(Request.TurnId, Request.SegmentId, Error);
			}
		});
		return;
	}

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

	if (!Error.IsEmpty() || IsTurnCancelled(Request.TurnId))
	{
		if (!Error.IsEmpty())
		{
			AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<USpeechGenSubsystem>(this), Request, Error]()
			{
				if (USpeechGenSubsystem* Subsystem = WeakThis.Get(); Subsystem && !Subsystem->IsTurnCancelled(Request.TurnId))
				{
					Subsystem->OnRequestFailed.Broadcast(Request.TurnId, Request.SegmentId, Error);
				}
			});
		}
		return;
	}

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

	AsyncTask(ENamedThreads::GameThread,
		[WeakThis = TWeakObjectPtr<USpeechGenSubsystem>(this), Result = MoveTemp(Result)]() mutable
		{
			if (USpeechGenSubsystem* Subsystem = WeakThis.Get(); Subsystem && !Subsystem->IsTurnCancelled(Result.TurnId))
			{
				Subsystem->OnSegmentReady.Broadcast(Result);
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
	CancelledTurns.Add(TurnId);
}

void USpeechGenSubsystem::CancelAll()
{
	FScopeLock Lock(&CancellationMutex);
	for (const TPair<FGuid, int32>& ActiveRequest : ActiveRequestCounts)
	{
		CancelledTurns.Add(ActiveRequest.Key);
	}
}

bool USpeechGenSubsystem::IsTurnCancelled(const FGuid TurnId) const
{
	FScopeLock Lock(&CancellationMutex);
	return CancelledTurns.Contains(TurnId);
}

void USpeechGenSubsystem::MarkRequestFinished(const FGuid TurnId)
{
	FScopeLock Lock(&CancellationMutex);
	int32* ActiveCount = ActiveRequestCounts.Find(TurnId);
	if (!ActiveCount || --(*ActiveCount) > 0)
	{
		return;
	}
	ActiveRequestCounts.Remove(TurnId);
	CancelledTurns.Remove(TurnId);
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
}
