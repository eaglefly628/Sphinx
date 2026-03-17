// PolygonDeriver.h - Polygon 推导算法核心
// 流程：DEM 地形分区 → 矢量数据（道路/河流/海岸线）切割 → 最终 Polygon
#pragma once

#include "CoreMinimal.h"
#include "Data/GISFeature.h"
#include "Polygon/LandUsePolygon.h"
#include "Polygon/RoadNetworkGraph.h"
#include "DEM/DEMTypes.h"
#include "PolygonDeriver.generated.h"

class ULandUseClassifyRules;
class UDEMParser;
class UTerrainAnalyzer;
struct FTerrainZone;

/** Polygon 生成完成的委托 */
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnPolygonsGenerated, const TArray<FLandUsePolygon>&, Polygons);

/**
 * 核心算法类：从 DEM + 矢量数据推导出土地分类 Polygon
 *
 * 算法流程：
 *   1. 加载 DEM 瓦片 → 高程网格
 *   2. 地形分析（坡度/坡向） → 栅格分类 → 连通区域 → 地形分区
 *   3. 加载矢量数据（GeoJSON：道路/河流/海岸线/水体）
 *   4. 用矢量线要素（道路/河流/海岸线）切割地形分区 → 细化 Polygon
 *   5. 用矢量面要素（水体/已有 landuse）覆盖/标记对应 Polygon
 *   6. 综合分类 + 填充 PCG 参数
 *
 * 纯计算，无渲染依赖，可在任何线程执行
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API UPolygonDeriver : public UObject
{
    GENERATED_BODY()

public:

    /**
     * 主入口：从 DEM + GeoJSON 生成 Polygon
     * @param DEMPath            DEM 文件或目录路径
     * @param GeoJsonPath        OSM GeoJSON 文件路径（含道路/河流/海岸线等）
     * @param OriginLon          区域中心经度
     * @param OriginLat          区域中心纬度
     * @return 生成的 Polygon 数组
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural")
    TArray<FLandUsePolygon> GeneratePolygons(
        const FString& DEMPath,
        const FString& GeoJsonPath,
        double OriginLon,
        double OriginLat
    );

    /** 异步版本 */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural")
    void GeneratePolygonsAsync(
        const FString& DEMPath,
        const FString& GeoJsonPath,
        double OriginLon,
        double OriginLat,
        const FOnPolygonsGenerated& OnComplete
    );

    // ======== 分步接口（调试 / AngelScript 逐步调用） ========

    /** Step 1: 加载 DEM 瓦片 */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Steps")
    bool LoadDEM(const FString& DEMPath);

    /** Step 2: 执行地形分析 → 地形分区 */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Steps")
    bool AnalyzeTerrain(
        double MinLon, double MinLat,
        double MaxLon, double MaxLat,
        double OriginLon, double OriginLat
    );

    /** Step 3: 加载 OSM 矢量数据（自动分类为道路/河流/海岸线/水体等） */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Steps")
    bool LoadVectorData(const FString& GeoJsonPath);

    /** Step 4: 用矢量线要素切割地形分区 → 细化 Polygon */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Steps")
    TArray<FLandUsePolygon> CutZonesWithVectors(double OriginLon, double OriginLat);

    /** Step 5: 对每个 Polygon 做综合分类 */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Steps")
    void ClassifyPolygons(UPARAM(ref) TArray<FLandUsePolygon>& Polygons);

    /** Step 6: 填充 PCG 生成参数 */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Steps")
    void AssignPCGParams(UPARAM(ref) TArray<FLandUsePolygon>& Polygons);

    // ======== 旧接口保留（兼容） ========

    /** 构建道路图（独立使用，如只需要道路网可视化） */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Steps")
    bool BuildRoadGraph();

    // ======== 参数配置 ========

    /** 道路等级权重（用于判断主干道/次干道） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GISProcedural|Config")
    TMap<FString, int32> RoadClassWeights;

    /** 最小 Polygon 面积（平方米，小于此值的合并或丢弃） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GISProcedural|Config")
    float MinPolygonArea = 500.0f;

    /** DEM 格式（Auto = 自动检测） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GISProcedural|Config")
    EDEMFormat DEMFormat = EDEMFormat::Auto;

    /** 地形分析分辨率（米） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GISProcedural|Config")
    float TerrainAnalysisResolution = 30.0f;

    /** 分类规则配置 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GISProcedural|Config")
    ULandUseClassifyRules* ClassifyRules = nullptr;

    // ======== 调试输出 ========

    /** 获取地形分区结果（调试用） */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Debug")
    const TArray<FTerrainZone>& GetTerrainZones() const { return TerrainZones; }

    /** 获取各类矢量要素数量统计 */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Debug")
    FString GetVectorStats() const;

private:
    /** DEM 解析器 */
    UPROPERTY()
    UDEMParser* DEMParserInstance = nullptr;

    /** 地形分析器 */
    UPROPERTY()
    UTerrainAnalyzer* TerrainAnalyzerInstance = nullptr;

    /** 地形分区结果 */
    TArray<FTerrainZone> TerrainZones;

    /** 分类后的矢量要素 */
    TArray<FGISFeature> RoadFeatures;
    TArray<FGISFeature> RiverFeatures;
    TArray<FGISFeature> CoastlineFeatures;
    TArray<FGISFeature> WaterBodyFeatures;
    TArray<FGISFeature> AllFeatures;

    /** 构建的道路网络图 */
    TSharedPtr<FRoadNetworkGraph> RoadGraph;

    /** Polygon ID 计数器 */
    int32 NextPolygonID = 0;

    // ---- 切割算法 ----

    /**
     * 用线要素切割一个地形分区多边形
     * 输入一个 Polygon + 一组切割线 → 输出多个子 Polygon
     */
    TArray<TArray<FVector>> CutPolygonWithLines(
        const TArray<FVector>& PolygonVerts,
        const TArray<TArray<FVector>>& CutLines
    ) const;

    /**
     * 将水体面域标记到对应 Polygon
     * 检测 Polygon 与水体面域的重叠，标记为 Water
     */
    void MarkWaterBodies(TArray<FLandUsePolygon>& Polygons) const;

    /**
     * 根据地形分区的 TerrainClass 初步设定 LandUseType
     */
    static ELandUseType TerrainClassToLandUse(int32 TerrainClass);

    /** 计算多边形面积（Shoelace 公式） */
    static float ComputePolygonArea(const TArray<FVector>& Vertices);

    /** 计算多边形中心点 */
    static FVector ComputePolygonCenter(const TArray<FVector>& Vertices);

    /** 判断多边形是否为顺时针方向 */
    static bool IsClockwise(const TArray<FVector>& Vertices);

    /** 从 DEM 元数据推断分析区域范围 */
    bool InferAnalysisBounds(double& MinLon, double& MinLat, double& MaxLon, double& MaxLat) const;
};
