#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "AircraftPawn.generated.h"

class UFighterAerodynamicsComponent;
class UFighterAutopilotComponent;
class UFighterInputComponent;
class UFlightTelemetryComponent;
class UCameraComponent;
class USpringArmComponent;

UCLASS(BlueprintType)
class SPHINXFLIGHT_API AAircraftPawn : public APawn
{
	GENERATED_BODY()

public:
	AAircraftPawn();

	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// --- Components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aircraft")
	TObjectPtr<UStaticMeshComponent> AircraftMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aircraft")
	TObjectPtr<UFighterAerodynamicsComponent> Aerodynamics;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aircraft")
	TObjectPtr<UFighterAutopilotComponent> Autopilot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aircraft")
	TObjectPtr<UFighterInputComponent> FighterInput;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aircraft")
	TObjectPtr<UFlightTelemetryComponent> Telemetry;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aircraft|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aircraft|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	// --- World origin for coordinate conversion ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aircraft|GIS")
	double OriginLonDeg = 113.376;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aircraft|GIS")
	double OriginLatDeg = 22.006;
};
