// TiledLandUseMapDataAsset.cpp - 瓦片化 DataAsset 清单实现
#include "Data/TiledLandUseMapDataAsset.h"

ULandUseMapDataAsset* UTiledLandUseMapDataAsset::LoadTile(const FString& TileID)
{
    // 缓存命中
    if (ULandUseMapDataAsset** Cached = LoadedTileCache.Find(TileID))
    {
        return *Cached;
    }

    // 查找软引用
    const TSoftObjectPtr<ULandUseMapDataAsset>* SoftPtr = TileAssetMap.Find(TileID);
    if (!SoftPtr || SoftPtr->IsNull())
    {
        UE_LOG(LogTemp, Warning, TEXT("TiledLandUseMapDataAsset: TileID '%s' not found"), *TileID);
        return nullptr;
    }

    // 同步加载
    ULandUseMapDataAsset* Asset = SoftPtr->LoadSynchronous();
    if (Asset)
    {
        LoadedTileCache.Add(TileID, Asset);
        UE_LOG(LogTemp, Log, TEXT("TiledLandUseMapDataAsset: Loaded tile '%s' (%d polygons)"),
            *TileID, Asset->Polygons.Num());
    }

    return Asset;
}

void UTiledLandUseMapDataAsset::LoadTileAsync(const FString& TileID)
{
    if (LoadedTileCache.Contains(TileID))
    {
        // 已加载，直接回调
        OnTileAssetLoaded.Broadcast(TileID, LoadedTileCache[TileID]);
        return;
    }

    const TSoftObjectPtr<ULandUseMapDataAsset>* SoftPtr = TileAssetMap.Find(TileID);
    if (!SoftPtr || SoftPtr->IsNull())
    {
        UE_LOG(LogTemp, Warning, TEXT("TiledLandUseMapDataAsset: TileID '%s' not found for async load"), *TileID);
        return;
    }

    FSoftObjectPath AssetPath = SoftPtr->ToSoftObjectPath();
    TWeakObjectPtr<UTiledLandUseMapDataAsset> WeakThis(this);
    StreamableManager.RequestAsyncLoad(
        AssetPath,
        FStreamableDelegate::CreateLambda([WeakThis, TileID, AssetPath]()
        {
            UTiledLandUseMapDataAsset* Self = WeakThis.Get();
            if (!Self)
            {
                UE_LOG(LogTemp, Warning, TEXT("TiledLandUseMapDataAsset: Owner GC'd before async load completed for '%s'"), *TileID);
                return;
            }

            UObject* Loaded = AssetPath.ResolveObject();
            ULandUseMapDataAsset* Asset = Cast<ULandUseMapDataAsset>(Loaded);
            if (Asset)
            {
                Self->LoadedTileCache.Add(TileID, Asset);
                UE_LOG(LogTemp, Log, TEXT("TiledLandUseMapDataAsset: Async loaded tile '%s' (%d polygons)"),
                    *TileID, Asset->Polygons.Num());
                Self->OnTileAssetLoaded.Broadcast(TileID, Asset);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("TiledLandUseMapDataAsset: Async load failed for tile '%s'"), *TileID);
            }
        })
    );
}

void UTiledLandUseMapDataAsset::UnloadTile(const FString& TileID)
{
    LoadedTileCache.Remove(TileID);
}

TArray<FString> UTiledLandUseMapDataAsset::GetTileIDsInWorldBounds(const FBox& WorldBounds) const
{
    TArray<FString> Result;

    if (TileSizeM <= 0.0f)
    {
        TileAssetMap.GetKeys(Result);
        return Result;
    }

    const float TileSizeCm = TileSizeM * 100.0f;

    const int32 MinCol = FMath::FloorToInt32(WorldBounds.Min.X / TileSizeCm);
    const int32 MaxCol = FMath::FloorToInt32(WorldBounds.Max.X / TileSizeCm);
    const int32 MinRow = FMath::FloorToInt32(WorldBounds.Min.Y / TileSizeCm);
    const int32 MaxRow = FMath::FloorToInt32(WorldBounds.Max.Y / TileSizeCm);

    for (int32 Col = MinCol; Col <= MaxCol; ++Col)
    {
        for (int32 Row = MinRow; Row <= MaxRow; ++Row)
        {
            const FString CandidateID = FString::Printf(TEXT("tile_%d_%d"), Col, Row);
            if (TileAssetMap.Contains(CandidateID))
            {
                Result.Add(CandidateID);
            }
        }
    }

    return Result;
}

TArray<FString> UTiledLandUseMapDataAsset::GetAllTileIDs() const
{
    TArray<FString> Result;
    TileAssetMap.GetKeys(Result);
    return Result;
}

bool UTiledLandUseMapDataAsset::IsTileLoaded(const FString& TileID) const
{
    return LoadedTileCache.Contains(TileID);
}

void UTiledLandUseMapDataAsset::RegisterTile(const FString& TileID, ULandUseMapDataAsset* Asset)
{
    if (!Asset) return;

    TileAssetMap.Add(TileID, Asset);
    LoadedTileCache.Add(TileID, Asset);
}
