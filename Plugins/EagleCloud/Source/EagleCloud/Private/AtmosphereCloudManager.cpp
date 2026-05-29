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
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "EngineUtils.h"

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
        // === 直接 toggle SkyAtmosphereComponent (确认: dump 显示它 visible=1 就是那条弧) ===
        // SkyAtmosphereComponent 不是 StaticMesh, 白名单遍历碰不到它; UDS HideSky 也没真关它.
        // 必须直接 SetVisibility. 高空 hide (Cesium atmosphere 接管), 低空 show.
        if (USkyAtmosphereComponent* SkyAtmos =
            UDSActorWithSkyAtmosphere->FindComponentByClass<USkyAtmosphereComponent>())
        {
            // TransformMode 修正 (snap 后 atmosphere 才真的跟 actor 走)
            if (SkyAtmos->TransformMode != ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform)
            {
                SkyAtmos->TransformMode = ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform;
                SkyAtmos->MarkRenderStateDirty();
                UE_LOG(LogEagleCloud, Log, TEXT("  SkyAtmosphere TransformMode -> PlanetTopAtComponentTransform"));
            }
            const bool bWas = SkyAtmos->IsVisible();
            SkyAtmos->SetVisibility(bUseUDS, /*bPropagateToChildren=*/false);
            UE_LOG(LogEagleCloud, Log, TEXT("  SkyAtmosphere SetVisibility: %d -> %d (now %d)"),
                   bWas ? 1 : 0, bUseUDS ? 1 : 0, SkyAtmos->IsVisible() ? 1 : 0);
        }

        // === 直接 toggle Space Nebula Sphere + Sky_Sphere (StaticMesh, dump 确认存在) ===
        // 这些可能 child-actor 包装也可能直挂; 白名单已覆盖, 这里 log 确认状态.

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

        // === 精确 toggle Sky dome StaticMesh (确认: "半圆 skybox") ===
        // UDS 的 Sky_Sphere / Space Nebula Sphere 等是包裹场景的大球 mesh, 内壁贴天空材质.
        // 从太空往外看到球的远端内壁 = 半圆弧. HideSky 没真正 hide 它们.
        // 含 child actors. 跳过装饰类 (ClockDisk/Compass/字母 N/S/E/W/Label/Handle).
        TArray<UStaticMeshComponent*> SkyMeshes;
        UDSActorWithSkyAtmosphere->GetComponents<UStaticMeshComponent>(SkyMeshes, /*bIncludeFromChildActors=*/true);
        for (UStaticMeshComponent* SM : SkyMeshes)
        {
            if (!SM) continue;
            const FString Nm = SM->GetName();
            // 只 toggle 天空/雾/星云类大 mesh, 不动罗盘/时钟/方位字母等小装饰
            const bool bIsSkyDome =
                Nm.Contains(TEXT("Sky")) || Nm.Contains(TEXT("Sphere")) ||
                Nm.Contains(TEXT("Nebula")) || Nm.Contains(TEXT("Fog")) ||
                Nm.Contains(TEXT("Cloud")) || Nm.Contains(TEXT("Dome")) ||
                Nm.Contains(TEXT("Atmosphere")) || Nm.Contains(TEXT("Star"));
            if (!bIsSkyDome) continue;
            SM->SetVisibility(bUseUDS, /*bPropagateToChildren=*/true);
            UE_LOG(LogEagleCloud, Log, TEXT("  SkyMesh '%s' (owner=%s) visibility -> %d"),
                   *Nm,
                   SM->GetOwner() ? *SM->GetOwner()->GetName() : TEXT("?"),
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

// ============ Diagnostics (editor buttons) ============

void AAtmosphereCloudManager::DumpUDSComponents()
{
    if (!UDSActorWithSkyAtmosphere)
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("DumpUDSComponents: UDSActorWithSkyAtmosphere is null"));
        return;
    }

    TArray<USceneComponent*> Comps;
    UDSActorWithSkyAtmosphere->GetComponents<USceneComponent>(Comps, /*bIncludeFromChildActors=*/true);

    UE_LOG(LogEagleCloud, Log, TEXT("===== DumpUDSComponents: %d scene components on %s ====="),
           Comps.Num(), *UDSActorWithSkyAtmosphere->GetName());
    for (USceneComponent* C : Comps)
    {
        if (!C) continue;
        UE_LOG(LogEagleCloud, Log, TEXT("  [%s] class=%s visible=%d owner=%s"),
               *C->GetName(),
               *C->GetClass()->GetName(),
               C->IsVisible() ? 1 : 0,
               C->GetOwner() ? *C->GetOwner()->GetName() : TEXT("?"));
    }
    UE_LOG(LogEagleCloud, Log, TEXT("===== DumpUDSComponents END ====="));
}

void AAtmosphereCloudManager::HideAllUDSComponents()
{
    if (!UDSActorWithSkyAtmosphere)
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("HideAllUDSComponents: UDSActorWithSkyAtmosphere is null"));
        return;
    }

    TArray<USceneComponent*> Comps;
    UDSActorWithSkyAtmosphere->GetComponents<USceneComponent>(Comps, /*bIncludeFromChildActors=*/true);

    int32 Count = 0;
    for (USceneComponent* C : Comps)
    {
        if (!C) continue;
        // 跳过 light component — 保留太阳/月亮光照 (Cesium Earth 需要)
        if (C->IsA<ULightComponent>()) continue;
        C->SetVisibility(false, /*bPropagateToChildren=*/false);
        ++Count;
    }
    UE_LOG(LogEagleCloud, Log, TEXT("HideAllUDSComponents: hid %d components (lights kept)"), Count);
}

void AAtmosphereCloudManager::ShowAllUDSComponents()
{
    if (!UDSActorWithSkyAtmosphere)
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("ShowAllUDSComponents: UDSActorWithSkyAtmosphere is null"));
        return;
    }

    TArray<USceneComponent*> Comps;
    UDSActorWithSkyAtmosphere->GetComponents<USceneComponent>(Comps, /*bIncludeFromChildActors=*/true);

    int32 Count = 0;
    for (USceneComponent* C : Comps)
    {
        if (!C) continue;
        C->SetVisibility(true, /*bPropagateToChildren=*/false);
        ++Count;
    }
    UE_LOG(LogEagleCloud, Log, TEXT("ShowAllUDSComponents: showed %d components"), Count);
}

void AAtmosphereCloudManager::DumpAllAtmosphereSources()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("DumpAllAtmosphereSources: World is null"));
        return;
    }

    UE_LOG(LogEagleCloud, Log, TEXT("================================================"));
    UE_LOG(LogEagleCloud, Log, TEXT("===== DumpAllAtmosphereSources (entire level) ====="));
    UE_LOG(LogEagleCloud, Log, TEXT("================================================"));

    // 遍历所有 actor, 找跟 atmosphere/sky/fog 相关的 component
    int32 SkyAtmosCount = 0, FogCount = 0, VolCloudCount = 0, SkyLightCount = 0, SkyMeshCount = 0, PostProcCount = 0;

    for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
    {
        AActor* A = *ActorIt;
        if (!A) continue;

        // SkyAtmosphereComponent
        TArray<USkyAtmosphereComponent*> SAs;
        A->GetComponents<USkyAtmosphereComponent>(SAs, /*bIncludeFromChildActors=*/true);
        for (USkyAtmosphereComponent* SA : SAs)
        {
            if (!SA) continue;
            UE_LOG(LogEagleCloud, Log, TEXT("[SkyAtmosphere] actor=%s comp=%s visible=%d transform=%d"),
                   *A->GetName(), *SA->GetName(),
                   SA->IsVisible() ? 1 : 0,
                   (int32)SA->TransformMode);
            ++SkyAtmosCount;
        }

        // ExponentialHeightFogComponent
        TArray<UExponentialHeightFogComponent*> Fs;
        A->GetComponents<UExponentialHeightFogComponent>(Fs, true);
        for (UExponentialHeightFogComponent* F : Fs)
        {
            if (!F) continue;
            UE_LOG(LogEagleCloud, Log, TEXT("[ExpHeightFog] actor=%s comp=%s visible=%d density=%.4f"),
                   *A->GetName(), *F->GetName(),
                   F->IsVisible() ? 1 : 0,
                   F->FogDensity);
            ++FogCount;
        }

        // SkyLightComponent
        TArray<USkyLightComponent*> SLs;
        A->GetComponents<USkyLightComponent>(SLs, true);
        for (USkyLightComponent* SL : SLs)
        {
            if (!SL) continue;
            UE_LOG(LogEagleCloud, Log, TEXT("[SkyLight] actor=%s comp=%s visible=%d intensity=%.2f"),
                   *A->GetName(), *SL->GetName(),
                   SL->IsVisible() ? 1 : 0,
                   SL->Intensity);
            ++SkyLightCount;
        }

        // StaticMeshComponent with sky/atmosphere/dome-like names
        TArray<UStaticMeshComponent*> SMs;
        A->GetComponents<UStaticMeshComponent>(SMs, true);
        for (UStaticMeshComponent* SM : SMs)
        {
            if (!SM) continue;
            const FString Nm = SM->GetName();
            const bool bLooksSky =
                Nm.Contains(TEXT("Sky")) || Nm.Contains(TEXT("Sphere")) ||
                Nm.Contains(TEXT("Nebula")) || Nm.Contains(TEXT("Dome")) ||
                Nm.Contains(TEXT("Atmosphere")) || Nm.Contains(TEXT("Halo")) ||
                Nm.Contains(TEXT("Cosmos")) || Nm.Contains(TEXT("Space")) ||
                Nm.Contains(TEXT("Star"));
            if (!bLooksSky) continue;
            UE_LOG(LogEagleCloud, Log, TEXT("[SkyMesh] actor=%s comp=%s visible=%d"),
                   *A->GetName(), *Nm, SM->IsVisible() ? 1 : 0);
            ++SkyMeshCount;
        }
    }

    UE_LOG(LogEagleCloud, Log, TEXT("------------------------------------------------"));
    UE_LOG(LogEagleCloud, Log, TEXT("Summary: SkyAtmosphere=%d Fog=%d SkyLight=%d SkyMesh=%d"),
           SkyAtmosCount, FogCount, SkyLightCount, SkyMeshCount);
    UE_LOG(LogEagleCloud, Log, TEXT("===== DumpAllAtmosphereSources END ====="));
}

void AAtmosphereCloudManager::HideUDSPostProcess()
{
    if (!UDSActorWithSkyAtmosphere)
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("HideUDSPostProcess: UDSActorWithSkyAtmosphere is null"));
        return;
    }

    TArray<UPostProcessComponent*> PPs;
    UDSActorWithSkyAtmosphere->GetComponents<UPostProcessComponent>(PPs, /*bIncludeFromChildActors=*/true);

    int32 Count = 0;
    for (UPostProcessComponent* PP : PPs)
    {
        if (!PP) continue;
        // PostProcessComponent 用 bEnabled 控制是否生效, 不是 Visibility
        PP->bEnabled = false;
        PP->MarkRenderStateDirty();
        UE_LOG(LogEagleCloud, Log, TEXT("HideUDSPostProcess: disabled '%s'"), *PP->GetName());
        ++Count;
    }
    UE_LOG(LogEagleCloud, Log, TEXT("HideUDSPostProcess: disabled %d PostProcess components"), Count);
}

void AAtmosphereCloudManager::ShowUDSPostProcess()
{
    if (!UDSActorWithSkyAtmosphere) return;

    TArray<UPostProcessComponent*> PPs;
    UDSActorWithSkyAtmosphere->GetComponents<UPostProcessComponent>(PPs, true);

    int32 Count = 0;
    for (UPostProcessComponent* PP : PPs)
    {
        if (!PP) continue;
        PP->bEnabled = true;
        PP->MarkRenderStateDirty();
        ++Count;
    }
    UE_LOG(LogEagleCloud, Log, TEXT("ShowUDSPostProcess: enabled %d PostProcess components"), Count);
}
