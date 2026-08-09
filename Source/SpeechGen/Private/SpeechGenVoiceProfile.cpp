#include "SpeechGen/SpeechGenVoiceProfile.h"

USpeechGenVoiceProfile::USpeechGenVoiceProfile()
{
	FSpeechGenVoiceWeight& DefaultVoice = NativeVoiceBlend.AddDefaulted_GetRef();
	DefaultVoice.VoiceId = TEXT("af_heart");
	DefaultVoice.Weight = 1.0f;
}

bool USpeechGenVoiceProfile::ResolveStyle(const ESpeechGenStyleMode StyleMode, const FName StyleId,
	TArray<FSpeechGenVoiceWeight>& OutBlend, float& OutSpeed, float& OutGain, float& OutPauseScale) const
{
	OutBlend = NativeVoiceBlend;
	OutSpeed = 1.0f;
	OutGain = 1.0f;
	OutPauseScale = 1.0f;

	if (StyleMode == ESpeechGenStyleMode::Native)
	{
		return !OutBlend.IsEmpty();
	}

	const FSpeechGenStyleDefinition* Style = DirectedStyles.FindByPredicate(
		[StyleId](const FSpeechGenStyleDefinition& Candidate)
		{
			return Candidate.StyleId == StyleId;
		});
	if (!Style)
	{
		return false;
	}

	if (!Style->VoiceBlend.IsEmpty())
	{
		OutBlend = Style->VoiceBlend;
	}
	OutSpeed = Style->Speed;
	OutGain = Style->Gain;
	OutPauseScale = Style->PauseScale;
	return !OutBlend.IsEmpty();
}
