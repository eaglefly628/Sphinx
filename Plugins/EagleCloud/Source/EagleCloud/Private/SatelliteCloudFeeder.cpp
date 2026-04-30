// SatelliteCloudFeeder.cpp
#include "SatelliteCloudFeeder.h"
#include "EagleCloudModule.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Canvas.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "UObject/UnrealType.h"

namespace
{
    // ---------- Reflection helpers ----------

    void SetFloatProp(AActor* Actor, FName Name, float Value)
    {
        if (!Actor) return;
        FProperty* P = Actor->GetClass()->FindPropertyByName(Name);
        if (!P)
        {
            UE_LOG(LogEagleCloud, Warning, TEXT("Float prop '%s' not found on %s"),
                   *Name.ToString(), *Actor->GetName());
            return;
        }
        if (FFloatProperty* FP = CastField<FFloatProperty>(P))
            FP->SetPropertyValue_InContainer(Actor, Value);
        else if (FDoubleProperty* DP = CastField<FDoubleProperty>(P))
            DP->SetPropertyValue_InContainer(Actor, static_cast<double>(Value));
    }

    void SetBoolProp(AActor* Actor, FName Name, bool Value)
    {
        if (!Actor) return;
        FBoolProperty* BP = CastField<FBoolProperty>(
            Actor->GetClass()->FindPropertyByName(Name));
        if (BP)
            BP->SetPropertyValue_InContainer(Actor, Value);
        else
            UE_LOG(LogEagleCloud, Warning, TEXT("Bool prop '%s' not found on %s"),
                   *Name.ToString(), *Actor->GetName());
    }

    UObject* GetObjectProp(AActor* Actor, FName Name)
    {
        if (!Actor) return nullptr;
        FObjectProperty* OP = CastField<FObjectProperty>(
            Actor->GetClass()->FindPropertyByName(Name));
        if (!OP) return nullptr;
        return OP->GetObjectPropertyValue_InContainer(Actor);
    }

    void SetVector2DStructProp(AActor* Actor, FName Name, const FVector2D& Value)
    {
        if (!Actor) return;
        FStructProperty* SP = CastField<FStructProperty>(
            Actor->GetClass()->FindPropertyByName(Name));
        if (!SP)
        {
            UE_LOG(LogEagleCloud, Warning, TEXT("Vector2D struct prop '%s' not found on %s"),
                   *Name.ToString(), *Actor->GetName());
            return;
        }
        if (FVector2D* Ptr = SP->ContainerPtrToValuePtr<FVector2D>(Actor))
        {
            *Ptr = Value;
        }
    }

    // ---------- Geo helpers ----------

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
    UTexture2D* SourceTex = GlobalCloudTexture ? GlobalCloudTexture.Get() : CloudTexture.Get();
    if (!SourceTex)
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("ApplyToUDS: no CloudTexture or GlobalCloudTexture assigned"));
        return false;
    }

    if (!CachedUDS)
    {
        CachedUDS = FindUDSActor();
    }
    if (!CachedUDS)
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("ApplyToUDS: Ultra_Dynamic_Sky actor not found in level"));
        return false;
    }

    EnableUDSPainting(CachedUDS);

    UTextureRenderTarget2D* RT = GetUDSCloudRT(CachedUDS);
    if (!RT)
    {
        UE_LOG(LogEagleCloud, Warning,
               TEXT("ApplyToUDS: UDS Cloud Coverage RT not yet allocated. ")
               TEXT("Will retry next refresh — set RefreshIntervalSeconds > 0."));
        return false;
    }

    // Branch on mode
    if (GlobalCloudTexture)
    {
        const FVector2D LatLon = GetSampleCenterLatLon();
        const FVector2D WorldXY = GetSampleCenterWorldXY();

        // Move UDS painted RT window to follow camera
        SetUDSTargetLocation(CachedUDS, WorldXY);

        return DrawGlobalRegionToRT(RT, LatLon.X, LatLon.Y);
    }
    else
    {
        return DrawLocalTextureToRT(RT);
    }
}

AActor* ASatelliteCloudFeeder::FindUDSActor() const
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), Found);
    for (AActor* A : Found)
    {
        if (A && A->GetClass()->GetName().Contains(TEXT("Ultra_Dynamic_Sky")))
        {
            return A;
        }
    }
    return nullptr;
}

UTextureRenderTarget2D* ASatelliteCloudFeeder::GetUDSCloudRT(AActor* UDS) const
{
    UObject* Obj = GetObjectProp(UDS, FName("Cloud Coverage Render Target"));
    return Cast<UTextureRenderTarget2D>(Obj);
}

void ASatelliteCloudFeeder::EnableUDSPainting(AActor* UDS) const
{
    SetBoolProp(UDS, FName("Cloud Painting Active"), true);
    SetBoolProp(UDS, FName("Force Cloud Coverage Target Active"), true);
    SetFloatProp(UDS, FName("Painted Cloud Coverage Opacity"), PaintedOpacity);
    SetFloatProp(UDS, FName("Painted Coverage Affects Global Values"), AffectsGlobalValues);
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
    // Convert UE world XY (cm) to lat/lon offset from origin via flat-earth approximation.
    // Sufficient for 200km-scale window. Replace with Cesium georef when integrated.
    const FVector2D WorldXY = GetSampleCenterWorldXY();
    const double XKm = WorldXY.X * 0.00001;  // cm -> km
    const double YKm = WorldXY.Y * 0.00001;

    // UE: +X = east (longitude), +Y = south (latitude decreases) by typical convention.
    // Adjust if your project uses different orientation.
    const double dLatDeg = -YKm / KM_PER_DEG_LAT;
    const double cosLat  = FMath::Cos(FMath::DegreesToRadians(OriginLatitude));
    const double dLonDeg = (FMath::Abs(cosLat) < 1e-6)
                           ? 0.0
                           : XKm / (KM_PER_DEG_LAT * cosLat);

    const double Lat = ClampLat(OriginLatitude + dLatDeg);
    const double Lon = WrapLon(OriginLongitude + dLonDeg);
    return FVector2D(Lat, Lon);
}

void ASatelliteCloudFeeder::SetUDSTargetLocation(AActor* UDS, const FVector2D& WorldXY) const
{
    SetVector2DStructProp(UDS, FName("Cloud Coverage Target Location"), WorldXY);
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
        FVector2D::ZeroVector,
        Size,
        FVector2D::ZeroVector,
        FVector2D::UnitVector,
        FLinearColor::White,
        BLEND_Opaque,
        0.f,
        FVector2D(0.5f, 0.5f)
    );

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
    //
    // Equirectangular UV: U = lon/360 + 0.5, V = 0.5 - lat/180  (north at top)
    const double HalfLatDeg = CoverageRadiusKm / KM_PER_DEG_LAT;
    const double cosLat     = FMath::Cos(FMath::DegreesToRadians(ClampLat(CenterLat)));
    const double HalfLonDeg = (FMath::Abs(cosLat) < 1e-6)
                              ? 180.0  // at poles, the whole longitude band collapses
                              : CoverageRadiusKm / (KM_PER_DEG_LAT * cosLat);

    const double LonMin = CenterLon - HalfLonDeg;
    const double LonMax = CenterLon + HalfLonDeg;
    // V is flipped (north = top of texture)
    const double LatTop = ClampLat(CenterLat + HalfLatDeg);
    const double LatBot = ClampLat(CenterLat - HalfLatDeg);

    const double UMin = LonMin / 360.0 + 0.5;
    const double VMin = 0.5 - LatTop / 180.0;
    const double USize = (LonMax - LonMin) / 360.0;
    const double VSize = (LatTop - LatBot) / 180.0;

    UCanvas* Canvas = nullptr;
    FVector2D Size;
    FDrawToRenderTargetContext Context;

    UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, RT, Canvas, Size, Context);
    if (!Canvas)
    {
        UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
        return false;
    }

    // K2_DrawTexture supports UV outside [0,1] — relies on the source texture's
    // address mode. Set GlobalCloudTexture to: X=Wrap (longitude is cyclic),
    // Y=Clamp (latitude poles are not cyclic). See plugin README.
    Canvas->K2_DrawTexture(
        GlobalCloudTexture,
        FVector2D::ZeroVector,                           // ScreenPosition
        Size,                                            // ScreenSize (full RT)
        FVector2D(UMin, VMin),                           // UV start
        FVector2D(USize, VSize),                         // UV size
        FLinearColor::White,
        BLEND_Opaque,
        0.f,
        FVector2D(0.5f, 0.5f)
    );

    UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
    return true;
}
