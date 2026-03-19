// TiledFileProvider.cpp - 瓦片化数据提供者实现
#include "Data/TiledFileProvider.h"
#include "Data/GeoJsonParser.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool UTiledFileProvider::Initialize()
{
    bInitialized = false;
    TileGrid.Empty();
    FeatureCache.Empty();

    if (ManifestPath.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("TiledFileProvider: ManifestPath is empty"));
        return false;
    }

    // 解析 manifest
    if (!FTileManifest::LoadFromFile(ManifestPath, Manifest))
    {
        UE_LOG(LogTemp, Error, TEXT("TiledFileProvider: Failed to load manifest: %s"), *ManifestPath);
        return false;
    }

    // 构建 O(1) 坐标索引
    for (int32 i = 0; i < Manifest.Tiles.Num(); ++i)
    {
        const FTileEntry& Entry = Manifest.Tiles[i];
        TileGrid.Add(Entry.GetCoord(), i);
    }

    // 记录 manifest 所在目录
    BaseDirectory = FPaths::GetPath(ManifestPath);

    // 创建解析器
    if (!Parser)
    {
        Parser = NewObject<UGeoJsonParser>(this);
    }

    bInitialized = true;

    UE_LOG(LogTemp, Log, TEXT("TiledFileProvider: Initialized with %d tiles, base=%s"),
        Manifest.Tiles.Num(), *BaseDirectory);

    return true;
}

void UTiledFileProvider::ClearCache()
{
    FeatureCache.Empty();
}

bool UTiledFileProvider::QueryFeatures(
    const FGeoRect& Bounds,
    TArray<FGISFeature>& OutFeatures)
{
    if (!bInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("TiledFileProvider: Not initialized"));
        return false;
    }

    // 查找与查询范围相交的所有 tile
    TArray<const FTileEntry*> MatchingTiles = Manifest.FindTilesInBounds(Bounds);

    if (MatchingTiles.Num() == 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("TiledFileProvider: No tiles match bounds (%.4f,%.4f)-(%.4f,%.4f)"),
            Bounds.MinLon, Bounds.MinLat, Bounds.MaxLon, Bounds.MaxLat);
        return true;  // 成功但无数据
    }

    // 全局 feature ID 去重集合（跨 tile 边界的要素可能出现在多个 tile 中）
    TSet<int32> SeenFeatureIDs;

    for (const FTileEntry* TilePtr : MatchingTiles)
    {
        const TArray<FGISFeature>* Features = LoadTileFeatures(*TilePtr);
        if (!Features)
        {
            continue;
        }

        for (const FGISFeature& Feature : *Features)
        {
            // 按 sphinx_feature_id 去重
            const FString* FeatureIDStr = Feature.Properties.Find(TEXT("sphinx_feature_id"));
            if (FeatureIDStr)
            {
                int32 FeatureID = FCString::Atoi(**FeatureIDStr);
                if (SeenFeatureIDs.Contains(FeatureID))
                {
                    continue;  // 已处理过的跨 tile 要素
                }
                SeenFeatureIDs.Add(FeatureID);
            }

            // 简单 AABB 过滤：要素质心是否在查询范围内
            bool bInBounds = false;
            if (Feature.Coordinates.Num() > 0)
            {
                // 取第一个坐标环的质心
                FVector2D Centroid(0, 0);
                for (const FVector2D& Coord : Feature.Coordinates)
                {
                    Centroid += Coord;
                }
                Centroid /= Feature.Coordinates.Num();

                bInBounds = (Centroid.X >= Bounds.MinLon && Centroid.X <= Bounds.MaxLon &&
                             Centroid.Y >= Bounds.MinLat && Centroid.Y <= Bounds.MaxLat);
            }
            else
            {
                bInBounds = true;  // 无坐标信息时包含
            }

            if (bInBounds)
            {
                OutFeatures.Add(Feature);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("TiledFileProvider: QueryFeatures returned %d features from %d tiles"),
        OutFeatures.Num(), MatchingTiles.Num());

    return true;
}

bool UTiledFileProvider::QueryElevation(
    const FGeoRect& Bounds,
    float Resolution,
    TArray<float>& OutGrid,
    int32& OutWidth, int32& OutHeight)
{
    // DEM 数据由现有 DEMParser 处理
    // TiledFileProvider 提供文件路径，具体解析委托给调用方
    // 当前实现：查找覆盖 tile 的 DEM 路径
    if (!bInitialized)
    {
        return false;
    }

    TArray<const FTileEntry*> MatchingTiles = Manifest.FindTilesInBounds(Bounds);
    if (MatchingTiles.Num() == 0)
    {
        return false;
    }

    // 对于单 tile 查询，直接返回该 tile 的 DEM 路径标识
    // 多 tile 合并需要在调用方处理
    UE_LOG(LogTemp, Verbose,
        TEXT("TiledFileProvider: QueryElevation found %d tiles with DEM data"),
        MatchingTiles.Num());

    // TODO: 实际 DEM 数据合并需要 DEMParser 配合
    // 目前返回 false 表示未实现
    return false;
}

bool UTiledFileProvider::QueryLandCover(
    const FGeoRect& Bounds,
    float Resolution,
    TArray<uint8>& OutClassGrid,
    int32& OutWidth, int32& OutHeight)
{
    if (!bInitialized)
    {
        return false;
    }

    // 查找覆盖 tile
    TArray<const FTileEntry*> MatchingTiles = Manifest.FindTilesInBounds(Bounds);
    if (MatchingTiles.Num() == 0)
    {
        return false;
    }

    // 单 tile 简单情况：直接读取该 tile 的 landcover.json
    if (MatchingTiles.Num() == 1 && !MatchingTiles[0]->LandCoverRelPath.IsEmpty())
    {
        const FString LCPath = FPaths::Combine(BaseDirectory, MatchingTiles[0]->LandCoverRelPath);

        FString JsonString;
        if (!FFileHelper::LoadFileToString(JsonString, *LCPath))
        {
            return false;
        }

        TSharedPtr<FJsonObject> LCObj;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
        if (!FJsonSerializer::Deserialize(Reader, LCObj) || !LCObj.IsValid())
        {
            return false;
        }

        OutWidth = LCObj->GetIntegerField(TEXT("width"));
        OutHeight = LCObj->GetIntegerField(TEXT("height"));

        const TArray<TSharedPtr<FJsonValue>>* ClassesArray = nullptr;
        if (!LCObj->TryGetArrayField(TEXT("classes"), ClassesArray))
        {
            return false;
        }

        OutClassGrid.Reserve(ClassesArray->Num());
        for (const TSharedPtr<FJsonValue>& Val : *ClassesArray)
        {
            OutClassGrid.Add(static_cast<uint8>(Val->AsNumber()));
        }

        return true;
    }

    // 多 tile 合并的 LandCover 需要空间拼接
    // TODO: 实现多 tile LandCover 合并
    UE_LOG(LogTemp, Verbose,
        TEXT("TiledFileProvider: Multi-tile LandCover merge not yet implemented (%d tiles)"),
        MatchingTiles.Num());

    return false;
}

const TArray<FGISFeature>* UTiledFileProvider::LoadTileFeatures(const FTileEntry& Entry)
{
    const FIntPoint Coord = Entry.GetCoord();

    // 缓存命中
    if (FCachedTileData* Cached = FeatureCache.Find(Coord))
    {
        Cached->LastAccessFrame = GFrameCounter;
        return &Cached->Features;
    }

    // 缓存未命中 → 加载
    if (Entry.GeoJsonRelPath.IsEmpty())
    {
        return nullptr;
    }

    const FString FullPath = FPaths::Combine(BaseDirectory, Entry.GeoJsonRelPath);

    if (!Parser)
    {
        Parser = NewObject<UGeoJsonParser>(this);
    }

    FCachedTileData NewData;
    NewData.LastAccessFrame = GFrameCounter;

    if (!Parser->ParseFile(FullPath, NewData.Features))
    {
        UE_LOG(LogTemp, Warning, TEXT("TiledFileProvider: Failed to parse tile (%d,%d): %s"),
            Entry.Col, Entry.Row, *FullPath);
        return nullptr;
    }

    // LRU 淘汰
    while (FeatureCache.Num() >= MaxCachedTiles)
    {
        EvictOldestCache();
    }

    FCachedTileData& Stored = FeatureCache.Add(Coord, MoveTemp(NewData));

    UE_LOG(LogTemp, Verbose, TEXT("TiledFileProvider: Loaded tile (%d,%d) with %d features, cache=%d/%d"),
        Entry.Col, Entry.Row, Stored.Features.Num(), FeatureCache.Num(), MaxCachedTiles);

    return &Stored.Features;
}

void UTiledFileProvider::EvictOldestCache()
{
    if (FeatureCache.Num() == 0) return;

    FIntPoint OldestKey;
    uint64 OldestFrame = UINT64_MAX;

    for (const auto& Pair : FeatureCache)
    {
        if (Pair.Value.LastAccessFrame < OldestFrame)
        {
            OldestFrame = Pair.Value.LastAccessFrame;
            OldestKey = Pair.Key;
        }
    }

    FeatureCache.Remove(OldestKey);
}
