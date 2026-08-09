#include "SpeechGenEditor/SpeechGenRuntimeInstallerSubsystem.h"

#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SpeechGen/SpeechGenSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpeechGenInstaller, Log, All);

void USpeechGenRuntimeInstallerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RuntimeDirectory = USpeechGenSubsystem::ResolveRuntimeDirectory();
	FString Error;
	if (!LoadManifest(Error))
	{
		SetState(ESpeechGenInstallState::Failed, Error);
		return;
	}

	if (ValidateInstalledFiles(false, Error))
	{
		SetState(ESpeechGenInstallState::Installed);
		return;
	}

	SetState(ESpeechGenInstallState::NotInstalled, Error);
	if (GetDefault<USpeechGenEditorSettings>()->bAutomaticallyInstallRuntime)
	{
		InstallOrUpdate(false);
	}
}

void USpeechGenRuntimeInstallerSubsystem::Deinitialize()
{
	if (ActiveRequest)
	{
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
	Super::Deinitialize();
}

bool USpeechGenRuntimeInstallerSubsystem::LoadManifest(FString& OutError)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SpeechGen"));
	if (!Plugin)
	{
		OutError = TEXT("SpeechGen plugin descriptor is unavailable.");
		return false;
	}

	const FString ManifestPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/RuntimeManifest.json"));
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *ManifestPath))
	{
		OutError = FString::Printf(TEXT("SpeechGen runtime manifest is missing: %s"), *ManifestPath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid())
	{
		OutError = TEXT("SpeechGen runtime manifest is invalid JSON.");
		return false;
	}

	Version = Root->GetStringField(TEXT("version"));
	if (Version != USpeechGenSubsystem::RuntimeVersion)
	{
		OutError = FString::Printf(TEXT("Unsupported SpeechGen runtime manifest version: %s"), *Version);
		return false;
	}

	Files.Reset();
	TotalBytes = 0;
	for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("files")))
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (!Object)
		{
			continue;
		}
		FManifestFile& File = Files.AddDefaulted_GetRef();
		File.Path = Object->GetStringField(TEXT("path"));
		File.Url = Object->GetStringField(TEXT("url"));
		File.Sha256 = Object->GetStringField(TEXT("sha256")).ToLower();
		File.Size = static_cast<int64>(Object->GetNumberField(TEXT("size")));
		TotalBytes += File.Size;
	}

	if (Files.IsEmpty() || !Files.ContainsByPredicate([](const FManifestFile& File)
	{
		return File.Path == TEXT("onnx/model_quantized.onnx");
	}))
	{
		OutError = TEXT("SpeechGen runtime manifest has no Kokoro Q8 model.");
		return false;
	}
	return true;
}

bool USpeechGenRuntimeInstallerSubsystem::ValidateInstalledFiles(const bool bVerifyHashes, FString& OutError) const
{
	for (const FManifestFile& File : Files)
	{
		const FString FullPath = FPaths::Combine(RuntimeDirectory, File.Path);
		const int64 ActualSize = IFileManager::Get().FileSize(*FullPath);
		if (ActualSize != File.Size)
		{
			OutError = FString::Printf(TEXT("Missing or incomplete SpeechGen file: %s"), *File.Path);
			return false;
		}
		if (bVerifyHashes && HashFile(FullPath) != File.Sha256)
		{
			OutError = FString::Printf(TEXT("SpeechGen integrity check failed: %s"), *File.Path);
			return false;
		}
	}
	return true;
}

void USpeechGenRuntimeInstallerSubsystem::InstallOrUpdate(const bool bForce)
{
	if (State == ESpeechGenInstallState::Downloading || State == ESpeechGenInstallState::Checking)
	{
		return;
	}

	FString Error;
	if (!bForce && ValidateInstalledFiles(true, Error))
	{
		SetState(ESpeechGenInstallState::Installed);
		return;
	}

	CurrentFileIndex = INDEX_NONE;
	CompletedBytes = 0;
	StagingDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SpeechGen/Install"),
		FString::Printf(TEXT("Staging-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)), Version);
	IFileManager::Get().MakeDirectory(*StagingDirectory, true);
	SetState(ESpeechGenInstallState::Downloading);
	DownloadNextFile();
}

void USpeechGenRuntimeInstallerSubsystem::VerifyInstallation()
{
	if (State == ESpeechGenInstallState::Downloading)
	{
		return;
	}
	SetState(ESpeechGenInstallState::Checking);
	FString Error;
	SetState(ValidateInstalledFiles(true, Error) ? ESpeechGenInstallState::Installed : ESpeechGenInstallState::Failed,
		Error);
}

void USpeechGenRuntimeInstallerSubsystem::DownloadNextFile()
{
	++CurrentFileIndex;
	if (!Files.IsValidIndex(CurrentFileIndex))
	{
		PromoteStagingRuntime();
		return;
	}

	const FManifestFile& File = Files[CurrentFileIndex];
	ActiveRequest = FHttpModule::Get().CreateRequest();
	ActiveRequest->SetVerb(TEXT("GET"));
	ActiveRequest->SetURL(File.Url);
	ActiveRequest->SetHeader(TEXT("User-Agent"), TEXT("SpeechGen-Unreal/1.0"));
	ActiveRequest->OnRequestProgress64().BindUObject(this, &ThisClass::HandleDownloadProgress);
	ActiveRequest->OnProcessRequestComplete().BindUObject(this, &ThisClass::HandleDownloadComplete);
	if (!ActiveRequest->ProcessRequest())
	{
		SetState(ESpeechGenInstallState::Failed,
			FString::Printf(TEXT("Unable to start SpeechGen download: %s"), *File.Path));
	}
}

void USpeechGenRuntimeInstallerSubsystem::HandleDownloadProgress(FHttpRequestPtr Request,
	const uint64 BytesSent, const uint64 BytesReceived)
{
	if (USpeechGenEditorSettings* Settings = GetMutableDefault<USpeechGenEditorSettings>())
	{
		Settings->DownloadProgress = TotalBytes > 0
			? static_cast<float>(CompletedBytes + static_cast<int64>(BytesReceived)) / static_cast<float>(TotalBytes)
			: 0.0f;
	}
}

void USpeechGenRuntimeInstallerSubsystem::HandleDownloadComplete(FHttpRequestPtr Request,
	FHttpResponsePtr Response, const bool bSucceeded)
{
	ActiveRequest.Reset();
	const FManifestFile& File = Files[CurrentFileIndex];
	if (!bSucceeded || !Response.IsValid() || !EHttpResponseCodes::IsOk(Response->GetResponseCode()))
	{
		SetState(ESpeechGenInstallState::Failed,
			FString::Printf(TEXT("SpeechGen download failed: %s"), *File.Path));
		return;
	}

	const TArray<uint8>& Content = Response->GetContent();
	if (Content.Num() != File.Size || HashBytes(Content) != File.Sha256)
	{
		SetState(ESpeechGenInstallState::Failed,
			FString::Printf(TEXT("SpeechGen download integrity check failed: %s"), *File.Path));
		return;
	}

	const FString Destination = FPaths::Combine(StagingDirectory, File.Path);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true);
	if (!FFileHelper::SaveArrayToFile(Content, *Destination))
	{
		SetState(ESpeechGenInstallState::Failed,
			FString::Printf(TEXT("Unable to write SpeechGen file: %s"), *Destination));
		return;
	}

	CompletedBytes += Content.Num();
	DownloadNextFile();
}

void USpeechGenRuntimeInstallerSubsystem::PromoteStagingRuntime()
{
	FString HashManifest;
	for (const FManifestFile& File : Files)
	{
		HashManifest += File.Sha256 + TEXT("  ") + File.Path + LINE_TERMINATOR;
	}
	if (!FFileHelper::SaveStringToFile(HashManifest, *FPaths::Combine(StagingDirectory, TEXT("manifest.sha256"))))
	{
		SetState(ESpeechGenInstallState::Failed, TEXT("Unable to write SpeechGen hash manifest."));
		return;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(RuntimeDirectory), true);
	if (IFileManager::Get().DirectoryExists(*RuntimeDirectory))
	{
		const FString BackupDirectory = RuntimeDirectory + TEXT(".bak-")
			+ FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
		if (!IFileManager::Get().Move(*BackupDirectory, *RuntimeDirectory, true, true, false, true))
		{
			SetState(ESpeechGenInstallState::Failed, TEXT("Unable to preserve the previous SpeechGen runtime."));
			return;
		}
	}

	if (!IFileManager::Get().Move(*RuntimeDirectory, *StagingDirectory, true, true, false, true))
	{
		SetState(ESpeechGenInstallState::Failed, TEXT("Unable to atomically activate the SpeechGen runtime."));
		return;
	}
	SetState(ESpeechGenInstallState::Installed);
}

void USpeechGenRuntimeInstallerSubsystem::SetState(const ESpeechGenInstallState NewState, const FString& Error)
{
	State = NewState;
	LastError = Error;
	RefreshSettings();
	if (NewState == ESpeechGenInstallState::Failed)
	{
		UE_LOG(LogSpeechGenInstaller, Error, TEXT("%s"), *Error);
	}
}

void USpeechGenRuntimeInstallerSubsystem::RefreshSettings() const
{
	USpeechGenEditorSettings* Settings = GetMutableDefault<USpeechGenEditorSettings>();
	Settings->InstallState = State;
	Settings->InstalledVersion = State == ESpeechGenInstallState::Installed ? Version : FString();
	Settings->RuntimePath = RuntimeDirectory;
	Settings->LastError = LastError;
	Settings->DownloadProgress = State == ESpeechGenInstallState::Installed ? 1.0f : Settings->DownloadProgress;
}

FString USpeechGenRuntimeInstallerSubsystem::HashBytes(const TConstArrayView64<uint8> Bytes)
{
	FSHA256Signature Signature{};
	if (!FPlatformMisc::GetSHA256Signature(Bytes.GetData(), static_cast<uint32>(Bytes.Num()), Signature))
	{
		return FString();
	}
	return Signature.ToString().ToLower();
}

FString USpeechGenRuntimeInstallerSubsystem::HashFile(const FString& Filename)
{
	TArray64<uint8> Bytes;
	return FFileHelper::LoadFileToArray(Bytes, *Filename) ? HashBytes(Bytes) : FString();
}
