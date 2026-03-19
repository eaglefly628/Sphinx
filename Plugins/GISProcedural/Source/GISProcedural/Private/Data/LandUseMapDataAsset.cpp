// LandUseMapDataAsset.cpp
#include "Data/LandUseMapDataAsset.h"

TArray<FLandUsePolygon> ULandUseMapDataAsset::GetPolygonsByType(ELandUseType Type) const
{
    TArray<FLandUsePolygon> Result;
    for (const FLandUsePolygon& Poly : Polygons)
    {
        if (Poly.LandUseType == Type)
        {
            Result.Add(Poly);
        }
    }
    return Result;
}

TArray<FLandUsePolygon> ULandUseMapDataAsset::GetPolygonsInWorldBounds(const FBox& WorldBounds) const
{
    // 有空间索引时走快速路径
    if (SpatialGrid.Num() > 0 && SpatialCellSize > 0.0f)
    {
        TArray<FLandUsePolygon> Result;
        TSet<int32> VisitedIndices;

        // 遍历 bounds 覆盖的所有 cell
        const int32 MinCellX = FMath::FloorToInt32(WorldBounds.Min.X / SpatialCellSize);
        const int32 MaxCellX = FMath::FloorToInt32(WorldBounds.Max.X / SpatialCellSize);
        const int32 MinCellY = FMath::FloorToInt32(WorldBounds.Min.Y / SpatialCellSize);
        const int32 MaxCellY = FMath::FloorToInt32(WorldBounds.Max.Y / SpatialCellSize);

        for (int32 CX = MinCellX; CX <= MaxCellX; ++CX)
        {
            for (int32 CY = MinCellY; CY <= MaxCellY; ++CY)
            {
                if (const TArray<int32>* Indices = SpatialGrid.Find(FIntPoint(CX, CY)))
                {
                    for (int32 Idx : *Indices)
                    {
                        if (!VisitedIndices.Contains(Idx))
                        {
                            VisitedIndices.Add(Idx);
                            const FLandUsePolygon& Poly = Polygons[Idx];
                            if (WorldBounds.IsInsideOrOn(Poly.WorldCenter))
                            {
                                Result.Add(Poly);
                            }
                        }
                    }
                }
            }
        }
        return Result;
    }

    // 回退：线性扫描
    TArray<FLandUsePolygon> Result;
    for (const FLandUsePolygon& Poly : Polygons)
    {
        if (WorldBounds.IsInsideOrOn(Poly.WorldCenter))
        {
            Result.Add(Poly);
        }
    }
    return Result;
}

float ULandUseMapDataAsset::GetTotalArea() const
{
    float Total = 0.0f;
    for (const FLandUsePolygon& Poly : Polygons)
    {
        Total += Poly.AreaSqM;
    }
    return Total;
}

void ULandUseMapDataAsset::BuildSpatialIndex(float CellSizeWorld)
{
    SpatialGrid.Empty();
    SpatialCellSize = CellSizeWorld;

    if (CellSizeWorld <= 0.0f || Polygons.Num() == 0)
    {
        return;
    }

    for (int32 i = 0; i < Polygons.Num(); ++i)
    {
        const FIntPoint Cell = WorldToCell(Polygons[i].WorldCenter);
        SpatialGrid.FindOrAdd(Cell).Add(i);
    }

    UE_LOG(LogTemp, Log, TEXT("LandUseMapDataAsset: Built spatial index with %d cells for %d polygons (cell=%.0fcm)"),
        SpatialGrid.Num(), Polygons.Num(), CellSizeWorld);
}

FIntPoint ULandUseMapDataAsset::WorldToCell(const FVector& WorldPos) const
{
    return FIntPoint(
        FMath::FloorToInt32(WorldPos.X / SpatialCellSize),
        FMath::FloorToInt32(WorldPos.Y / SpatialCellSize)
    );
}

int32 ULandUseMapDataAsset::GetCountByType(ELandUseType Type) const
{
    int32 Count = 0;
    for (const FLandUsePolygon& Poly : Polygons)
    {
        if (Poly.LandUseType == Type)
        {
            ++Count;
        }
    }
    return Count;
}

// ============ Phase 3: Tiled DataAsset 管理 ============

void ULandUseMapDataAsset::LoadTileAsync(const FString& TileID)
{
    if (LoadedTileCache.Contains(TileID))
    {
        // 已加载，直接回调
        OnTileLoaded.Broadcast(TileID);
        return;
    }

    const TSoftObjectPtr<ULandUseMapDataAsset>* SoftPtr = TileAssets.Find(TileID);
    if (!SoftPtr)
    {
        UE_LOG(LogTemp, Warning, TEXT("LandUseMapDataAsset: TileID '%s' not found in TileAssets map"), *TileID);
        return;
    }

    if (SoftPtr->IsNull())
    {
        UE_LOG(LogTemp, Warning, TEXT("LandUseMapDataAsset: TileID '%s' has null soft reference"), *TileID);
        return;
    }

    // 异步流式加载
    FSoftObjectPath AssetPath = SoftPtr->ToSoftObjectPath();
    StreamableManager.RequestAsyncLoad(
        AssetPath,
        FStreamableDelegate::CreateLambda([this, TileID, AssetPath]()
        {
            UObject* Loaded = AssetPath.ResolveObject();
            ULandUseMapDataAsset* TileAsset = Cast<ULandUseMapDataAsset>(Loaded);
            if (TileAsset)
            {
                LoadedTileCache.Add(TileID, TileAsset);
                UE_LOG(LogTemp, Log, TEXT("LandUseMapDataAsset: Async loaded tile '%s' (%d polygons)"),
                    *TileID, TileAsset->Polygons.Num());
                OnTileLoaded.Broadcast(TileID);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("LandUseMapDataAsset: Failed to load tile '%s'"), *TileID);
            }
        })
    );
}

ULandUseMapDataAsset* ULandUseMapDataAsset::LoadTileSync(const FString& TileID)
{
    if (ULandUseMapDataAsset** Cached = LoadedTileCache.Find(TileID))
    {
        return *Cached;
    }

    const TSoftObjectPtr<ULandUseMapDataAsset>* SoftPtr = TileAssets.Find(TileID);
    if (!SoftPtr || SoftPtr->IsNull())
    {
        return nullptr;
    }

    ULandUseMapDataAsset* TileAsset = SoftPtr->LoadSynchronous();
    if (TileAsset)
    {
        LoadedTileCache.Add(TileID, TileAsset);
    }
    return TileAsset;
}

void ULandUseMapDataAsset::UnloadTile(const FString& TileID)
{
    LoadedTileCache.Remove(TileID);
}

TArray<FString> ULandUseMapDataAsset::GetTileIDsInWorldBounds(const FBox& WorldBounds) const
{
    TArray<FString> Result;

    if (TileSizeM <= 0.0f)
    {
        // 无瓦片信息，返回所有
        TileAssets.GetKeys(Result);
        return Result;
    }

    const float TileSizeCm = TileSizeM * 100.0f;

    // 计算 bounds 覆盖的 tile 范围
    const int32 MinCol = FMath::FloorToInt32(WorldBounds.Min.X / TileSizeCm);
    const int32 MaxCol = FMath::FloorToInt32(WorldBounds.Max.X / TileSizeCm);
    const int32 MinRow = FMath::FloorToInt32(WorldBounds.Min.Y / TileSizeCm);
    const int32 MaxRow = FMath::FloorToInt32(WorldBounds.Max.Y / TileSizeCm);

    for (int32 Col = MinCol; Col <= MaxCol; ++Col)
    {
        for (int32 Row = MinRow; Row <= MaxRow; ++Row)
        {
            const FString CandidateID = FString::Printf(TEXT("tile_%d_%d"), Col, Row);
            if (TileAssets.Contains(CandidateID))
            {
                Result.Add(CandidateID);
            }
        }
    }
    return Result;
}
