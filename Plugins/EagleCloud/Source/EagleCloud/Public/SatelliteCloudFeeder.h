// SatelliteCloudFeeder.h
//
// Phase A: Push a 2D cloud cover texture into UDS Cloud Coverage Render Target,
// so volumetric clouds form per the texture pattern in a 200km x 200km area.
//
// Usage:
//   1. Generate test PNG via Tools/Weather/gen_cloud_test.py
//   2. Import PNG into Content/Weather/ -> UTexture2D
//   3. Drop ASatelliteCloudFeeder in level, assign CloudTexture
//   4. PIE -> UDS volumetric clouds match texture
//
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SatelliteCloudFeeder.generated.h"

class UTexture2D;
class UTextureRenderTarget2D;

/**
 * Drives UDS cloud coverage from a satellite/test cloud texture via reflection.
 * White = full cloud, Black = clear sky.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup=(EagleCloud))
class EAGLECLOUD_API ASatelliteCloudFeeder : public AActor
{
    GENERATED_BODY()

public:
    ASatelliteCloudFeeder();

    /** Source cloud cover texture. Grayscale: white=cloud, black=clear. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud")
    TObjectPtr<UTexture2D> CloudTexture = nullptr;

    /** If true, automatically apply on BeginPlay. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud")
    bool bApplyOnBeginPlay = true;

    /** Refresh interval in seconds. 0 = apply once. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud", meta = (ClampMin = "0"))
    float RefreshIntervalSeconds = 0.f;

    /**
     * UDS painted-coverage opacity (0..1). 1 = texture fully drives clouds.
     * Maps to UDS "Painted Cloud Coverage Opacity".
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud", meta = (ClampMin = "0", ClampMax = "1"))
    float PaintedOpacity = 1.0f;

    /**
     * How much painted coverage replaces UDS global cloud coverage (0..1).
     * Maps to UDS "Painted Coverage Affects Global Values". 1 = total override.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EagleCloud", meta = (ClampMin = "0", ClampMax = "1"))
    float AffectsGlobalValues = 1.0f;

    /** Push CloudTexture into UDS. Returns true on success. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "EagleCloud")
    bool ApplyToUDS();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY(Transient) TObjectPtr<AActor> CachedUDS = nullptr;
    float TimeSinceRefresh = 0.f;

    /** Find Ultra_Dynamic_Sky actor in world by class name. */
    AActor* FindUDSActor() const;

    /** Read Cloud Coverage Render Target from UDS via reflection. */
    UTextureRenderTarget2D* GetUDSCloudRT(AActor* UDS) const;

    /** Enable UDS painting flags + force the RT to be allocated. */
    void EnableUDSPainting(AActor* UDS) const;

    /** Draw CloudTexture into the given RT using a Canvas. */
    bool DrawTextureToRT(UTextureRenderTarget2D* RT) const;
};
