// LandUseMapDataAsset.h - 离线持久化数据资产
// 存储生成的 LandUsePolygon 数组，序列化为 .uasset
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/StreamableManager.h"
#include "Data/IGISDataProvider.h"
#include "Polygon/LandUsePolygon.h"
#include "LandUseMapDataAsset.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTileLoaded, const FString&, TileID);

/**
 * 土地分类数据资产
 *
 * Editor 离线生成 → 存为 .uasset → Runtime/PCG 直接读取
 * 和 ArcGIS 插件在同一 Content 目录下共存，互不冲突。
 *
 * 用法：
 *   1. Editor 时：GISWorldBuilder 生成 polygon → SaveToDataAsset()
 *   2. 在 Content/GISData/ 下产生 .uasset 文件
 *   3. PCG Graph 引用此 DataAsset → 读取 polygon 数据 → 生成实例
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API ULandUseMapDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:

    /** 所有土地分类 Polygon */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS")
    TArray<FLandUsePolygon> Polygons;

    /** 数据源名称（记录来源） */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIS|Meta")
    FString SourceProvider;

    /** 数据覆盖的地理范围 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIS|Meta")
    FGeoRect SourceBounds;

    /** 坐标原点（用于 Geo ↔ World 转换） */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIS|Meta")
    double OriginLongitude = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIS|Meta")
    double OriginLatitude = 0.0;

    /** 生成时间 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIS|Meta")
    FDateTime GeneratedTime;

    // ============ 查询接口 ============

    /** 按类型获取 Polygon */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    TArray<FLandUsePolygon> GetPolygonsByType(ELandUseType Type) const;

    /** 获取指定世界坐标范围内的 Polygon（用于 WP Cell 按需加载） */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    TArray<FLandUsePolygon> GetPolygonsInWorldBounds(const FBox& WorldBounds) const;

    /** 获取总面积 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    float GetTotalArea() const;

    /** 按类型统计数量 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    int32 GetCountByType(ELandUseType Type) const;

    /** Polygon 总数 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    int32 GetPolygonCount() const { return Polygons.Num(); }

    // ============ 空间索引（Phase 2+，大规模查询加速） ============

    /**
     * 构建空间网格索引
     * 将 Polygon 按 WorldCenter 分配到固定大小的网格 cell 中，
     * 后续 GetPolygonsInWorldBounds 使用索引实现 O(k) 查询（k = 命中 cell 数）。
     *
     * @param CellSizeWorld 网格 cell 边长（UE 世界单位 = cm），默认 50000 = 500m
     */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    void BuildSpatialIndex(float CellSizeWorld = 50000.0f);

    /** 空间索引是否已建立 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    bool HasSpatialIndex() const { return SpatialGrid.Num() > 0; }

    /** 清除空间索引 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    void ClearSpatialIndex() { SpatialGrid.Empty(); SpatialCellSize = 0.0f; }

    // ============ Phase 3: Tiled DataAsset 管理 ============

    /** 瓦片 DataAsset 软引用表（TileID → DataAsset 路径） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Tiling")
    TMap<FString, TSoftObjectPtr<ULandUseMapDataAsset>> TileAssets;

    /** 瓦片尺寸（米） */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GIS|Tiling")
    float TileSizeM = 0.0f;

    /**
     * 异步加载指定瓦片
     * @param TileID 瓦片标识（如 "tile_3_5"）
     */
    UFUNCTION(BlueprintCallable, Category = "GIS|Tiling")
    void LoadTileAsync(const FString& TileID);

    /** 同步加载瓦片，返回加载的 DataAsset（阻塞） */
    UFUNCTION(BlueprintCallable, Category = "GIS|Tiling")
    ULandUseMapDataAsset* LoadTileSync(const FString& TileID);

    /** 卸载瓦片（释放内存） */
    UFUNCTION(BlueprintCallable, Category = "GIS|Tiling")
    void UnloadTile(const FString& TileID);

    /** 获取指定世界范围内的瓦片 ID 列表 */
    UFUNCTION(BlueprintCallable, Category = "GIS|Tiling")
    TArray<FString> GetTileIDsInWorldBounds(const FBox& WorldBounds) const;

    /** 瓦片加载完成回调 */
    UPROPERTY(BlueprintAssignable, Category = "GIS|Tiling")
    FOnTileLoaded OnTileLoaded;

private:
    /** 空间网格索引：cell 坐标 → Polygon 在 Polygons 数组中的索引列表 */
    TMap<FIntPoint, TArray<int32>> SpatialGrid;

    /** 空间网格 cell 尺寸（UE 世界单位） */
    float SpatialCellSize = 0.0f;

    /** 世界坐标 → cell 坐标 */
    FIntPoint WorldToCell(const FVector& WorldPos) const;

    /** 已加载的瓦片 DataAsset 缓存 */
    UPROPERTY(Transient)
    TMap<FString, ULandUseMapDataAsset*> LoadedTileCache;

    /** 流式加载管理器 */
    FStreamableManager StreamableManager;
};
