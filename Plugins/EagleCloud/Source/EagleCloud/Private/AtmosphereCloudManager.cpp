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
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/VolumetricCloudComponent.h"

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

    if (DesiredMode == LastAppliedAtmosphereMode) return;

    const bool bUseUDS = (DesiredMode == 0);

    UE_LOG(LogEagleCloud, Log,
           TEXT("==== AtmosphereMode TRANSITION: %d -> %d (bUseUDS=%s) at altitude %.1f km, camera=(%.0f,%.0f,%.0f) ===="),
           LastAppliedAtmosphereMode, DesiredMode,
           bUseUDS ? TEXT("true") : TEXT("false"),
           AltitudeKm, CameraLoc.X, CameraLoc.Y, CameraLoc.Z);

    // === Multi-component hide: SkyAtmosphere + ExponentialHeightFog + VolumetricCloud ===
    // UDS 不只是 SkyAtmosphere，还可能挂 ExponentialHeightFog（看起来像切面）和
    // VolumetricCloud。要彻底"消失"需要全 hide。
    if (UDSActorWithSkyAtmosphere)
    {
        const FVector OldLoc = UDSActorWithSkyAtmosphere->GetActorLocation();
        UE_LOG(LogEagleCloud, Log, TEXT("  UDS actor BEFORE: location=(%.0f,%.0f,%.0f)"),
               OldLoc.X, OldLoc.Y, OldLoc.Z);

        if (USkyAtmosphereComponent* SkyAtmos =
            UDSActorWithSkyAtmosphere->FindComponentByClass<USkyAtmosphereComponent>())
        {
            const bool bWasVisible = SkyAtmos->IsVisible();
            SkyAtmos->SetVisibility(bUseUDS, true);
            UE_LOG(LogEagleCloud, Log, TEXT("  SkyAtmosphere: visible %d -> %d (set to %d, now %d)"),
                   bWasVisible ? 1 : 0, bUseUDS ? 1 : 0,
                   bUseUDS ? 1 : 0, SkyAtmos->IsVisible() ? 1 : 0);
        }
        else
        {
            UE_LOG(LogEagleCloud, Warning, TEXT("  SkyAtmosphere component NOT found on UDS actor"));
        }

        if (UExponentialHeightFogComponent* Fog =
            UDSActorWithSkyAtmosphere->FindComponentByClass<UExponentialHeightFogComponent>())
        {
            Fog->SetVisibility(bUseUDS, true);
            UE_LOG(LogEagleCloud, Log, TEXT("  ExponentialHeightFog: visibility set to %d"), bUseUDS ? 1 : 0);
        }

        if (UVolumetricCloudComponent* VC =
            UDSActorWithSkyAtmosphere->FindComponentByClass<UVolumetricCloudComponent>())
        {
            VC->SetVisibility(bUseUDS, true);
            UE_LOG(LogEagleCloud, Log, TEXT("  VolumetricCloud: visibility set to %d"), bUseUDS ? 1 : 0);
        }

        // === Snap UDS XY to camera (only on transition into UDS), Z 锁定 GroundReferenceZ ===
        if (bUseUDS && bSnapUDSToCameraOnReactivation)
        {
            const FVector SnapLoc(CameraLoc.X, CameraLoc.Y, GroundReferenceZ);
            UDSActorWithSkyAtmosphere->SetActorLocation(SnapLoc, /*bSweep=*/false);
            const FVector NewLoc = UDSActorWithSkyAtmosphere->GetActorLocation();
            UE_LOG(LogEagleCloud, Log,
                   TEXT("  UDS SNAP: requested=(%.0f,%.0f,%.0f) actual=(%.0f,%.0f,%.0f)"),
                   SnapLoc.X, SnapLoc.Y, SnapLoc.Z,
                   NewLoc.X, NewLoc.Y, NewLoc.Z);
        }
        else if (bUseUDS)
        {
            UE_LOG(LogEagleCloud, Log, TEXT("  UDS snap SKIPPED (bSnapUDSToCameraOnReactivation=false)"));
        }
    }
    else
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("  UDSActorWithSkyAtmosphere is null"));
    }

    if (CesiumSunSkyActor)
    {
        const bool bWasHidden = CesiumSunSkyActor->IsHidden();
        CesiumSunSkyActor->SetActorHiddenInGame(bUseUDS);
        CesiumSunSkyActor->SetActorEnableCollision(!bUseUDS);
        UE_LOG(LogEagleCloud, Log, TEXT("  CesiumSunSky: hidden %d -> %d"),
               bWasHidden ? 1 : 0, CesiumSunSkyActor->IsHidden() ? 1 : 0);
    }
    else
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("  CesiumSunSkyActor is null"));
    }

    if (bUseUDS && Bridge && Feeder)
    {
        Bridge->SetPaintingState(true, true, Feeder->PaintedOpacity, Feeder->AffectsGlobalValues);
        Bridge->SetCoverageWindow(FVector2D(CameraLoc.X, CameraLoc.Y));
        UE_LOG(LogEagleCloud, Log, TEXT("  Painting state + coverage window reset"));
    }

    LastAppliedAtmosphereMode = DesiredMode;
}
