// GISWorldBuilder.cpp - 世界构建器实现
#include "Runtime/GISWorldBuilder.h"
#include "Runtime/CesiumBridgeComponent.h"
#include "GISProceduralModule.h"
#include "Components/GISPolygonComponent.h"
#include "Data/LocalFileProvider.h"
#include "Data/ArcGISRestProvider.h"
#include "Data/TiledFileProvider.h"
#include "Data/GISCoordinate.h"
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
        UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: BeginPlay → auto-generate triggered"));
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

    UE_LOG(LogGIS, Verbose, TEXT("GISWorldBuilder: InitDeriver complete (MinArea=%.1f, Resolution=%.1f)"),
        MinPolygonArea, TerrainAnalysisResolution);
}

IGISDataProvider* AGISWorldBuilder::CreateDataProvider()
{
    UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: CreateDataProvider → mode=%d"), static_cast<int32>(DataSourceType));

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
            UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: LocalFile provider → GeoJSON=%s"), *GeoJsonPath);
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
            UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: ArcGIS provider → URL=%s"), *FeatureServiceUrl);
            return ArcGISRestProviderInstance;
        }

        case EGISDataSourceType::TiledFile:
        {
            if (!TiledFileProviderInstance)
            {
                TiledFileProviderInstance = NewObject<UTiledFileProvider>(this);
            }

            UTiledFileProvider* TiledProvider = CastChecked<UTiledFileProvider>(TiledFileProviderInstance);
            TiledProvider->ManifestPath = FPaths::Combine(
                FPaths::ProjectContentDir(), TileManifestPath);

            if (!TiledProvider->IsInitialized())
            {
                UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: Initializing TiledFileProvider → manifest=%s"), *TileManifestPath);
                if (!TiledProvider->Initialize())
                {
                    UE_LOG(LogGIS, Error,
                        TEXT("GISWorldBuilder: Failed to initialize TiledFileProvider"));
                    return nullptr;
                }
            }

            return static_cast<IGISDataProvider*>(TiledProvider);
        }

        case EGISDataSourceType::CesiumTiled:
        {
            // Cesium 地形 + 本地 GeoJSON：
            // 矢量数据仍走 TiledFileProvider，高程来自离线 DEM 缓存
            if (!TiledFileProviderInstance)
            {
                TiledFileProviderInstance = NewObject<UTiledFileProvider>(this);
            }

            UTiledFileProvider* TiledProvider = CastChecked<UTiledFileProvider>(TiledFileProviderInstance);
            TiledProvider->ManifestPath = FPaths::Combine(
                FPaths::ProjectContentDir(), TileManifestPath);

            if (!TiledProvider->IsInitialized())
            {
                UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: CesiumTiled → initializing TiledFileProvider"));
                if (!TiledProvider->Initialize())
                {
                    UE_LOG(LogGIS, Error, TEXT("GISWorldBuilder: CesiumTiled → TiledFileProvider init failed"));
                    return nullptr;
                }
            }

            // 创建 CesiumBridge 组件
            if (!CesiumBridgeInstance)
            {
                CesiumBridgeInstance = NewObject<UCesiumBridgeComponent>(this);
                CesiumBridgeInstance->RegisterComponent();
            }
            CesiumBridgeInstance->CesiumGeoreferenceActor = CesiumGeoreferenceActor;
            CesiumBridgeInstance->OriginLongitude = OriginLongitude;
            CesiumBridgeInstance->OriginLatitude = OriginLatitude;
            CesiumBridgeInstance->UpdateCachedProjection();

            // 加载离线 DEM 高程缓存
            const FString FullDEMCacheDir = FPaths::Combine(FPaths::ProjectContentDir(), DEMCacheDirectory);
            TArray<FString> CacheFiles;
            IFileManager::Get().FindFiles(CacheFiles, *FullDEMCacheDir, TEXT("*.bin"));
            for (const FString& FileName : CacheFiles)
            {
                // 文件名格式: elevation_X_Y.bin → tile coord (X, Y)
                FString BaseName = FPaths::GetBaseFilename(FileName);
                TArray<FString> Parts;
                BaseName.ParseIntoArray(Parts, TEXT("_"));
                if (Parts.Num() >= 3)
                {
                    const int32 TileX = FCString::Atoi(*Parts[Parts.Num() - 2]);
                    const int32 TileY = FCString::Atoi(*Parts[Parts.Num() - 1]);
                    CesiumBridgeInstance->LoadDEMCache(
                        FPaths::Combine(FullDEMCacheDir, FileName),
                        FIntPoint(TileX, TileY));
                }
            }

            // 注入到 PolygonDeriver
            PolygonDeriver->CesiumBridge = CesiumBridgeInstance;

            UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: CesiumTiled → ready (bridge=%s, DEM tiles=%d)"),
                CesiumGeoreferenceActor.IsNull() ? TEXT("Mercator fallback") : TEXT("Cesium"),
                CesiumBridgeInstance->GetCachedTileCount());

            return static_cast<IGISDataProvider*>(TiledProvider);
        }

        case EGISDataSourceType::DataAsset:
        default:
            return nullptr;
    }
}

void AGISWorldBuilder::GenerateAll()
{
    UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: ===== GenerateAll START (mode=%d) ====="), static_cast<int32>(DataSourceType));
    const double StartTime = FPlatformTime::Seconds();

    ClearGenerated();

    // DataAsset 模式：直接加载
    if (DataSourceType == EGISDataSourceType::DataAsset)
    {
        LoadFromDataAsset();
        PostGenerate();
        UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: ===== GenerateAll END (DataAsset, %.3fs) ====="),
            FPlatformTime::Seconds() - StartTime);
        return;
    }

    // DataProvider 模式
    InitDeriver();
    IGISDataProvider* Provider = CreateDataProvider();

    if (Provider)
    {
        PolygonDeriver->SetDataProvider(Provider);
        PolygonDeriver->SetQueryBounds(QueryBounds);

        UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: Using DataProvider '%s', starting polygon generation..."),
            *Provider->GetProviderName());
        GeneratedPolygons = PolygonDeriver->GenerateFromProvider(OriginLongitude, OriginLatitude);
    }
    else
    {
        // 回退到传统文件路径模式
        const FString FullDEMPath = FPaths::Combine(FPaths::ProjectContentDir(), DEMPath);
        const FString FullGeoJsonPath = FPaths::Combine(FPaths::ProjectContentDir(), GeoJsonPath);

        UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: Fallback to legacy mode DEM=%s, GeoJSON=%s"),
            *FullDEMPath, *FullGeoJsonPath);

        GeneratedPolygons = PolygonDeriver->GeneratePolygons(
            FullDEMPath, FullGeoJsonPath,
            OriginLongitude, OriginLatitude
        );
    }

    PostGenerate();
    UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: ===== GenerateAll END (%d polygons, %.3fs) ====="),
        GeneratedPolygons.Num(), FPlatformTime::Seconds() - StartTime);
}

void AGISWorldBuilder::GenerateAllAsync()
{
    UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: GenerateAllAsync START"));
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
    UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: Async generation complete → %d polygons"), Polygons.Num());
    PostGenerate();
}

void AGISWorldBuilder::LoadFromDataAsset()
{
    if (!LandUseDataAsset)
    {
        UE_LOG(LogGIS, Error, TEXT("GISWorldBuilder: No DataAsset assigned"));
        return;
    }

    GeneratedPolygons = LandUseDataAsset->Polygons;
    UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: Loaded %d polygons from DataAsset '%s'"),
        GeneratedPolygons.Num(), *LandUseDataAsset->GetName());
}

void AGISWorldBuilder::PostGenerate()
{
    UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: PostGenerate → %d polygons ready"), GeneratedPolygons.Num());

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
    const int32 ActorCount = SpawnedActors.Num();
    for (AActor* Actor : SpawnedActors)
    {
        if (IsValid(Actor))
        {
            Actor->Destroy();
        }
    }
    SpawnedActors.Empty();
    GeneratedPolygons.Empty();

    // 清理 CesiumBridge DEM 缓存以释放内存
    if (CesiumBridgeInstance)
    {
        CesiumBridgeInstance->ClearDEMCache();
    }

    if (UWorld* World = GetWorld())
    {
        FlushPersistentDebugLines(World);
    }

    if (ActorCount > 0)
    {
        UE_LOG(LogGIS, Verbose, TEXT("GISWorldBuilder: Cleared %d spawned actors"), ActorCount);
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

    UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: Spawned %d polygon actors"), SpawnedActors.Num());
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

    UE_LOG(LogGIS, Verbose, TEXT("GISWorldBuilder: Drew debug lines for %d polygons"), Polygons.Num());
}

void AGISWorldBuilder::PrintStatistics() const
{
    if (GeneratedPolygons.Num() == 0)
    {
        UE_LOG(LogGIS, Warning, TEXT("GISWorldBuilder: No polygons generated"));
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

    UE_LOG(LogGIS, Log, TEXT("========== GIS World Builder Statistics =========="));
    UE_LOG(LogGIS, Log, TEXT("Total Polygons: %d | Total Area: %.1f m2"), GeneratedPolygons.Num(), TotalArea);

    for (const auto& Pair : CountMap)
    {
        const FString TypeName = UEnum::GetDisplayValueAsText(Pair.Key).ToString();
        const float TypeArea = AreaMap.FindRef(Pair.Key);
        const float Pct = (TotalArea > 0) ? (TypeArea / TotalArea * 100.0f) : 0.0f;
        UE_LOG(LogGIS, Log, TEXT("  %-12s: %3d polygons | %10.1f m2 | %5.1f%%"),
            *TypeName, Pair.Value, TypeArea, Pct);
    }

    UE_LOG(LogGIS, Log, TEXT("=================================================="));
}

#if WITH_EDITOR
void AGISWorldBuilder::GenerateInEditor()
{
    UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: GenerateInEditor called"));
    GenerateAll();
}

void AGISWorldBuilder::ClearEditorPreview()
{
    UE_LOG(LogGIS, Verbose, TEXT("GISWorldBuilder: ClearEditorPreview called"));
    ClearGenerated();
}

void AGISWorldBuilder::GenerateAndSaveDataAsset()
{
    UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: ===== GenerateAndSaveDataAsset START ====="));

    // 先生成
    if (DataSourceType == EGISDataSourceType::DataAsset)
    {
        UE_LOG(LogGIS, Warning, TEXT("GISWorldBuilder: Cannot generate from DataAsset mode, switch to LocalFile or ArcGIS first"));
        return;
    }

    GenerateAll();

    if (GeneratedPolygons.Num() == 0)
    {
        UE_LOG(LogGIS, Warning, TEXT("GISWorldBuilder: No polygons to save"));
        return;
    }

    // 创建或更新 DataAsset
    const FString PackagePath = TEXT("/Game/GISData/");
    const FString AssetName = FString::Printf(TEXT("LandUse_%s"),
        *GetActorLabel().Replace(TEXT(" "), TEXT("_")));
    const FString FullPath = PackagePath + AssetName;

    UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: Creating DataAsset package → %s"), *FullPath);

    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        UE_LOG(LogGIS, Error, TEXT("GISWorldBuilder: Failed to create package %s"), *FullPath);
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
    else if (DataSourceType == EGISDataSourceType::TiledFile)
    {
        DataAsset->SourceProvider = FString::Printf(TEXT("TiledFile: %s"), *TileManifestPath);
    }
    else if (DataSourceType == EGISDataSourceType::CesiumTiled)
    {
        DataAsset->SourceProvider = FString::Printf(TEXT("CesiumTiled: %s"), *TileManifestPath);
    }

    // 自动构建空间索引以加速运行时查询
    UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: Building spatial index..."));
    DataAsset->BuildSpatialIndex();

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
        UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: Saved DataAsset to %s (%d polygons)"),
            *PackageFilePath, GeneratedPolygons.Num());

        // 自动设置引用
        LandUseDataAsset = DataAsset;
    }
    else
    {
        UE_LOG(LogGIS, Error, TEXT("GISWorldBuilder: Failed to save DataAsset to %s"), *PackageFilePath);
    }

    UE_LOG(LogGIS, Log, TEXT("GISWorldBuilder: ===== GenerateAndSaveDataAsset END ====="));
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
