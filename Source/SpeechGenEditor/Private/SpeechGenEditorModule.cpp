#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SpeechGen/SpeechGenSubsystem.h"
#include "SpeechGenEditor/SpeechGenEditorSettings.h"
#include "SpeechGenVoiceWeightCustomization.h"
#include "SpeechGenVoiceProfileDetails.h"
#include "UObject/UnrealType.h"

namespace
{
	ESpeechGenEditorLifecycleMode GetEffectiveEditorLifecycle()
	{
		const USpeechGenEditorSettings* EditorSettings = GetDefault<USpeechGenEditorSettings>();
		return EditorSettings->bOverrideEditorLifecycle
			? EditorSettings->EditorLifecycle
			: GetDefault<USpeechGenProjectSettings>()->DevelopmentDefaults.EditorLifecycle;
	}
}

class FSpeechGenEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.RegisterCustomPropertyTypeLayout(TEXT("SpeechGenVoiceWeight"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FSpeechGenVoiceWeightCustomization::MakeInstance));
		PropertyEditor.RegisterCustomClassLayout(TEXT("SpeechGenVoiceProfile"),
			FOnGetDetailCustomizationInstance::CreateStatic(&FSpeechGenVoiceProfileDetails::MakeInstance));
		PropertyEditor.NotifyCustomizationModuleChanged();
		WorldInitializedHandle = FWorldDelegates::OnPostWorldInitialization.AddRaw(
			this, &FSpeechGenEditorModule::HandleWorldInitialized);
		PostPIEStartedHandle = FEditorDelegates::PostPIEStarted.AddRaw(
			this, &FSpeechGenEditorModule::HandlePostPIEStarted);
		SettingsChangedHandle = GetMutableDefault<USpeechGenEditorSettings>()->OnSettingChanged().AddRaw(
			this, &FSpeechGenEditorModule::HandleSettingsChanged);
		ProjectSettingsChangedHandle = GetMutableDefault<USpeechGenProjectSettings>()->OnSettingChanged().AddRaw(
			this, &FSpeechGenEditorModule::HandleProjectSettingsChanged);
	}

	virtual void ShutdownModule() override
	{
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyEditor.UnregisterCustomPropertyTypeLayout(TEXT("SpeechGenVoiceWeight"));
			PropertyEditor.UnregisterCustomClassLayout(TEXT("SpeechGenVoiceProfile"));
			PropertyEditor.NotifyCustomizationModuleChanged();
		}
		FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitializedHandle);
		FEditorDelegates::PostPIEStarted.Remove(PostPIEStartedHandle);
		if (UObjectInitialized())
		{
			GetMutableDefault<USpeechGenEditorSettings>()->OnSettingChanged().Remove(SettingsChangedHandle);
			GetMutableDefault<USpeechGenProjectSettings>()->OnSettingChanged().Remove(ProjectSettingsChangedHandle);
		}
	}

private:
	static bool ShouldRunForWorld(const UWorld& World, const ESpeechGenEditorLifecycleMode Mode)
	{
		return Mode == ESpeechGenEditorLifecycleMode::EditorSession
			|| (Mode == ESpeechGenEditorLifecycleMode::PIESession && World.WorldType == EWorldType::PIE);
	}

	static void ReconcileWorld(UWorld& World, const ESpeechGenEditorLifecycleMode Mode)
	{
		UGameInstance* GameInstance = World.GetGameInstance();
		USpeechGenSubsystem* SpeechGen = GameInstance
			? GameInstance->GetSubsystem<USpeechGenSubsystem>() : nullptr;
		if (!SpeechGen)
		{
			return;
		}
		if (ShouldRunForWorld(World, Mode))
		{
			SpeechGen->StartRuntime();
		}
		else
		{
			SpeechGen->StopRuntime();
		}
	}

	void HandleWorldInitialized(UWorld* World, const UWorld::InitializationValues)
	{
		if (World && GetEffectiveEditorLifecycle()
			== ESpeechGenEditorLifecycleMode::EditorSession)
		{
			ReconcileWorld(*World, ESpeechGenEditorLifecycleMode::EditorSession);
		}
	}

	void HandlePostPIEStarted(bool)
	{
		if (!GEngine || GetEffectiveEditorLifecycle()
			!= ESpeechGenEditorLifecycleMode::PIESession)
		{
			return;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World(); World && World->WorldType == EWorldType::PIE)
			{
				ReconcileWorld(*World, ESpeechGenEditorLifecycleMode::PIESession);
			}
		}
	}

	void HandleSettingsChanged(UObject*, FPropertyChangedEvent& Event)
	{
		const FName PropertyName = Event.GetPropertyName();
		if ((PropertyName != GET_MEMBER_NAME_CHECKED(USpeechGenEditorSettings, bOverrideEditorLifecycle)
			&& PropertyName != GET_MEMBER_NAME_CHECKED(USpeechGenEditorSettings, EditorLifecycle)) || !GEngine)
		{
			return;
		}
		const ESpeechGenEditorLifecycleMode Mode = GetEffectiveEditorLifecycle();
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World())
			{
				ReconcileWorld(*World, Mode);
			}
		}
	}

	void HandleProjectSettingsChanged(UObject*, FPropertyChangedEvent& Event)
	{
		if (Event.GetPropertyName() != GET_MEMBER_NAME_CHECKED(USpeechGenProjectSettings, DevelopmentDefaults)
			|| !GEngine)
		{
			return;
		}
		const ESpeechGenEditorLifecycleMode Mode = GetEffectiveEditorLifecycle();
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World())
			{
				ReconcileWorld(*World, Mode);
			}
		}
	}

	FDelegateHandle WorldInitializedHandle;
	FDelegateHandle PostPIEStartedHandle;
	FDelegateHandle SettingsChangedHandle;
	FDelegateHandle ProjectSettingsChangedHandle;
};

IMPLEMENT_MODULE(FSpeechGenEditorModule, SpeechGenEditor)
