// SatelliteCloudFeeder.cpp
#include "SatelliteCloudFeeder.h"
#include "EagleCloudModule.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Canvas.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"

namespace
{
    // ---- Reflection helpers ----

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
}

ASatelliteCloudFeeder::ASatelliteCloudFeeder()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ASatelliteCloudFeeder::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogEagleCloud, Log, TEXT("===== SatelliteCloudFeeder START ====="));
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
    if (!CloudTexture)
    {
        UE_LOG(LogEagleCloud, Warning, TEXT("ApplyToUDS: CloudTexture not assigned"));
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

    // Step 1: enable painting flags so UDS allocates and uses the RT
    EnableUDSPainting(CachedUDS);

    // Step 2: get the RT (may be null if UDS hasn't allocated yet on this frame)
    UTextureRenderTarget2D* RT = GetUDSCloudRT(CachedUDS);
    if (!RT)
    {
        UE_LOG(LogEagleCloud, Warning,
               TEXT("ApplyToUDS: UDS Cloud Coverage Render Target not yet allocated. ")
               TEXT("Will retry — set RefreshIntervalSeconds > 0 or call ApplyToUDS again next frame."));
        return false;
    }

    // Step 3: draw our cloud texture into UDS RT
    const bool bDrawn = DrawTextureToRT(RT);
    if (bDrawn)
    {
        UE_LOG(LogEagleCloud, Log,
               TEXT("ApplyToUDS: drew '%s' (%dx%d) into UDS RT (%dx%d)"),
               *CloudTexture->GetName(),
               CloudTexture->GetSizeX(), CloudTexture->GetSizeY(),
               RT->SizeX, RT->SizeY);
    }
    return bDrawn;
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
    // From UDS property dump:
    //   Cloud Painting Active                  (bool)
    //   Force Cloud Coverage Target Active     (bool)  -> forces RT allocation
    //   Painted Cloud Coverage Opacity         (double, 0..1)
    //   Painted Coverage Affects Global Values (double, 0..1)
    SetBoolProp(UDS, FName("Cloud Painting Active"), true);
    SetBoolProp(UDS, FName("Force Cloud Coverage Target Active"), true);
    SetFloatProp(UDS, FName("Painted Cloud Coverage Opacity"), PaintedOpacity);
    SetFloatProp(UDS, FName("Painted Coverage Affects Global Values"), AffectsGlobalValues);
}

bool ASatelliteCloudFeeder::DrawTextureToRT(UTextureRenderTarget2D* RT) const
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
