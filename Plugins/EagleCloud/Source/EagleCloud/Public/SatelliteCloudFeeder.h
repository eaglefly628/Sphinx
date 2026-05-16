// SatelliteCloudFeeder.h
//
// Bridges a global equirectangular cloud cover texture (e.g. NASA GIBS) into
// UDS Ultra Dynamic Sky volumetric clouds via AEagleCloudUDSBridge (BP bridge).
//
// Two modes:
//   A) Local mode  (CloudTexture set, GlobalCloudTexture null)
//      Draws the source texture 1:1 into UDS Cloud Coverage RT (Phase A test).
//
//   B) Global mode (GlobalCloudTexture set, takes precedence)
//      Each tick:
//        1. Compute camera world XY -> lat/lon (flat-earth offset from
//           OriginLatitude/Longitude; will be replaced by Cesium georef later).
//        2. Compute UV sub-rect on the equirectangular global texture for the
//           local CoverageRadiusKm box around the camera.
//        3. Draw that UV sub-region into UDS Cloud Coverage RT (via Bridge).
//        4. Slide UDS Cloud Coverage Target Location to follow camera (via Bridge).
//      Result: UDS volumetric clouds locally form per the global cloud field
//      and the 200km window slides with the camera.
//
// All UDS access goes through Bridge (BlueprintImplementableEvent). No
// string-based FProperty reflection. If Bridge is null, ApplyToUDS warns and
// returns false; BeginPlay ensureMsgf-warns once and disables Tick.
//
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SatelliteCloudFeeder.generated.h"

class UTexture2D;
class UTextureRenderTarget2D;
class AEagleCloudUDSBridge;

UCLASS(BlueprintType, Blueprintable, ClassGroup=(EagleCloud))
class EAGLECLOUD_API ASatelliteCloudFeeder : public AActor
{
    GENERATED_BODY()

public:
    ASatelliteCloudFeeder();

    // ---------- BP Bridge (mandatory) ----------

    /**
     * BP bridge that translates our calls into UDS variable writes.
     * Drop a BP_EagleCloudBridge actor into the level and assign here.
     * NULL is a configuration error and triggers ensureMsgf at BeginPlay.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Bridge")
    TObjectPtr<AEagleCloudUDSBridge> Bridge = nullptr;

    // ---------- Local mode (Phase A) ----------

    /** Local cloud cover texture (Phase A). Draws 1:1 into UDS RT. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Local")
    TObjectPtr<UTexture2D> CloudTexture = nullptr;

    // ---------- Global mode (Phase B) ----------

    /**
     * Global equirectangular cloud cover texture (e.g. GIBS True Color).
     * lon -180..180 maps to U 0..1, lat 90..-90 maps to V 0..1 (north at top).
     * If set, takes precedence over CloudTexture.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Global")
    TObjectPtr<UTexture2D> GlobalCloudTexture = nullptr;

    /** Lat (deg) at UE world (0,0,0). Used until Cesium georef is wired. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Global")
    double OriginLatitude = 31.23;  // Shanghai default

    /** Lon (deg) at UE world (0,0,0). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Global")
    double OriginLongitude = 121.47;

    /** Half-size of UDS coverage area in km. Default 100 -> 200km x 200km box. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Global", meta = (ClampMin = "1"))
    double CoverageRadiusKm = 100.0;

    /** Sample around player camera; otherwise around world origin. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Global")
    bool bFollowPlayerCamera = true;

    // ---------- Common ----------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud")
    bool bApplyOnBeginPlay = true;

    /**
     * Refresh interval (seconds). Setting to 0 fires only at BeginPlay; the UDS
     * RT is usually not yet allocated then, so 0.2 is the safer default.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud", meta = (ClampMin = "0"))
    float RefreshIntervalSeconds = 0.2f;

    /** UDS "Painted Cloud Coverage Opacity" (0..1). 1 = texture fully drives clouds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud", meta = (ClampMin = "0", ClampMax = "1"))
    float PaintedOpacity = 1.0f;

    /** UDS "Painted Coverage Affects Global Values" (0..1). 1 = total override. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud", meta = (ClampMin = "0", ClampMax = "1"))
    float AffectsGlobalValues = 1.0f;

    /**
     * No-data fallback threshold (0..1). Pixels darker than this are treated
     * as "no data" and blended with procedural noise in M_AtmosphereShell.
     * Forwarded to MPC.NoDataThreshold by AAtmosphereCloudManager each tick.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Fallback", meta = (ClampMin = "0", ClampMax = "1"))
    float NoDataThreshold = 0.05f;

    // ---------- Diagnostics ----------

    /**
     * Print step-by-step trace each ApplyToUDS tick (RT name, source size,
     * sample lat/lon, draw OK). With BP Bridge, silent failures are gone, so
     * verbose logging is opt-in and defaults off.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Debug")
    bool bVerboseLogging = false;

    /** Push the right texture into UDS now. Returns true on success. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "EagleCloud")
    bool ApplyToUDS();

    /**
     * Sync PaintedOpacity / AffectsGlobalValues to UDS without redrawing the RT.
     * Called by AAtmosphereCloudManager each tick so LOD changes propagate.
     */
    UFUNCTION(BlueprintCallable, Category = "EagleCloud")
    void SyncPropertiesToUDS();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    float TimeSinceRefresh = 0.f;

    /** FVector2D(latDeg, lonDeg) for the sample center. */
    FVector2D GetSampleCenterLatLon() const;

    /** Camera (or world origin) XY in UE world cm. */
    FVector2D GetSampleCenterWorldXY() const;

    /** Local-mode draw: full source texture into RT. */
    bool DrawLocalTextureToRT(UTextureRenderTarget2D* RT) const;

    /** Global-mode draw: sample UV sub-rect of global texture into RT. */
    bool DrawGlobalRegionToRT(UTextureRenderTarget2D* RT, double CenterLat, double CenterLon) const;
};
