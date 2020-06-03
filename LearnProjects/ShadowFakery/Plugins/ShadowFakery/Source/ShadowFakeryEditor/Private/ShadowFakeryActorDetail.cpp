// Fill out your copyright notice in the Description page of Project Settings.



#include "ShadowFakeryActorDetail.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Application/SlateWindowHelper.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "IDetailsView.h"

#include "Dialogs/DlgPickAssetPath.h"
#include "AssetRegistryModule.h"
#include "ShadowFakeryInst.h"



#define LOCTEXT_NAMESPACE "ShadowFakeryActorDetails"

TSharedRef<IDetailCustomization> FShadowFakeryActorDetails::MakeInstance()
{
	return MakeShareable(new FShadowFakeryActorDetails);
}

void FShadowFakeryActorDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& ProcMeshCategory = DetailBuilder.EditCategory("GenerateShadowDistanceField");

	const FText ConvertToStaticMeshText = LOCTEXT("GenerateShadowFakeryTexture", "Generate ShadowFakery Texture");

	// Cache set of selected things
	SelectedObjectsList = DetailBuilder.GetSelectedObjects();

	ProcMeshCategory.AddCustomRow(ConvertToStaticMeshText, false)
		.NameContent()
		[
			SNullWidget::NullWidget
		]
		.ValueContent()
		.VAlign(VAlign_Center)
		.MaxDesiredWidth(250)
		[
			SNew(SButton)
			.VAlign(VAlign_Center)
			//.ToolTipText(LOCTEXT("ConvertToStaticMeshTooltip", "Create a new StaticMesh asset using current geometry from this ProceduralMeshComponent. Does not modify instance."))
			.OnClicked(this, &FShadowFakeryActorDetails::ClickedOnConvertToStaticMesh)
			.IsEnabled(this, &FShadowFakeryActorDetails::ConvertToStaticMeshEnabled)
			.Content()
			[
				SNew(STextBlock)
				.Text(ConvertToStaticMeshText)
			]
		];
}

FReply FShadowFakeryActorDetails::ClickedOnConvertToStaticMesh()
{
	AShadowFakeryInst* CurInst = GetFirstSelectedShadowFakeryActor();
	if (CurInst)
		CurInst->GenerateShadowDistanceField();
	return FReply::Handled();
}

bool FShadowFakeryActorDetails::ConvertToStaticMeshEnabled() const
{
	return GetFirstSelectedShadowFakeryActor() != nullptr;
}

AShadowFakeryInst* FShadowFakeryActorDetails::GetFirstSelectedShadowFakeryActor() const
{
	AShadowFakeryInst* ShadowFakeryInst = nullptr;
	for (const TWeakObjectPtr<UObject>& Object : SelectedObjectsList)
	{
		AShadowFakeryInst* TestProcComp = Cast<AShadowFakeryInst>(Object.Get());
		// See if this one is good
		if (TestProcComp != nullptr && !TestProcComp->IsTemplate())
		{
			ShadowFakeryInst = TestProcComp;
			break;
		}
	}

	return ShadowFakeryInst;
}

#undef LOCTEXT_NAMESPACE