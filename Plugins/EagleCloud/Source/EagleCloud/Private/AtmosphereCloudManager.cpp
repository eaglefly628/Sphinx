// AtmosphereCloudManager.cpp
//
// LOD blender between macro shell and UDS volumetric. UDS access is delegated
// to AEagleCloudUDSBridge — no string reflection here.
//
#include "AtmosphereCloudManager.h"
#include "EagleCloudModule.h"
#include "SatelliteCloudFeeder.h"
#include "EagleCloudUDSBridge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Components/SkyAtmosphereComponent.h"

AAtmosphereCloudManager::AAtmosphereCloudManager()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AAtmosphereCloudManager::BeginPlay()
{
    Super::BeginPlay();

    // Spare the user one drag: if Bridge wasn't set on Manager, borrow from Feeder.
    if (!Bridge && Feeder)
    {
        Bridge = Feeder->Bridge;
    }

    ensureMsgf(Bridge,
               TEXT("AAtmosphereCloudManager: no Bridge (neither Manager.Bridge nor Feeder->Bridge). "
                    "Sky Mode management will be skipped. Drop a BP_EagleCloudBridge into the level "
                    "and assign it to Feeder.Bridge or Manager.Bridge."));
}

void AAtmosphereCloudManager::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    const FVector CameraLoc = GetCameraWorldLocation();
    CurrentAltitudeKm = (CameraLoc.Z - GroundReferenceZ) * 0.00001;  // cm -> km

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
    const float UDSDensity = 1.f - T;

    CurrentMacroAlpha = MacroAlpha;
    CurrentUDSDensity = UDSDensity;

    ApplyBlend(MacroAlpha, UDSDensity, CurrentAltitudeKm);
    ApplySkyMode(CurrentAltitudeKm);
    ApplyAtmosphereMode(CurrentAltitudeKm, CameraLoc);
}

FVector AAtmosphereCloudManager::GetCameraWorldLocation() const
{
    UWorld* World = GetWorld();
    if (!World) return FVector::ZeroVector;

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC || !PC->PlayerCameraManager) return FVector::ZeroVector;

    return PC->PlayerCameraManager->GetCameraLocation();  // cm
}

double AAtmosphereCloudManager::ComputeCameraAltitudeKm() const
{
    return (GetCameraWorldLocation().Z - GroundReferenceZ) * 0.00001;  // cm -> km
}

void AAtmosphereCloudManager::ApplyBlend(float MacroAlpha, float UDSDensity, double AltitudeKm)
{
    // 1. Drive UDS coverage strength through the feeder's painted-coverage params.
    //    Feeder.SyncPropertiesToUDS pushes the new values through the bridge.
    if (Feeder)
    {
        Feeder->AffectsGlobalValues = UDSDensity;
        Feeder->SyncPropertiesToUDS();
    }

    // 2. Toggle macro shell visibility. Below low altitude, hide entirely to skip drawcall.
    if (MacroShellActor)
    {
        const bool bShouldShow = MacroAlpha > 0.001f;
        if (MacroShellActor->IsHidden() == bShouldShow)
        {
            MacroShellActor->SetActorHiddenInGame(!bShouldShow);
        }
    }

    // 3. Write blend factors to MPC for material consumption.
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
    if (!bManageUDSSkyMode || !Bridge) return;

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
        // First tick inside the dead band — pick a side based on altitude.
        DesiredMode = (AltitudeKm >= HighAltitudeKm) ? SpaceMode : VolumetricMode;
    }

    if (DesiredMode == LastAppliedSkyMode) return;

    Bridge->SetSkyMode(DesiredMode);
    LastAppliedSkyMode = DesiredMode;

    UE_LOG(LogEagleCloud, Log,
           TEXT("UDS Sky Mode -> %d at altitude %.1f km (%s)"),
           DesiredMode, AltitudeKm,
           DesiredMode == SpaceMode ? TEXT("Space") : TEXT("Volumetric"));
}

void AAtmosphereCloudManager::ApplyAtmosphereMode(double AltitudeKm, const FVector& CameraLoc)
{
    if (!bManageAtmosphereSwitch) return;

    // 用 SkyMode hysteresis 复用 — 切换点相同（atmosphere 跟 SkyMode 同步切）
    int32 DesiredMode = LastAppliedAtmosphereMode;
    if (AltitudeKm >= HighAltitudeKm + SkyModeSwitchHysteresisKm)
    {
        DesiredMode = 1;  // Cesium
    }
    else if (AltitudeKm <= HighAltitudeKm - SkyModeSwitchHysteresisKm)
    {
        DesiredMode = 0;  // UDS
    }
    else if (LastAppliedAtmosphereMode == -1)
    {
        DesiredMode = (AltitudeKm >= HighAltitudeKm) ? 1 : 0;
    }

    const bool bUseUDS = (DesiredMode == 0);

    // === UDS atmosphere 跟相机（关键：再次激活时位置不会留在老平面）===
    // 即使 mode 没变，每 tick 都同步位置，确保 atmosphere 永远以相机为中心。
    if (bMakeUDSFollowCamera && bUseUDS && UDSActorWithSkyAtmosphere)
    {
        UDSActorWithSkyAtmosphere->SetActorLocation(CameraLoc, /*bSweep=*/false);
    }

    // === Mode 切换（visibility toggle）===
    if (DesiredMode == LastAppliedAtmosphereMode) return;

    if (UDSActorWithSkyAtmosphere)
    {
        if (USkyAtmosphereComponent* SkyAtmos =
            UDSActorWithSkyAtmosphere->FindComponentByClass<USkyAtmosphereComponent>())
        {
            SkyAtmos->SetVisibility(bUseUDS, /*bPropagateToChildren=*/true);
        }
    }

    if (CesiumSunSkyActor)
    {
        CesiumSunSkyActor->SetActorHiddenInGame(bUseUDS);
        CesiumSunSkyActor->SetActorEnableCollision(!bUseUDS);
    }

    // 切回 UDS 时重置 painting state + coverage window —— 避免 Space 期间状态丢失
    if (bUseUDS && Bridge && Feeder)
    {
        Bridge->SetPaintingState(true, true, Feeder->PaintedOpacity, Feeder->AffectsGlobalValues);
        Bridge->SetCoverageWindow(FVector2D(CameraLoc.X, CameraLoc.Y));
    }

    UE_LOG(LogEagleCloud, Log,
           TEXT("Atmosphere mode -> %s at altitude %.1f km"),
           bUseUDS ? TEXT("UDS (snap to camera)") : TEXT("Cesium"), AltitudeKm);

    LastAppliedAtmosphereMode = DesiredMode;
}
