// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ShadowFakeryGame.h"
#include "Modules/ModuleManager.h"


void FShadowFakeryGameModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FShadowFakeryGameModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

IMPLEMENT_PRIMARY_GAME_MODULE( FShadowFakeryGameModule, ShadowFakeryGame, "ShadowFakeryGame");
