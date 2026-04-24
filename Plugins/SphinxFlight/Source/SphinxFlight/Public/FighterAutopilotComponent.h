#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FighterAutopilotComponent.generated.h"

class UFighterAerodynamicsComponent;
class ARunwayActor;

UENUM(BlueprintType)
enum class EFlightPhase : uint8
{
	Idle,
	TakeoffRoll,
	Rotate,
	Climb,
	Cruise,
	Descent,
	FinalApproach,
	Flare,
	Touchdown,
	RollOut,
	ManualOverride
};

USTRUCT(BlueprintType)
struct FPIDController
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) float Kp = 1.0f;
	UPROPERTY(EditAnywhere) float Ki = 0.0f;
	UPROPERTY(EditAnywhere) float Kd = 0.1f;

	float Integral = 0.0f;
	float PrevError = 0.0f;

	float Update(float Error, float DeltaTime);
	void Reset();
};

UCLASS(ClassGroup=(SphinxFlight), meta=(BlueprintSpawnableComponent))
class SPHINXFLIGHT_API UFighterAutopilotComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFighterAutopilotComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- State ---

	UPROPERTY(BlueprintReadOnly, Category="Autopilot")
	EFlightPhase CurrentPhase = EFlightPhase::Idle;

	UPROPERTY(BlueprintReadOnly, Category="Autopilot")
	EFlightPhase PhaseBeforeOverride = EFlightPhase::Idle;

	// --- References ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot")
	TObjectPtr<ARunwayActor> AssignedRunway;

	// --- Takeoff parameters ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|Takeoff")
	float RotationSpeedMPS = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|Takeoff")
	float RotationPitchDeg = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|Takeoff")
	float GearRetractAltM = 50.0f;

	// --- Cruise parameters ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|Cruise")
	float CruiseAltitudeM = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|Cruise")
	float CruiseSpeedMPS = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|Cruise")
	float CruiseHeadingDeg = 0.0f;

	// --- Approach parameters ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|Approach")
	float ApproachSpeedMPS = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|Approach")
	float FlareAltitudeM = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|Approach")
	float TouchdownSinkRateMPS = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|Approach")
	float TaxiSpeedMPS = 10.0f;

	// --- PID tuning ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|PID")
	FPIDController PitchPID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|PID")
	FPIDController RollPID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|PID")
	FPIDController SpeedPID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|PID")
	FPIDController AltitudePID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|PID")
	FPIDController HeadingPID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autopilot|PID")
	FPIDController GlideSlopePID;

	// --- Commands ---

	UFUNCTION(BlueprintCallable, Category="Autopilot")
	void CommandTakeoff();

	UFUNCTION(BlueprintCallable, Category="Autopilot")
	void CommandApproach();

	UFUNCTION(BlueprintCallable, Category="Autopilot")
	void ToggleManualOverride();

	UPROPERTY(BlueprintReadOnly, Category="Autopilot")
	float TimeSincePhaseChange = 0.0f;

private:
	UPROPERTY()
	TObjectPtr<UFighterAerodynamicsComponent> AeroComp;

	void SetPhase(EFlightPhase NewPhase);

	void TickIdle(float DeltaTime);
	void TickTakeoffRoll(float DeltaTime);
	void TickRotate(float DeltaTime);
	void TickClimb(float DeltaTime);
	void TickCruise(float DeltaTime);
	void TickDescent(float DeltaTime);
	void TickFinalApproach(float DeltaTime);
	void TickFlare(float DeltaTime);
	void TickTouchdown(float DeltaTime);
	void TickRollOut(float DeltaTime);

	void ApplySpeedHold(float TargetMPS, float DeltaTime);
	void ApplyAltitudeHold(float TargetM, float DeltaTime);
	void ApplyHeadingHold(float TargetDeg, float DeltaTime);
	void ApplyPitchHold(float TargetDeg, float DeltaTime);
	void ApplyWingsLevel(float DeltaTime);

	float GetRadioAltitude() const;
};
