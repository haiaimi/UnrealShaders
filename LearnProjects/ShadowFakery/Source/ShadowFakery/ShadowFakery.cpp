// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ShadowFakery.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "Interfaces/IProjectManager.h"
#include "ShaderCore.h"


void FShadowFakeryModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	FString ProjShaderDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"));
	
	AddShaderSourceDirectoryMapping(TEXT("/Shaders"), ProjShaderDir);
}

void FShadowFakeryModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

IMPLEMENT_PRIMARY_GAME_MODULE( FShadowFakeryModule, ShadowFakery, "ShadowFakery");
