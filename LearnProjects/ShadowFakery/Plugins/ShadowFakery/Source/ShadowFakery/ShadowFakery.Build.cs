// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System;
using System.IO;
using UnrealBuildTool;

public class ShadowFakery : ModuleRules
{
    public ShadowFakery(ReadOnlyTargetRules Target) : base(Target)
    {
        bLegacyPublicIncludePaths = false;
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "RenderCore", "RHI", "Projects", "Foliage" });

        PrivateDependencyModuleNames.AddRange(new string[] { });

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true

        //Add intel embree sdk
        string IntelTBBLibs = Target.UEThirdPartySourceDirectory + "IntelTBB/IntelTBB-2019u8/lib/";

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string SDKDir = Target.UEThirdPartySourceDirectory + "IntelEmbree/Embree270/Win64/";
            string LibraryPath = Path.Combine(PluginDirectory, "Source/ThirdParty/");

            PublicIncludePaths.Add(SDKDir + "include");
            PublicAdditionalLibraries.Add(LibraryPath + "Embree/lib/embree.lib");
            RuntimeDependencies.Add("$(EngineDir)/Binaries/Win64/embree.dll");
            PublicDefinitions.Add("USE_EMBREE=1");
        
            //RuntimeDependencies.Add("$(TargetOutputDir)/embree.2.14.0.dll", SDKDir + "lib/embree.2.14.0.dll");
            //RuntimeDependencies.Add("$(TargetOutputDir)/tbb.dll", IntelTBBLibs + "Win64/vc14/tbb.dll");
            //RuntimeDependencies.Add("$(TargetOutputDir)/tbbmalloc.dll", IntelTBBLibs + "Win64/vc14/tbbmalloc.dll");
            //PublicDefinitions.Add("USE_EMBREE=1");
        }
    }
}