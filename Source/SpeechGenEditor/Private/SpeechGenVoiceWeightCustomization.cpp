#include "SpeechGenVoiceWeightCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "SpeechGen/SpeechGenSubsystem.h"
#include "SpeechGen/SpeechGenVoiceProfile.h"
#include "Widgets/Input/SSearchableComboBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SpeechGenVoiceWeightCustomization"

TSharedRef<IPropertyTypeCustomization> FSpeechGenVoiceWeightCustomization::MakeInstance()
{
	return MakeShared<FSpeechGenVoiceWeightCustomization>();
}

void FSpeechGenVoiceWeightCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> StructHandle, FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils&)
{
	HeaderRow.NameContent()[StructHandle->CreatePropertyNameWidget()]
	.ValueContent()[SNew(STextBlock).Text(this, &FSpeechGenVoiceWeightCustomization::GetSelectedVoiceLabel)];
}

void FSpeechGenVoiceWeightCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> StructHandle, IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils&)
{
	VoiceIdHandle = StructHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FSpeechGenVoiceWeight, VoiceId));
	TSharedPtr<IPropertyHandle> WeightHandle = StructHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FSpeechGenVoiceWeight, Weight));
	if (!VoiceIdHandle.IsValid() || !WeightHandle.IsValid())
	{
		return;
	}

	RefreshVoiceOptions();
	ChildBuilder.AddCustomRow(LOCTEXT("VoiceIdSearch", "Voice ID"))
	.NameContent()[VoiceIdHandle->CreatePropertyNameWidget()]
	.ValueContent().MinDesiredWidth(320.0f)
	[
		SAssignNew(VoiceCombo, SSearchableComboBox)
		.OptionsSource(&VoiceOptions)
		.MaxListHeight(420.0f)
		.OnComboBoxOpening_Lambda([this]()
		{
			RefreshVoiceOptions();
			VoiceCombo->RefreshOptions();
		})
		.OnGenerateWidget_Lambda([](TSharedPtr<FString> Option)
		{
			return SNew(STextBlock).Text(FText::FromString(*Option));
		})
		.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Option, ESelectInfo::Type)
		{
			if (Option.IsValid() && VoiceIdHandle.IsValid())
			{
				if (const FName* VoiceId = VoiceIdsByLabel.Find(*Option))
				{
					VoiceIdHandle->SetValue(*VoiceId);
				}
			}
		})
		[
			SNew(STextBlock).Text(this, &FSpeechGenVoiceWeightCustomization::GetSelectedVoiceLabel)
		]
	];
	ChildBuilder.AddProperty(WeightHandle.ToSharedRef());
}

void FSpeechGenVoiceWeightCustomization::RefreshVoiceOptions()
{
	VoiceOptions.Reset();
	VoiceIdsByLabel.Reset();
	for (const FString& Id : USpeechGenSubsystem::GetAvailableVoiceIds())
	{
		const FString Label = DescribeVoice(FName(*Id)).ToString();
		VoiceOptions.Add(MakeShared<FString>(Label));
		VoiceIdsByLabel.Add(Label, FName(*Id));
	}
}

FText FSpeechGenVoiceWeightCustomization::GetSelectedVoiceLabel() const
{
	if (VoiceOptions.IsEmpty())
	{
		return LOCTEXT("VoicesUnavailable", "SpeechGen voices unavailable (install runtime)");
	}
	FName VoiceId;
	if (!VoiceIdHandle.IsValid() || VoiceIdHandle->GetValue(VoiceId) != FPropertyAccess::Success)
	{
		return LOCTEXT("SelectVoice", "Select a voice");
	}
	return DescribeVoice(VoiceId);
}

FText FSpeechGenVoiceWeightCustomization::DescribeVoice(const FName VoiceId)
{
	const TOptional<ESpeechGenLanguage> Language = USpeechGenVoiceProfile::GetVoiceLanguage(VoiceId);
	if (!Language.IsSet())
	{
		return FText::FromName(VoiceId);
	}

	const FString Id = VoiceId.ToString();
	FText LanguageLabel;
	switch (Language.GetValue())
	{
	case ESpeechGenLanguage::English:
		LanguageLabel = Id[0] == TEXT('b')
			? LOCTEXT("BritishEnglish", "English (UK)") : LOCTEXT("AmericanEnglish", "English (US)");
		break;
	case ESpeechGenLanguage::MandarinChinese:
		LanguageLabel = LOCTEXT("MandarinChinese", "Mandarin Chinese");
		break;
	case ESpeechGenLanguage::Spanish:
		LanguageLabel = LOCTEXT("Spanish", "Spanish");
		break;
	case ESpeechGenLanguage::Hindi:
		LanguageLabel = LOCTEXT("Hindi", "Hindi");
		break;
	case ESpeechGenLanguage::Italian:
		LanguageLabel = LOCTEXT("Italian", "Italian");
		break;
	case ESpeechGenLanguage::BrazilianPortuguese:
		LanguageLabel = LOCTEXT("BrazilianPortuguese", "Portuguese (Brazil)");
		break;
	default:
		return FText::FromName(VoiceId);
	}

	const FText GenderLabel = Id[1] == TEXT('f')
		? LOCTEXT("Female", "Female") : LOCTEXT("Male", "Male");
	FString Name = Id.Mid(3).Replace(TEXT("_"), TEXT(" "));
	if (!Name.IsEmpty())
	{
		Name[0] = FChar::ToUpper(Name[0]);
	}
	return FText::Format(LOCTEXT("VoiceLabel", "{0} | {1} | {2} ({3})"),
		LanguageLabel, GenderLabel, FText::FromString(Name), FText::FromName(VoiceId));
}

#undef LOCTEXT_NAMESPACE
