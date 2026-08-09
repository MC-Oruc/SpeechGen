#include "SpeechGenEditor/SpeechGenEditorSettings.h"

#include "Editor.h"
#include "SpeechGenEditor/SpeechGenRuntimeInstallerSubsystem.h"

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
