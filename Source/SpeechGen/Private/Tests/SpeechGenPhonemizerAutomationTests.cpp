#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Phonemizer/KokoroPhonemizer.h"
#include "SpeechGen/SpeechGenSubsystem.h"
#include "SpeechGen/SpeechGenAudioCache.h"
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
		{ESpeechGenLanguage::MandarinChinese, TEXT("你好，世界！")},
		{ESpeechGenLanguage::TurkishExperimental, TEXT("Merhaba, bugün nasılsın?")},
		{ESpeechGenLanguage::AzerbaijaniExperimental, TEXT("Salam, necəsən?")},
		{ESpeechGenLanguage::GermanExperimental, TEXT("Hallo, wie geht es dir?")},
		{ESpeechGenLanguage::DutchExperimental, TEXT("Hallo, hoe gaat het?")}
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
				return;
			}
			SpeechGen->StartRuntime();
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
				VoiceProfile->NativeVoiceBlend[0].VoiceId = InferenceCase.VoiceId;

				FSpeechGenRequest Request;
				Request.TurnId = FGuid::NewGuid();
				Request.Text = InferenceCase.Text;
				Request.Language = InferenceCase.Language;
				Request.VoiceBlend = VoiceProfile->NativeVoiceBlend;
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
			Test.TestFalse(TEXT("Kokoro language inference preserves synthesized phonemes"), Phonemes.IsEmpty());
			PcmSamples.Reset();
			Phonemes.Reset();
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
			Phonemes = Result.Phonemes;
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
		const FInferenceCase Cases[10] = {
			{ESpeechGenLanguage::English, TEXT("af_heart"), TEXT("The voice is ready.")},
			{ESpeechGenLanguage::Spanish, TEXT("ef_dora"), TEXT("Hola, cómo estás?")},
			{ESpeechGenLanguage::Hindi, TEXT("hf_alpha"), TEXT("नमस्ते दुनिया।")},
			{ESpeechGenLanguage::Italian, TEXT("if_sara"), TEXT("Ciao, come stai?")},
			{ESpeechGenLanguage::BrazilianPortuguese, TEXT("pf_dora"), TEXT("Olá, como você está?")},
			{ESpeechGenLanguage::MandarinChinese, TEXT("zf_xiaobei"), TEXT("你好，世界！")},
			{ESpeechGenLanguage::TurkishExperimental, TEXT("if_sara"), TEXT("Merhaba, bugün nasılsın?")},
			{ESpeechGenLanguage::AzerbaijaniExperimental, TEXT("if_sara"), TEXT("Salam, necəsən?")},
			{ESpeechGenLanguage::GermanExperimental, TEXT("bf_emma"), TEXT("Hallo, wie geht es dir?")},
			{ESpeechGenLanguage::DutchExperimental, TEXT("bf_emma"), TEXT("Hallo, hoe gaat het?")}
		};
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<USpeechGenVoiceProfile> VoiceProfile;
		TObjectPtr<USpeechGenSubsystem> SpeechGen = nullptr;
		FDelegateHandle ReadyHandle;
		FDelegateHandle FailedHandle;
		FGuid ExpectedSegmentId;
		TArray<int16> PcmSamples;
		FString Phonemes;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechGenAudioCacheIdentityTest,
	"SpeechGen.AudioCache.RequestIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeechGenAudioCacheIdentityTest::RunTest(const FString& Parameters)
{
	FSpeechGenRequest Request;
	Request.Text = TEXT("The courier used the side gate.");
	Request.Language = ESpeechGenLanguage::English;
	FSpeechGenVoiceWeight& Voice = Request.VoiceBlend.AddDefaulted_GetRef();
	Voice.VoiceId = TEXT("am_adam");
	Voice.Weight = 1.0f;
	const FString Original = FSpeechGenAudioCache::MakeKey(Request);
	TestEqual(TEXT("Identical request uses the same cache entry"), FSpeechGenAudioCache::MakeKey(Request), Original);
	Request.Text += TEXT(" ");
	TestNotEqual(TEXT("Text matches exactly"), FSpeechGenAudioCache::MakeKey(Request), Original);
	Request.Text.RemoveAt(Request.Text.Len() - 1);
	Request.Language = ESpeechGenLanguage::GermanExperimental;
	TestNotEqual(TEXT("Language is part of the identity"), FSpeechGenAudioCache::MakeKey(Request), Original);
	Request.Language = ESpeechGenLanguage::English;
	Request.VoiceBlend[0].VoiceId = TEXT("am_michael");
	TestNotEqual(TEXT("Voice blend is part of the identity"), FSpeechGenAudioCache::MakeKey(Request), Original);
	Request.VoiceBlend[0].VoiceId = TEXT("am_adam");
	Request.Speed = 1.1f;
	TestNotEqual(TEXT("Synthesis settings are part of the identity"), FSpeechGenAudioCache::MakeKey(Request), Original);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechGenAudioCachePersistenceTest,
	"SpeechGen.AudioCache.Persistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeechGenAudioCachePersistenceTest::RunTest(const FString& Parameters)
{
	FSpeechGenRequest Request;
	Request.Text = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Request.Language = ESpeechGenLanguage::English;
	FSpeechGenVoiceWeight& Voice = Request.VoiceBlend.AddDefaulted_GetRef();
	Voice.VoiceId = TEXT("am_adam");
	Voice.Weight = 1.0f;
	FSpeechGenResult Generated;
	Generated.SampleRate = 24000;
	Generated.PcmSamples = {0, 100, -100, 0};
	FString Error;
	if (!TestTrue(TEXT("Generated clip is persisted"),
		FSpeechGenAudioCache::SaveGenerated(Request, Generated, Error)))
	{
		AddError(Error);
		return false;
	}
	FSpeechGenResult Loaded;
	TestTrue(TEXT("Exact clip loads"), FSpeechGenAudioCache::TryLoad(
		FSpeechGenAudioCache::GetGeneratedDirectory(), Request, Loaded, Error));
	TestTrue(TEXT("PCM survives roundtrip"), Loaded.PcmSamples == Generated.PcmSamples);
	Request.Text += TEXT("changed");
	TestFalse(TEXT("Different text misses"), FSpeechGenAudioCache::TryLoad(
		FSpeechGenAudioCache::GetGeneratedDirectory(), Request, Loaded, Error));
	Request.Text.LeftChopInline(7);
	TestTrue(TEXT("Clip moves to recoverable backup"), FSpeechGenAudioCache::MoveToBackup(
		FSpeechGenAudioCache::GetGeneratedDirectory(), FSpeechGenAudioCache::MakeKey(Request), Error));
	TestFalse(TEXT("Backed-up clip is no longer active"), FSpeechGenAudioCache::TryLoad(
		FSpeechGenAudioCache::GetGeneratedDirectory(), Request, Loaded, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeechGenAudioCacheWaveImportTest,
	"SpeechGen.AudioCache.WaveImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeechGenAudioCacheWaveImportTest::RunTest(const FString& Parameters)
{
	const FString TestDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/SpeechGen"));
	const FString WavePath = FPaths::Combine(TestDirectory,
		FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".wav"));
	const FString CacheDirectory = FPaths::Combine(TestDirectory, TEXT("Cache"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	const uint8 WaveBytes[] = {
		'R','I','F','F', 44,0,0,0, 'W','A','V','E', 'f','m','t',' ',
		16,0,0,0, 1,0, 1,0, 0xC0,0x5D,0,0, 0x80,0xBB,0,0,
		2,0, 16,0, 'd','a','t','a', 8,0,0,0, 0,0, 100,0, 156,255, 0,0
	};
	TArray<uint8> Bytes;
	Bytes.Append(WaveBytes, UE_ARRAY_COUNT(WaveBytes));
	if (!TestTrue(TEXT("Test WAV saved"), FFileHelper::SaveArrayToFile(Bytes, *WavePath))) return false;
	FSpeechGenRequest Request;
	Request.Text = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Request.Language = ESpeechGenLanguage::English;
	FSpeechGenVoiceWeight& Voice = Request.VoiceBlend.AddDefaulted_GetRef();
	Voice.VoiceId = TEXT("af_nova");
	Voice.Weight = 1.0f;
	FString Error;
	TestTrue(TEXT("PCM WAV imports"), FSpeechGenAudioCache::ImportWave(CacheDirectory, Request, WavePath, Error));
	FSpeechGenResult Loaded;
	TestTrue(TEXT("Imported clip is selected by exact request"),
		FSpeechGenAudioCache::TryLoad(CacheDirectory, Request, Loaded, Error));
	TestEqual(TEXT("Imported PCM sample count"), Loaded.PcmSamples.Num(), 4);
	TestTrue(TEXT("Imported clip moved to backup"),
		FSpeechGenAudioCache::MoveToBackup(CacheDirectory, FSpeechGenAudioCache::MakeKey(Request), Error));
	TestTrue(TEXT("Input WAV moved to backup"),
		IFileManager::Get().Move(*(WavePath + TEXT(".bak")), *WavePath, false));
	return true;
}

#endif
