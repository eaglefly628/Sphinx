// GISWorldBuilder.cpp - 纯 C++ 版世界构建器实现
#include "Runtime/GISWorldBuilder.h"
#include "Components/GISPolygonComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

AGISWorldBuilder::AGISWorldBuilder()
{
    PrimaryActorTick.bCanEverTick = false;

    // 默认道路等级权重
    RoadClassWeights.Add(TEXT("motorway"), 100);
    RoadClassWeights.Add(TEXT("trunk"), 80);
    RoadClassWeights.Add(TEXT("primary"), 60);
    RoadClassWeights.Add(TEXT("secondary"), 40);
    RoadClassWeights.Add(TEXT("tertiary"), 20);
    RoadClassWeights.Add(TEXT("residential"), 10);
    RoadClassWeights.Add(TEXT("unclassified"), 5);
}

void AGISWorldBuilder::BeginPlay()
{
    Super::BeginPlay();

    if (bAutoGenerateOnPlay)
    {
        GenerateAll();
    }
}

void AGISWorldBuilder::InitDeriver()
{
    if (!PolygonDeriver)
    {
        PolygonDeriver = NewObject<UPolygonDeriver>(this);
    }

    PolygonDeriver->MinPolygonArea = MinPolygonArea;
    PolygonDeriver->ClassifyRules = ClassifyRules;
    PolygonDeriver->RoadClassWeights = RoadClassWeights;
}

void AGISWorldBuilder::GenerateAll()
{
    ClearGenerated();
    InitDeriver();

    // 拼接完整路径
    const FString FullGeoJsonPath = FPaths::Combine(
        FPaths::ProjectContentDir(), RoadsGeoJsonPath);
    const FString FullDEMPath = DEMFilePath.IsEmpty()
        ? FString()
        : FPaths::Combine(FPaths::ProjectContentDir(), DEMFilePath);

    UE_LOG(LogTemp, Log, TEXT("GISWorldBuilder: Generating from %s"), *FullGeoJsonPath);

    // 同步生成
    GeneratedPolygons = PolygonDeriver->GeneratePolygons(
        FullGeoJsonPath,
        FullDEMPath,
        OriginLongitude,
        OriginLatitude
    );

    UE_LOG(LogTemp, Log, TEXT("GISWorldBuilder: Generated %d polygons"), GeneratedPolygons.Num());

    // 生成子 Actor
    if (bSpawnPerPolygonActors)
    {
        SpawnPolygonActors(GeneratedPolygons);
    }

    // 调试绘制
    if (bDrawDebugPolygons)
    {
        DrawDebugPolygons(GeneratedPolygons);
    }

    PrintStatistics();
}

void AGISWorldBuilder::GenerateAllAsync()
{
    ClearGenerated();
    InitDeriver();

    const FString FullGeoJsonPath = FPaths::Combine(
        FPaths::ProjectContentDir(), RoadsGeoJsonPath);
    const FString FullDEMPath = DEMFilePath.IsEmpty()
        ? FString()
        : FPaths::Combine(FPaths::ProjectContentDir(), DEMFilePath);

    FOnPolygonsGenerated Callback;
    Callback.BindDynamic(this, &AGISWorldBuilder::OnPolygonsGenerated);

    PolygonDeriver->GeneratePolygonsAsync(
        FullGeoJsonPath, FullDEMPath,
        OriginLongitude, OriginLatitude,
        Callback
    );
}

void AGISWorldBuilder::OnPolygonsGenerated(const TArray<FLandUsePolygon>& Polygons)
{
    GeneratedPolygons = Polygons;

    UE_LOG(LogTemp, Log, TEXT("GISWorldBuilder: Async generated %d polygons"), Polygons.Num());

    if (bSpawnPerPolygonActors)
    {
        SpawnPolygonActors(GeneratedPolygons);
    }

    if (bDrawDebugPolygons)
    {
        DrawDebugPolygons(GeneratedPolygons);
    }

    PrintStatistics();
}

void AGISWorldBuilder::ClearGenerated()
{
    // 销毁已生成的子 Actor
    for (AActor* Actor : SpawnedActors)
    {
        if (IsValid(Actor))
        {
            Actor->Destroy();
        }
    }
    SpawnedActors.Empty();
    GeneratedPolygons.Empty();

    // 清除调试绘制
    if (UWorld* World = GetWorld())
    {
        FlushPersistentDebugLines(World);
    }
}

TArray<FLandUsePolygon> AGISWorldBuilder::GetPolygonsByType(ELandUseType Type) const
{
    TArray<FLandUsePolygon> Result;
    for (const FLandUsePolygon& Poly : GeneratedPolygons)
    {
        if (Poly.LandUseType == Type)
        {
            Result.Add(Poly);
        }
    }
    return Result;
}

void AGISWorldBuilder::SpawnPolygonActors(const TArray<FLandUsePolygon>& Polygons)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    for (const FLandUsePolygon& Poly : Polygons)
    {
        FActorSpawnParameters Params;
        Params.Owner = this;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        AActor* PolygonActor = World->SpawnActor<AActor>(
            AActor::StaticClass(),
            Poly.WorldCenter,
            FRotator::ZeroRotator,
            Params
        );

        if (PolygonActor)
        {
            // 设置名称
            FString ActorLabel = FString::Printf(TEXT("GIS_Poly_%d_%s"),
                Poly.PolygonID,
                *UEnum::GetDisplayValueAsText(Poly.LandUseType).ToString());

#if WITH_EDITOR
            PolygonActor->SetActorLabel(ActorLabel);
#endif

            // 添加 GISPolygonComponent
            UGISPolygonComponent* Comp = NewObject<UGISPolygonComponent>(
                PolygonActor, UGISPolygonComponent::StaticClass(),
                FName(*ActorLabel));
            Comp->SetPolygonData(Poly);
            Comp->RegisterComponent();
            PolygonActor->AddInstanceComponent(Comp);

            // 挂载到 WorldBuilder 下
            PolygonActor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);

            SpawnedActors.Add(PolygonActor);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("GISWorldBuilder: Spawned %d polygon actors"), SpawnedActors.Num());
}

void AGISWorldBuilder::DrawDebugPolygons(const TArray<FLandUsePolygon>& Polygons) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    for (const FLandUsePolygon& Poly : Polygons)
    {
        const FColor Color = GetLandUseColor(Poly.LandUseType);
        const int32 N = Poly.WorldVertices.Num();

        // 画边框
        for (int32 i = 0; i < N; ++i)
        {
            const int32 Next = (i + 1) % N;
            DrawDebugLine(
                World,
                Poly.WorldVertices[i],
                Poly.WorldVertices[Next],
                Color,
                (DebugDrawDuration < 0.0f),  // persistent if duration < 0
                DebugDrawDuration,
                0,
                4.0f
            );
        }

        // 在中心画标签
        DrawDebugString(
            World,
            Poly.WorldCenter + FVector(0, 0, 200),
            FString::Printf(TEXT("#%d %s\n%.0fm2"),
                Poly.PolygonID,
                *UEnum::GetDisplayValueAsText(Poly.LandUseType).ToString(),
                Poly.AreaSqM),
            nullptr,
            Color,
            (DebugDrawDuration < 0.0f) ? -1.0f : DebugDrawDuration,
            false,
            1.2f
        );
    }
}

void AGISWorldBuilder::PrintStatistics() const
{
    if (GeneratedPolygons.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("GISWorldBuilder: No polygons generated"));
        return;
    }

    // 按类型统计
    TMap<ELandUseType, int32> CountMap;
    TMap<ELandUseType, float> AreaMap;
    float TotalArea = 0.0f;

    for (const FLandUsePolygon& Poly : GeneratedPolygons)
    {
        CountMap.FindOrAdd(Poly.LandUseType) += 1;
        AreaMap.FindOrAdd(Poly.LandUseType) += Poly.AreaSqM;
        TotalArea += Poly.AreaSqM;
    }

    UE_LOG(LogTemp, Log, TEXT("========== GIS World Builder Statistics =========="));
    UE_LOG(LogTemp, Log, TEXT("Total Polygons: %d | Total Area: %.1f m2"), GeneratedPolygons.Num(), TotalArea);

    for (const auto& Pair : CountMap)
    {
        const FString TypeName = UEnum::GetDisplayValueAsText(Pair.Key).ToString();
        const float TypeArea = AreaMap.FindRef(Pair.Key);
        const float Pct = (TotalArea > 0) ? (TypeArea / TotalArea * 100.0f) : 0.0f;
        UE_LOG(LogTemp, Log, TEXT("  %-12s: %3d polygons | %10.1f m2 | %5.1f%%"),
            *TypeName, Pair.Value, TypeArea, Pct);
    }

    UE_LOG(LogTemp, Log, TEXT("=================================================="));
}

#if WITH_EDITOR
void AGISWorldBuilder::GenerateInEditor()
{
    GenerateAll();
}

void AGISWorldBuilder::ClearEditorPreview()
{
    ClearGenerated();
}
#endif

FColor AGISWorldBuilder::GetLandUseColor(ELandUseType Type)
{
    switch (Type)
    {
        case ELandUseType::Residential: return FColor(255, 235, 59);   // 黄色
        case ELandUseType::Commercial:  return FColor(244, 67, 54);    // 红色
        case ELandUseType::Industrial:  return FColor(255, 152, 0);    // 橙色
        case ELandUseType::Forest:      return FColor(76, 175, 80);    // 绿色
        case ELandUseType::Farmland:    return FColor(139, 195, 74);   // 浅绿
        case ELandUseType::Water:       return FColor(33, 150, 243);   // 蓝色
        case ELandUseType::Road:        return FColor(158, 158, 158);  // 灰色
        case ELandUseType::OpenSpace:   return FColor(224, 224, 224);  // 浅灰
        case ELandUseType::Military:    return FColor(120, 0, 0);      // 深红
        default:                        return FColor(200, 200, 200);
    }
}
