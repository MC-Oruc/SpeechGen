#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Phonemizer/KokoroPhonemizer.h"
#include "SpeechGen/SpeechGenSubsystem.h"
#include "SpeechGen/SpeechGenVoiceProfile.h"
#include "UObject/StrongObjectPtr.h"

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
		Phonemizer.Encode(ESpeechGenLanguage::English, TEXT("Kokoro speaks forty two words."),
			TokenIds, Phonemes, Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("Encoded input contains boundary and phoneme tokens"), TokenIds.Num() > 4);
	TestEqual(TEXT("Kokoro start boundary token is present"), TokenIds[0], static_cast<int64>(0));
	TestEqual(TEXT("Kokoro end boundary token is present"), TokenIds.Last(), static_cast<int64>(0));
	TestFalse(TEXT("Phoneme diagnostics are populated"), Phonemes.IsEmpty());

	struct FCase
	{
		ESpeechGenLanguage Language;
		const TCHAR* Text;
	};
	const FCase Cases[] = {
		{ESpeechGenLanguage::Spanish, TEXT("Hola, cómo estás?")},
		{ESpeechGenLanguage::Hindi, TEXT("नमस्ते दुनिया।")},
		{ESpeechGenLanguage::Italian, TEXT("Ciao, come stai?")},
		{ESpeechGenLanguage::BrazilianPortuguese, TEXT("Olá, como você está?")},
		{ESpeechGenLanguage::MandarinChinese, TEXT("你好，世界！")}
	};
	for (const FCase& TestCase : Cases)
	{
		TokenIds.Reset();
		Phonemes.Reset();
		Error.Reset();
		if (!TestTrue(TEXT("Official Kokoro language encodes"),
			Phonemizer.Encode(TestCase.Language, TestCase.Text, TokenIds, Phonemes, Error)))
		{
			AddError(Error);
			continue;
		}
		TestTrue(TEXT("Multilingual phonemes are populated"), !Phonemes.IsEmpty());
		TestTrue(TEXT("Multilingual tokens are bounded"), TokenIds.Num() >= 3 && TokenIds.Num() <= 512);
	}
	return true;
}

namespace
{
	class FSpeechGenInferenceCommand final : public IAutomationLatentCommand
	{
		struct FInferenceCase
		{
			ESpeechGenLanguage Language;
			FName VoiceId;
			const TCHAR* Text;
		};

	public:
		explicit FSpeechGenInferenceCommand(FAutomationTestBase& InTest)
			: Test(InTest)
			, StartedAt(FPlatformTime::Seconds())
		{
			if (!GEngine)
			{
				Test.AddError(TEXT("GEngine is unavailable."));
				bFinished = true;
				return;
			}
			GameInstance.Reset(NewObject<UGameInstance>(GEngine));
			GameInstance->InitializeStandalone(TEXT("SpeechGenInferenceWorld"));
			SpeechGen = GameInstance->GetSubsystem<USpeechGenSubsystem>();
			if (!SpeechGen)
			{
				Test.AddError(TEXT("SpeechGen subsystem was not created."));
				bFinished = true;
			}
		}

		virtual ~FSpeechGenInferenceCommand() override
		{
			Cleanup();
		}

		virtual bool Update() override
		{
			if (bFinished)
			{
				Cleanup();
				return true;
			}
			if (FPlatformTime::Seconds() - StartedAt > 120.0)
			{
				Test.AddError(TEXT("Kokoro inference timed out."));
				Cleanup();
				return true;
			}

			if (!bSubmitted)
			{
				const FSpeechGenRuntimeStatus Status = SpeechGen->GetRuntimeStatus();
				if (Status.State == ESpeechGenRuntimeState::Failed
					|| Status.State == ESpeechGenRuntimeState::Unavailable)
				{
					Test.AddError(FString::Printf(TEXT("SpeechGen runtime load failed: %s"), *Status.Error));
					Cleanup();
					return true;
				}
				if (Status.State != ESpeechGenRuntimeState::Ready)
				{
					return false;
				}

				if (!VoiceProfile.IsValid())
				{
					VoiceProfile.Reset(NewObject<USpeechGenVoiceProfile>(GameInstance.Get()));
					ReadyHandle = SpeechGen->OnSegmentReady.AddRaw(this, &FSpeechGenInferenceCommand::HandleReady);
					FailedHandle = SpeechGen->OnRequestFailed.AddRaw(this, &FSpeechGenInferenceCommand::HandleFailed);
				}
				const FInferenceCase& InferenceCase = Cases[CurrentCase];
				VoiceProfile->Language = InferenceCase.Language;
				VoiceProfile->NativeVoiceBlend[0].VoiceId = InferenceCase.VoiceId;

				FSpeechGenRequest Request;
				Request.TurnId = FGuid::NewGuid();
				Request.Text = InferenceCase.Text;
				Request.VoiceProfile = VoiceProfile.Get();
				ExpectedSegmentId = SpeechGen->SynthesizeAsync(Request);
				bSubmitted = true;
				return false;
			}

			if (!bReceivedResult)
			{
				return false;
			}
			Test.TestFalse(TEXT("Kokoro language inference returns PCM samples"), PcmSamples.IsEmpty());
			Test.TestTrue(TEXT("Kokoro language inference returns a positive duration"), DurationSeconds > 0.0f);
			PcmSamples.Reset();
			DurationSeconds = 0.0f;
			bReceivedResult = false;
			bSubmitted = false;
			++CurrentCase;
			if (CurrentCase == UE_ARRAY_COUNT(Cases))
			{
				Cleanup();
				return true;
			}
			return false;
		}

	private:
		void HandleReady(const FSpeechGenResult& Result)
		{
			if (Result.SegmentId != ExpectedSegmentId)
			{
				return;
			}
			PcmSamples = Result.PcmSamples;
			DurationSeconds = Result.DurationSeconds;
			bReceivedResult = true;
		}

		void HandleFailed(const FGuid TurnId, const FGuid SegmentId, const FString& Error)
		{
			(void)TurnId;
			if (SegmentId == ExpectedSegmentId)
			{
				Test.AddError(FString::Printf(TEXT("Kokoro inference failed: %s"), *Error));
				bFinished = true;
			}
		}

		void Cleanup()
		{
			if (SpeechGen)
			{
				SpeechGen->OnSegmentReady.Remove(ReadyHandle);
				SpeechGen->OnRequestFailed.Remove(FailedHandle);
				SpeechGen = nullptr;
			}
			VoiceProfile.Reset();
			if (GameInstance.IsValid())
			{
				UWorld* World = GameInstance->GetWorld();
				GameInstance->Shutdown();
				if (World)
				{
					World->DestroyWorld(false);
					GEngine->DestroyWorldContext(World);
				}
				GameInstance.Reset();
			}
			bFinished = true;
		}

		FAutomationTestBase& Test;
		const FInferenceCase Cases[6] = {
			{ESpeechGenLanguage::English, TEXT("af_heart"), TEXT("The voice is ready.")},
			{ESpeechGenLanguage::Spanish, TEXT("ef_dora"), TEXT("Hola, cómo estás?")},
			{ESpeechGenLanguage::Hindi, TEXT("hf_alpha"), TEXT("नमस्ते दुनिया।")},
			{ESpeechGenLanguage::Italian, TEXT("if_sara"), TEXT("Ciao, come stai?")},
			{ESpeechGenLanguage::BrazilianPortuguese, TEXT("pf_dora"), TEXT("Olá, como você está?")},
			{ESpeechGenLanguage::MandarinChinese, TEXT("zf_xiaobei"), TEXT("你好，世界！")}
		};
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<USpeechGenVoiceProfile> VoiceProfile;
		TObjectPtr<USpeechGenSubsystem> SpeechGen = nullptr;
		FDelegateHandle ReadyHandle;
		FDelegateHandle FailedHandle;
		FGuid ExpectedSegmentId;
		TArray<int16> PcmSamples;
		double StartedAt = 0.0;
		float DurationSeconds = 0.0f;
		int32 CurrentCase = 0;
		bool bSubmitted = false;
		bool bReceivedResult = false;
		bool bFinished = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpeechGenInferenceTest,
	"SpeechGen.Runtime.Inference.KokoroCpuMixed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeechGenInferenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	ADD_LATENT_AUTOMATION_COMMAND(FSpeechGenInferenceCommand(*this));
	return true;
}

#endif
