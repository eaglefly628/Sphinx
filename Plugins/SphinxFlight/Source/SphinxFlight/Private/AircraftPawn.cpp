#include "AircraftPawn.h"
#include "FighterAerodynamicsComponent.h"
#include "FighterAutopilotComponent.h"
#include "FighterInputComponent.h"
#include "FlightTelemetryComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "SphinxFlightModule.h"

AAircraftPawn::AAircraftPawn()
{
	PrimaryActorTick.bCanEverTick = false;

	AircraftMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AircraftMesh"));
	RootComponent = AircraftMesh;
	AircraftMesh->SetSimulatePhysics(false);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 2000.0f;
	CameraBoom->SetRelativeRotation(FRotator(-20.0f, 0.0f, 0.0f));
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 3.0f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);

	Aerodynamics = CreateDefaultSubobject<UFighterAerodynamicsComponent>(TEXT("Aerodynamics"));
	Autopilot = CreateDefaultSubobject<UFighterAutopilotComponent>(TEXT("Autopilot"));
	FighterInput = CreateDefaultSubobject<UFighterInputComponent>(TEXT("FighterInput"));
	Telemetry = CreateDefaultSubobject<UFlightTelemetryComponent>(TEXT("Telemetry"));
}

void AAircraftPawn::BeginPlay()
{
	Super::BeginPlay();

	Telemetry->OriginLonDeg = OriginLonDeg;
	Telemetry->OriginLatDeg = OriginLatDeg;

	UE_LOG(LogSphinxFlight, Log, TEXT("===== AircraftPawn START ====="));
	UE_LOG(LogSphinxFlight, Log, TEXT("Origin: Lon=%.6f Lat=%.6f"), OriginLonDeg, OriginLatDeg);
}

void AAircraftPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}
