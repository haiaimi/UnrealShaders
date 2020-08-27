// Copyright Epic Games, Inc. All Rights Reserved.

#include "FluidSimulation.h"
#include "Modules/ModuleManager.h"

void FFluidSimulationModule::StartupModule()
{
	FString ProjectShaderDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders/Private"));
	AddShaderSourceDirectoryMapping(TEXT("/Shaders/Private"), ProjectShaderDir);
}

void FFluidSimulationModule::ShutdownModule()
{

}


IMPLEMENT_PRIMARY_GAME_MODULE( FFluidSimulationModule, FluidSimulation, "FluidSimulation" );