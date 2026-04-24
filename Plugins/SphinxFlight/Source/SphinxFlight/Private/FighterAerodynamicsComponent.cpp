#include "FighterAerodynamicsComponent.h"
#include "FlightGeoUtils.h"
#include "SphinxFlightModule.h"

UFighterAerodynamicsComponent::UFighterAerodynamicsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UFighterAerodynamicsComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                                  FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickPhysics(DeltaTime);
}

void UFighterAerodynamicsComponent::TickPhysics(float DeltaTime)
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	const FTransform& WorldTF = Owner->GetActorTransform();
	const FRotator Rotation = Owner->GetActorRotation();

	AltitudeM = FFlightGeoUtils::UnrealToMeters(Owner->GetActorLocation().Z);
	AirspeedMPS = VelocityMPS.Size();

	const FVector Forward = WorldTF.GetUnitAxis(EAxis::X);
	const FVector Right = WorldTF.GetUnitAxis(EAxis::Y);
	const FVector Up = WorldTF.GetUnitAxis(EAxis::Z);

	FVector LocalVel = WorldTF.InverseTransformVectorNoScale(VelocityMPS);
	const float AlphaRad = ComputeAngleOfAttack(LocalVel);
	const float BetaRad = ComputeSideslipAngle(LocalVel);

	AngleOfAttackDeg = FMath::RadiansToDegrees(AlphaRad);
	SideslipAngleDeg = FMath::RadiansToDegrees(BetaRad);
	VerticalSpeedMPS = VelocityMPS.Z;
	PitchDeg = Rotation.Pitch;
	RollDeg = Rotation.Roll;
	YawDeg = Rotation.Yaw;

	const float Rho = GetAirDensity(AltitudeM);
	const float V = FMath::Max(AirspeedMPS, 0.1f);
	const float Q = 0.5f * Rho * V * V;

	// Lift
	const float Cl = EvaluateLiftCoefficient(AlphaRad);
	const float LiftN = Q * WingAreaM2 * Cl;

	// Drag
	const float Cd = EvaluateDragCoefficient(Cl);
	float GearDragAdd = bGearDown ? 0.02f : 0.0f;
	float FlapDragAdd = FlapDeflectionDeg / 40.0f * 0.03f;
	const float TotalCd = Cd + GearDragAdd + FlapDragAdd;
	const float DragN = Q * WingAreaM2 * TotalCd;

	// Thrust
	const float MaxT = bAfterburner ? MaxAfterburnerThrustN : MaxThrustN;
	const float ThrustN = MaxT * FMath::Clamp(ThrottleInput, 0.0f, 1.0f);

	// Force directions in world space
	FVector VelDir = VelocityMPS.GetSafeNormal();
	if (VelDir.IsNearlyZero()) VelDir = Forward;

	FVector LiftDir = FVector::CrossProduct(VelDir, Right).GetSafeNormal();
	if (FVector::DotProduct(LiftDir, FVector::UpVector) < 0.0f)
		LiftDir = -LiftDir;

	const FVector GravityN = FVector(0.0f, 0.0f, -9.81f * EmptyMassKg);

	FVector TotalForceN = Forward * ThrustN
		+ LiftDir * LiftN
		- VelDir * DragN
		+ GravityN;

	// Brake on ground
	if (bBrakeEngaged && AltitudeM < 1.0f)
	{
		float BrakeForce = FMath::Min(AirspeedMPS * EmptyMassKg * 2.0f, 50000.0f);
		TotalForceN -= VelDir * BrakeForce;
	}

	// Ground constraint
	if (Owner->GetActorLocation().Z <= 0.0f && TotalForceN.Z < 0.0f)
	{
		TotalForceN.Z = 0.0f;
		if (VelocityMPS.Z < 0.0f) VelocityMPS.Z = 0.0f;
	}

	// Integrate linear
	FVector AccelMPS2 = TotalForceN / EmptyMassKg;
	VelocityMPS += AccelMPS2 * DeltaTime;
	GForce = AccelMPS2.Size() / 9.81f;

	FVector DeltaPosCm = VelocityMPS * DeltaTime * FFlightGeoUtils::CmPerMeter;
	Owner->AddActorWorldOffset(DeltaPosCm, false, nullptr, ETeleportType::TeleportPhysics);

	// Ground clamp
	FVector Pos = Owner->GetActorLocation();
	if (Pos.Z < 0.0f)
	{
		Pos.Z = 0.0f;
		Owner->SetActorLocation(Pos);
	}

	// Moments
	FVector MomentNm = ComputeControlMoments(Q, DeltaTime);

	// Angular acceleration (simplified — no cross-coupling)
	FVector AngAccel(
		MomentNm.X / IxxRoll,
		MomentNm.Y / IyyPitch,
		MomentNm.Z / IzzYaw
	);

	// Angular damping
	const float Damping = 2.0f;
	AngularVelocityRadPS += (AngAccel - AngularVelocityRadPS * Damping) * DeltaTime;

	FRotator DeltaRot(
		FMath::RadiansToDegrees(AngularVelocityRadPS.Y * DeltaTime),
		FMath::RadiansToDegrees(AngularVelocityRadPS.Z * DeltaTime),
		FMath::RadiansToDegrees(AngularVelocityRadPS.X * DeltaTime)
	);
	Owner->AddActorWorldRotation(DeltaRot, false, nullptr, ETeleportType::TeleportPhysics);
}

float UFighterAerodynamicsComponent::GetAirDensity(float AltitudeMeters) const
{
	// ISA standard atmosphere (troposphere approximation)
	const float T0 = 288.15f;
	const float LapseRate = 0.0065f;
	const float P0 = 101325.0f;
	const float R = 287.05f;
	const float g = 9.81f;

	float T = T0 - LapseRate * AltitudeMeters;
	T = FMath::Max(T, 216.65f);
	float P = P0 * FMath::Pow(T / T0, g / (LapseRate * R));
	return P / (R * T);
}

float UFighterAerodynamicsComponent::EvaluateLiftCoefficient(float AlphaRad) const
{
	float AlphaDeg = FMath::RadiansToDegrees(AlphaRad);

	if (LiftCurve)
	{
		return LiftCurve->GetFloatValue(AlphaDeg);
	}

	// Default: linear region with stall
	// Cl_alpha ~ 2*pi per radian in thin airfoil theory → ~0.11 per degree
	// Stall at ~15 deg, Cl_max ~1.6
	if (FMath::Abs(AlphaDeg) < 15.0f)
	{
		return AlphaDeg * 0.11f;
	}
	// Post-stall: gradual Cl drop
	float Sign = FMath::Sign(AlphaDeg);
	float AbsAlpha = FMath::Abs(AlphaDeg);
	float ClMax = 1.6f;
	float Decay = FMath::Clamp((AbsAlpha - 15.0f) / 20.0f, 0.0f, 1.0f);
	return Sign * ClMax * (1.0f - Decay * 0.6f);
}

float UFighterAerodynamicsComponent::EvaluateDragCoefficient(float Cl) const
{
	if (DragCurve)
	{
		return DragCurve->GetFloatValue(Cl);
	}
	return Cd0 + InducedDragFactor * Cl * Cl;
}

FVector UFighterAerodynamicsComponent::ComputeControlMoments(float Q, float DeltaTime) const
{
	float Area = WingAreaM2;
	float Span = WingSpanM;
	float Chord = Area / Span;

	// Control effectiveness scales with dynamic pressure
	float PitchMoment = ElevatorInput * Q * Area * Chord * 0.8f;
	float RollMoment = AileronInput * Q * Area * Span * 0.15f;
	float YawMoment = RudderInput * Q * Area * Span * 0.08f;

	// Pitch: positive elevator input → nose up → negative Y moment in body frame
	// Roll: positive aileron → right roll → positive X
	// Yaw: positive rudder → nose right → positive Z
	return FVector(RollMoment, -PitchMoment, YawMoment);
}

float UFighterAerodynamicsComponent::ComputeAngleOfAttack(const FVector& LocalVelocity) const
{
	if (FMath::Abs(LocalVelocity.X) < 0.01f) return 0.0f;
	return FMath::Atan2(-LocalVelocity.Z, LocalVelocity.X);
}

float UFighterAerodynamicsComponent::ComputeSideslipAngle(const FVector& LocalVelocity) const
{
	float Speed = LocalVelocity.Size();
	if (Speed < 0.01f) return 0.0f;
	return FMath::Asin(FMath::Clamp(LocalVelocity.Y / Speed, -1.0f, 1.0f));
}
