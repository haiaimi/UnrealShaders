// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkyAtmosphereCommonData.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"



class USkyAtmosphereComponent;



struct FAtmosphereSetup
{
	ENGINE_API static const float CmToSkyUnit;
	ENGINE_API static const float SkyUnitToCm;

	FVector PlanetCenterKm;			// In sky unit (kilometers)
	float BottomRadiusKm;			// idem
	float TopRadiusKm;				// idem

	float MultiScatteringFactor;

	FLinearColor RayleighScattering;// Unit is 1/km
	float RayleighDensityExpScale;

	FLinearColor MieScattering;		// Unit is 1/km
	FLinearColor MieExtinction;		// idem
	FLinearColor MieAbsorption;		// idem
	float MieDensityExpScale;
	float MiePhaseG;

	FLinearColor AbsorptionExtinction;
	float AbsorptionDensity0LayerWidth;
	float AbsorptionDensity0ConstantTerm;
	float AbsorptionDensity0LinearTerm;
	float AbsorptionDensity1ConstantTerm;
	float AbsorptionDensity1LinearTerm;

	FLinearColor GroundAlbedo;

	float TransmittanceMinLightElevationAngle;

	//@StarLight code - START Precomputed Multi Scattering on mobile, edit by wanghai
	bool bUsePrecomputedAtmpsphereLuts;
	bool bShouldUpdatePrecomputedAtmosphereLuts;
	bool bUseStaticLight;
	int32 ScatteringLevel;
	//@StarLight code - END Precomputed Multi Scattering on mobile, edit by wanghai

	ENGINE_API FAtmosphereSetup(const USkyAtmosphereComponent& SkyAtmosphereComponent);

	ENGINE_API FLinearColor GetTransmittanceAtGroundLevel(const FVector& SunDirection) const;

	ENGINE_API void UpdateTransform(const FTransform& ComponentTransform, uint8 TranformMode);
};


