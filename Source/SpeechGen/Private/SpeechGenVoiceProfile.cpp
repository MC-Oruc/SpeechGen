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

	bool IsExperimentalLanguage(const ESpeechGenLanguage Language)
	{
		return Language == ESpeechGenLanguage::TurkishExperimental
			|| Language == ESpeechGenLanguage::AzerbaijaniExperimental
			|| Language == ESpeechGenLanguage::GermanExperimental
			|| Language == ESpeechGenLanguage::DutchExperimental;
	}
}

USpeechGenVoiceProfile::USpeechGenVoiceProfile()
{
	FSpeechGenVoiceWeight& DefaultVoice = NativeVoiceBlend.AddDefaulted_GetRef();
	DefaultVoice.VoiceId = TEXT("af_heart");
	DefaultVoice.Weight = 1.0f;
}

bool USpeechGenVoiceProfile::Resolve(const FName VoiceVariantId, const ESpeechGenStyleMode StyleMode,
	const FName StyleId, TArray<FSpeechGenVoiceWeight>& OutBlend, float& OutSpeed, float& OutGain,
	float& OutPauseScale, FString& OutError) const
{
	OutBlend = NativeVoiceBlend;
	OutSpeed = 1.0f;
	OutGain = 1.0f;
	OutPauseScale = 1.0f;

	const FSpeechGenStyleDefinition* Style = nullptr;
	if (StyleMode == ESpeechGenStyleMode::Directed)
	{
		Style = DirectedStyles.FindByPredicate(
			[StyleId](const FSpeechGenStyleDefinition& Candidate)
			{
				return Candidate.StyleId == StyleId;
			});
		if (!Style)
		{
			OutError = FString::Printf(TEXT("Voice profile does not define style '%s'."), *StyleId.ToString());
			return false;
		}

		if (!Style->VoiceBlend.IsEmpty())
		{
			OutBlend = Style->VoiceBlend;
		}
		OutSpeed = Style->Speed;
		OutGain = Style->Gain;
		OutPauseScale = Style->PauseScale;
	}

	if (!VoiceVariantId.IsNone())
	{
		const FSpeechGenVoiceVariant* Variant = Style
			? Style->VoiceVariants.FindByPredicate(
				[VoiceVariantId](const FSpeechGenVoiceVariant& Candidate)
				{
					return Candidate.VariantId == VoiceVariantId;
				})
			: nullptr;
		if (!Variant)
		{
			Variant = NativeVoiceVariants.FindByPredicate(
				[VoiceVariantId](const FSpeechGenVoiceVariant& Candidate)
				{
					return Candidate.VariantId == VoiceVariantId;
				});
		}
		if (!Variant || Variant->VoiceBlend.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Voice profile does not define variant '%s'."),
				*VoiceVariantId.ToString());
			return false;
		}
		OutBlend = Variant->VoiceBlend;
	}

	if (OutBlend.IsEmpty())
	{
		OutError = TEXT("Voice profile has no resolved voice blend.");
		return false;
	}
	return true;
}

bool USpeechGenVoiceProfile::ValidateVoiceBlend(const ESpeechGenLanguage Language,
	const TArray<FSpeechGenVoiceWeight>& Blend, FString& OutError)
{
	for (const FSpeechGenVoiceWeight& Voice : Blend)
	{
		const TOptional<ESpeechGenLanguage> VoiceLanguage = ResolveVoiceLanguage(Voice.VoiceId);
		if (!VoiceLanguage.IsSet())
		{
			OutError = FString::Printf(TEXT("Voice '%s' is not supported by SpeechGen."), *Voice.VoiceId.ToString());
			return false;
		}
		if (!IsExperimentalLanguage(Language) && VoiceLanguage.GetValue() != Language)
		{
			OutError = FString::Printf(TEXT("Voice '%s' does not match the speech request language."),
				*Voice.VoiceId.ToString());
			return false;
		}
	}
	return !Blend.IsEmpty();
}
