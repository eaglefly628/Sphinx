// SatelliteCloudFeeder.cpp
//
// All UDS access goes through Bridge (BP-implemented). No FProperty reflection.
//
#include "SatelliteCloudFeeder.h"
#include "EagleCloudModule.h"
#include "EagleCloudUDSBridge.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Canvas.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
    constexpr double KM_PER_DEG_LAT = 111.32;  // mean meridian length per degree

    /** Wrap longitude into [-180, 180]. */
    double WrapLon(double Lon)
    {
        Lon = FMath::Fmod(Lon + 180.0, 360.0);
        if (Lon < 0.0) Lon += 360.0;
        return Lon - 180.0;
    }

    /** Clamp latitude to [-89, 89] (poles cause UV singularity). */
    double ClampLat(double Lat)
    {
        return FMath::Clamp(Lat, -89.0, 89.0);
    }
}

ASatelliteCloudFeeder::ASatelliteCloudFeeder()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ASatelliteCloudFeeder::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogEagleCloud, Log, TEXT("===== SatelliteCloudFeeder START ====="));
    UE_LOG(LogEagleCloud, Log, TEXT("Mode: %s"),
           GlobalCloudTexture ? TEXT("GLOBAL (sample by lat/lon)") :
           CloudTexture       ? TEXT("LOCAL (Phase A)") :
                                TEXT("NONE (no texture assigned)"));

    if (!ensureMsgf(Bridge,
                    TEXT("ASatelliteCloudFeeder: Bridge is null. Drop a BP_EagleCloudBridge "
                         "actor into the level and assign it to the Bridge field.")))
    {
        SetActorTickEnabled(false);
        return;
    }

    const bool bInit = Bridge->InitializeUDS();
    UE_LOG(LogEagleCloud, Log, TEXT("Bridge.InitializeUDS() -> %s"),
           bInit ? TEXT("OK") : TEXT("FAIL (check BP_EagleCloudBridge)"));

    if (GlobalCloudTexture)
    {
        UE_LOG(LogEagleCloud, Log, TEXT("Origin lat/lon: (%.4f, %.4f), CoverageRadius: %.1f km, FollowCamera: %s"),
               OriginLatitude, OriginLongitude, CoverageRadiusKm,
               bFollowPlayerCamera ? TEXT("yes") : TEXT("no"));
    }

    if (bApplyOnBeginPlay)
    {
        ApplyToUDS();
    }
}

void ASatelliteCloudFeeder::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (RefreshIntervalSeconds <= 0.f) return;

    TimeSinceRefresh += DeltaSeconds;
    if (TimeSinceRefresh >= RefreshIntervalSeconds)
    {
        TimeSinceRefresh = 0.f;
        ApplyToUDS();
    }
}

bool ASatelliteCloudFeeder::ApplyToUDS()
{
    if (bVerboseLogging) UE_LOG(LogEagleCloud, Log, TEXT("--- ApplyToUDS tick ---"));

    if (!Bridge)
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("ApplyToUDS: Bridge is null"));
        return false;
    }

    UTexture2D* SourceTex = GlobalCloudTexture ? GlobalCloudTexture.Get() : CloudTexture.Get();
    if (!SourceTex)
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("ApplyToUDS: no CloudTexture or GlobalCloudTexture assigned"));
        return false;
    }

    // Push painted-cloud parameters via the bridge (4 UDS variables in one call).
    Bridge->SetPaintingState(true, true, PaintedOpacity, AffectsGlobalValues);

    // Get UDS's painted-coverage RT via the bridge.
    UTextureRenderTarget2D* RT = Bridge->GetUDSCloudRT();
    if (!RT)
    {
        UE_LOG(LogEagleCloud, Warning,
               TEXT("ApplyToUDS: Bridge.GetUDSCloudRT() returned null. ")
               TEXT("Check BP_EagleCloudBridge.GetUDSCloudRT implementation."));
        return false;
    }
    if (bVerboseLogging)
    {
        UE_LOG(LogEagleCloud, Log, TEXT("  RT: %s (%dx%d)  source: %s (%dx%d)"),
               *RT->GetName(), RT->SizeX, RT->SizeY,
               *SourceTex->GetName(), SourceTex->GetSizeX(), SourceTex->GetSizeY());
    }

    bool bDrawn = false;
    if (GlobalCloudTexture)
    {
        const FVector2D LatLon  = GetSampleCenterLatLon();
        const FVector2D WorldXY = GetSampleCenterWorldXY();
        if (bVerboseLogging)
        {
            UE_LOG(LogEagleCloud, Log, TEXT("  sample center: lat=%.4f lon=%.4f worldXY=(%.0f, %.0f) cm"),
                   LatLon.X, LatLon.Y, WorldXY.X, WorldXY.Y);
        }

        // Slide UDS coverage window with the camera (via bridge).
        Bridge->SetCoverageWindow(WorldXY);

        bDrawn = DrawGlobalRegionToRT(RT, LatLon.X, LatLon.Y);
    }
    else
    {
        bDrawn = DrawLocalTextureToRT(RT);
    }

    if (bVerboseLogging)
    {
        UE_LOG(LogEagleCloud, Log, TEXT("  draw -> %s"), bDrawn ? TEXT("OK") : TEXT("FAIL"));
    }
    return bDrawn;
}

void ASatelliteCloudFeeder::SyncPropertiesToUDS()
{
    if (Bridge)
    {
        Bridge->SetPaintingState(true, true, PaintedOpacity, AffectsGlobalValues);
    }
}

FVector2D ASatelliteCloudFeeder::GetSampleCenterWorldXY() const
{
    if (!bFollowPlayerCamera) return FVector2D::ZeroVector;

    UWorld* World = GetWorld();
    if (!World) return FVector2D::ZeroVector;

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC) return FVector2D::ZeroVector;

    APlayerCameraManager* CamMgr = PC->PlayerCameraManager;
    if (!CamMgr) return FVector2D::ZeroVector;

    const FVector Loc = CamMgr->GetCameraLocation();
    return FVector2D(Loc.X, Loc.Y);
}

FVector2D ASatelliteCloudFeeder::GetSampleCenterLatLon() const
{
    // Convert UE world XY (cm) to lat/lon offset from origin via flat-earth approx.
    // Sufficient for 200km-scale window. Replace with Cesium georef when integrated.
    const FVector2D WorldXY = GetSampleCenterWorldXY();
    const double XKm = WorldXY.X * 0.00001;
    const double YKm = WorldXY.Y * 0.00001;

    const double dLatDeg = -YKm / KM_PER_DEG_LAT;
    const double cosLat  = FMath::Cos(FMath::DegreesToRadians(OriginLatitude));
    const double dLonDeg = (FMath::Abs(cosLat) < 1e-6)
                           ? 0.0
                           : XKm / (KM_PER_DEG_LAT * cosLat);

    return FVector2D(ClampLat(OriginLatitude + dLatDeg),
                     WrapLon(OriginLongitude + dLonDeg));
}

bool ASatelliteCloudFeeder::DrawLocalTextureToRT(UTextureRenderTarget2D* RT) const
{
    if (!RT || !CloudTexture) return false;

    UWorld* World = GetWorld();
    if (!World) return false;

    UCanvas* Canvas = nullptr;
    FVector2D Size;
    FDrawToRenderTargetContext Context;

    UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, RT, Canvas, Size, Context);
    if (!Canvas)
    {
        UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
        return false;
    }

    Canvas->K2_DrawTexture(
        CloudTexture,
        FVector2D::ZeroVector, Size,
        FVector2D::ZeroVector, FVector2D::UnitVector,
        FLinearColor::White, BLEND_Opaque, 0.f, FVector2D(0.5f, 0.5f));

    UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
    return true;
}

bool ASatelliteCloudFeeder::DrawGlobalRegionToRT(
    UTextureRenderTarget2D* RT, double CenterLat, double CenterLon) const
{
    if (!RT || !GlobalCloudTexture) return false;

    UWorld* World = GetWorld();
    if (!World) return false;

    // Compute UV sub-rect on equirectangular global texture for the local
    // (CoverageRadiusKm * 2) box centered on (CenterLat, CenterLon).
    // Equirectangular UV: U = lon/360 + 0.5, V = 0.5 - lat/180 (north at top).
    const double HalfLatDeg = CoverageRadiusKm / KM_PER_DEG_LAT;
    const double cosLat     = FMath::Cos(FMath::DegreesToRadians(ClampLat(CenterLat)));
    const double HalfLonDeg = (FMath::Abs(cosLat) < 1e-6)
                              ? 180.0  // at poles, longitude band collapses
                              : CoverageRadiusKm / (KM_PER_DEG_LAT * cosLat);

    const double LonMin = CenterLon - HalfLonDeg;
    const double LonMax = CenterLon + HalfLonDeg;
    const double LatTop = ClampLat(CenterLat + HalfLatDeg);
    const double LatBot = ClampLat(CenterLat - HalfLatDeg);

    const double UMin  = LonMin / 360.0 + 0.5;
    const double VMin  = 0.5 - LatTop / 180.0;
    const double USize = (LonMax - LonMin) / 360.0;
    const double VSize = (LatTop - LatBot) / 180.0;

    if (bVerboseLogging)
    {
        UE_LOG(LogEagleCloud, Log,
               TEXT("  DrawGlobalRegionToRT: UV start=(%.4f, %.4f) size=(%.4f, %.4f) RT=%dx%d"),
               UMin, VMin, USize, VSize, RT->SizeX, RT->SizeY);
    }

    UCanvas* Canvas = nullptr;
    FVector2D Size;
    FDrawToRenderTargetContext Context;

    UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, RT, Canvas, Size, Context);
    if (!Canvas)
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("  BeginDrawCanvasToRenderTarget returned null Canvas"));
        UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
        return false;
    }

    // K2_DrawTexture supports UV outside [0,1] — relies on the source texture's
    // address mode. Set GlobalCloudTexture to: X=Wrap (longitude is cyclic),
    // Y=Clamp (latitude poles are not cyclic).
    Canvas->K2_DrawTexture(
        GlobalCloudTexture,
        FVector2D::ZeroVector, Size,
        FVector2D(UMin, VMin), FVector2D(USize, VSize),
        FLinearColor::White, BLEND_Opaque, 0.f, FVector2D(0.5f, 0.5f));

    UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
    return true;
}
