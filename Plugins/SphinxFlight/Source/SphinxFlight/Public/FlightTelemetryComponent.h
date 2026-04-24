#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlightTelemetryComponent.generated.h"

class UFighterAerodynamicsComponent;
class UFighterAutopilotComponent;

USTRUCT(BlueprintType)
struct FFlightTelemetryData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) float AirspeedKnots = 0.0f;
	UPROPERTY(BlueprintReadOnly) float AirspeedMPS = 0.0f;
	UPROPERTY(BlueprintReadOnly) float AltitudeM = 0.0f;
	UPROPERTY(BlueprintReadOnly) float AltitudeFt = 0.0f;
	UPROPERTY(BlueprintReadOnly) float VerticalSpeedFPM = 0.0f;
	UPROPERTY(BlueprintReadOnly) float HeadingDeg = 0.0f;
	UPROPERTY(BlueprintReadOnly) float PitchDeg = 0.0f;
	UPROPERTY(BlueprintReadOnly) float RollDeg = 0.0f;
	UPROPERTY(BlueprintReadOnly) float AngleOfAttackDeg = 0.0f;
	UPROPERTY(BlueprintReadOnly) float GForce = 1.0f;
	UPROPERTY(BlueprintReadOnly) float ThrottlePercent = 0.0f;
	UPROPERTY(BlueprintReadOnly) bool bGearDown = true;
	UPROPERTY(BlueprintReadOnly) bool bAfterburner = false;
	UPROPERTY(BlueprintReadOnly) float FlapsDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly) double LongitudeDeg = 0.0;
	UPROPERTY(BlueprintReadOnly) double LatitudeDeg = 0.0;

	UPROPERTY(BlueprintReadOnly) FString FlightPhase = TEXT("Idle");
	UPROPERTY(BlueprintReadOnly) bool bAutopilotActive = true;
};

UCLASS(ClassGroup=(SphinxFlight), meta=(BlueprintSpawnableComponent))
class SPHINXFLIGHT_API UFlightTelemetryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFlightTelemetryComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(BlueprintReadOnly, Category="Telemetry")
	FFlightTelemetryData Data;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Telemetry")
	double OriginLonDeg = 113.376;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Telemetry")
	double OriginLatDeg = 22.006;

private:
	UPROPERTY()
	TObjectPtr<UFighterAerodynamicsComponent> AeroComp;

	UPROPERTY()
	TObjectPtr<UFighterAutopilotComponent> AutopilotComp;
};
