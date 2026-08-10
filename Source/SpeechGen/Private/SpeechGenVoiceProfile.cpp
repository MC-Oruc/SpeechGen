#include "SpeechGen/SpeechGenVoiceProfile.h"

namespace
{
	TOptional<ESpeechGenLanguage> ResolveVoiceLanguage(const FName VoiceId)
	{
		const FString Id = VoiceId.ToString();
		if (Id.Len() < 2 || (Id[1] != TEXT('f') && Id[1] != TEXT('m')))
		{
			return {};
		}

		switch (Id[0])
		{
		case TEXT('a'):
		case TEXT('b'):
			return ESpeechGenLanguage::English;
		case TEXT('z'):
			return ESpeechGenLanguage::MandarinChinese;
		case TEXT('e'):
			return ESpeechGenLanguage::Spanish;
		case TEXT('h'):
			return ESpeechGenLanguage::Hindi;
		case TEXT('i'):
			return ESpeechGenLanguage::Italian;
		case TEXT('p'):
			return ESpeechGenLanguage::BrazilianPortuguese;
		default:
			return {};
		}
	}
}

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

bool USpeechGenVoiceProfile::ValidateVoiceBlend(const TArray<FSpeechGenVoiceWeight>& Blend, FString& OutError) const
{
	for (const FSpeechGenVoiceWeight& Voice : Blend)
	{
		const TOptional<ESpeechGenLanguage> VoiceLanguage = ResolveVoiceLanguage(Voice.VoiceId);
		if (!VoiceLanguage.IsSet())
		{
			OutError = FString::Printf(TEXT("Voice '%s' is not supported by SpeechGen."), *Voice.VoiceId.ToString());
			return false;
		}
		if (VoiceLanguage.GetValue() != Language)
		{
			OutError = FString::Printf(TEXT("Voice '%s' does not match the voice profile language."),
				*Voice.VoiceId.ToString());
			return false;
		}
	}
	return !Blend.IsEmpty();
}
