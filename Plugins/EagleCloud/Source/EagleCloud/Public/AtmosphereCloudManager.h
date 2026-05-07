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
//
//   Low < Altitude < High       -> Transition band
//     - T = (Altitude - LowAltitudeKm) / (HighAltitudeKm - LowAltitudeKm)
//     - MacroAlpha   = T
//     - UDSDensity   = 1 - T  (with optional smoothstep curve)
//
//   Altitude < LowAltitudeKm    -> Ground view
//     - MacroAlpha   = 0  (shell hidden — set Visible=false to skip drawcall)
//     - UDSDensity   = 1  (volumetric only)
//
// MacroAlpha is exposed via a Material Parameter Collection (assigned by user)
// so the sphere shell material reads the same value. UDSDensity is applied as
// the SatelliteCloudFeeder.AffectsGlobalValues parameter (UDS Painted Coverage
// Affects Global Values), which scales how much the painted RT replaces UDS's
// own cloud coverage value.
//
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AtmosphereCloudManager.generated.h"

class ASatelliteCloudFeeder;
class AStaticMeshActor;
class UMaterialParameterCollection;

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

    // ---------- LOD thresholds ----------

    /** Below this altitude (km), volumetric only. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|LOD", meta = (ClampMin = "0"))
    double LowAltitudeKm = 20.0;

    /** Above this altitude (km), macro shell only (UDS volumetric off). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|LOD", meta = (ClampMin = "1"))
    double HighAltitudeKm = 500.0;

    /** Use smoothstep curve for the transition (vs linear lerp). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|LOD")
    bool bUseSmoothstep = true;

    // ---------- UDS Sky Mode switch (true volumetric off above HighAltitudeKm) ----------
    //
    // Above HighAltitudeKm, fading AffectsGlobalValues alone still leaves UDS's
    // volumetric cloud pass running — wasting GPU when the macro shell is the
    // only thing visible. Toggling UDS's Sky Mode enum from "Volumetric Clouds"
    // to "Space" disables the volumetric pass entirely.
    //
    // UDS Sky Mode is a Blueprint enum. Default values match UDS's standard
    // ordering (Volumetric=0 ... Space=5) but are exposed so users can override
    // if a UDS update reorders them.

    /** Manage UDS Sky Mode automatically based on altitude. Disable to control manually. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|SkyMode")
    bool bManageUDSSkyMode = true;

    /** UDS actor property name (Blueprint enum). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|SkyMode|Advanced")
    FName UDS_SkyModeProperty = TEXT("Sky Mode");

    /** Enum value for "Volumetric Clouds" (default 0 in stock UDS). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|SkyMode|Advanced", meta = (ClampMin = "0", ClampMax = "255"))
    uint8 UDS_SkyMode_Volumetric = 0;

    /** Enum value for "Space" (default 5 in stock UDS: Volumetric/Static/2D/None/Aurora/Space). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|SkyMode|Advanced", meta = (ClampMin = "0", ClampMax = "255"))
    uint8 UDS_SkyMode_Space = 5;

    /**
     * Hysteresis (km) around HighAltitudeKm to avoid mode flapping at the boundary.
     * Switch to Space when alt > High + H, switch back to Volumetric when alt < High - H.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|SkyMode", meta = (ClampMin = "0"))
    double SkyModeSwitchHysteresisKm = 25.0;

    // ---------- Geo (matches feeder for ground reference) ----------

    /**
     * Z (UE world cm) of the ground / sea level reference. Camera altitude is
     * computed as (CameraZ - GroundZ) / 100000 km. For Cesium-based projects,
     * keep this at 0 and rely on Cesium for absolute height (future).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Geo")
    double GroundReferenceZ = 0.0;

    // ---------- MPC parameter names ----------

    /** Scalar param name in MPC to receive macro shell alpha. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|MPC")
    FName MPC_MacroAlpha = TEXT("MacroAlpha");

    /** Scalar param name in MPC to receive UDS volumetric density mix (0..1). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|MPC")
    FName MPC_UDSDensity = TEXT("UDSDensity");

    /** Scalar param name in MPC to receive current altitude (km). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|MPC")
    FName MPC_AltitudeKm = TEXT("AltitudeKm");

    /**
     * Scalar param name in MPC for no-data fallback threshold (forwarded from
     * Feeder->NoDataThreshold each tick). M_AtmosphereShell reads this to blend
     * procedural noise into pixels where NASA_Density < threshold:
     *   FallbackMask = 1 - smoothstep(0, NoDataThreshold, NASADensity)
     *   Final = lerp(NASADensity, ProceduralNoise * 0.5, FallbackMask)
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|MPC")
    FName MPC_NoDataThreshold = TEXT("NoDataThreshold");

    /** Last computed values (read-only, exposed for debug). */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "EagleCloud|Debug")
    double CurrentAltitudeKm = 0.0;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "EagleCloud|Debug")
    float CurrentMacroAlpha = 0.f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "EagleCloud|Debug")
    float CurrentUDSDensity = 1.f;

    virtual void Tick(float DeltaSeconds) override;

private:
    /** Compute camera altitude in km above GroundReferenceZ. */
    double ComputeCameraAltitudeKm() const;

    /** Write to MPC and/or directly to Feeder + MacroShell. */
    void ApplyBlend(float MacroAlpha, float UDSDensity, double AltitudeKm);

    /** Toggle UDS Sky Mode (Volumetric <-> Space) based on altitude with hysteresis. */
    void ApplySkyMode(double AltitudeKm);

    /** Locate the Ultra_Dynamic_Sky actor in the level. */
    AActor* FindUDSActor() const;

    UPROPERTY(Transient) TWeakObjectPtr<AActor> CachedUDS;

    /** Last sky mode we wrote to UDS. -1 = never written yet. */
    int32 LastAppliedSkyMode = -1;
};
