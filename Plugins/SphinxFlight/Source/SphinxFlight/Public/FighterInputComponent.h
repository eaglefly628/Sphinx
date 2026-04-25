#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputActionValue.h"
#include "FighterInputComponent.generated.h"

class UInputAction;
class UInputMappingContext;
class UFighterAerodynamicsComponent;
class UFighterAutopilotComponent;

UCLASS(ClassGroup=(SphinxFlight), meta=(BlueprintSpawnableComponent))
class SPHINXFLIGHT_API UFighterInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFighterInputComponent();

	virtual void BeginPlay() override;

	// --- Input Assets (set in Blueprint or Data Asset) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
	TObjectPtr<UInputMappingContext> FlightMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
	TObjectPtr<UInputAction> IA_Pitch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
	TObjectPtr<UInputAction> IA_Roll;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
	TObjectPtr<UInputAction> IA_Yaw;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
	TObjectPtr<UInputAction> IA_Throttle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
	TObjectPtr<UInputAction> IA_ToggleGear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
	TObjectPtr<UInputAction> IA_ToggleAfterburner;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
	TObjectPtr<UInputAction> IA_ToggleAutopilot;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
	TObjectPtr<UInputAction> IA_Flaps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
	TObjectPtr<UInputAction> IA_Brake;

	// --- Weapons (mirrors legacy "shoot_bullet" / "shoot_rocket") ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input|Weapons")
	TObjectPtr<UInputAction> IA_FireGun;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input|Weapons")
	TObjectPtr<UInputAction> IA_FireMissile;

	// --- Camera / Look (mirrors legacy "look" / "LookUp" / "Turn") ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input|Camera")
	TObjectPtr<UInputAction> IA_LookMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input|Camera")
	TObjectPtr<UInputAction> IA_LookAxis;

	UPROPERTY(BlueprintReadOnly, Category="Input|Camera")
	bool bLookModeActive = false;

	UPROPERTY(BlueprintReadOnly, Category="Input|Camera")
	FVector2D LookAxisDelta = FVector2D::ZeroVector;

private:
	void SetupInputBindings();

	void OnPitch(const FInputActionValue& Value);
	void OnRoll(const FInputActionValue& Value);
	void OnYaw(const FInputActionValue& Value);
	void OnThrottle(const FInputActionValue& Value);
	void OnToggleGear(const FInputActionValue& Value);
	void OnToggleAfterburner(const FInputActionValue& Value);
	void OnToggleAutopilot(const FInputActionValue& Value);
	void OnFlaps(const FInputActionValue& Value);
	void OnBrake(const FInputActionValue& Value);
	void OnFireGun(const FInputActionValue& Value);
	void OnFireGunReleased(const FInputActionValue& Value);
	void OnFireMissile(const FInputActionValue& Value);
	void OnLookModePressed(const FInputActionValue& Value);
	void OnLookModeReleased(const FInputActionValue& Value);
	void OnLookAxis(const FInputActionValue& Value);

	UPROPERTY()
	TObjectPtr<UFighterAerodynamicsComponent> AeroComp;

	UPROPERTY()
	TObjectPtr<UFighterAutopilotComponent> AutopilotComp;
};
