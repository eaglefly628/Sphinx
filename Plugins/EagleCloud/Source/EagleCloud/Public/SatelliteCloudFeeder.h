// SatelliteCloudFeeder.h
//
// Bridges a global equirectangular cloud cover texture (e.g. NASA GIBS) into
// UDS Ultra Dynamic Sky volumetric clouds.
//
// Two modes:
//   A) Local mode  (CloudTexture set, GlobalCloudTexture null)
//      Draws the source texture 1:1 into UDS Cloud Coverage RT.
//      RT covers a fixed 200km x 200km area at world origin. For Phase A test.
//
//   B) Global mode (GlobalCloudTexture set)
//      Each tick:
//        1. Get camera world XY
//        2. Convert to lat/lon (via OriginLatitude/Longitude flat-earth offset
//           — replace with Cesium georef once integrated)
//        3. Compute the UV rectangle on the global equirectangular texture for
//           the local CoverageRadiusKm box around the camera
//        4. Draw that UV sub-region into UDS Cloud Coverage RT
//        5. Move UDS Cloud Coverage Target Location to follow camera XY
//      Result: UDS volumetric clouds locally form per the global cloud field,
//      and the 200km window slides with the camera.
//
// Data flow recap:
//   GIBS PNG -> UTexture2D -> K2_DrawTexture(uvSubrect) -> UDS RT
//                                                          \-> UDS volumetric
//                                                              clouds in 200km
//
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SatelliteCloudFeeder.generated.h"

class UTexture2D;
class UTextureRenderTarget2D;

UCLASS(BlueprintType, Blueprintable, ClassGroup=(EagleCloud))
class EAGLECLOUD_API ASatelliteCloudFeeder : public AActor
{
    GENERATED_BODY()

public:
    ASatelliteCloudFeeder();

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

    /** Lat (deg) corresponding to UE world (0,0,0). Used until Cesium georef is wired. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Global")
    double OriginLatitude = 31.23;  // Shanghai default

    /** Lon (deg) corresponding to UE world (0,0,0). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Global")
    double OriginLongitude = 121.47;

    /** Half-size of UDS coverage area in km. Default 100 -> 200km x 200km box. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Global", meta = (ClampMin = "1"))
    double CoverageRadiusKm = 100.0;

    /** If true, sample around player camera; otherwise sample around world origin. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud|Global")
    bool bFollowPlayerCamera = true;

    // ---------- Common ----------

    /** Apply on BeginPlay. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud")
    bool bApplyOnBeginPlay = true;

    /**
     * Refresh interval (seconds). 0 = once. For Global mode + bFollowPlayerCamera,
     * set this small (e.g. 0.2) so the window slides with the camera.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud", meta = (ClampMin = "0"))
    float RefreshIntervalSeconds = 0.0f;

    /** UDS "Painted Cloud Coverage Opacity" (0..1). 1 = texture fully drives clouds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud", meta = (ClampMin = "0", ClampMax = "1"))
    float PaintedOpacity = 1.0f;

    /** UDS "Painted Coverage Affects Global Values" (0..1). 1 = total override. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud", meta = (ClampMin = "0", ClampMax = "1"))
    float AffectsGlobalValues = 1.0f;

    /** Push the right texture into UDS now. Returns true on success. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "EagleCloud")
    bool ApplyToUDS();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY(Transient) TObjectPtr<AActor> CachedUDS = nullptr;
    float TimeSinceRefresh = 0.f;

    AActor* FindUDSActor() const;
    UTextureRenderTarget2D* GetUDSCloudRT(AActor* UDS) const;
    void EnableUDSPainting(AActor* UDS) const;

    /** Returns FVector2D(latDeg, lonDeg) for the sample center. */
    FVector2D GetSampleCenterLatLon() const;

    /** Camera (or world origin) XY in UE world cm. */
    FVector2D GetSampleCenterWorldXY() const;

    /** Set UDS Cloud Coverage Target Location to follow camera. */
    void SetUDSTargetLocation(AActor* UDS, const FVector2D& WorldXY) const;

    /** Local-mode draw: full source texture into RT. */
    bool DrawLocalTextureToRT(UTextureRenderTarget2D* RT) const;

    /** Global-mode draw: sample UV sub-rect of global texture into RT. */
    bool DrawGlobalRegionToRT(UTextureRenderTarget2D* RT, double CenterLat, double CenterLon) const;
};
