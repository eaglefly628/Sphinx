#include "RunwayActor.h"
#include "FlightGeoUtils.h"

ARunwayActor::ARunwayActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

FVector ARunwayActor::GetThresholdWorldPosition(double OriginLon, double OriginLat) const
{
	return FFlightGeoUtils::LLHToWorld(RunwayLonDeg, RunwayLatDeg, RunwayElevationM,
	                                   OriginLon, OriginLat);
}

FVector ARunwayActor::GetRunwayDirection() const
{
	float HeadingRad = FMath::DegreesToRadians(RunwayHeadingDeg);
	// UE: X = North, Y = East
	return FVector(FMath::Cos(HeadingRad), FMath::Sin(HeadingRad), 0.0f).GetSafeNormal();
}

FVector ARunwayActor::GetEndWorldPosition(double OriginLon, double OriginLat) const
{
	FVector Threshold = GetThresholdWorldPosition(OriginLon, OriginLat);
	FVector Dir = GetRunwayDirection();
	return Threshold + Dir * FFlightGeoUtils::MetersToUnreal(RunwayLengthM);
}
