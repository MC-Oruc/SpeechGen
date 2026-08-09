using UnrealBuildTool;

public class SpeechGenEditor : ModuleRules
{
    public SpeechGenEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "DeveloperSettings",
            "Engine",
            "EditorSubsystem",
            "HTTP",
            "Json",
            "JsonUtilities",
            "Projects",
            "Settings",
            "Slate",
            "SlateCore",
            "SpeechGen",
            "UnrealEd"
        });
    }
}
