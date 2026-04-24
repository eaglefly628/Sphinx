#include "FighterAutopilotComponent.h"
#include "FighterAerodynamicsComponent.h"
#include "RunwayActor.h"
#include "FlightGeoUtils.h"
#include "SphinxFlightModule.h"

// --- PID ---

float FPIDController::Update(float Error, float DeltaTime)
{
	Integral += Error * DeltaTime;
	Integral = FMath::Clamp(Integral, -50.0f, 50.0f);
	float Derivative = (DeltaTime > SMALL_NUMBER) ? (Error - PrevError) / DeltaTime : 0.0f;
	PrevError = Error;
	return Kp * Error + Ki * Integral + Kd * Derivative;
}

void FPIDController::Reset()
{
	Integral = 0.0f;
	PrevError = 0.0f;
}

// --- Autopilot ---

UFighterAutopilotComponent::UFighterAutopilotComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	PitchPID.Kp = 0.05f; PitchPID.Ki = 0.001f; PitchPID.Kd = 0.02f;
	RollPID.Kp = 0.03f; RollPID.Ki = 0.0f; RollPID.Kd = 0.01f;
	SpeedPID.Kp = 0.01f; SpeedPID.Ki = 0.001f; SpeedPID.Kd = 0.0f;
	AltitudePID.Kp = 0.005f; AltitudePID.Ki = 0.0001f; AltitudePID.Kd = 0.01f;
	HeadingPID.Kp = 0.02f; HeadingPID.Ki = 0.0f; HeadingPID.Kd = 0.005f;
	GlideSlopePID.Kp = 0.04f; GlideSlopePID.Ki = 0.001f; GlideSlopePID.Kd = 0.02f;
}

void UFighterAutopilotComponent::BeginPlay()
{
	Super::BeginPlay();
	AeroComp = GetOwner()->FindComponentByClass<UFighterAerodynamicsComponent>();
	if (!AeroComp)
	{
		UE_LOG(LogSphinxFlight, Error, TEXT("Autopilot requires FighterAerodynamicsComponent"));
	}
}

void UFighterAutopilotComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                               FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!AeroComp) return;

	TimeSincePhaseChange += DeltaTime;

	switch (CurrentPhase)
	{
	case EFlightPhase::Idle:           TickIdle(DeltaTime); break;
	case EFlightPhase::TakeoffRoll:    TickTakeoffRoll(DeltaTime); break;
	case EFlightPhase::Rotate:         TickRotate(DeltaTime); break;
	case EFlightPhase::Climb:          TickClimb(DeltaTime); break;
	case EFlightPhase::Cruise:         TickCruise(DeltaTime); break;
	case EFlightPhase::Descent:        TickDescent(DeltaTime); break;
	case EFlightPhase::FinalApproach:  TickFinalApproach(DeltaTime); break;
	case EFlightPhase::Flare:          TickFlare(DeltaTime); break;
	case EFlightPhase::Touchdown:      TickTouchdown(DeltaTime); break;
	case EFlightPhase::RollOut:        TickRollOut(DeltaTime); break;
	case EFlightPhase::ManualOverride: break;
	}
}

void UFighterAutopilotComponent::SetPhase(EFlightPhase NewPhase)
{
	UE_LOG(LogSphinxFlight, Log, TEXT("===== Flight Phase: %d -> %d ====="),
	       static_cast<int>(CurrentPhase), static_cast<int>(NewPhase));
	CurrentPhase = NewPhase;
	TimeSincePhaseChange = 0.0f;
}

void UFighterAutopilotComponent::CommandTakeoff()
{
	if (CurrentPhase == EFlightPhase::Idle || CurrentPhase == EFlightPhase::RollOut)
	{
		SetPhase(EFlightPhase::TakeoffRoll);
		if (AssignedRunway)
		{
			CruiseHeadingDeg = AssignedRunway->RunwayHeadingDeg;
		}
	}
}

void UFighterAutopilotComponent::CommandApproach()
{
	if (CurrentPhase == EFlightPhase::Cruise)
	{
		SetPhase(EFlightPhase::Descent);
	}
}

void UFighterAutopilotComponent::ToggleManualOverride()
{
	if (CurrentPhase == EFlightPhase::ManualOverride)
	{
		SetPhase(PhaseBeforeOverride);
	}
	else
	{
		PhaseBeforeOverride = CurrentPhase;
		SetPhase(EFlightPhase::ManualOverride);
	}
}

// --- Phase implementations ---

void UFighterAutopilotComponent::TickIdle(float DeltaTime)
{
	AeroComp->ThrottleInput = 0.0f;
	AeroComp->bBrakeEngaged = true;
	AeroComp->ElevatorInput = 0.0f;
	AeroComp->AileronInput = 0.0f;
	AeroComp->RudderInput = 0.0f;
}

void UFighterAutopilotComponent::TickTakeoffRoll(float DeltaTime)
{
	AeroComp->ThrottleInput = 1.0f;
	AeroComp->bAfterburner = true;
	AeroComp->bBrakeEngaged = false;
	AeroComp->bGearDown = true;
	AeroComp->FlapDeflectionDeg = 20.0f;

	ApplyHeadingHold(CruiseHeadingDeg, DeltaTime);
	AeroComp->ElevatorInput = 0.0f;

	if (AeroComp->AirspeedMPS >= RotationSpeedMPS)
	{
		SetPhase(EFlightPhase::Rotate);
	}
}

void UFighterAutopilotComponent::TickRotate(float DeltaTime)
{
	AeroComp->ThrottleInput = 1.0f;
	AeroComp->bAfterburner = true;
	AeroComp->FlapDeflectionDeg = 20.0f;

	ApplyPitchHold(RotationPitchDeg, DeltaTime);
	ApplyHeadingHold(CruiseHeadingDeg, DeltaTime);

	float AltAGL = GetRadioAltitude();
	if (AltAGL >= GearRetractAltM)
	{
		AeroComp->bGearDown = false;
		AeroComp->FlapDeflectionDeg = 0.0f;
		SetPhase(EFlightPhase::Climb);
	}
}

void UFighterAutopilotComponent::TickClimb(float DeltaTime)
{
	AeroComp->bAfterburner = false;
	ApplySpeedHold(CruiseSpeedMPS * 0.8f, DeltaTime);
	ApplyAltitudeHold(CruiseAltitudeM, DeltaTime);
	ApplyHeadingHold(CruiseHeadingDeg, DeltaTime);
	ApplyWingsLevel(DeltaTime);

	if (AeroComp->AltitudeM >= CruiseAltitudeM * 0.95f)
	{
		SetPhase(EFlightPhase::Cruise);
	}
}

void UFighterAutopilotComponent::TickCruise(float DeltaTime)
{
	ApplySpeedHold(CruiseSpeedMPS, DeltaTime);
	ApplyAltitudeHold(CruiseAltitudeM, DeltaTime);
	ApplyHeadingHold(CruiseHeadingDeg, DeltaTime);
	ApplyWingsLevel(DeltaTime);
}

void UFighterAutopilotComponent::TickDescent(float DeltaTime)
{
	float TargetAltM = 300.0f;
	ApplySpeedHold(ApproachSpeedMPS * 1.3f, DeltaTime);
	ApplyAltitudeHold(TargetAltM, DeltaTime);
	ApplyWingsLevel(DeltaTime);

	AeroComp->bGearDown = true;
	AeroComp->FlapDeflectionDeg = 20.0f;

	if (AssignedRunway)
	{
		float ReciprocalHeading = FMath::Fmod(AssignedRunway->RunwayHeadingDeg + 180.0f, 360.0f);
		ApplyHeadingHold(ReciprocalHeading, DeltaTime);
	}

	if (AeroComp->AltitudeM <= TargetAltM * 1.1f)
	{
		SetPhase(EFlightPhase::FinalApproach);
	}
}

void UFighterAutopilotComponent::TickFinalApproach(float DeltaTime)
{
	ApplySpeedHold(ApproachSpeedMPS, DeltaTime);
	ApplyWingsLevel(DeltaTime);

	AeroComp->bGearDown = true;
	AeroComp->FlapDeflectionDeg = 40.0f;

	if (AssignedRunway)
	{
		float ReciprocalHeading = FMath::Fmod(AssignedRunway->RunwayHeadingDeg + 180.0f, 360.0f);
		ApplyHeadingHold(ReciprocalHeading, DeltaTime);

		// Glide slope tracking
		float GlideAngleRad = FMath::DegreesToRadians(AssignedRunway->GlideSlopeDeg);
		float DesiredSinkRate = -AeroComp->AirspeedMPS * FMath::Sin(GlideAngleRad);
		float SinkError = DesiredSinkRate - AeroComp->VerticalSpeedMPS;
		float PitchCmd = GlideSlopePID.Update(SinkError, DeltaTime);
		AeroComp->ElevatorInput = FMath::Clamp(PitchCmd, -1.0f, 1.0f);
	}

	if (GetRadioAltitude() <= FlareAltitudeM)
	{
		SetPhase(EFlightPhase::Flare);
	}
}

void UFighterAutopilotComponent::TickFlare(float DeltaTime)
{
	ApplyWingsLevel(DeltaTime);

	// Reduce sink rate to touchdown value
	float SinkError = -TouchdownSinkRateMPS - AeroComp->VerticalSpeedMPS;
	float PitchCmd = GlideSlopePID.Update(SinkError, DeltaTime);
	AeroComp->ElevatorInput = FMath::Clamp(PitchCmd, -1.0f, 1.0f);
	AeroComp->ThrottleInput = FMath::Max(AeroComp->ThrottleInput - 0.5f * DeltaTime, 0.0f);

	if (GetRadioAltitude() <= 0.5f)
	{
		SetPhase(EFlightPhase::Touchdown);
	}
}

void UFighterAutopilotComponent::TickTouchdown(float DeltaTime)
{
	AeroComp->ThrottleInput = 0.0f;
	AeroComp->bAfterburner = false;
	AeroComp->bBrakeEngaged = true;
	AeroComp->ElevatorInput = 0.0f;
	AeroComp->FlapDeflectionDeg = 0.0f;

	if (AeroComp->AirspeedMPS <= TaxiSpeedMPS)
	{
		SetPhase(EFlightPhase::RollOut);
	}
}

void UFighterAutopilotComponent::TickRollOut(float DeltaTime)
{
	AeroComp->ThrottleInput = 0.0f;
	AeroComp->bBrakeEngaged = true;

	if (AeroComp->AirspeedMPS < 1.0f)
	{
		SetPhase(EFlightPhase::Idle);
	}
}

// --- PID helpers ---

void UFighterAutopilotComponent::ApplySpeedHold(float TargetMPS, float DeltaTime)
{
	float Error = TargetMPS - AeroComp->AirspeedMPS;
	float Cmd = SpeedPID.Update(Error, DeltaTime);
	AeroComp->ThrottleInput = FMath::Clamp(AeroComp->ThrottleInput + Cmd, 0.0f, 1.0f);
}

void UFighterAutopilotComponent::ApplyAltitudeHold(float TargetM, float DeltaTime)
{
	float AltError = TargetM - AeroComp->AltitudeM;
	float DesiredPitch = AltitudePID.Update(AltError, DeltaTime);
	DesiredPitch = FMath::Clamp(DesiredPitch, -20.0f, 20.0f);
	ApplyPitchHold(DesiredPitch, DeltaTime);
}

void UFighterAutopilotComponent::ApplyHeadingHold(float TargetDeg, float DeltaTime)
{
	float Error = FMath::FindDeltaAngleDegrees(AeroComp->YawDeg, TargetDeg);
	float Cmd = HeadingPID.Update(Error, DeltaTime);
	AeroComp->RudderInput = FMath::Clamp(Cmd, -1.0f, 1.0f);
}

void UFighterAutopilotComponent::ApplyPitchHold(float TargetDeg, float DeltaTime)
{
	float Error = TargetDeg - AeroComp->PitchDeg;
	float Cmd = PitchPID.Update(Error, DeltaTime);
	AeroComp->ElevatorInput = FMath::Clamp(Cmd, -1.0f, 1.0f);
}

void UFighterAutopilotComponent::ApplyWingsLevel(float DeltaTime)
{
	float Error = -AeroComp->RollDeg;
	float Cmd = RollPID.Update(Error, DeltaTime);
	AeroComp->AileronInput = FMath::Clamp(Cmd, -1.0f, 1.0f);
}

float UFighterAutopilotComponent::GetRadioAltitude() const
{
	return AeroComp ? AeroComp->AltitudeM : 0.0f;
}
