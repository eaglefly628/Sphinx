// AtmosphereCloudManager.cpp
#include "AtmosphereCloudManager.h"
#include "EagleCloudModule.h"
#include "SatelliteCloudFeeder.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"

AAtmosphereCloudManager::AAtmosphereCloudManager()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AAtmosphereCloudManager::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    CurrentAltitudeKm = ComputeCameraAltitudeKm();

    // Compute blend factor T in [0,1] across the transition band
    float T = 0.f;
    if (HighAltitudeKm > LowAltitudeKm)
    {
        T = static_cast<float>(
            (CurrentAltitudeKm - LowAltitudeKm) /
            (HighAltitudeKm - LowAltitudeKm));
        T = FMath::Clamp(T, 0.f, 1.f);
        if (bUseSmoothstep)
        {
            // smoothstep: 3t^2 - 2t^3
            T = T * T * (3.f - 2.f * T);
        }
    }
    else if (CurrentAltitudeKm > HighAltitudeKm)
    {
        T = 1.f;
    }

    const float MacroAlpha = T;
    // UDS density inversely fades — but stays at 1 below LowAltitudeKm
    const float UDSDensity = 1.f - T;

    CurrentMacroAlpha = MacroAlpha;
    CurrentUDSDensity = UDSDensity;

    ApplyBlend(MacroAlpha, UDSDensity, CurrentAltitudeKm);
}

double AAtmosphereCloudManager::ComputeCameraAltitudeKm() const
{
    UWorld* World = GetWorld();
    if (!World) return 0.0;

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC || !PC->PlayerCameraManager) return 0.0;

    const double CamZ = PC->PlayerCameraManager->GetCameraLocation().Z;  // cm
    return (CamZ - GroundReferenceZ) * 0.00001;  // cm -> km
}

void AAtmosphereCloudManager::ApplyBlend(float MacroAlpha, float UDSDensity, double AltitudeKm)
{
    // 1. Drive UDS through the feeder. Painted Coverage Affects Global Values
    //    determines how much the painted RT replaces UDS's own cloud coverage.
    //    Above HighAltitudeKm we set it to ~UDSDensity (=0) so volumetric clouds
    //    fade out — combined with UDS's own cloud coverage going to 0 from MPC
    //    (handled by user material logic), volumetric is effectively off.
    if (Feeder)
    {
        Feeder->AffectsGlobalValues = UDSDensity;
        // Push the new value to UDS by reapplying (cheap — just sets the float prop)
        Feeder->ApplyToUDS();
    }

    // 2. Toggle macro shell visibility. Below low altitude, hide entirely to
    //    skip drawcall (Gemini's optimization tip).
    if (MacroShellActor)
    {
        const bool bShouldShow = MacroAlpha > 0.001f;
        if (MacroShellActor->IsHidden() == bShouldShow)
        {
            MacroShellActor->SetActorHiddenInGame(!bShouldShow);
        }
    }

    // 3. Write blend factors to MPC for material consumption (sphere shell alpha,
    //    optional UDS density override in cloud material).
    if (MPC)
    {
        UWorld* World = GetWorld();
        if (World)
        {
            UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, MPC_MacroAlpha, MacroAlpha);
            UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, MPC_UDSDensity, UDSDensity);
            UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, MPC_AltitudeKm,
                                                             static_cast<float>(AltitudeKm));
        }
    }
}
