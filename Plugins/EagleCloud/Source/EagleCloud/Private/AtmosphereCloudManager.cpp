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

    // === Visibility toggle: 用 UDS 自家 HideSky 函数 (Bridge.HideSky) ===
    // UDS 内部知道该 hide 哪些 component — 不用我们手动 toggle StaticMesh/SkyAtmosphere/Fog,
    // 避免跟 UDS BP tick 打架. 高空 hide fog+atmosphere+sky_sphere/clouds, 保留 lights.
    if (Bridge)
    {
        Bridge->HideUDSSky(
            /*bHideEntireActor=*/    false,     // 保留 actor 本身 (sun 需要)
            /*bHideLights=*/         false,     // 保留 sun/moon 光照 (Cesium Earth 需要)
            /*bHideFogAtmosphere=*/  !bUseUDS,  // 高空 hide atmosphere (Cesium 接管)
            /*bHideSkySphereClouds=*/ !bUseUDS, // 高空 hide sky dome + cloud mesh (切面来源)
            /*bHidePostProcessing=*/ false      // 保留 post processing
        );
        UE_LOG(LogEagleCloud, Log, TEXT("  Bridge.HideUDSSky(actor=0, lights=0, fog/atmos=%d, sky/clouds=%d, post=0)"),
               !bUseUDS ? 1 : 0, !bUseUDS ? 1 : 0);
    }
    else
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("  Bridge is null, HideUDSSky skipped"));
    }

    if (UDSActorWithSkyAtmosphere)
    {
        // SkyAtmosphere TransformMode 修正 (一次性, snap 后 atmosphere 才真的跟 actor 走)
        if (USkyAtmosphereComponent* SkyAtmos =
            UDSActorWithSkyAtmosphere->FindComponentByClass<USkyAtmosphereComponent>())
        {
            if (SkyAtmos->TransformMode != ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform)
            {
                SkyAtmos->TransformMode = ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform;
                SkyAtmos->MarkRenderStateDirty();
                UE_LOG(LogEagleCloud, Log, TEXT("  SkyAtmosphere TransformMode -> PlanetTopAtComponentTransform"));
            }
        }

        // === 精确 toggle ExponentialHeightFog (确认是切面来源之一: "平面圆环") ===
        // UDS HideSky 没管到这个 fog component, 高空时它的 fog plane 从外面看是圆环切面.
        // 含 child actors (UDS 的 fog 可能是 child actor 包装的).
        TArray<UExponentialHeightFogComponent*> FogComps;
        UDSActorWithSkyAtmosphere->GetComponents<UExponentialHeightFogComponent>(FogComps, /*bIncludeFromChildActors=*/true);
        for (UExponentialHeightFogComponent* Fog : FogComps)
        {
            if (!Fog) continue;
            Fog->SetVisibility(bUseUDS, /*bPropagateToChildren=*/true);
            UE_LOG(LogEagleCloud, Log, TEXT("  ExponentialHeightFog '%s' (owner=%s) visibility -> %d"),
                   *Fog->GetName(),
                   Fog->GetOwner() ? *Fog->GetOwner()->GetName() : TEXT("?"),
                   bUseUDS ? 1 : 0);
        }

        const FVector OldLoc = UDSActorWithSkyAtmosphere->GetActorLocation();
        UE_LOG(LogEagleCloud, Log, TEXT("  UDS actor BEFORE: location=(%.0f,%.0f,%.0f)"),
               OldLoc.X, OldLoc.Y, OldLoc.Z);

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
        UE_LOG(LogEagleCloud, Warning, TEXT("  UDSActorWithSkyAtmosphere is null (TransformMode/Snap skipped)"));
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
