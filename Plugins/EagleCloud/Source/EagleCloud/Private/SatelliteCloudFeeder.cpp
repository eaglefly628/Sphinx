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

    void SetFloatProp(AActor* Actor, FName Name, float Value, bool bVerbose)
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
        {
            FP->SetPropertyValue_InContainer(Actor, Value);
            if (bVerbose)
                UE_LOG(LogEagleCloud, Log, TEXT("    [+] SetFloat '%s' = %.3f  (FFloatProperty)"),
                       *Name.ToString(), Value);
        }
        else if (FDoubleProperty* DP = CastField<FDoubleProperty>(P))
        {
            DP->SetPropertyValue_InContainer(Actor, static_cast<double>(Value));
            if (bVerbose)
                UE_LOG(LogEagleCloud, Log, TEXT("    [+] SetFloat '%s' = %.3f  (FDoubleProperty)"),
                       *Name.ToString(), Value);
        }
        else
        {
            UE_LOG(LogEagleCloud, Warning,
                   TEXT("Prop '%s' on %s exists but is not Float/Double (CPP type=%s)"),
                   *Name.ToString(), *Actor->GetName(), *P->GetClass()->GetName());
        }
    }

    void SetBoolProp(AActor* Actor, FName Name, bool Value, bool bVerbose)
    {
        if (!Actor) return;
        FBoolProperty* BP = CastField<FBoolProperty>(
            Actor->GetClass()->FindPropertyByName(Name));
        if (BP)
        {
            BP->SetPropertyValue_InContainer(Actor, Value);
            if (bVerbose)
                UE_LOG(LogEagleCloud, Log, TEXT("    [+] SetBool '%s' = %s"),
                       *Name.ToString(), Value ? TEXT("true") : TEXT("false"));
        }
        else
        {
            UE_LOG(LogEagleCloud, Warning, TEXT("Bool prop '%s' not found on %s"),
                   *Name.ToString(), *Actor->GetName());
        }
    }

    UObject* GetObjectProp(AActor* Actor, FName Name, bool bVerbose)
    {
        if (!Actor) return nullptr;
        FObjectProperty* OP = CastField<FObjectProperty>(
            Actor->GetClass()->FindPropertyByName(Name));
        if (!OP)
        {
            if (bVerbose)
                UE_LOG(LogEagleCloud, Warning,
                       TEXT("Object prop '%s' not found on %s (no FObjectProperty by this name)"),
                       *Name.ToString(), *Actor->GetName());
            return nullptr;
        }
        UObject* Obj = OP->GetObjectPropertyValue_InContainer(Actor);
        if (bVerbose)
            UE_LOG(LogEagleCloud, Log, TEXT("    [?] GetObject '%s' -> %s"),
                   *Name.ToString(),
                   Obj ? *Obj->GetName() : TEXT("nullptr"));
        return Obj;
    }

    void SetVector2DStructProp(AActor* Actor, FName Name, const FVector2D& Value, bool bVerbose)
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
            if (bVerbose)
                UE_LOG(LogEagleCloud, Log, TEXT("    [+] SetVec2D '%s' = (%.1f, %.1f)"),
                       *Name.ToString(), Value.X, Value.Y);
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
    if (bVerboseLogging)
    {
        UE_LOG(LogEagleCloud, Log, TEXT("--- ApplyToUDS tick ---"));
    }

    UTexture2D* SourceTex = GlobalCloudTexture ? GlobalCloudTexture.Get() : CloudTexture.Get();
    if (!SourceTex)
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("ApplyToUDS: no CloudTexture or GlobalCloudTexture assigned"));
        return false;
    }
    if (bVerboseLogging)
    {
        UE_LOG(LogEagleCloud, Log, TEXT("  source: %s (%dx%d)"),
               *SourceTex->GetName(), SourceTex->GetSizeX(), SourceTex->GetSizeY());
    }

    if (!CachedUDS.IsValid())
    {
        CachedUDS = FindUDSActor();
    }
    if (!CachedUDS.IsValid())
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("ApplyToUDS: Ultra_Dynamic_Sky actor not found in level"));
        return false;
    }
    if (bVerboseLogging)
    {
        UE_LOG(LogEagleCloud, Log, TEXT("  UDS: %s (class=%s)"),
               *CachedUDS->GetName(), *CachedUDS->GetClass()->GetName());
    }

    EnableUDSPainting(CachedUDS.Get());

    UTextureRenderTarget2D* RT = GetUDSCloudRT(CachedUDS.Get());
    if (!RT)
    {
        UE_LOG(LogEagleCloud, Warning,
               TEXT("ApplyToUDS: UDS Cloud Coverage RT not yet allocated. ")
               TEXT("Will retry next refresh — set RefreshIntervalSeconds > 0. ")
               TEXT("Or the UDS property name doesn't match — call DumpUDSState to verify."));
        return false;
    }
    if (bVerboseLogging)
    {
        UE_LOG(LogEagleCloud, Log, TEXT("  RT: %s (%dx%d)"),
               *RT->GetName(), RT->SizeX, RT->SizeY);
    }

    bool bDrawn = false;
    if (GlobalCloudTexture)
    {
        const FVector2D LatLon = GetSampleCenterLatLon();
        const FVector2D WorldXY = GetSampleCenterWorldXY();
        if (bVerboseLogging)
        {
            UE_LOG(LogEagleCloud, Log, TEXT("  sample center: lat=%.4f, lon=%.4f, worldXY=(%.0f, %.0f) cm"),
                   LatLon.X, LatLon.Y, WorldXY.X, WorldXY.Y);
        }

        // Move UDS painted RT window to follow camera
        SetUDSTargetLocation(CachedUDS.Get(), WorldXY);

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
    if (!CachedUDS.IsValid())
    {
        CachedUDS = FindUDSActor();
    }
    if (CachedUDS.IsValid())
    {
        // SyncPropertiesToUDS is called from AAtmosphereCloudManager every tick;
        // suppress per-call logging here to avoid spam regardless of bVerboseLogging.
        const bool bWasVerbose = bVerboseLogging;
        bVerboseLogging = false;
        EnableUDSPainting(CachedUDS.Get());
        bVerboseLogging = bWasVerbose;
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
    UObject* Obj = GetObjectProp(UDS, FName("Cloud Coverage Render Target"), bVerboseLogging);
    if (!Obj && bVerboseLogging)
    {
        UE_LOG(LogEagleCloud, Warning,
               TEXT("    [!] GetUDSCloudRT: 'Cloud Coverage Render Target' is null on %s ")
               TEXT("(prop missing OR object pointer empty). Run DumpUDSState to confirm."),
               UDS ? *UDS->GetName() : TEXT("nullptr"));
    }
    UTextureRenderTarget2D* RT = Cast<UTextureRenderTarget2D>(Obj);
    if (Obj && !RT && bVerboseLogging)
    {
        UE_LOG(LogEagleCloud, Warning,
               TEXT("    [!] GetUDSCloudRT: prop returned %s (class=%s), not UTextureRenderTarget2D"),
               *Obj->GetName(), *Obj->GetClass()->GetName());
    }
    return RT;
}

void ASatelliteCloudFeeder::EnableUDSPainting(AActor* UDS) const
{
    if (bVerboseLogging)
    {
        UE_LOG(LogEagleCloud, Log, TEXT("  EnableUDSPainting on %s:"), *UDS->GetName());
    }
    SetBoolProp (UDS, FName("Cloud Painting Active"),                  true,                bVerboseLogging);
    SetBoolProp (UDS, FName("Force Cloud Coverage Target Active"),     true,                bVerboseLogging);
    SetFloatProp(UDS, FName("Painted Cloud Coverage Opacity"),         PaintedOpacity,      bVerboseLogging);
    SetFloatProp(UDS, FName("Painted Coverage Affects Global Values"), AffectsGlobalValues, bVerboseLogging);
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
    SetVector2DStructProp(UDS, FName("Cloud Coverage Target Location"), WorldXY, bVerboseLogging);
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
        UE_LOG(LogEagleCloud, Warning, TEXT("  DrawGlobalRegionToRT: BeginDrawCanvasToRenderTarget returned null Canvas"));
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

void ASatelliteCloudFeeder::DumpUDSState()
{
    AActor* UDS = FindUDSActor();
    if (!UDS)
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("DumpUDSState: Ultra_Dynamic_Sky actor not found in level"));
        return;
    }

    UE_LOG(LogEagleCloud, Log, TEXT("===== DumpUDSState ====="));
    UE_LOG(LogEagleCloud, Log, TEXT("UDS actor: %s  (class=%s)"),
           *UDS->GetName(), *UDS->GetClass()->GetName());

    UE_LOG(LogEagleCloud, Log, TEXT("--- Probing the names EagleCloud uses ---"));

    auto ProbeBool = [UDS](const TCHAR* Key)
    {
        FBoolProperty* P = CastField<FBoolProperty>(UDS->GetClass()->FindPropertyByName(FName(Key)));
        if (P)
        {
            const bool V = P->GetPropertyValue_InContainer(UDS);
            UE_LOG(LogEagleCloud, Log, TEXT("  [bool ] '%s' = %s"),
                   Key, V ? TEXT("true") : TEXT("false"));
        }
        else
        {
            UE_LOG(LogEagleCloud, Warning, TEXT("  [bool ] '%s' NOT FOUND"), Key);
        }
    };

    auto ProbeFloat = [UDS](const TCHAR* Key)
    {
        FProperty* P = UDS->GetClass()->FindPropertyByName(FName(Key));
        if (FFloatProperty* FP = CastField<FFloatProperty>(P))
            UE_LOG(LogEagleCloud, Log, TEXT("  [float] '%s' = %.3f  (FFloat)"),
                   Key, FP->GetPropertyValue_InContainer(UDS));
        else if (FDoubleProperty* DP = CastField<FDoubleProperty>(P))
            UE_LOG(LogEagleCloud, Log, TEXT("  [float] '%s' = %.3f  (FDouble)"),
                   Key, DP->GetPropertyValue_InContainer(UDS));
        else if (P)
            UE_LOG(LogEagleCloud, Warning, TEXT("  [float] '%s' exists but type=%s"),
                   Key, *P->GetClass()->GetName());
        else
            UE_LOG(LogEagleCloud, Warning, TEXT("  [float] '%s' NOT FOUND"), Key);
    };

    auto ProbeObject = [UDS](const TCHAR* Key)
    {
        FObjectProperty* OP = CastField<FObjectProperty>(UDS->GetClass()->FindPropertyByName(FName(Key)));
        if (!OP)
        {
            UE_LOG(LogEagleCloud, Warning, TEXT("  [obj  ] '%s' NOT FOUND"), Key);
            return;
        }
        UObject* Obj = OP->GetObjectPropertyValue_InContainer(UDS);
        UE_LOG(LogEagleCloud, Log, TEXT("  [obj  ] '%s' -> %s  (class=%s)"),
               Key,
               Obj ? *Obj->GetName() : TEXT("nullptr"),
               Obj ? *Obj->GetClass()->GetName() : TEXT("-"));
    };

    auto ProbeEnum = [UDS](const TCHAR* Key)
    {
        FProperty* P = UDS->GetClass()->FindPropertyByName(FName(Key));
        if (FByteProperty* BP = CastField<FByteProperty>(P))
        {
            const uint8 V = BP->GetPropertyValue_InContainer(UDS);
            UE_LOG(LogEagleCloud, Log, TEXT("  [byte ] '%s' = %d  (FByteProperty)"), Key, V);
        }
        else if (FEnumProperty* EP = CastField<FEnumProperty>(P))
        {
            const void* Ptr = EP->ContainerPtrToValuePtr<void>(UDS);
            const int64 V = (Ptr && EP->GetUnderlyingProperty())
                            ? EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(Ptr)
                            : -1;
            UE_LOG(LogEagleCloud, Log, TEXT("  [enum ] '%s' = %lld  (FEnumProperty)"), Key, V);
        }
        else if (P)
            UE_LOG(LogEagleCloud, Warning, TEXT("  [enum ] '%s' exists but type=%s"),
                   Key, *P->GetClass()->GetName());
        else
            UE_LOG(LogEagleCloud, Warning, TEXT("  [enum ] '%s' NOT FOUND"), Key);
    };

    // Properties Feeder writes to
    ProbeBool  (TEXT("Cloud Painting Active"));
    ProbeBool  (TEXT("Force Cloud Coverage Target Active"));
    ProbeFloat (TEXT("Painted Cloud Coverage Opacity"));
    ProbeFloat (TEXT("Painted Coverage Affects Global Values"));
    ProbeObject(TEXT("Cloud Coverage Render Target"));

    // Properties AAtmosphereCloudManager writes to
    ProbeEnum  (TEXT("Sky Mode"));

    // UDS rendering config to confirm
    ProbeEnum  (TEXT("Cloud Type"));
    ProbeEnum  (TEXT("Cloud Render Mode"));
    ProbeFloat (TEXT("Cloud Coverage"));
    ProbeFloat (TEXT("Bottom Altitude"));

    // Enumerate every UDS property whose name hints at clouds — so we can spot
    // the real names if our reflection strings are wrong.
    UE_LOG(LogEagleCloud, Log, TEXT("--- All UDS props matching cloud/paint/coverage/sky/RT ---"));
    int32 Hits = 0;
    for (TFieldIterator<FProperty> It(UDS->GetClass()); It; ++It)
    {
        FProperty* P = *It;
        if (!P) continue;
        const FString N = P->GetName();
        const bool bMatch =
            N.Contains(TEXT("cloud"),    ESearchCase::IgnoreCase) ||
            N.Contains(TEXT("paint"),    ESearchCase::IgnoreCase) ||
            N.Contains(TEXT("coverage"), ESearchCase::IgnoreCase) ||
            N.Contains(TEXT("sky"),      ESearchCase::IgnoreCase) ||
            N.Contains(TEXT("render target"), ESearchCase::IgnoreCase);
        if (bMatch)
        {
            UE_LOG(LogEagleCloud, Log, TEXT("  - '%s'  CPP=%s"),
                   *N, *P->GetClass()->GetName());
            Hits++;
        }
    }
    UE_LOG(LogEagleCloud, Log, TEXT("--- (%d matching props) ---"), Hits);
    UE_LOG(LogEagleCloud, Log, TEXT("===== End DumpUDSState ====="));
}
