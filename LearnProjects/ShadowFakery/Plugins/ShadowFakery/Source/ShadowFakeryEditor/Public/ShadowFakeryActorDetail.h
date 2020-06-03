// Fill out your copyright notice in the Description page of Project Settings.

#pragma once


#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "IDetailCustomization.h"


class IDetailLayoutBuilder;
class AShadowFakeryInst;

class FShadowFakeryActorDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/** Handle clicking the convert button */
	FReply ClickedOnConvertToStaticMesh();

	/** Is the convert button enabled */
	bool ConvertToStaticMeshEnabled() const;

	/** Util to get the ProcMeshComp we want to convert */
	AShadowFakeryInst* GetFirstSelectedShadowFakeryActor() const;

	/** Cached array of selected objects */
	TArray< TWeakObjectPtr<UObject> > SelectedObjectsList;
};
