// GISWorldBuilder.cpp - 世界构建器实现
#include "Runtime/GISWorldBuilder.h"
#include "Components/GISPolygonComponent.h"
#include "Data/LocalFileProvider.h"
#include "Data/ArcGISRestProvider.h"
#include "Data/LandUseMapDataAsset.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

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
    PolygonDeriver->DEMFormat = DEMFormat;
    PolygonDeriver->TerrainAnalysisResolution = TerrainAnalysisResolution;
}

IGISDataProvider* AGISWorldBuilder::CreateDataProvider()
{
    switch (DataSourceType)
    {
        case EGISDataSourceType::LocalFile:
        {
            if (!LocalFileProviderInstance)
            {
                LocalFileProviderInstance = NewObject<ULocalFileProvider>(this);
            }
            LocalFileProviderInstance->GeoJsonFilePath =
                FPaths::Combine(FPaths::ProjectContentDir(), GeoJsonPath);
            LocalFileProviderInstance->DEMPath =
                FPaths::Combine(FPaths::ProjectContentDir(), DEMPath);
            LocalFileProviderInstance->DEMFormat = DEMFormat;
            return LocalFileProviderInstance;
        }

        case EGISDataSourceType::ArcGISRest:
        {
            if (!ArcGISRestProviderInstance)
            {
                ArcGISRestProviderInstance = NewObject<UArcGISRestProvider>(this);
            }
            ArcGISRestProviderInstance->FeatureServiceUrl = FeatureServiceUrl;
            ArcGISRestProviderInstance->ApiKey = ArcGISApiKey;
            ArcGISRestProviderInstance->AdditionalLayerUrls = AdditionalLayerUrls;
            return ArcGISRestProviderInstance;
        }

        case EGISDataSourceType::DataAsset:
        default:
            return nullptr;
    }
}

void AGISWorldBuilder::GenerateAll()
{
    ClearGenerated();

    // DataAsset 模式：直接加载
    if (DataSourceType == EGISDataSourceType::DataAsset)
    {
        LoadFromDataAsset();
        PostGenerate();
        return;
    }

    // DataProvider 模式
    InitDeriver();
    IGISDataProvider* Provider = CreateDataProvider();

    if (Provider)
    {
        PolygonDeriver->SetDataProvider(Provider);
        PolygonDeriver->SetQueryBounds(QueryBounds);

        UE_LOG(LogTemp, Log, TEXT("GISWorldBuilder: Using DataProvider '%s'"), *Provider->GetProviderName());
        GeneratedPolygons = PolygonDeriver->GenerateFromProvider(OriginLongitude, OriginLatitude);
    }
    else
    {
        // 回退到传统文件路径模式
        const FString FullDEMPath = FPaths::Combine(FPaths::ProjectContentDir(), DEMPath);
        const FString FullGeoJsonPath = FPaths::Combine(FPaths::ProjectContentDir(), GeoJsonPath);

        UE_LOG(LogTemp, Log, TEXT("GISWorldBuilder: Fallback to legacy mode DEM=%s, GeoJSON=%s"),
            *FullDEMPath, *FullGeoJsonPath);

        GeneratedPolygons = PolygonDeriver->GeneratePolygons(
            FullDEMPath, FullGeoJsonPath,
            OriginLongitude, OriginLatitude
        );
    }

    PostGenerate();
}

void AGISWorldBuilder::GenerateAllAsync()
{
    ClearGenerated();

    // 异步仅支持传统模式
    InitDeriver();

    const FString FullDEMPath = FPaths::Combine(FPaths::ProjectContentDir(), DEMPath);
    const FString FullGeoJsonPath = FPaths::Combine(FPaths::ProjectContentDir(), GeoJsonPath);

    FOnPolygonsGenerated Callback;
    Callback.BindDynamic(this, &AGISWorldBuilder::OnPolygonsGenerated);

    PolygonDeriver->GeneratePolygonsAsync(
        FullDEMPath, FullGeoJsonPath,
        OriginLongitude, OriginLatitude,
        Callback
    );
}

void AGISWorldBuilder::OnPolygonsGenerated(const TArray<FLandUsePolygon>& Polygons)
{
    GeneratedPolygons = Polygons;
    PostGenerate();
}

void AGISWorldBuilder::LoadFromDataAsset()
{
    if (!LandUseDataAsset)
    {
        UE_LOG(LogTemp, Error, TEXT("GISWorldBuilder: No DataAsset assigned"));
        return;
    }

    GeneratedPolygons = LandUseDataAsset->Polygons;
    UE_LOG(LogTemp, Log, TEXT("GISWorldBuilder: Loaded %d polygons from DataAsset"), GeneratedPolygons.Num());
}

void AGISWorldBuilder::PostGenerate()
{
    UE_LOG(LogTemp, Log, TEXT("GISWorldBuilder: %d polygons ready"), GeneratedPolygons.Num());

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
    for (AActor* Actor : SpawnedActors)
    {
        if (IsValid(Actor))
        {
            Actor->Destroy();
        }
    }
    SpawnedActors.Empty();
    GeneratedPolygons.Empty();

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
    if (!World) return;

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
            FString ActorLabel = FString::Printf(TEXT("GIS_Poly_%d_%s"),
                Poly.PolygonID,
                *UEnum::GetDisplayValueAsText(Poly.LandUseType).ToString());

#if WITH_EDITOR
            PolygonActor->SetActorLabel(ActorLabel);
#endif

            UGISPolygonComponent* Comp = NewObject<UGISPolygonComponent>(
                PolygonActor, UGISPolygonComponent::StaticClass(),
                FName(*ActorLabel));
            Comp->SetPolygonData(Poly);
            Comp->RegisterComponent();
            PolygonActor->AddInstanceComponent(Comp);

            PolygonActor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);

            SpawnedActors.Add(PolygonActor);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("GISWorldBuilder: Spawned %d polygon actors"), SpawnedActors.Num());
}

void AGISWorldBuilder::DrawDebugPolygons(const TArray<FLandUsePolygon>& Polygons) const
{
    UWorld* World = GetWorld();
    if (!World) return;

    for (const FLandUsePolygon& Poly : Polygons)
    {
        const FColor Color = GetLandUseColor(Poly.LandUseType);
        const int32 N = Poly.WorldVertices.Num();

        for (int32 i = 0; i < N; ++i)
        {
            const int32 Next = (i + 1) % N;
            DrawDebugLine(
                World,
                Poly.WorldVertices[i],
                Poly.WorldVertices[Next],
                Color,
                (DebugDrawDuration < 0.0f),
                DebugDrawDuration,
                0,
                4.0f
            );
        }

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

void AGISWorldBuilder::GenerateAndSaveDataAsset()
{
    // 先生成
    if (DataSourceType == EGISDataSourceType::DataAsset)
    {
        UE_LOG(LogTemp, Warning, TEXT("GISWorldBuilder: Cannot generate from DataAsset mode, switch to LocalFile or ArcGIS first"));
        return;
    }

    GenerateAll();

    if (GeneratedPolygons.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("GISWorldBuilder: No polygons to save"));
        return;
    }

    // 创建或更新 DataAsset
    const FString PackagePath = TEXT("/Game/GISData/");
    const FString AssetName = FString::Printf(TEXT("LandUse_%s"),
        *GetActorLabel().Replace(TEXT(" "), TEXT("_")));
    const FString FullPath = PackagePath + AssetName;

    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        UE_LOG(LogTemp, Error, TEXT("GISWorldBuilder: Failed to create package %s"), *FullPath);
        return;
    }

    ULandUseMapDataAsset* DataAsset = NewObject<ULandUseMapDataAsset>(
        Package, FName(*AssetName), RF_Public | RF_Standalone);

    DataAsset->Polygons = GeneratedPolygons;
    DataAsset->OriginLongitude = OriginLongitude;
    DataAsset->OriginLatitude = OriginLatitude;
    DataAsset->SourceBounds = QueryBounds;
    DataAsset->GeneratedTime = FDateTime::Now();

    // 记录数据源
    if (DataSourceType == EGISDataSourceType::LocalFile)
    {
        DataAsset->SourceProvider = FString::Printf(TEXT("LocalFile: %s"), *GeoJsonPath);
    }
    else if (DataSourceType == EGISDataSourceType::ArcGISRest)
    {
        DataAsset->SourceProvider = FString::Printf(TEXT("ArcGIS: %s"), *FeatureServiceUrl);
    }

    // 保存
    FAssetRegistryModule::AssetCreated(DataAsset);
    DataAsset->MarkPackageDirty();

    const FString PackageFilePath = FPackageName::LongPackageNameToFilename(
        FullPath, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    const bool bSaved = UPackage::SavePackage(Package, DataAsset, *PackageFilePath, SaveArgs);

    if (bSaved)
    {
        UE_LOG(LogTemp, Log, TEXT("GISWorldBuilder: Saved DataAsset to %s (%d polygons)"),
            *PackageFilePath, GeneratedPolygons.Num());

        // 自动设置引用
        LandUseDataAsset = DataAsset;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("GISWorldBuilder: Failed to save DataAsset to %s"), *PackageFilePath);
    }
}
#endif

FColor AGISWorldBuilder::GetLandUseColor(ELandUseType Type)
{
    switch (Type)
    {
        case ELandUseType::Residential: return FColor(255, 235, 59);
        case ELandUseType::Commercial:  return FColor(244, 67, 54);
        case ELandUseType::Industrial:  return FColor(255, 152, 0);
        case ELandUseType::Forest:      return FColor(76, 175, 80);
        case ELandUseType::Farmland:    return FColor(139, 195, 74);
        case ELandUseType::Water:       return FColor(33, 150, 243);
        case ELandUseType::Road:        return FColor(158, 158, 158);
        case ELandUseType::OpenSpace:   return FColor(224, 224, 224);
        case ELandUseType::Military:    return FColor(120, 0, 0);
        default:                        return FColor(200, 200, 200);
    }
}
