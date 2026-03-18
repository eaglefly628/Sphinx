// LandUseMapDataAsset.h - 离线持久化数据资产
// 存储生成的 LandUsePolygon 数组，序列化为 .uasset
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/IGISDataProvider.h"
#include "Polygon/LandUsePolygon.h"
#include "LandUseMapDataAsset.generated.h"

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
};
