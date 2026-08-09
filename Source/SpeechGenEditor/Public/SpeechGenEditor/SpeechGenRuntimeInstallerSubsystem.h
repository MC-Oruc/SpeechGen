#pragma once

#include "CoreMinimal.h"
#include "HttpFwd.h"
#include "EditorSubsystem.h"
#include "SpeechGenEditor/SpeechGenEditorSettings.h"
#include "SpeechGenRuntimeInstallerSubsystem.generated.h"

UCLASS()
class SPEECHGENEDITOR_API USpeechGenRuntimeInstallerSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void InstallOrUpdate(bool bForce);
	void VerifyInstallation();

private:
	struct FManifestFile
	{
		FString Path;
		FString Url;
		FString Sha256;
		int64 Size = 0;
	};

	bool LoadManifest(FString& OutError);
	bool ValidateInstalledFiles(bool bVerifyHashes, FString& OutError) const;
	void DownloadNextFile();
	void HandleDownloadProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived);
	void HandleDownloadComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);
	void PromoteStagingRuntime();
	void SetState(ESpeechGenInstallState State, const FString& Error = FString());
	void RefreshSettings() const;
	static FString HashBytes(TConstArrayView64<uint8> Bytes);
	static FString HashFile(const FString& Filename);

	FString Version;
	TArray<FManifestFile> Files;
	FString RuntimeDirectory;
	FString StagingDirectory;
	int32 CurrentFileIndex = INDEX_NONE;
	int64 CompletedBytes = 0;
	int64 TotalBytes = 0;
	FHttpRequestPtr ActiveRequest;
	ESpeechGenInstallState State = ESpeechGenInstallState::NotInstalled;
	FString LastError;
};
