#include "SpeechGenVoiceProfileDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "SpeechGen/SpeechGenVoiceProfile.h"
#include "SpeechGenEditor/SpeechGenVoicePreviewSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SpeechGenVoiceProfileDetails"

TSharedRef<IDetailCustomization> FSpeechGenVoiceProfileDetails::MakeInstance()
{
	return MakeShared<FSpeechGenVoiceProfileDetails>();
}

void FSpeechGenVoiceProfileDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1)
	{
		return;
	}
	Profile = Cast<USpeechGenVoiceProfile>(Objects[0].Get());
	if (!Profile.IsValid() || !GEditor)
	{
		return;
	}
	USpeechGenVoicePreviewSubsystem* Preview = GEditor->GetEditorSubsystem<USpeechGenVoicePreviewSubsystem>();
	if (!Preview)
	{
		return;
	}

	RefreshVariants();
	ESpeechGenLanguage Language = ESpeechGenLanguage::English;
	if (!Profile->NativeVoiceBlend.IsEmpty())
	{
		Language = USpeechGenVoiceProfile::GetVoiceLanguage(Profile->NativeVoiceBlend[0].VoiceId)
			.Get(Language);
	}
	switch (Language)
	{
	case ESpeechGenLanguage::MandarinChinese:
		PreviewText = LOCTEXT("MandarinSample", "你好，这是语音试听。").ToString();
		break;
	case ESpeechGenLanguage::Spanish:
		PreviewText = LOCTEXT("SpanishSample", "Hola, esta es una prueba de voz.").ToString();
		break;
	case ESpeechGenLanguage::Hindi:
		PreviewText = LOCTEXT("HindiSample", "नमस्ते, यह आवाज़ का नमूना है।").ToString();
		break;
	case ESpeechGenLanguage::Italian:
		PreviewText = LOCTEXT("ItalianSample", "Ciao, questa è una prova della voce.").ToString();
		break;
	case ESpeechGenLanguage::BrazilianPortuguese:
		PreviewText = LOCTEXT("PortugueseSample", "Olá, esta é uma amostra de voz.").ToString();
		break;
	default:
		PreviewText = LOCTEXT("EnglishSample", "Hello, this is a voice preview.").ToString();
		break;
	}

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(
		TEXT("SpeechGen|Preview"), LOCTEXT("PreviewCategory", "Voice Preview"));
	Category.AddCustomRow(LOCTEXT("VariantSearch", "Variant"))
	.NameContent()[SNew(STextBlock).Text(LOCTEXT("VariantLabel", "Variant"))]
	.ValueContent().MinDesiredWidth(300.0f)
	[
		SNew(SComboBox<TSharedPtr<FName>>)
		.OptionsSource(&Variants)
		.OnGenerateWidget_Lambda([](TSharedPtr<FName> Variant)
		{
			return SNew(STextBlock).Text(Variant.IsValid() && !Variant->IsNone()
				? FText::FromName(*Variant) : LOCTEXT("DefaultVariant", "Default blend"));
		})
		.OnSelectionChanged_Lambda([this](TSharedPtr<FName> Variant, ESelectInfo::Type)
		{
			SelectedVariant = Variant.IsValid() ? *Variant : NAME_None;
		})
		[
			SNew(STextBlock).Text_Lambda([this]() { return GetSelectedVariantLabel(); })
		]
	];
	Category.AddCustomRow(LOCTEXT("TextSearch", "Test phrase"))
	.NameContent()[SNew(STextBlock).Text(LOCTEXT("TestPhrase", "Test phrase"))]
	.ValueContent().MinDesiredWidth(300.0f)
	[
		SNew(SEditableTextBox)
		.Text(FText::FromString(PreviewText))
		.OnTextChanged_Lambda([this](const FText& Text) { PreviewText = Text.ToString(); })
	];
	Category.AddCustomRow(LOCTEXT("PlaySearch", "Play preview"))
	.WholeRowContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("PlayPreview", "Synthesize and Play"))
			.IsEnabled_Lambda([Preview]() { return !Preview->IsBusy(); })
			.OnClicked_Lambda([this, Preview]()
			{
			if (const USpeechGenVoiceProfile* CurrentProfile = Profile.Get())
			{
				Preview->Preview(*CurrentProfile, SelectedVariant, PreviewText);
			}
			return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("StopPreview", "Stop"))
			.OnClicked_Lambda([Preview]()
			{
				Preview->StopPreview();
				return FReply::Handled();
			})
		]
	];
	Category.AddCustomRow(LOCTEXT("StatusSearch", "Status"))
	.WholeRowContent()
	[
		SNew(STextBlock).Text_Lambda([Preview]() { return Preview->GetStatusText(); })
	];
	Category.AddCustomRow(LOCTEXT("MetricsSearch", "Metrics"))
	.WholeRowContent()
	[
		SNew(STextBlock)
		.Text_Lambda([Preview]() { return Preview->GetMetricsText(); })
		.AutoWrapText(true)
	];
}

void FSpeechGenVoiceProfileDetails::RefreshVariants()
{
	Variants.Reset();
	Variants.Add(MakeShared<FName>(NAME_None));
	if (const USpeechGenVoiceProfile* CurrentProfile = Profile.Get())
	{
		for (const FSpeechGenVoiceVariant& Variant : CurrentProfile->NativeVoiceVariants)
		{
			if (!Variant.VariantId.IsNone())
			{
				Variants.Add(MakeShared<FName>(Variant.VariantId));
			}
		}
	}
}

FText FSpeechGenVoiceProfileDetails::GetSelectedVariantLabel() const
{
	return SelectedVariant.IsNone()
		? LOCTEXT("DefaultVariant", "Default blend") : FText::FromName(SelectedVariant);
}

#undef LOCTEXT_NAMESPACE
