// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ShadowFakeryEditor.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ShadowFakeryInst.h"
#include "ShadowFakeryActorDetail.h"
#include "ShadowFakeryEditor.h"




class FShadowFakeryEditor : public IShadowFakeryEditor
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FShadowFakeryEditor, ShadowFakeryEditor)



void FShadowFakeryEditor::StartupModule()
{
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout(AShadowFakeryInst::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FShadowFakeryActorDetails::MakeInstance));
	}
}


void FShadowFakeryEditor::ShutdownModule()
{

}



