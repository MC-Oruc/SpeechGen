#pragma once

#include "CoreMinimal.h"
#include "SpeechGen/SpeechGenTypes.h"

struct SPEECHGEN_API FSpeechGenAudioCacheEntry
{
	FString Key;
	FString Text;
	FString VoiceDescription;
	int32 SampleRate = 0;
	int32 SampleCount = 0;
	bool bImported = false;
	bool bValid = false;
};

class SPEECHGEN_API FSpeechGenAudioCache
{
public:
	static FString GetGeneratedDirectory();
	static FString MakeKey(const FSpeechGenRequest& Request);
	static bool TryLoad(const FString& Directory, const FSpeechGenRequest& Request,
		FSpeechGenResult& OutResult, FString& OutError);
	static bool SaveGenerated(const FSpeechGenRequest& Request,
		const FSpeechGenResult& Result, FString& OutError);
	static bool ImportWave(const FString& Directory, const FSpeechGenRequest& Request,
		const FString& WavePath, FString& OutError);
	static void List(const FString& Directory, TArray<FSpeechGenAudioCacheEntry>& OutEntries);
	static bool MoveToBackup(const FString& Directory, const FString& Key, FString& OutError);
};
