// AtmosphereCloudManager.cpp
#include "AtmosphereCloudManager.h"
#include "EagleCloudModule.h"
#include "SatelliteCloudFeeder.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"

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
    ApplySkyMode(CurrentAltitudeKm);
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
        // Sync only float props to UDS — does NOT redraw the RT.
        // The RT blit is controlled by Feeder's own RefreshIntervalSeconds timer.
        Feeder->SyncPropertiesToUDS();
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
            if (Feeder)
            {
                UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, MPC_NoDataThreshold,
                                                                 Feeder->NoDataThreshold);
            }
        }
    }
}

void AAtmosphereCloudManager::ApplySkyMode(double AltitudeKm)
{
    if (!bManageUDSSkyMode) return;

    // Hysteresis around HighAltitudeKm: switch up at High+H, switch down at High-H.
    // Inside the dead band we keep LastAppliedSkyMode untouched.
    const int32 SpaceMode      = static_cast<int32>(UDS_SkyMode_Space);
    const int32 VolumetricMode = static_cast<int32>(UDS_SkyMode_Volumetric);

    int32 DesiredMode = LastAppliedSkyMode;
    if (AltitudeKm >= HighAltitudeKm + SkyModeSwitchHysteresisKm)
    {
        DesiredMode = SpaceMode;
    }
    else if (AltitudeKm <= HighAltitudeKm - SkyModeSwitchHysteresisKm)
    {
        DesiredMode = VolumetricMode;
    }
    else if (LastAppliedSkyMode == -1)
    {
        // First tick and we're inside the dead band — pick a side based on altitude.
        DesiredMode = (AltitudeKm >= HighAltitudeKm) ? SpaceMode : VolumetricMode;
    }

    if (DesiredMode == LastAppliedSkyMode) return;

    if (!CachedUDS.IsValid())
    {
        CachedUDS = FindUDSActor();
        if (!CachedUDS.IsValid())
        {
            // UDS not in level yet — try again next tick
            return;
        }
    }

    AActor* UDS = CachedUDS.Get();
    FProperty* Prop = UDS->GetClass()->FindPropertyByName(UDS_SkyModeProperty);
    if (!Prop)
    {
        UE_LOG(LogEagleCloud, Warning,
               TEXT("UDS property '%s' not found — disabling sky mode management. ")
               TEXT("Verify UDS_SkyModeProperty matches the actual property name."),
               *UDS_SkyModeProperty.ToString());
        bManageUDSSkyMode = false;
        return;
    }

    bool bWritten = false;
    if (FByteProperty* BP = CastField<FByteProperty>(Prop))
    {
        BP->SetPropertyValue_InContainer(UDS, static_cast<uint8>(DesiredMode));
        bWritten = true;
    }
    else if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
    {
        if (FNumericProperty* Underlying = EP->GetUnderlyingProperty())
        {
            void* ValuePtr = EP->ContainerPtrToValuePtr<void>(UDS);
            if (ValuePtr)
            {
                Underlying->SetIntPropertyValue(ValuePtr, static_cast<int64>(DesiredMode));
                bWritten = true;
            }
        }
    }

    if (bWritten)
    {
        LastAppliedSkyMode = DesiredMode;
        UE_LOG(LogEagleCloud, Log,
               TEXT("UDS Sky Mode -> %d at altitude %.1f km (%s)"),
               DesiredMode, AltitudeKm,
               DesiredMode == SpaceMode ? TEXT("Space") : TEXT("Volumetric"));
    }
    else
    {
        UE_LOG(LogEagleCloud, Warning,
               TEXT("UDS property '%s' is neither Byte nor Enum — cannot write sky mode."),
               *UDS_SkyModeProperty.ToString());
        bManageUDSSkyMode = false;
    }
}

AActor* AAtmosphereCloudManager::FindUDSActor() const
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), Found);
    for (AActor* A : Found)
    {
        if (A && A->GetClass()->GetName().Contains(TEXT("Ultra_Dynamic_Sky")))
        {
            return A;
        }
    }
    return nullptr;
}
