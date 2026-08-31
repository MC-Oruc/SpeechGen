#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SpeechGenProjectSettings.generated.h"

UENUM()
enum class ESpeechGenEditorLifecycleMode : uint8
{
	EditorSession UMETA(DisplayName = "Editor Session"),
	PIESession UMETA(DisplayName = "PIE Session"),
	Manual UMETA(DisplayName = "Manual")
};

USTRUCT()
struct SPEECHGEN_API FSpeechGenDevelopmentDefaults
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, Category = "Development")
	ESpeechGenEditorLifecycleMode EditorLifecycle = ESpeechGenEditorLifecycleMode::Manual;
};

USTRUCT()
struct SPEECHGEN_API FSpeechGenPackagedDefaults
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, Category = "Packaged Game")
	bool bStartRuntimeOnGameInstance = true;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SpeechGen"))
class SPEECHGEN_API USpeechGenProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Development")
	FSpeechGenDevelopmentDefaults DevelopmentDefaults;

	UPROPERTY(Config, EditAnywhere, Category = "Packaged Game")
	FSpeechGenPackagedDefaults PackagedDefaults;

	virtual FName GetCategoryName() const override;
};
