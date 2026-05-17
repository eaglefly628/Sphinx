// EagleCloudUDSBridge.h
//
// C++ ↔ UDS (pure-Blueprint plugin) bridge using BlueprintImplementableEvent.
//
// Why: UDS has no C++ source. Talking to it from C++ via FProperty string
// reflection is fragile — every UDS update can silently break field names
// and our reflection failures are warnings, not compile errors.
//
// This class declares the minimal contract our cloud system needs from UDS.
// A Blueprint subclass (BP_EagleCloudBridge) implements each event using
// ordinary Blueprint nodes (Get Actor of Class, Set Variable, etc.). If a
// UDS variable rename breaks a wire, the BP fails to compile — we get a
// loud red error in the editor instead of a silent runtime no-op.
//
// Setup (editor):
//   1. Content Browser → right-click → Blueprint Class → All Classes →
//      pick AEagleCloudUDSBridge → name it BP_EagleCloudBridge.
//   2. In its Event Graph implement the 5 Implementable Events listed below.
//   3. Drop one BP_EagleCloudBridge actor into the level.
//   4. Assign it to ASatelliteCloudFeeder.Bridge and (optionally)
//      AAtmosphereCloudManager.Bridge.
//
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EagleCloudUDSBridge.generated.h"

class UTextureRenderTarget2D;

UCLASS(Blueprintable, ClassGroup=(EagleCloud))
class EAGLECLOUD_API AEagleCloudUDSBridge : public AActor
{
    GENERATED_BODY()

public:
    AEagleCloudUDSBridge();

    /**
     * BP-implemented: locate the Ultra_Dynamic_Sky actor in the level, cache
     * its reference inside the BP, and (optionally) toggle Cloud Painting on.
     * Returns true on success. Called once at Feeder.BeginPlay.
     */
    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "EagleCloud|Bridge")
    bool InitializeUDS();

    /**
     * BP-implemented: return UDS's painted Cloud Coverage Render Target.
     * Feeder writes its NASA texture window into this RT each refresh.
     * If your UDS version exposes the RT under a different variable name,
     * the wire breaks at compile time in BP — visible immediately.
     */
    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "EagleCloud|Bridge")
    UTextureRenderTarget2D* GetUDSCloudRT();

    /**
     * BP-implemented: set UDS's "Cloud Coverage Target Location" (FVector2D)
     * so the painted-coverage window slides with the camera world position.
     */
    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "EagleCloud|Bridge")
    void SetCoverageWindow(FVector2D WorldXY);

    /**
     * BP-implemented: write UDS's painted-cloud parameters in one call.
     *   bActive       -> "Cloud Painting Active"
     *   bForce        -> "Force Cloud Coverage Target Active"
     *   Opacity       -> "Painted Cloud Coverage Opacity"
     *   AffectsGlobal -> "Painted Coverage Affects Global Values"
     */
    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "EagleCloud|Bridge")
    void SetPaintingState(bool bActive, bool bForce, float Opacity, float AffectsGlobal);

    /**
     * BP-implemented: write UDS's "Sky Mode" enum.
     * AAtmosphereCloudManager calls this with 0=Volumetric Clouds, 5=Space
     * (UDS 9.x default ordering; user can override the constants on Manager).
     * BP cast int -> UDS's enum type before assigning.
     */
    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "EagleCloud|Bridge")
    void SetSkyMode(int32 ModeIndex);

    /**
     * BP-implemented: call UDS's own Hide Sky function (Utility Functions category).
     * UDS knows internally which components to hide for each category — avoids
     * fighting UDS BP tick by manually toggling individual components.
     *
     * AAtmosphereCloudManager calls this at atmosphere mode transitions:
     *   - High altitude (Cesium mode): bHideFogAtmosphere=true, bHideSkySphereClouds=true,
     *     others=false. UDS hides atmosphere/sky/cloud meshes, keeps sun lighting.
     *   - Low altitude (UDS mode): all false. UDS shows everything.
     *
     * BP implementation:
     *   Event HideUDSSky -> CachedUDS.HideSky (Target=CachedUDS, NOT self!)
     *   连 5 个 bool 直接到 CachedUDS.HideSky 的同名 input.
     *
     * 名字加 UDS 前缀避免跟 Ultra_Dynamic_Sky.HideSky 同名导致 BP 里递归调 self.
     */
    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "EagleCloud|Bridge")
    void HideUDSSky(bool bHideEntireActor, bool bHideLights, bool bHideFogAtmosphere,
                    bool bHideSkySphereClouds, bool bHidePostProcessing);
};
