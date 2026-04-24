#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RunwayActor.generated.h"

UCLASS(BlueprintType)
class SPHINXFLIGHT_API ARunwayActor : public AActor
{
	GENERATED_BODY()

public:
	ARunwayActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runway")
	double RunwayLonDeg = 113.376;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runway")
	double RunwayLatDeg = 22.006;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runway")
	float RunwayElevationM = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runway")
	float RunwayHeadingDeg = 230.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runway")
	float RunwayLengthM = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runway")
	float RunwayWidthM = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runway|ILS")
	float GlideSlopeDeg = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runway|ILS")
	float DecisionHeightM = 60.0f;

	UFUNCTION(BlueprintCallable, Category="Runway")
	FVector GetThresholdWorldPosition(double OriginLon, double OriginLat) const;

	UFUNCTION(BlueprintCallable, Category="Runway")
	FVector GetRunwayDirection() const;

	UFUNCTION(BlueprintCallable, Category="Runway")
	FVector GetEndWorldPosition(double OriginLon, double OriginLat) const;
};
