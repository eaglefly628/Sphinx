// TiledFileProvider.h - 瓦片化数据提供者
// 从 tile_manifest.json 读取预处理瓦片，实现 IGISDataProvider 接口
#pragma once

#include "CoreMinimal.h"
#include "Data/IGISDataProvider.h"
#include "Data/TileManifest.h"
#include "TiledFileProvider.generated.h"

class UGeoJsonParser;

/**
 * 瓦片化文件数据提供者
 *
 * 读取 Python 预处理管线输出的 tile_manifest.json，
 * 按需加载对应瓦片的 GeoJSON/DEM/LandCover 数据。
 *
 * 核心优化：
 * - TileGrid TMap<FIntPoint, int32> O(1) 瓦片定位
 * - LRU 缓存避免重复读盘
 * - 对 PolygonDeriver 完全透明（只暴露 IGISDataProvider 接口）
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API UTiledFileProvider : public UObject, public IGISDataProvider
{
    GENERATED_BODY()

public:
    /** Manifest 文件的绝对路径 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|TiledFile")
    FString ManifestPath;

    /** 最大缓存瓦片数 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|TiledFile", meta = (ClampMin = "1"))
    int32 MaxCachedTiles = 50;

    /**
     * 初始化：加载 manifest 并构建瓦片索引
     * @return 是否成功
     */
    UFUNCTION(BlueprintCallable, Category = "GIS|TiledFile")
    bool Initialize();

    /** 获取已加载的 manifest */
    UFUNCTION(BlueprintCallable, Category = "GIS|TiledFile")
    const FTileManifest& GetManifest() const { return Manifest; }

    /** 获取已初始化状态 */
    UFUNCTION(BlueprintCallable, Category = "GIS|TiledFile")
    bool IsInitialized() const { return bInitialized; }

    /** 清除缓存 */
    UFUNCTION(BlueprintCallable, Category = "GIS|TiledFile")
    void ClearCache();

    // ============ IGISDataProvider 接口 ============

    virtual bool QueryFeatures(
        const FGeoRect& Bounds,
        TArray<FGISFeature>& OutFeatures) override;

    virtual bool QueryElevation(
        const FGeoRect& Bounds,
        float Resolution,
        TArray<float>& OutGrid,
        int32& OutWidth, int32& OutHeight) override;

    virtual bool QueryLandCover(
        const FGeoRect& Bounds,
        float Resolution,
        TArray<uint8>& OutClassGrid,
        int32& OutWidth, int32& OutHeight) override;

    virtual FString GetProviderName() const override
    {
        return FString::Printf(TEXT("TiledFile(%d tiles)"), Manifest.Tiles.Num());
    }

    virtual bool IsAvailable() const override { return bInitialized; }

private:
    /** 已解析的 manifest */
    FTileManifest Manifest;

    /** 瓦片坐标 → Tiles 数组索引，O(1) 查找 */
    TMap<FIntPoint, int32> TileGrid;

    /** manifest 所在目录（用于拼接相对路径） */
    FString BaseDirectory;

    /** 是否已初始化 */
    bool bInitialized = false;

    // ---- 瓦片缓存 ----

    /** 缓存的瓦片 GeoJSON 要素 */
    struct FCachedTileData
    {
        TArray<FGISFeature> Features;
        uint64 LastAccessFrame = 0;
    };

    /** GeoJSON 要素缓存：TileID → 要素数据 */
    TMap<FIntPoint, FCachedTileData> FeatureCache;

    /** 加载单个瓦片的 GeoJSON 要素（带缓存） */
    const TArray<FGISFeature>* LoadTileFeatures(const FTileEntry& Entry);

    /** LRU 缓存淘汰 */
    void EvictOldestCache();

    /** GeoJSON 解析器实例 */
    UPROPERTY()
    UGeoJsonParser* Parser = nullptr;
};
