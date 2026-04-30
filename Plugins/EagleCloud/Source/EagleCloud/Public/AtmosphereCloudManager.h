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
};
