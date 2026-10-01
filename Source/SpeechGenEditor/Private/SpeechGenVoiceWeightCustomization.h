#pragma once

#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class SSearchableComboBox;

class FSpeechGenVoiceWeightCustomization final : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructHandle,
		FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& Utils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructHandle,
		IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& Utils) override;

private:
	void RefreshVoiceOptions();
	FText GetSelectedVoiceLabel() const;
	static FText DescribeVoice(FName VoiceId);

	TSharedPtr<IPropertyHandle> VoiceIdHandle;
	TSharedPtr<SSearchableComboBox> VoiceCombo;
	TArray<TSharedPtr<FString>> VoiceOptions;
	TMap<FString, FName> VoiceIdsByLabel;
};
