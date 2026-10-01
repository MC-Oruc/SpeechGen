#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "SpeechGen/SpeechGenSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechGenVoicePreviewOwnershipTest,
	"SpeechGen.Editor.VoicePreview.Ownership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeechGenVoicePreviewOwnershipTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UGameInstance* PreviewGameInstance = NewObject<UGameInstance>(GetTransientPackage());
	USpeechGenSubsystem* PreviewRuntime = USpeechGenSubsystem::CreateEditorPreviewRuntime(*PreviewGameInstance);
	TestNotNull(TEXT("Preview runtime is created"), PreviewRuntime);
	TestTrue(TEXT("Preview runtime has a valid GameInstance outer"),
		PreviewRuntime && PreviewRuntime->GetOuter() == PreviewGameInstance);
	return true;
}

#endif
