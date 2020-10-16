// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "SceneViewExtension.h"
#include "RenderResource.h"


/**
 * This file was used to simulate 3D fluid, such as smoke, fire 
 */
 
 /**
  * This struct was used as the fluid proxy on game thread
  */
 class FVolumeFluidProxy : public TSharedFromThis<FVolumeFluidProxy, ESPMode::ThreadSafe>
 {
 public:
	FTransform FluidVolumeTransform;

	FIntVector FluidVolumeSize = FIntVector(128);

	FIntPoint RayMarchRTSize;

	uint32 IterationCount = 20u;

	float VorticityScale = 0.2f;

	float TimeStep = 0.1f;

	FTextureResource* TextureResource = nullptr;

	class FTextureRenderTargetResource* TextureRenderTargetResource = nullptr;

	ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::ES3_1;
 };

 class FVolumeFluidSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FVolumeFluidSceneViewExtension(const FAutoRegister& Register):
		FSceneViewExtensionBase(Register)
	{}

    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) {}

	/**
	 * Called on game thread when creating the view.
	 */
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) {}

    /**
     * Called on game thread when view family is about to be rendered.
     */
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) {}

    /**
     * Called on render thread at the start of rendering.
     */
    virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) {}

	/**
     * Called on render thread at the start of rendering, for each view, after PreRenderViewFamily_RenderThread call.
     */
    virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView);

	/**
	 * Called right after Base Pass rendering finished
	 */
	virtual void PostRenderBasePass_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView);

};

 class FFluidSmiulationManager : public FRenderResource
 {
 public:

	 virtual ~FFluidSmiulationManager() {}

	 virtual void InitRHI() override
	 {
		
	 }

	 virtual void ReleaseRHI() override
	 {
		FRenderResource::ReleaseRHI();
		VolumeFluidExtension.Reset();
	 }

	 void RegisterFluidExtension()
	 {
		// Regiser view extension used to render fluid
		if(!VolumeFluidExtension.IsValid())
			VolumeFluidExtension = FSceneViewExtensions::NewExtension<FVolumeFluidSceneViewExtension>();
	 }

	 void AddFluidProxy(TSharedPtr<class FVolumeFluidProxy, ESPMode::ThreadSafe> FluidProxy)
	 {
		if(AllFluidProxys.AddUnique(FluidProxy) != INDEX_NONE)
			RegisterFluidExtension();
	 }

private:
	TSharedPtr<FVolumeFluidSceneViewExtension, ESPMode::ThreadSafe> VolumeFluidExtension;

	TArray<TWeakPtr<FVolumeFluidProxy, ESPMode::ThreadSafe>> AllFluidProxys;

	friend FVolumeFluidSceneViewExtension;
 };

void UpdateFluid3D(FRHICommandListImmediate& RHICmdList, const FVolumeFluidProxy& ResourceParam, class FViewInfo* InView);