using UnrealBuildTool;
using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;

public class SpeechGen : ModuleRules
{
    private const string RuntimeVersion = "v1.0-q8";

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
        string HashManifest = Path.Combine(RuntimeDirectory, "manifest.sha256");
        IReadOnlyList<string> Files = ValidateRuntime(RuntimeDirectory, HashManifest,
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
    }

    private static IReadOnlyList<string> ValidateRuntime(string RuntimeDirectory, string HashManifest,
        string CanonicalManifest)
    {
        if (!File.Exists(HashManifest))
        {
            throw new BuildException(
                $"SpeechGen runtime {RuntimeVersion} is not installed. Open the editor and wait for automatic SpeechGen preparation before packaging.");
        }

        Dictionary<string, string> InstalledHashes = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (string Line in File.ReadAllLines(HashManifest))
        {
            string Trimmed = Line.Trim();
            if (Trimmed.Length == 0 || Trimmed.StartsWith("#", StringComparison.Ordinal))
            {
                continue;
            }

            int Separator = Trimmed.IndexOf("  ", StringComparison.Ordinal);
            if (Separator != 64)
            {
                throw new BuildException($"SpeechGen hash manifest is malformed: {HashManifest}");
            }

            string ExpectedHash = Trimmed.Substring(0, 64).ToLowerInvariant();
            string RelativeFile = Trimmed.Substring(Separator + 2).Replace('/', Path.DirectorySeparatorChar);
            if (InstalledHashes.ContainsKey(RelativeFile))
            {
                throw new BuildException($"SpeechGen hash manifest contains a duplicate path: {RelativeFile}");
            }
            InstalledHashes.Add(RelativeFile, ExpectedHash);
        }

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
            string ExpectedHash = FileEntry.GetStringField("sha256").ToLowerInvariant();
            if (!InstalledHashes.TryGetValue(RelativeFile, out string InstalledHash)
                || !InstalledHash.Equals(ExpectedHash, StringComparison.Ordinal))
            {
                throw new BuildException($"SpeechGen runtime manifest does not match the pinned package: {RelativeFile}");
            }

            string FullPath = Path.GetFullPath(Path.Combine(RuntimeDirectory, RelativeFile));
            string RuntimeRoot = Path.GetFullPath(RuntimeDirectory) + Path.DirectorySeparatorChar;
            if (!FullPath.StartsWith(RuntimeRoot, StringComparison.OrdinalIgnoreCase) || !File.Exists(FullPath))
            {
                throw new BuildException($"SpeechGen runtime file is missing or invalid: {RelativeFile}");
            }

            using SHA256 Sha = SHA256.Create();
            using FileStream Stream = File.OpenRead(FullPath);
            string ActualHash = BitConverter.ToString(Sha.ComputeHash(Stream)).Replace("-", "").ToLowerInvariant();
            if (!ActualHash.Equals(ExpectedHash, StringComparison.Ordinal))
            {
                throw new BuildException($"SpeechGen runtime integrity check failed: {RelativeFile}");
            }

            Files.Add(RelativeFile);
        }

        if (InstalledHashes.Count != Files.Count)
        {
			throw new BuildException($"SpeechGen runtime manifest contains files outside the pinned package: {HashManifest}");
        }

        Files.Add("manifest.sha256");
        return Files;
    }
}
