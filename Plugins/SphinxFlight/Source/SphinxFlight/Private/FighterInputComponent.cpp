#include "FighterInputComponent.h"
#include "FighterAerodynamicsComponent.h"
#include "FighterAutopilotComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "SphinxFlightModule.h"

UFighterInputComponent::UFighterInputComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UFighterInputComponent::BeginPlay()
{
	Super::BeginPlay();

	AeroComp = GetOwner()->FindComponentByClass<UFighterAerodynamicsComponent>();
	AutopilotComp = GetOwner()->FindComponentByClass<UFighterAutopilotComponent>();

	SetupInputBindings();
}

void UFighterInputComponent::SetupInputBindings()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn) return;

	APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
	if (!PC) return;

	if (FlightMappingContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(FlightMappingContext, 0);
		}
	}

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(OwnerPawn->InputComponent);
	if (!EIC) return;

	if (IA_Pitch)             EIC->BindAction(IA_Pitch, ETriggerEvent::Triggered, this, &UFighterInputComponent::OnPitch);
	if (IA_Roll)              EIC->BindAction(IA_Roll, ETriggerEvent::Triggered, this, &UFighterInputComponent::OnRoll);
	if (IA_Yaw)               EIC->BindAction(IA_Yaw, ETriggerEvent::Triggered, this, &UFighterInputComponent::OnYaw);
	if (IA_Throttle)          EIC->BindAction(IA_Throttle, ETriggerEvent::Triggered, this, &UFighterInputComponent::OnThrottle);
	if (IA_ToggleGear)        EIC->BindAction(IA_ToggleGear, ETriggerEvent::Started, this, &UFighterInputComponent::OnToggleGear);
	if (IA_ToggleAfterburner) EIC->BindAction(IA_ToggleAfterburner, ETriggerEvent::Started, this, &UFighterInputComponent::OnToggleAfterburner);
	if (IA_ToggleAutopilot)   EIC->BindAction(IA_ToggleAutopilot, ETriggerEvent::Started, this, &UFighterInputComponent::OnToggleAutopilot);
	if (IA_Flaps)             EIC->BindAction(IA_Flaps, ETriggerEvent::Triggered, this, &UFighterInputComponent::OnFlaps);
	if (IA_Brake)             EIC->BindAction(IA_Brake, ETriggerEvent::Triggered, this, &UFighterInputComponent::OnBrake);

	UE_LOG(LogSphinxFlight, Log, TEXT("Flight input bindings set up"));
}

void UFighterInputComponent::OnPitch(const FInputActionValue& Value)
{
	if (!AeroComp || (AutopilotComp && AutopilotComp->CurrentPhase != EFlightPhase::ManualOverride)) return;
	AeroComp->ElevatorInput = Value.Get<float>();
}

void UFighterInputComponent::OnRoll(const FInputActionValue& Value)
{
	if (!AeroComp || (AutopilotComp && AutopilotComp->CurrentPhase != EFlightPhase::ManualOverride)) return;
	AeroComp->AileronInput = Value.Get<float>();
}

void UFighterInputComponent::OnYaw(const FInputActionValue& Value)
{
	if (!AeroComp || (AutopilotComp && AutopilotComp->CurrentPhase != EFlightPhase::ManualOverride)) return;
	AeroComp->RudderInput = Value.Get<float>();
}

void UFighterInputComponent::OnThrottle(const FInputActionValue& Value)
{
	if (!AeroComp || (AutopilotComp && AutopilotComp->CurrentPhase != EFlightPhase::ManualOverride)) return;
	AeroComp->ThrottleInput = FMath::Clamp(AeroComp->ThrottleInput + Value.Get<float>() * 0.02f, 0.0f, 1.0f);
}

void UFighterInputComponent::OnToggleGear(const FInputActionValue& Value)
{
	if (!AeroComp) return;
	AeroComp->bGearDown = !AeroComp->bGearDown;
	UE_LOG(LogSphinxFlight, Log, TEXT("Gear: %s"), AeroComp->bGearDown ? TEXT("DOWN") : TEXT("UP"));
}

void UFighterInputComponent::OnToggleAfterburner(const FInputActionValue& Value)
{
	if (!AeroComp) return;
	AeroComp->bAfterburner = !AeroComp->bAfterburner;
	UE_LOG(LogSphinxFlight, Log, TEXT("Afterburner: %s"), AeroComp->bAfterburner ? TEXT("ON") : TEXT("OFF"));
}

void UFighterInputComponent::OnToggleAutopilot(const FInputActionValue& Value)
{
	if (AutopilotComp)
	{
		AutopilotComp->ToggleManualOverride();
	}
}

void UFighterInputComponent::OnFlaps(const FInputActionValue& Value)
{
	if (!AeroComp || (AutopilotComp && AutopilotComp->CurrentPhase != EFlightPhase::ManualOverride)) return;
	AeroComp->FlapDeflectionDeg = FMath::Clamp(AeroComp->FlapDeflectionDeg + Value.Get<float>() * 5.0f, 0.0f, 40.0f);
}

void UFighterInputComponent::OnBrake(const FInputActionValue& Value)
{
	if (!AeroComp) return;
	AeroComp->bBrakeEngaged = Value.Get<float>() > 0.5f;
}
