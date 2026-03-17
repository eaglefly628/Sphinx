// PolygonDeriver.h - Polygon 推导算法核心
#pragma once

#include "CoreMinimal.h"
#include "Data/GISFeature.h"
#include "Polygon/LandUsePolygon.h"
#include "Polygon/RoadNetworkGraph.h"
#include "PolygonDeriver.generated.h"

class ULandUseClassifyRules;

/** Polygon 生成完成的委托 */
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnPolygonsGenerated, const TArray<FLandUsePolygon>&, Polygons);

/**
 * 核心算法类：从道路网推导出土地分类 Polygon
 * 纯计算，无渲染依赖，可在任何线程执行
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API UPolygonDeriver : public UObject
{
    GENERATED_BODY()

public:

    /**
     * 主入口：从 GeoJSON 文件生成 Polygon
     * @param RoadsGeoJsonPath   道路矢量文件路径
     * @param DEMFilePath        DEM 高程文件路径（可选，用于坡度/海拔分类）
     * @param OriginLon          区域中心经度（坐标转换用）
     * @param OriginLat          区域中心纬度
     * @return 生成的 Polygon 数组
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural")
    TArray<FLandUsePolygon> GeneratePolygons(
        const FString& RoadsGeoJsonPath,
        const FString& DEMFilePath,
        double OriginLon,
        double OriginLat
    );

    /**
     * 异步版本（大数据量用）
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural")
    void GeneratePolygonsAsync(
        const FString& RoadsGeoJsonPath,
        const FString& DEMFilePath,
        double OriginLon,
        double OriginLat,
        const FOnPolygonsGenerated& OnComplete
    );

    // ======== 分步接口（给 AngelScript 用，可逐步调试） ========

    /** Step 1: 解析道路数据 */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Steps")
    bool LoadRoadNetwork(const FString& GeoJsonPath);

    /** Step 2: 构建道路图（Graph） */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Steps")
    bool BuildRoadGraph();

    /** Step 3: 从道路图提取闭合面域 */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Steps")
    TArray<FLandUsePolygon> ExtractPolygons();

    /** Step 4: 对每个 Polygon 做分类 */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Steps")
    void ClassifyPolygons(UPARAM(ref) TArray<FLandUsePolygon>& Polygons);

    /** Step 5: 填充 PCG 生成参数 */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Steps")
    void AssignPCGParams(UPARAM(ref) TArray<FLandUsePolygon>& Polygons);

    // ======== 参数配置 ========

    /** 道路等级权重（用于判断主干道/次干道） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GISProcedural|Config")
    TMap<FString, int32> RoadClassWeights;

    /** 最小 Polygon 面积（平方米，小于此值的合并或丢弃） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GISProcedural|Config")
    float MinPolygonArea = 500.0f;

    /** 分类规则配置 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GISProcedural|Config")
    ULandUseClassifyRules* ClassifyRules = nullptr;

private:
    /** 已加载的道路要素 */
    TArray<FGISFeature> RoadFeatures;

    /** 构建的道路网络图 */
    TSharedPtr<FRoadNetworkGraph> RoadGraph;

    /** Polygon ID 计数器 */
    int32 NextPolygonID = 0;

    /**
     * Planar Face Extraction 核心算法
     * 从平面图中提取所有闭合面
     */
    TArray<TArray<FVector>> ExtractPlanarFaces() const;

    /** 计算多边形面积（Shoelace 公式） */
    static float ComputePolygonArea(const TArray<FVector>& Vertices);

    /** 计算多边形中心点 */
    static FVector ComputePolygonCenter(const TArray<FVector>& Vertices);

    /** 判断多边形是否为顺时针方向 */
    static bool IsClockwise(const TArray<FVector>& Vertices);
};
