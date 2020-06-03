// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System;
using System.IO;
using UnrealBuildTool;

public class ShadowFakery : ModuleRules
{
    public ShadowFakery(ReadOnlyTargetRules Target) : base(Target)
    {
        bLegacyPublicIncludePaths = true;
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "RenderCore", "RHI", "Projects" });

        PrivateDependencyModuleNames.AddRange(new string[] { });

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true

        //Add intel embree sdk
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string SDKDir = Target.UEThirdPartySourceDirectory + "IntelEmbree/Embree270/Win64/";
            string LibraryPath = Path.Combine(PluginDirectory, "Source/ThirdParty/");

            PublicIncludePaths.Add(SDKDir + "include");
            PublicAdditionalLibraries.Add(LibraryPath + "Embree/lib/embree.lib");
            RuntimeDependencies.Add("$(EngineDir)/Binaries/Win64/embree.dll");
            RuntimeDependencies.Add("$(EngineDir)/Binaries/Win64/tbb.dll", Target.UEThirdPartySourceDirectory + "IntelEmbree/Embree2140/Win64/lib/tbb.dll"); // Take latest version to avoid overwriting the editor's copy
            RuntimeDependencies.Add("$(EngineDir)/Binaries/Win64/tbbmalloc.dll", Target.UEThirdPartySourceDirectory + "IntelEmbree/Embree2140/Win64/lib/tbbmalloc.dll");
            PublicDefinitions.Add("USE_EMBREE=1");
        }
    }
}