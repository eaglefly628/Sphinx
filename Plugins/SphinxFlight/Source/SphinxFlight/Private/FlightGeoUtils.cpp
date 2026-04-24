#include "FlightGeoUtils.h"

FVector FFlightGeoUtils::LLHToWorld(double LonDeg, double LatDeg, double AltM,
                                    double OriginLonDeg, double OriginLatDeg)
{
	const double DegToRad = PI / 180.0;
	const double OriginLatRad = OriginLatDeg * DegToRad;
	const double MPerDegLon = EarthRadiusM * FMath::Cos(OriginLatRad) * DegToRad;
	const double MPerDegLat = EarthRadiusM * DegToRad;

	const double DeltaLon = LonDeg - OriginLonDeg;
	const double DeltaLat = LatDeg - OriginLatDeg;

	const double EastM = DeltaLon * MPerDegLon;
	const double NorthM = DeltaLat * MPerDegLat;

	// UE: X = North (forward), Y = East (right), Z = Up
	return FVector(NorthM * CmPerMeter, EastM * CmPerMeter, AltM * CmPerMeter);
}

void FFlightGeoUtils::WorldToLLH(const FVector& WorldPos,
                                 double OriginLonDeg, double OriginLatDeg,
                                 double& OutLonDeg, double& OutLatDeg, double& OutAltM)
{
	const double DegToRad = PI / 180.0;
	const double OriginLatRad = OriginLatDeg * DegToRad;
	const double MPerDegLon = EarthRadiusM * FMath::Cos(OriginLatRad) * DegToRad;
	const double MPerDegLat = EarthRadiusM * DegToRad;

	const double NorthM = WorldPos.X / CmPerMeter;
	const double EastM = WorldPos.Y / CmPerMeter;

	OutLatDeg = OriginLatDeg + NorthM / MPerDegLat;
	OutLonDeg = OriginLonDeg + EastM / MPerDegLon;
	OutAltM = WorldPos.Z / CmPerMeter;
}

float FFlightGeoUtils::MetersToUnreal(float Meters)
{
	return Meters * static_cast<float>(CmPerMeter);
}

float FFlightGeoUtils::UnrealToMeters(float Unreal)
{
	return Unreal / static_cast<float>(CmPerMeter);
}
