// TiledWorldBuilder.cpp - 编辑器批量 tile 生成实现
#include "Runtime/TiledWorldBuilder.h"
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
    // 初始化 TiledFileProvider
    if (!FileProvider)
    {
        FileProvider = NewObject<UTiledFileProvider>(this);
    }

    FileProvider->ManifestPath = FPaths::Combine(
        FPaths::ProjectContentDir(), ManifestPath);

    if (!FileProvider->Initialize())
    {
        UE_LOG(LogTemp, Error, TEXT("TiledWorldBuilder: Failed to initialize TiledFileProvider"));
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

    return true;
}

#if WITH_EDITOR
void ATiledWorldBuilder::GenerateAllTiles()
{
    if (!InitBuildComponents())
    {
        return;
    }

    const FTileManifest& Manifest = FileProvider->GetManifest();
    UE_LOG(LogTemp, Log, TEXT("TiledWorldBuilder: Starting generation for %d tiles"), Manifest.Tiles.Num());

    // 创建清单 DataAsset
    const FString AssetName = TEXT("TiledLandUse_") +
        FPaths::GetBaseFilename(ManifestPath).Replace(TEXT(" "), TEXT("_"));
    const FString FullPath = OutputPackagePath + AssetName;

    UPackage* CatalogPackage = CreatePackage(*FullPath);
    if (!CatalogPackage)
    {
        UE_LOG(LogTemp, Error, TEXT("TiledWorldBuilder: Failed to create package %s"), *FullPath);
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
    int32 TotalPolygons = 0;

    for (const FTileEntry& Entry : Manifest.Tiles)
    {
        const FString TileID = FString::Printf(TEXT("tile_%d_%d"), Entry.Col, Entry.Row);

        ULandUseMapDataAsset* TileAsset = GenerateTileDataAsset(Entry, TileID);
        if (TileAsset)
        {
            TiledAsset->RegisterTile(TileID, TileAsset);
            TotalPolygons += TileAsset->Polygons.Num();
            SuccessCount++;
        }
    }

    // 保存清单
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

    UE_LOG(LogTemp, Log, TEXT("========================================"));
    UE_LOG(LogTemp, Log, TEXT("TiledWorldBuilder: Generation complete"));
    UE_LOG(LogTemp, Log, TEXT("  Tiles: %d/%d succeeded"), SuccessCount, Manifest.Tiles.Num());
    UE_LOG(LogTemp, Log, TEXT("  Total polygons: %d"), TotalPolygons);
    UE_LOG(LogTemp, Log, TEXT("  Saved catalog: %s"), *PackageFilePath);
    UE_LOG(LogTemp, Log, TEXT("========================================"));
}

void ATiledWorldBuilder::GenerateSingleTestTile()
{
    if (!InitBuildComponents())
    {
        return;
    }

    const FTileManifest& Manifest = FileProvider->GetManifest();
    if (Manifest.Tiles.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("TiledWorldBuilder: No tiles in manifest"));
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
    UE_LOG(LogTemp, Log, TEXT("TiledWorldBuilder: Testing with tile %s (%d features)"),
        *TileID, BestTile->FeatureCount);

    ULandUseMapDataAsset* Asset = GenerateTileDataAsset(*BestTile, TileID);
    if (Asset)
    {
        UE_LOG(LogTemp, Log, TEXT("TiledWorldBuilder: Test tile generated %d polygons"), Asset->Polygons.Num());
    }
}
#endif

ULandUseMapDataAsset* ATiledWorldBuilder::GenerateTileDataAsset(
    const FTileEntry& Entry, const FString& TileID)
{
#if WITH_EDITOR
    // 查询该 tile 的要素
    TArray<FGISFeature> Features;
    if (!FileProvider->QueryFeatures(Entry.GeoBounds, Features))
    {
        UE_LOG(LogTemp, Warning, TEXT("TiledWorldBuilder: Failed to query features for %s"), *TileID);
        return nullptr;
    }

    if (Features.Num() == 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("TiledWorldBuilder: Tile %s has no features, skipping"), *TileID);
        return nullptr;
    }

    // 设置数据源并生成 polygon
    PolygonDeriver->SetDataProvider(FileProvider);
    PolygonDeriver->SetQueryBounds(Entry.GeoBounds);

    const FTileManifest& Manifest = FileProvider->GetManifest();
    TArray<FLandUsePolygon> Polygons = PolygonDeriver->GenerateFromProvider(
        Manifest.OriginLongitude, Manifest.OriginLatitude);

    if (Polygons.Num() == 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("TiledWorldBuilder: Tile %s generated 0 polygons"), *TileID);
        return nullptr;
    }

    // 设置 tile 坐标
    for (FLandUsePolygon& Poly : Polygons)
    {
        Poly.TileCoord = FIntPoint(Entry.Col, Entry.Row);
    }

    // 尝试 LandCover 融合
    TArray<uint8> LandCoverGrid;
    int32 LCWidth = 0, LCHeight = 0;
    if (FileProvider->QueryLandCover(Entry.GeoBounds, 10.0f, LandCoverGrid, LCWidth, LCHeight))
    {
        FBox2D GridBounds2D(
            FVector2D(Entry.GeoBounds.MinLon, Entry.GeoBounds.MinLat),
            FVector2D(Entry.GeoBounds.MaxLon, Entry.GeoBounds.MaxLat));
        ULandUseClassifier::FuseLandCoverData(Polygons, LandCoverGrid, LCWidth, LCHeight, GridBounds2D);
    }

    // 创建 DataAsset
    const FString AssetName = TileID;
    const FString TilePackagePath = OutputPackagePath + TEXT("Tiles/") + AssetName;

    UPackage* Package = CreatePackage(*TilePackagePath);
    if (!Package)
    {
        UE_LOG(LogTemp, Error, TEXT("TiledWorldBuilder: Failed to create package for %s"), *TileID);
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

    UE_LOG(LogTemp, Log, TEXT("TiledWorldBuilder: Generated %s → %d polygons"),
        *TileID, Asset->Polygons.Num());

    return Asset;
#else
    return nullptr;
#endif
}
