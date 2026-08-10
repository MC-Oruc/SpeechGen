#pragma once

#include "CoreMinimal.h"
#include "Misc/QueuedThreadPool.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SpeechGen/SpeechGenTypes.h"
#include <atomic>
#include "SpeechGenSubsystem.generated.h"

class FKokoroPhonemizer;
class UNNEModelData;
namespace UE::NNE
{
	class IModelCPU;
	class IModelInstanceCPU;
}

UCLASS()
class SPEECHGEN_API USpeechGenSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 SampleRate = 24000;
	static constexpr const TCHAR* RuntimeVersion = TEXT("v1.0-cpu-mixed.1");

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "SpeechGen")
	FSpeechGenRuntimeStatus GetRuntimeStatus() const { return RuntimeStatus; }

	FGuid SynthesizeAsync(const FSpeechGenRequest& Request);
	void CancelTurn(FGuid TurnId);
	void CancelAll();

	FSpeechGenSegmentReady OnSegmentReady;
	FSpeechGenRequestFailed OnRequestFailed;
	FSpeechGenRuntimeStateChanged OnRuntimeStateChanged;

	static FString ResolveRuntimeDirectory();

private:
	struct FResolvedRequest;

	void BeginRuntimeLoad();
	void CompleteRuntimeLoad(TSharedPtr<UE::NNE::IModelCPU> InModel,
		TSharedPtr<UE::NNE::IModelInstanceCPU> InInstance, TSharedPtr<FKokoroPhonemizer> InPhonemizer,
		const FString& Error);
	bool ResolveRequest(const FSpeechGenRequest& Request, FResolvedRequest& OutRequest, FString& OutError) const;
	void ExecuteRequest(FResolvedRequest Request);
	void MarkRequestFinished(FGuid TurnId, const TSharedPtr<std::atomic_bool, ESPMode::ThreadSafe>& CancellationFlag);
	void SetRuntimeState(ESpeechGenRuntimeState State, const FString& Error = FString());
	static bool IsRequestCancelled(const FResolvedRequest& Request);

	UPROPERTY(Transient)
	TObjectPtr<UNNEModelData> ModelData;

	UPROPERTY(VisibleAnywhere, Category = "SpeechGen")
	FSpeechGenRuntimeStatus RuntimeStatus;

	TSharedPtr<UE::NNE::IModelCPU> Model;
	TSharedPtr<UE::NNE::IModelInstanceCPU> ModelInstance;
	TSharedPtr<FKokoroPhonemizer> Phonemizer;
	TUniquePtr<FQueuedThreadPool> WorkerPool;
	mutable FCriticalSection CancellationMutex;
	TMap<FGuid, TArray<TSharedPtr<std::atomic_bool, ESPMode::ThreadSafe>>> ActiveCancellationFlags;
	std::atomic<int32> PendingRequestCount{0};
	bool bShuttingDown = false;
};
