#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FighterAerodynamicsComponent.generated.h"

UCLASS(ClassGroup=(SphinxFlight), meta=(BlueprintSpawnableComponent))
class SPHINXFLIGHT_API UFighterAerodynamicsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFighterAerodynamicsComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Aircraft Parameters (F-16 defaults) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aerodynamics")
	float WingAreaM2 = 27.87f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aerodynamics")
	float Cd0 = 0.015f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aerodynamics")
	float InducedDragFactor = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aerodynamics")
	float MaxThrustN = 76300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aerodynamics")
	float MaxAfterburnerThrustN = 127000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aerodynamics")
	float EmptyMassKg = 8570.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aerodynamics")
	float WingSpanM = 9.96f;

	// --- Lift / Drag curves (UCurveFloat assets) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aerodynamics|Curves")
	TObjectPtr<UCurveFloat> LiftCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aerodynamics|Curves")
	TObjectPtr<UCurveFloat> DragCurve;

	// --- Control inputs (set by Autopilot or Player) ---

	UPROPERTY(BlueprintReadWrite, Category="Aerodynamics|Input")
	float ThrottleInput = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category="Aerodynamics|Input")
	float ElevatorInput = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category="Aerodynamics|Input")
	float AileronInput = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category="Aerodynamics|Input")
	float RudderInput = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category="Aerodynamics|Input")
	bool bAfterburner = false;

	UPROPERTY(BlueprintReadWrite, Category="Aerodynamics|Input")
	bool bGearDown = true;

	UPROPERTY(BlueprintReadWrite, Category="Aerodynamics|Input")
	bool bBrakeEngaged = true;

	UPROPERTY(BlueprintReadWrite, Category="Aerodynamics|Input")
	float FlapDeflectionDeg = 0.0f;

	// --- Telemetry output (read by other components) ---

	UPROPERTY(BlueprintReadOnly, Category="Aerodynamics|State")
	FVector VelocityMPS = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="Aerodynamics|State")
	float AirspeedMPS = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Aerodynamics|State")
	float AltitudeM = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Aerodynamics|State")
	float AngleOfAttackDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Aerodynamics|State")
	float SideslipAngleDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Aerodynamics|State")
	float VerticalSpeedMPS = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Aerodynamics|State")
	float PitchDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Aerodynamics|State")
	float RollDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Aerodynamics|State")
	float YawDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Aerodynamics|State")
	float GForce = 1.0f;

private:
	void TickPhysics(float DeltaTime);

	float GetAirDensity(float AltitudeMeters) const;
	float EvaluateLiftCoefficient(float AlphaRad) const;
	float EvaluateDragCoefficient(float Cl) const;
	FVector ComputeControlMoments(float Q, float DeltaTime) const;
	float ComputeAngleOfAttack(const FVector& LocalVelocity) const;
	float ComputeSideslipAngle(const FVector& LocalVelocity) const;

	FVector AccumulatedForce = FVector::ZeroVector;
	FVector AngularVelocityRadPS = FVector::ZeroVector;

	// Moment of inertia estimates (kg*m^2) — simplified ellipsoid
	float IxxRoll = 12875.0f;
	float IyyPitch = 75674.0f;
	float IzzYaw = 85552.0f;
};
