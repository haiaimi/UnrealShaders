// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
namespace UnrealBuildTool.Rules
{
	public class ShadowFakeryEditor : ModuleRules
	{
        public ShadowFakeryEditor(ReadOnlyTargetRules Target) : base(Target)
		{
            bLegacyPublicIncludePaths = true;
            PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                    "Slate",
                    "SlateCore",
                    "Engine",
                    "UnrealEd",
                    "PropertyEditor",
                    "RenderCore",
                    "RHI",
                    "AssetTools",
                    "AssetRegistry",
                    "ShadowFakery"
                }
				);
		}
	}
}