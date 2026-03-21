// TiledWorldBuilder.cpp - 编辑器批量 tile 生成实现
#include "Runtime/TiledWorldBuilder.h"
#include "GISProceduralModule.h"
#include "Polygon/PolygonDeriver.h"
#include "Polygon/LandUseClassifier.h"
#include "Data/TiledFileProvider.h"
#include "Data/TiledLandUseMapDataAsset.h"
#include "Data/LandUseMapDataAsset.h"
#include "Data/RasterLandCoverParser.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

ATiledWorldBuilder::ATiledWorldBuilder()
{
    PrimaryActorTick.bCanEverTick = false;

    // 默认道路权重
    RoadClassWeights.Add(TEXT("motorway"), 100);
    RoadClassWeights.Add(TEXT("trunk"), 80);
    RoadClassWeights.Add(TEXT("primary"), 60);
    RoadClassWeights.Add(TEXT("secondary"), 40);
    RoadClassWeights.Add(TEXT("tertiary"), 20);
    RoadClassWeights.Add(TEXT("residential"), 10);
    RoadClassWeights.Add(TEXT("unclassified"), 5);
}

bool ATiledWorldBuilder::InitBuildComponents()
{
    UE_LOG(LogGIS, Log, TEXT("TiledWorldBuilder: InitBuildComponents..."));

    // 初始化 TiledFileProvider
    if (!FileProvider)
    {
        FileProvider = NewObject<UTiledFileProvider>(this);
    }

    FileProvider->ManifestPath = FPaths::Combine(
        FPaths::ProjectContentDir(), ManifestPath);

    if (!FileProvider->Initialize())
    {
        UE_LOG(LogGIS, Error, TEXT("TiledWorldBuilder: Failed to initialize TiledFileProvider"));
        return false;
    }

    // 初始化 PolygonDeriver
    if (!PolygonDeriver)
    {
        PolygonDeriver = NewObject<UPolygonDeriver>(this);
    }

    PolygonDeriver->MinPolygonArea = MinPolygonArea;
    PolygonDeriver->ClassifyRules = ClassifyRules;
    PolygonDeriver->RoadClassWeights = RoadClassWeights;
    PolygonDeriver->DEMFormat = DEMFormat;

    UE_LOG(LogGIS, Log, TEXT("TiledWorldBuilder: InitBuildComponents complete"));
    return true;
}

#if WITH_EDITOR
void ATiledWorldBuilder::GenerateAllTiles()
{
    UE_LOG(LogGIS, Log, TEXT("TiledWorldBuilder: ===== GenerateAllTiles START ====="));
    const double StartTime = FPlatformTime::Seconds();

    if (!InitBuildComponents())
    {
        return;
    }

    const FTileManifest& Manifest = FileProvider->GetManifest();
    UE_LOG(LogGIS, Log, TEXT("TiledWorldBuilder: Processing %d tiles (grid %dx%d, tile=%.0fm)"),
        Manifest.Tiles.Num(), Manifest.NumCols, Manifest.NumRows, Manifest.TileSizeM);

    // 创建清单 DataAsset
    const FString AssetName = TEXT("TiledLandUse_") +
        FPaths::GetBaseFilename(ManifestPath).Replace(TEXT(" "), TEXT("_"));
    const FString FullPath = OutputPackagePath + AssetName;

    UPackage* CatalogPackage = CreatePackage(*FullPath);
    if (!CatalogPackage)
    {
        UE_LOG(LogGIS, Error, TEXT("TiledWorldBuilder: Failed to create package %s"), *FullPath);
        return;
    }

    UTiledLandUseMapDataAsset* TiledAsset = NewObject<UTiledLandUseMapDataAsset>(
        CatalogPackage, FName(*AssetName), RF_Public | RF_Standalone);

    TiledAsset->TileSizeM = Manifest.TileSizeM;
    TiledAsset->OriginLongitude = Manifest.OriginLongitude;
    TiledAsset->OriginLatitude = Manifest.OriginLatitude;
    TiledAsset->UTMZone = Manifest.UTMZone;
    TiledAsset->NumCols = Manifest.NumCols;
    TiledAsset->NumRows = Manifest.NumRows;
    TiledAsset->GeneratedTime = FDateTime::Now();

    int32 SuccessCount = 0;
    int32 SkipCount = 0;
    int32 FailCount = 0;
    int32 TotalPolygons = 0;

    for (int32 i = 0; i < Manifest.Tiles.Num(); ++i)
    {
        const FTileEntry& Entry = Manifest.Tiles[i];
        const FString TileID = FString::Printf(TEXT("tile_%d_%d"), Entry.Col, Entry.Row);

        UE_LOG(LogGIS, Log, TEXT("TiledWorldBuilder: [%d/%d] Processing %s (%d features)..."),
            i + 1, Manifest.Tiles.Num(), *TileID, Entry.FeatureCount);

        ULandUseMapDataAsset* TileAsset = GenerateTileDataAsset(Entry, TileID);
        if (TileAsset)
        {
            TiledAsset->RegisterTile(TileID, TileAsset);
            TotalPolygons += TileAsset->Polygons.Num();
            SuccessCount++;
        }
        else if (Entry.FeatureCount == 0)
        {
            SkipCount++;
        }
        else
        {
            FailCount++;
        }
    }

    // 保存清单
    UE_LOG(LogGIS, Log, TEXT("TiledWorldBuilder: Saving catalog DataAsset..."));
    FAssetRegistryModule::AssetCreated(TiledAsset);
    TiledAsset->MarkPackageDirty();

    const FString PackageFilePath = FPackageName::LongPackageNameToFilename(
        FullPath, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(CatalogPackage, TiledAsset, *PackageFilePath, SaveArgs);

    GeneratedTiledAsset = TiledAsset;
    LastGeneratedTileCount = SuccessCount;
    LastTotalPolygonCount = TotalPolygons;

    const double Elapsed = FPlatformTime::Seconds() - StartTime;
    UE_LOG(LogGIS, Log, TEXT("========================================"));
    UE_LOG(LogGIS, Log, TEXT("TiledWorldBuilder: Generation complete"));
    UE_LOG(LogGIS, Log, TEXT("  Tiles: %d success / %d skipped / %d failed (of %d)"),
        SuccessCount, SkipCount, FailCount, Manifest.Tiles.Num());
    UE_LOG(LogGIS, Log, TEXT("  Total polygons: %d"), TotalPolygons);
    UE_LOG(LogGIS, Log, TEXT("  Saved catalog: %s"), *PackageFilePath);
    UE_LOG(LogGIS, Log, TEXT("  Elapsed: %.2fs (%.1f tiles/sec)"),
        Elapsed, Elapsed > 0 ? Manifest.Tiles.Num() / Elapsed : 0);
    UE_LOG(LogGIS, Log, TEXT("========================================"));
}

void ATiledWorldBuilder::GenerateSingleTestTile()
{
    UE_LOG(LogGIS, Log, TEXT("TiledWorldBuilder: GenerateSingleTestTile START"));

    if (!InitBuildComponents())
    {
        return;
    }

    const FTileManifest& Manifest = FileProvider->GetManifest();
    if (Manifest.Tiles.Num() == 0)
    {
        UE_LOG(LogGIS, Warning, TEXT("TiledWorldBuilder: No tiles in manifest"));
        return;
    }

    // 找要素最多的 tile 作为测试
    const FTileEntry* BestTile = &Manifest.Tiles[0];
    for (const FTileEntry& Entry : Manifest.Tiles)
    {
        if (Entry.FeatureCount > BestTile->FeatureCount)
        {
            BestTile = &Entry;
        }
    }

    const FString TileID = FString::Printf(TEXT("tile_%d_%d"), BestTile->Col, BestTile->Row);
    UE_LOG(LogGIS, Log, TEXT("TiledWorldBuilder: Selected test tile %s (%d features)"),
        *TileID, BestTile->FeatureCount);

    ULandUseMapDataAsset* Asset = GenerateTileDataAsset(*BestTile, TileID);
    if (Asset)
    {
        UE_LOG(LogGIS, Log, TEXT("TiledWorldBuilder: Test tile → %d polygons"), Asset->Polygons.Num());
    }
    else
    {
        UE_LOG(LogGIS, Warning, TEXT("TiledWorldBuilder: Test tile generation failed"));
    }
}
#endif

ULandUseMapDataAsset* ATiledWorldBuilder::GenerateTileDataAsset(
    const FTileEntry& Entry, const FString& TileID)
{
#if WITH_EDITOR
    const double TileStart = FPlatformTime::Seconds();

    // Step 1: 查询该 tile 的要素
    TArray<FGISFeature> Features;
    if (!FileProvider->QueryFeatures(Entry.GeoBounds, Features))
    {
        UE_LOG(LogGIS, Warning, TEXT("TiledWorldBuilder: [%s] Failed to query features"), *TileID);
        return nullptr;
    }

    if (Features.Num() == 0)
    {
        UE_LOG(LogGIS, Verbose, TEXT("TiledWorldBuilder: [%s] No features, skipping"), *TileID);
        return nullptr;
    }

    UE_LOG(LogGIS, Verbose, TEXT("TiledWorldBuilder: [%s] Step 1/5 → %d features loaded"), *TileID, Features.Num());

    // Step 2: 设置数据源并生成 polygon
    PolygonDeriver->SetDataProvider(FileProvider);
    PolygonDeriver->SetQueryBounds(Entry.GeoBounds);

    const FTileManifest& Manifest = FileProvider->GetManifest();
    TArray<FLandUsePolygon> Polygons = PolygonDeriver->GenerateFromProvider(
        Manifest.OriginLongitude, Manifest.OriginLatitude);

    if (Polygons.Num() == 0)
    {
        UE_LOG(LogGIS, Verbose, TEXT("TiledWorldBuilder: [%s] Generated 0 polygons, skipping"), *TileID);
        return nullptr;
    }

    UE_LOG(LogGIS, Verbose, TEXT("TiledWorldBuilder: [%s] Step 2/5 → %d polygons generated"), *TileID, Polygons.Num());

    // Step 3: 设置 tile 坐标
    for (FLandUsePolygon& Poly : Polygons)
    {
        Poly.TileCoord = FIntPoint(Entry.Col, Entry.Row);
    }

    // Step 4: 尝试 LandCover 融合
    TArray<uint8> LandCoverGrid;
    int32 LCWidth = 0, LCHeight = 0;
    if (FileProvider->QueryLandCover(Entry.GeoBounds, 10.0f, LandCoverGrid, LCWidth, LCHeight))
    {
        FBox2D GridBounds2D(
            FVector2D(Entry.GeoBounds.MinLon, Entry.GeoBounds.MinLat),
            FVector2D(Entry.GeoBounds.MaxLon, Entry.GeoBounds.MaxLat));
        ULandUseClassifier::FuseLandCoverData(Polygons, LandCoverGrid, LCWidth, LCHeight, GridBounds2D);
        UE_LOG(LogGIS, Verbose, TEXT("TiledWorldBuilder: [%s] Step 4/5 → LandCover fused (%dx%d grid)"),
            *TileID, LCWidth, LCHeight);
    }
    else
    {
        UE_LOG(LogGIS, Verbose, TEXT("TiledWorldBuilder: [%s] Step 4/5 → No LandCover data"), *TileID);
    }

    // Step 5: 创建 DataAsset
    const FString AssetName = TileID;
    const FString TilePackagePath = OutputPackagePath + TEXT("Tiles/") + AssetName;

    UPackage* Package = CreatePackage(*TilePackagePath);
    if (!Package)
    {
        UE_LOG(LogGIS, Error, TEXT("TiledWorldBuilder: [%s] Failed to create package"), *TileID);
        return nullptr;
    }

    ULandUseMapDataAsset* Asset = NewObject<ULandUseMapDataAsset>(
        Package, FName(*AssetName), RF_Public | RF_Standalone);

    Asset->Polygons = MoveTemp(Polygons);
    Asset->OriginLongitude = Manifest.OriginLongitude;
    Asset->OriginLatitude = Manifest.OriginLatitude;
    Asset->SourceBounds = Entry.GeoBounds;
    Asset->SourceProvider = FString::Printf(TEXT("TiledFile:%s"), *TileID);
    Asset->GeneratedTime = FDateTime::Now();

    // 构建空间索引
    Asset->BuildSpatialIndex();

    // 保存
    FAssetRegistryModule::AssetCreated(Asset);
    Asset->MarkPackageDirty();

    const FString PackageFilePath = FPackageName::LongPackageNameToFilename(
        TilePackagePath, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(Package, Asset, *PackageFilePath, SaveArgs);

    UE_LOG(LogGIS, Log, TEXT("TiledWorldBuilder: [%s] Step 5/5 → saved %d polygons (%.3fs)"),
        *TileID, Asset->Polygons.Num(), FPlatformTime::Seconds() - TileStart);

    return Asset;
#else
    return nullptr;
#endif
}
