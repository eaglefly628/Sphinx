#pragma once

#include "CoreMinimal.h"

struct SPHINXFLIGHT_API FFlightGeoUtils
{
	static constexpr double EarthRadiusM = 6378137.0;
	static constexpr double CmPerMeter = 100.0;

	static FVector LLHToWorld(double LonDeg, double LatDeg, double AltM,
	                          double OriginLonDeg, double OriginLatDeg);

	static void WorldToLLH(const FVector& WorldPos,
	                       double OriginLonDeg, double OriginLatDeg,
	                       double& OutLonDeg, double& OutLatDeg, double& OutAltM);

	static float MetersToUnreal(float Meters);
	static float UnrealToMeters(float Unreal);
};
