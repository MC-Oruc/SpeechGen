#include "Misc/AutomationTest.h"

#include "SpeechGen/SpeechGenVoiceProfile.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpeechGenVoiceProfileVariantTest,
	"SpeechGen.Runtime.VoiceProfile.Variants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeechGenVoiceProfileVariantTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	USpeechGenVoiceProfile* Profile = NewObject<USpeechGenVoiceProfile>();

	FSpeechGenVoiceVariant& British = Profile->NativeVoiceVariants.AddDefaulted_GetRef();
	British.VariantId = TEXT("british_english");
	British.VoiceBlend.Add({TEXT("bf_emma"), 1.0f});

	FSpeechGenStyleDefinition& Calm = Profile->DirectedStyles.AddDefaulted_GetRef();
	Calm.StyleId = TEXT("calm");
	Calm.VoiceBlend.Add({TEXT("af_bella"), 1.0f});

	TArray<FSpeechGenVoiceWeight> Blend;
	float Speed = 0.0f;
	float Gain = 0.0f;
	float PauseScale = 0.0f;
	FString Error;
	TestTrue(TEXT("Native variant resolves"), Profile->Resolve(TEXT("british_english"),
		ESpeechGenStyleMode::Native, NAME_None, Blend, Speed, Gain, PauseScale, Error));
	TestEqual(TEXT("Native variant supplies its authored voice"), Blend[0].VoiceId, FName(TEXT("bf_emma")));

	Blend.Reset();
	TestTrue(TEXT("Directed style inherits the profile variant"), Profile->Resolve(TEXT("british_english"),
		ESpeechGenStyleMode::Directed, TEXT("calm"), Blend, Speed, Gain, PauseScale, Error));
	TestEqual(TEXT("Inherited profile variant remains authoritative"), Blend[0].VoiceId, FName(TEXT("bf_emma")));

	Blend.Reset();
	TestFalse(TEXT("Unknown variant is rejected"), Profile->Resolve(TEXT("unknown"),
		ESpeechGenStyleMode::Native, NAME_None, Blend, Speed, Gain, PauseScale, Error));
	return true;
}

#endif
