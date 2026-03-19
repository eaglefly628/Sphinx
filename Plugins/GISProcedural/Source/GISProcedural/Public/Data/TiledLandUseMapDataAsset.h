// TiledLandUseMapDataAsset.h - 瓦片化 DataAsset 清单
// 管理所有 tile 的 ULandUseMapDataAsset 软引用
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/StreamableManager.h"
#include "Data/LandUseMapDataAsset.h"
#include "Data/TileManifest.h"
#include "TiledLandUseMapDataAsset.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTileAssetLoaded, const FString&, TileID, ULandUseMapDataAsset*, TileAsset);

/**
 * 瓦片化 LandUse DataAsset 清单
 *
 * 每个 tile 对应一个独立的 ULandUseMapDataAsset，
 * 本资产持有所有 tile 的软引用，支持按需异步加载/卸载。
 *
 * 生成流程：
 *   TiledWorldBuilder 遍历 tile_manifest → 逐 tile 调用 PolygonDeriver
 *   → 每个 tile 生成一个 ULandUseMapDataAsset → 注册到本清单
 *
 * 运行时流程：
 *   World Partition cell 激活 → GetTileIDsInWorldBounds()
 *   → LoadTileAsync() → OnTileAssetLoaded → PCG 采样
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API UTiledLandUseMapDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    /** 每个 tile 的 DataAsset 软引用：key = "tile_0_3" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Tile")
    TMap<FString, TSoftObjectPtr<ULandUseMapDataAsset>> TileAssetMap;

    /** 瓦片尺寸（米） */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIS|Tile")
    float TileSizeM = 1024.0f;

    /** 投影原点经度 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIS|Tile")
    double OriginLongitude = 0.0;

    /** 投影原点纬度 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIS|Tile")
    double OriginLatitude = 0.0;

    /** UTM Zone */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIS|Tile")
    int32 UTMZone = 0;

    /** 网格列数 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIS|Tile")
    int32 NumCols = 0;

    /** 网格行数 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIS|Tile")
    int32 NumRows = 0;

    /** 生成时间 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIS|Tile")
    FDateTime GeneratedTime;

    // ============ 运行时接口 ============

    /** 同步加载指定 tile（阻塞） */
    UFUNCTION(BlueprintCallable, Category = "GIS|Tile")
    ULandUseMapDataAsset* LoadTile(const FString& TileID);

    /** 异步加载指定 tile */
    UFUNCTION(BlueprintCallable, Category = "GIS|Tile")
    void LoadTileAsync(const FString& TileID);

    /** 卸载指定 tile */
    UFUNCTION(BlueprintCallable, Category = "GIS|Tile")
    void UnloadTile(const FString& TileID);

    /** 查询世界范围内的 tile ID 列表 */
    UFUNCTION(BlueprintCallable, Category = "GIS|Tile")
    TArray<FString> GetTileIDsInWorldBounds(const FBox& WorldBounds) const;

    /** 获取所有 tile ID */
    UFUNCTION(BlueprintCallable, Category = "GIS|Tile")
    TArray<FString> GetAllTileIDs() const;

    /** tile 是否已加载 */
    UFUNCTION(BlueprintCallable, Category = "GIS|Tile")
    bool IsTileLoaded(const FString& TileID) const;

    /** 获取已加载的 tile 数量 */
    UFUNCTION(BlueprintCallable, Category = "GIS|Tile")
    int32 GetLoadedTileCount() const { return LoadedTileCache.Num(); }

    /** 异步加载完成回调 */
    UPROPERTY(BlueprintAssignable, Category = "GIS|Tile")
    FOnTileAssetLoaded OnTileAssetLoaded;

    // ============ 编辑器工具 ============

    /** 注册一个 tile DataAsset（编辑器生成时调用） */
    void RegisterTile(const FString& TileID, ULandUseMapDataAsset* Asset);

private:
    /** 已加载的 tile 缓存（Transient，不序列化） */
    UPROPERTY(Transient)
    TMap<FString, ULandUseMapDataAsset*> LoadedTileCache;

    /** 流式加载管理器 */
    FStreamableManager StreamableManager;
};
