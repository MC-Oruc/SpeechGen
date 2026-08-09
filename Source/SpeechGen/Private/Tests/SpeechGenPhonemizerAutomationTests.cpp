#include "Misc/AutomationTest.h"

#include "Phonemizer/KokoroPhonemizer.h"
#include "SpeechGen/SpeechGenSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpeechGenPhonemizerTest,
	"SpeechGen.Runtime.Phonemizer.English",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeechGenPhonemizerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FKokoroPhonemizer Phonemizer;
	FString Error;
	if (!TestTrue(TEXT("Pinned CMUdict and Kokoro vocabulary load"),
		Phonemizer.Initialize(USpeechGenSubsystem::ResolveRuntimeDirectory(), Error)))
	{
		AddError(Error);
		return false;
	}

	TArray<int64> TokenIds;
	FString Phonemes;
	if (!TestTrue(TEXT("English text and numbers encode"),
		Phonemizer.Encode(TEXT("Kokoro speaks forty two words."), TokenIds, Phonemes, Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("Encoded input contains boundary and phoneme tokens"), TokenIds.Num() > 4);
	TestEqual(TEXT("Kokoro start boundary token is present"), TokenIds[0], static_cast<int64>(0));
	TestEqual(TEXT("Kokoro end boundary token is present"), TokenIds.Last(), static_cast<int64>(0));
	TestFalse(TEXT("Phoneme diagnostics are populated"), Phonemes.IsEmpty());
	return true;
}

#endif

