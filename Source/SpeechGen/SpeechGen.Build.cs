using UnrealBuildTool;
using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;

public class SpeechGen : ModuleRules
{
    private const string RuntimeVersion = "v1.0-cpu-mixed.2";

    public SpeechGen(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "NNE"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "DeveloperSettings",
            "Json",
            "JsonUtilities",
            "Projects"
        });

        string FliteRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "flite"));
        PrivateIncludePaths.Add(Path.Combine(FliteRoot, "include"));
        PrivateIncludePaths.Add(Path.Combine(FliteRoot, "lang", "cmulex"));

        bool IsBuildPluginHost = Target.ProjectFile != null
            && Path.GetFileNameWithoutExtension(Target.ProjectFile.FullName).Equals("HostProject", StringComparison.OrdinalIgnoreCase);
        if (Target.Platform == UnrealTargetPlatform.Win64 && !Target.bBuildEditor && Target.ProjectFile != null
            && !IsBuildPluginHost)
        {
            StageInstalledRuntime(Target);
        }
    }

    private void StageInstalledRuntime(ReadOnlyTargetRules Target)
    {
        string RuntimeDirectory = Path.Combine(Target.ProjectFile.Directory.FullName, "Saved", "SpeechGen",
            "Runtimes", "Kokoro", "Win64", RuntimeVersion);
        IReadOnlyList<string> Files = CollectRuntimeFiles(RuntimeDirectory,
            Path.Combine(PluginDirectory, "Resources", "RuntimeManifest.json"));

        foreach (string RelativeFile in Files)
        {
            string SourceFile = Path.Combine(RuntimeDirectory, RelativeFile);
            string StagedFile = Path.Combine("$(TargetOutputDir)", "SpeechGen", "Runtimes", "Kokoro", "Win64",
                RuntimeVersion, RelativeFile);
            RuntimeDependencies.Add(StagedFile, SourceFile, StagedFileType.NonUFS);
        }

        RuntimeDependencies.Add(
            Path.Combine("$(TargetOutputDir)", "Licenses", "SpeechGen", "ThirdParty-LICENSES.md"),
            Path.Combine(PluginDirectory, "ThirdParty-LICENSES.md"),
            StagedFileType.NonUFS);
        RuntimeDependencies.Add(
            Path.Combine("$(TargetOutputDir)", "Licenses", "SpeechGen", "Apache-2.0.txt"),
            Path.Combine(PluginDirectory, "Licenses", "Apache-2.0.txt"),
            StagedFileType.NonUFS);
        RuntimeDependencies.Add(
            Path.Combine("$(TargetOutputDir)", "Licenses", "SpeechGen", "CMUdict-LICENSE.txt"),
            Path.Combine(PluginDirectory, "Licenses", "CMUdict-LICENSE.txt"),
            StagedFileType.NonUFS);
        RuntimeDependencies.Add(
            Path.Combine("$(TargetOutputDir)", "Licenses", "SpeechGen", "Flite-COPYING.txt"),
            Path.Combine(PluginDirectory, "ThirdParty", "flite", "COPYING"),
            StagedFileType.NonUFS);
        RuntimeDependencies.Add(
            Path.Combine("$(TargetOutputDir)", "Licenses", "SpeechGen", "Pypinyin-LICENSE.txt"),
            Path.Combine(PluginDirectory, "Licenses", "Pypinyin-LICENSE.txt"),
            StagedFileType.NonUFS);
        RuntimeDependencies.Add(
            Path.Combine("$(TargetOutputDir)", "Licenses", "SpeechGen", "PinyinToIpa-LICENSE.txt"),
            Path.Combine(PluginDirectory, "Licenses", "PinyinToIpa-LICENSE.txt"),
            StagedFileType.NonUFS);
    }

    private static IReadOnlyList<string> CollectRuntimeFiles(string RuntimeDirectory, string CanonicalManifest)
    {
        if (!File.Exists(CanonicalManifest))
        {
            throw new BuildException($"SpeechGen canonical runtime manifest is missing: {CanonicalManifest}");
        }

        List<string> Files = new List<string>();
        JsonObject Manifest = JsonObject.Read(new FileReference(CanonicalManifest));
        if (!Manifest.GetStringField("version").Equals(RuntimeVersion, StringComparison.Ordinal))
        {
            throw new BuildException($"SpeechGen canonical runtime version does not match {RuntimeVersion}.");
        }
        foreach (JsonObject FileEntry in Manifest.GetObjectArrayField("files"))
        {
            string RelativeFile = FileEntry.GetStringField("path").Replace('/', Path.DirectorySeparatorChar);
            string FullPath = Path.GetFullPath(Path.Combine(RuntimeDirectory, RelativeFile));
            string RuntimeRoot = Path.GetFullPath(RuntimeDirectory) + Path.DirectorySeparatorChar;
            long ExpectedSize = FileEntry.GetIntegerField("size");
            if (!FullPath.StartsWith(RuntimeRoot, StringComparison.OrdinalIgnoreCase)
                || !File.Exists(FullPath)
                || new FileInfo(FullPath).Length != ExpectedSize)
            {
                throw new BuildException($"SpeechGen runtime file is missing or incomplete: {RelativeFile}");
            }

            Files.Add(RelativeFile);
        }
        return Files;
    }
}
