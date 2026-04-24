#include "FlightTelemetryComponent.h"
#include "FighterAerodynamicsComponent.h"
#include "FighterAutopilotComponent.h"
#include "FlightGeoUtils.h"

static constexpr float MPS_TO_KNOTS = 1.94384f;
static constexpr float M_TO_FT = 3.28084f;

UFlightTelemetryComponent::UFlightTelemetryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UFlightTelemetryComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                              FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!AeroComp)
	{
		AeroComp = GetOwner()->FindComponentByClass<UFighterAerodynamicsComponent>();
	}
	if (!AutopilotComp)
	{
		AutopilotComp = GetOwner()->FindComponentByClass<UFighterAutopilotComponent>();
	}

	if (!AeroComp) return;

	Data.AirspeedMPS = AeroComp->AirspeedMPS;
	Data.AirspeedKnots = Data.AirspeedMPS * MPS_TO_KNOTS;
	Data.AltitudeM = AeroComp->AltitudeM;
	Data.AltitudeFt = Data.AltitudeM * M_TO_FT;
	Data.VerticalSpeedFPM = AeroComp->VerticalSpeedMPS * M_TO_FT * 60.0f;
	Data.HeadingDeg = AeroComp->YawDeg;
	Data.PitchDeg = AeroComp->PitchDeg;
	Data.RollDeg = AeroComp->RollDeg;
	Data.AngleOfAttackDeg = AeroComp->AngleOfAttackDeg;
	Data.GForce = AeroComp->GForce;
	Data.ThrottlePercent = AeroComp->ThrottleInput * 100.0f;
	Data.bGearDown = AeroComp->bGearDown;
	Data.bAfterburner = AeroComp->bAfterburner;
	Data.FlapsDeg = AeroComp->FlapDeflectionDeg;

	double OutLon, OutLat, OutAlt;
	FFlightGeoUtils::WorldToLLH(GetOwner()->GetActorLocation(), OriginLonDeg, OriginLatDeg,
	                            OutLon, OutLat, OutAlt);
	Data.LongitudeDeg = OutLon;
	Data.LatitudeDeg = OutLat;

	if (AutopilotComp)
	{
		Data.bAutopilotActive = (AutopilotComp->CurrentPhase != EFlightPhase::ManualOverride);

		static const TMap<EFlightPhase, FString> PhaseNames = {
			{EFlightPhase::Idle, TEXT("Idle")},
			{EFlightPhase::TakeoffRoll, TEXT("Takeoff Roll")},
			{EFlightPhase::Rotate, TEXT("Rotate")},
			{EFlightPhase::Climb, TEXT("Climb")},
			{EFlightPhase::Cruise, TEXT("Cruise")},
			{EFlightPhase::Descent, TEXT("Descent")},
			{EFlightPhase::FinalApproach, TEXT("Final Approach")},
			{EFlightPhase::Flare, TEXT("Flare")},
			{EFlightPhase::Touchdown, TEXT("Touchdown")},
			{EFlightPhase::RollOut, TEXT("Roll Out")},
			{EFlightPhase::ManualOverride, TEXT("Manual")},
		};

		if (const FString* Name = PhaseNames.Find(AutopilotComp->CurrentPhase))
		{
			Data.FlightPhase = *Name;
		}
	}
}
