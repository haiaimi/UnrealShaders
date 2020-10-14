// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLInteractiveWater.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FSLInteractiveWaterModule"

void FSLInteractiveWaterModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("SLInteractiveWater"))->GetBaseDir(), TEXT("Shaders/Private"));
	AddShaderSourceDirectoryMapping(TEXT("/SLShaders"), PluginShaderDir);
}

void FSLInteractiveWaterModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSLInteractiveWaterModule, SLInteractiveWater)