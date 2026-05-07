// AtmosphereCloudManager.h
//
// Drives LOD blending between macro (atmosphere shell) and micro (UDS volumetric)
// cloud rendering based on camera altitude above the planet surface.
//
// Architecture (mirrors planetary rendering pipelines like Star Citizen / MSFS):
//
//   Altitude > HighAltitudeKm   -> Space view
//     - MacroAlpha   = 1  (sphere shell fully visible)
//     - UDSDensity   = 0  (volumetric clouds turned off, save GPU)
//     - UDS Sky Mode -> Space (truly disables volumetric pass)
//
//   Low < Altitude < High       -> Transition band
//     - T = (Altitude - LowAltitudeKm) / (HighAltitudeKm - LowAltitudeKm)
//     - MacroAlpha   = T
//     - UDSDensity   = 1 - T  (with optional smoothstep curve)
//     - UDS Sky Mode follows the side of HighAltitudeKm we're on (with hysteresis)
//
//   Altitude < LowAltitudeKm    -> Ground view
//     - MacroAlpha   = 0  (shell hidden — Visible=false to skip drawcall)
//     - UDSDensity   = 1  (volumetric only)
//     - UDS Sky Mode = Volumetric Clouds
//
// All UDS access is delegated to AEagleCloudUDSBridge — no string reflection.
//
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AtmosphereCloudManager.generated.h"

class ASatelliteCloudFeeder;
class UMaterialParameterCollection;
class AEagleCloudUDSBridge;

UCLASS(BlueprintType, Blueprintable, ClassGroup=(EagleCloud))
class EAGLECLOUD_API AAtmosphereCloudManager : public AActor
{
    GENERATED_BODY()

public:
    AAtmosphereCloudManager();

    // ---------- References ----------

    /** SatelliteCloudFeeder we're driving (we modulate its AffectsGlobalValues). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Refs")
    TObjectPtr<ASatelliteCloudFeeder> Feeder = nullptr;

    /** Optional sphere mesh actor used as the macro atmosphere shell (visibility toggled). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Refs")
    TObjectPtr<AActor> MacroShellActor = nullptr;

    /** Optional MPC. Scalar params named below get written each tick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Refs")
    TObjectPtr<UMaterialParameterCollection> MPC = nullptr;

    /**
     * BP bridge for UDS Sky Mode writes. If left null, BeginPlay falls back
     * to Feeder->Bridge so users can drag the bridge once.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Refs")
    TObjectPtr<AEagleCloudUDSBridge> Bridge = nullptr;

    // ---------- LOD thresholds ----------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|LOD", meta = (ClampMin = "0"))
    double LowAltitudeKm = 20.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|LOD", meta = (ClampMin = "1"))
    double HighAltitudeKm = 500.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|LOD")
    bool bUseSmoothstep = true;

    // ---------- UDS Sky Mode switch ----------

    /** Manage UDS Sky Mode automatically based on altitude. Disable to control manually. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|SkyMode")
    bool bManageUDSSkyMode = true;

    /**
     * Enum index for "Volumetric Clouds" (default 0 in stock UDS 9.x:
     * Volumetric / Static / 2D Dynamic / None / Aurora / Space).
     * Cast to UDS's enum type happens in BP_EagleCloudBridge.SetSkyMode.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|SkyMode|Advanced", meta = (ClampMin = "0", ClampMax = "255"))
    uint8 UDS_SkyMode_Volumetric = 0;

    /** Enum index for "Space" (default 5 in stock UDS 9.x). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|SkyMode|Advanced", meta = (ClampMin = "0", ClampMax = "255"))
    uint8 UDS_SkyMode_Space = 5;

    /**
     * Hysteresis (km) around HighAltitudeKm to avoid mode flapping at the boundary.
     * Switch to Space at alt > High + H, switch back to Volumetric at alt < High - H.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|SkyMode", meta = (ClampMin = "0"))
    double SkyModeSwitchHysteresisKm = 25.0;

    // ---------- Geo (matches feeder for ground reference) ----------

    /**
     * Z (UE world cm) of ground/sea-level reference. Camera altitude is
     * (CameraZ - GroundZ) / 100000 km. For Cesium projects keep at 0.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Geo")
    double GroundReferenceZ = 0.0;

    // ---------- MPC parameter names ----------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|MPC")
    FName MPC_MacroAlpha = TEXT("MacroAlpha");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|MPC")
    FName MPC_UDSDensity = TEXT("UDSDensity");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|MPC")
    FName MPC_AltitudeKm = TEXT("AltitudeKm");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|MPC")
    FName MPC_NoDataThreshold = TEXT("NoDataThreshold");

    /** Last computed values (read-only, exposed for debug). */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "EagleCloud|Debug")
    double CurrentAltitudeKm = 0.0;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "EagleCloud|Debug")
    float CurrentMacroAlpha = 0.f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "EagleCloud|Debug")
    float CurrentUDSDensity = 1.f;

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    /** Compute camera altitude in km above GroundReferenceZ. */
    double ComputeCameraAltitudeKm() const;

    /** Drive Feeder/MacroShell/MPC. */
    void ApplyBlend(float MacroAlpha, float UDSDensity, double AltitudeKm);

    /** Toggle UDS Sky Mode (Volumetric <-> Space) via Bridge with hysteresis. */
    void ApplySkyMode(double AltitudeKm);

    /** Last sky mode index we wrote. -1 = never written yet. */
    int32 LastAppliedSkyMode = -1;
};
