#include "SpeechGenEditor/SpeechGenEditorSettings.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SpeechGen/SpeechGenSubsystem.h"
#include "SpeechGenEditor/SpeechGenRuntimeInstallerSubsystem.h"

namespace
{
	template <typename CallbackType>
	void ForEachSpeechGenSubsystem(CallbackType&& Callback)
	{
		if (!GEngine)
		{
			return;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
			if (USpeechGenSubsystem* SpeechGen = GameInstance
				? GameInstance->GetSubsystem<USpeechGenSubsystem>() : nullptr)
			{
				Callback(*SpeechGen);
			}
		}
	}
}

FName USpeechGenEditorSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

void USpeechGenEditorSettings::InstallOrUpdate()
{
	if (GEditor)
	{
		GEditor->GetEditorSubsystem<USpeechGenRuntimeInstallerSubsystem>()->InstallOrUpdate(false);
	}
}

void USpeechGenEditorSettings::Reinstall()
{
	if (GEditor)
	{
		GEditor->GetEditorSubsystem<USpeechGenRuntimeInstallerSubsystem>()->InstallOrUpdate(true);
	}
}

void USpeechGenEditorSettings::StartRuntime()
{
	ForEachSpeechGenSubsystem([](USpeechGenSubsystem& SpeechGen) { SpeechGen.StartRuntime(); });
}

void USpeechGenEditorSettings::StopRuntime()
{
	ForEachSpeechGenSubsystem([](USpeechGenSubsystem& SpeechGen) { SpeechGen.StopRuntime(); });
}
