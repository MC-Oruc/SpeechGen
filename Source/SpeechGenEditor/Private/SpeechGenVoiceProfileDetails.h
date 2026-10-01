#pragma once

#include "IDetailCustomization.h"

class USpeechGenVoiceProfile;

class FSpeechGenVoiceProfileDetails final : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	void RefreshVariants();
	FText GetSelectedVariantLabel() const;

	TWeakObjectPtr<USpeechGenVoiceProfile> Profile;
	TArray<TSharedPtr<FName>> Variants;
	FName SelectedVariant = NAME_None;
	FString PreviewText;
};
