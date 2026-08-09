#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SpeechGenEditorSettings.generated.h"

UENUM()
enum class ESpeechGenInstallState : uint8
{
	NotInstalled,
	Downloading,
	Installed,
	Failed
};

UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig, meta = (DisplayName = "SpeechGen"))
class SPEECHGENEDITOR_API USpeechGenEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bAutomaticallyInstallRuntime = true;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Runtime")
	ESpeechGenInstallState InstallState = ESpeechGenInstallState::NotInstalled;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Runtime", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DownloadProgress = 0.0f;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Runtime")
	FString InstalledVersion;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Runtime")
	FString RuntimePath;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Diagnostics")
	FString LastError;

	UFUNCTION(CallInEditor, Category = "Runtime")
	void InstallOrUpdate();

	UFUNCTION(CallInEditor, Category = "Runtime")
	void Reinstall();

	virtual FName GetCategoryName() const override;
};
