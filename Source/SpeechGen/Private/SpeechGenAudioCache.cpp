#include "SpeechGen/SpeechGenAudioCache.h"
#include "SpeechGen/SpeechGenSubsystem.h"

#include "Audio.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/SecureHash.h"
#include "Serialization/BufferArchive.h"

namespace
{
	constexpr uint32 CacheMagic = 0x53474341;
	constexpr int32 CacheVersion = 1;
	constexpr int64 MaximumClipBytes = 64 * 1024 * 1024;
	constexpr TCHAR CacheExtension[] = TEXT(".socvoice");

	struct FStoredClip
	{
		FString Key;
		FString Text;
		FString VoiceDescription;
		int32 SampleRate = 0;
		bool bImported = false;
		int32 SampleCount = 0;
		TArray<int16> Samples;
	};

	FString GetClipPath(const FString& Directory, const FString& Key)
	{
		return FPaths::Combine(Directory, Key + CacheExtension);
	}

	bool IsSafeKey(const FString& Key)
	{
		if (Key.Len() != 40)
		{
			return false;
		}
		for (const TCHAR Character : Key)
		{
			if (!((Character >= TEXT('0') && Character <= TEXT('9'))
				|| (Character >= TEXT('a') && Character <= TEXT('f'))))
			{
				return false;
			}
		}
		return true;
	}

	FString DescribeVoice(const FSpeechGenRequest& Request)
	{
		FString Description;
		for (const FSpeechGenVoiceWeight& Voice : Request.VoiceBlend)
		{
			Description += FString::Printf(TEXT("%s:%.9g;"), *Voice.VoiceId.ToString(), Voice.Weight);
		}
		return Description;
	}

	bool ReadClip(const FString& Path, FStoredClip& OutClip, const bool bReadSamples = true)
	{
		const int64 FileSize = IFileManager::Get().FileSize(*Path);
		if (FileSize <= 0 || FileSize > MaximumClipBytes)
		{
			return false;
		}
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path));
		if (!Reader)
		{
			return false;
		}
		uint32 Magic = 0;
		int32 Version = 0;
		*Reader << Magic;
		*Reader << Version;
		if (Magic != CacheMagic || Version != CacheVersion)
		{
			return false;
		}
		*Reader << OutClip.Key;
		*Reader << OutClip.Text;
		*Reader << OutClip.VoiceDescription;
		*Reader << OutClip.SampleRate;
		*Reader << OutClip.bImported;
		// TArray serialization reads the count before allocating; reject oversized payloads first.
		int32 SampleCount = 0;
		*Reader << SampleCount;
		if (Reader->IsError() || SampleCount <= 0
			|| static_cast<int64>(SampleCount) * static_cast<int64>(sizeof(int16)) > Reader->TotalSize() - Reader->Tell())
		{
			return false;
		}
		OutClip.SampleCount = SampleCount;
		if (!bReadSamples)
		{
			return IsSafeKey(OutClip.Key) && OutClip.SampleRate > 0;
		}
		Reader->Seek(Reader->Tell() - sizeof(int32));
		*Reader << OutClip.Samples;
		return !Reader->IsError() && IsSafeKey(OutClip.Key)
			&& OutClip.SampleRate > 0 && OutClip.Samples.Num() == SampleCount;
	}

	bool WriteClip(const FString& Directory, FStoredClip& Clip, const bool bAllowOverwrite, FString& OutError)
	{
		if (!IsSafeKey(Clip.Key) || Clip.Samples.IsEmpty()
			|| !IFileManager::Get().MakeDirectory(*Directory, true))
		{
			OutError = NSLOCTEXT("SpeechGenAudioCache", "InvalidClipOrDirectory", "Speech cache directory or clip is invalid.").ToString();
			return false;
		}
		FBufferArchive Bytes;
		uint32 Magic = CacheMagic;
		int32 Version = CacheVersion;
		Bytes << Magic;
		Bytes << Version;
		Bytes << Clip.Key;
		Bytes << Clip.Text;
		Bytes << Clip.VoiceDescription;
		Bytes << Clip.SampleRate;
		Bytes << Clip.bImported;
		Bytes << Clip.Samples;
		if (Bytes.Num() > MaximumClipBytes)
		{
			OutError = NSLOCTEXT("SpeechGenAudioCache", "ClipTooLarge", "Speech clip exceeds cache size limit.").ToString();
			return false;
		}
		const FString TargetPath = GetClipPath(Directory, Clip.Key);
		if (!bAllowOverwrite && IFileManager::Get().FileExists(*TargetPath))
		{
			OutError = NSLOCTEXT("SpeechGenAudioCache", "AuthoredClipExists", "An exact authored speech clip already exists. Move it to backup before importing a replacement.").ToString();
			return false;
		}
		const FString StagingPath = TargetPath + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
		if (!FFileHelper::SaveArrayToFile(Bytes, *StagingPath))
		{
			OutError = NSLOCTEXT("SpeechGenAudioCache", "WriteFailed", "Speech clip could not be written.").ToString();
			return false;
		}
		if (!IFileManager::Get().Move(*TargetPath, *StagingPath, true))
		{
			IFileManager::Get().Move(*(StagingPath + TEXT(".bak")), *StagingPath, true);
			OutError = NSLOCTEXT("SpeechGenAudioCache", "PublishFailed", "Speech clip could not be published.").ToString();
			return false;
		}
		return true;
	}
}

FString FSpeechGenAudioCache::GetGeneratedDirectory()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SpeechGen/AudioCache"));
}

FString FSpeechGenAudioCache::MakeKey(const FSpeechGenRequest& Request)
{
	FString Identity = FString::Printf(TEXT("%s|%d|%d|%.9g|%.9g|%.9g|%d:"),
		USpeechGenSubsystem::RuntimeVersion, static_cast<int32>(Request.Language),
		USpeechGenSubsystem::SampleRate, FMath::Clamp(Request.Speed, 0.7f, 1.3f),
		FMath::Clamp(Request.Gain, 0.0f, 2.0f), FMath::Clamp(Request.PauseScale, 0.0f, 1.0f),
		Request.Text.Len());
	Identity += Request.Text;
	Identity += TEXT("|") + DescribeVoice(Request);
	FTCHARToUTF8 Utf8(*Identity);
	uint8 Hash[FSHA1::DigestSize];
	FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Hash);
	return BytesToHex(Hash, FSHA1::DigestSize).ToLower();
}

bool FSpeechGenAudioCache::TryLoad(const FString& Directory, const FSpeechGenRequest& Request,
	FSpeechGenResult& OutResult, FString& OutError)
{
	OutError.Reset();
	const FString Key = MakeKey(Request);
	const FString Path = GetClipPath(Directory, Key);
	if (!IFileManager::Get().FileExists(*Path))
	{
		return false;
	}
	FStoredClip Clip;
	if (!ReadClip(Path, Clip) || Clip.Key != Key || Clip.Text != Request.Text
		|| Clip.SampleRate != USpeechGenSubsystem::SampleRate)
	{
		OutError = FText::Format(NSLOCTEXT("SpeechGenAudioCache", "InvalidEntry", "Speech cache entry is invalid: {0}"),
			FText::FromString(Path)).ToString();
		return false;
	}
	OutResult.TurnId = Request.TurnId;
	OutResult.SegmentId = Request.SegmentId;
	OutResult.SampleRate = Clip.SampleRate;
	OutResult.PcmSamples = MoveTemp(Clip.Samples);
	OutResult.DurationSeconds = static_cast<float>(OutResult.PcmSamples.Num()) / Clip.SampleRate;
	return true;
}

bool FSpeechGenAudioCache::SaveGenerated(const FSpeechGenRequest& Request,
	const FSpeechGenResult& Result, FString& OutError)
{
	FStoredClip Clip;
	Clip.Key = MakeKey(Request);
	Clip.Text = Request.Text;
	Clip.VoiceDescription = DescribeVoice(Request);
	Clip.SampleRate = Result.SampleRate;
	Clip.Samples = Result.PcmSamples;
	return WriteClip(GetGeneratedDirectory(), Clip, true, OutError);
}

bool FSpeechGenAudioCache::ImportWave(const FString& Directory, const FSpeechGenRequest& Request,
	const FString& WavePath, FString& OutError)
{
	TArray<uint8> WaveBytes;
	if (!FFileHelper::LoadFileToArray(WaveBytes, *WavePath))
	{
		OutError = NSLOCTEXT("SpeechGenAudioCache", "WaveLoadFailed", "WAV file could not be loaded.").ToString();
		return false;
	}
	FWaveModInfo WaveInfo;
	if (!WaveInfo.ReadWaveInfo(WaveBytes.GetData(), WaveBytes.Num(), &OutError)
		|| !WaveInfo.pFormatTag || *WaveInfo.pFormatTag != FWaveModInfo::WAVE_INFO_FORMAT_PCM
		|| !WaveInfo.pBitsPerSample || *WaveInfo.pBitsPerSample != 16
		|| !WaveInfo.pChannels || *WaveInfo.pChannels != 1
		|| !WaveInfo.pSamplesPerSec || *WaveInfo.pSamplesPerSec != USpeechGenSubsystem::SampleRate
		|| !WaveInfo.SampleDataStart
		|| WaveInfo.SampleDataSize == 0 || WaveInfo.SampleDataSize % sizeof(int16) != 0)
	{
		OutError = NSLOCTEXT("SpeechGenAudioCache", "InvalidWaveFormat", "WAV must be PCM 16-bit mono at SpeechGen sample rate.").ToString();
		return false;
	}
	FStoredClip Clip;
	Clip.Key = MakeKey(Request);
	Clip.Text = Request.Text;
	Clip.VoiceDescription = DescribeVoice(Request);
	Clip.SampleRate = USpeechGenSubsystem::SampleRate;
	Clip.bImported = true;
	Clip.Samples.SetNumUninitialized(WaveInfo.SampleDataSize / sizeof(int16));
	FMemory::Memcpy(Clip.Samples.GetData(), WaveInfo.SampleDataStart, WaveInfo.SampleDataSize);
	return WriteClip(Directory, Clip, false, OutError);
}

void FSpeechGenAudioCache::List(const FString& Directory, TArray<FSpeechGenAudioCacheEntry>& OutEntries)
{
	OutEntries.Reset();
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory, TEXT("*.socvoice")), true, false);
	for (const FString& File : Files)
	{
		FSpeechGenAudioCacheEntry& Entry = OutEntries.AddDefaulted_GetRef();
		Entry.Key = FPaths::GetBaseFilename(File);
		FStoredClip Clip;
		Entry.bValid = ReadClip(FPaths::Combine(Directory, File), Clip, false) && Clip.Key == Entry.Key;
		if (Entry.bValid)
		{
			Entry.Text = MoveTemp(Clip.Text);
			Entry.VoiceDescription = MoveTemp(Clip.VoiceDescription);
			Entry.SampleRate = Clip.SampleRate;
			Entry.SampleCount = Clip.SampleCount;
			Entry.bImported = Clip.bImported;
		}
	}
	OutEntries.Sort([](const FSpeechGenAudioCacheEntry& A, const FSpeechGenAudioCacheEntry& B)
	{
		return A.Key < B.Key;
	});
}

bool FSpeechGenAudioCache::MoveToBackup(const FString& Directory, const FString& Key,
	FString& OutError)
{
	if (!IsSafeKey(Key))
	{
		OutError = NSLOCTEXT("SpeechGenAudioCache", "InvalidKey", "Invalid speech cache key.").ToString();
		return false;
	}
	const FString Path = GetClipPath(Directory, Key);
	FString BackupPath = Path + TEXT(".bak");
	if (IFileManager::Get().FileExists(*BackupPath))
	{
		BackupPath = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".bak");
	}
	if (!IFileManager::Get().FileExists(*Path)
		|| !IFileManager::Get().Move(*BackupPath, *Path, false))
	{
		OutError = NSLOCTEXT("SpeechGenAudioCache", "BackupFailed", "Speech cache entry could not be moved to backup.").ToString();
		return false;
	}
	return true;
}
