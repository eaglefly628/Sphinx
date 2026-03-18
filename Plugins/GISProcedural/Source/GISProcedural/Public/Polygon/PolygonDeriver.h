// PolygonDeriver.h - Polygon 推导算法核心
// 流程：数据源查询 → 地形分区（可选） → 矢量切割 → 最终 Polygon
#pragma once

#include "CoreMinimal.h"
#include "Data/GISFeature.h"
#include "Data/IGISDataProvider.h"
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
 * 核心算法类：从 GIS 数据源推导出土地分类 Polygon
 *
 * 两种工作模式：
 *
 * A. DataProvider 模式（推荐）：
 *    SetDataProvider() → GenerateFromProvider()
 *    数据来自 IGISDataProvider（本地文件 / ArcGIS REST / 自定义）
 *
 * B. 传统模式（向后兼容）：
 *    GeneratePolygons(DEMPath, GeoJsonPath, ...)
 *    直接指定文件路径
 *
 * 纯计算，无渲染依赖，可在任何线程执行
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API UPolygonDeriver : public UObject
{
    GENERATED_BODY()

public:

    // ======== DataProvider 模式（推荐） ========

    /**
     * 设置数据源
     * @param Provider 数据源实现（不持有所有权，调用方管理生命周期）
     */
    void SetDataProvider(IGISDataProvider* Provider) { DataProvider = Provider; }

    /**
     * 从 DataProvider 生成 Polygon
     * @param Bounds      查询范围
     * @param OriginLon   坐标原点经度
     * @param OriginLat   坐标原点纬度
     * @return 生成的 Polygon 数组
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural")
    TArray<FLandUsePolygon> GenerateFromProvider(
        double OriginLon,
        double OriginLat
    );

    /** 设置查询范围（经纬度） */
    void SetQueryBounds(const FGeoRect& Bounds) { QueryBounds = Bounds; }

    // ======== 传统模式（向后兼容） ========

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

    /** Step 3: 加载矢量数据（从 DataProvider 或 GeoJSON 文件） */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Steps")
    bool LoadVectorData(const FString& GeoJsonPath);

    /** Step 3b: 从 DataProvider 加载矢量数据 */
    bool LoadVectorDataFromProvider(const FGeoRect& Bounds);

    /** Step 4: 用矢量线要素切割地形分区 → 细化 Polygon */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Steps")
    TArray<FLandUsePolygon> CutZonesWithVectors(double OriginLon, double OriginLat);

    /** Step 4b: 仅从矢量数据生成 Polygon（无 DEM 地形分区） */
    TArray<FLandUsePolygon> GenerateFromVectorsOnly(double OriginLon, double OriginLat);

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
    /** 数据源（DataProvider 模式） */
    IGISDataProvider* DataProvider = nullptr;

    /** 查询范围 */
    FGeoRect QueryBounds;

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

    // ---- 内部方法 ----

    /** 将 AllFeatures 按类别拆分到各数组 */
    void CategorizeFeatures();

    /** 从所有要素推断地理范围 */
    FGeoRect InferBoundsFromFeatures() const;

    // ---- 切割算法 ----

    TArray<TArray<FVector>> CutPolygonWithLines(
        const TArray<FVector>& PolygonVerts,
        const TArray<TArray<FVector>>& CutLines
    ) const;

    void MarkWaterBodies(TArray<FLandUsePolygon>& Polygons) const;

    static ELandUseType TerrainClassToLandUse(int32 TerrainClass);
    static float ComputePolygonArea(const TArray<FVector>& Vertices);
    static FVector ComputePolygonCenter(const TArray<FVector>& Vertices);
    static bool IsClockwise(const TArray<FVector>& Vertices);

    bool InferAnalysisBounds(double& MinLon, double& MinLat, double& MaxLon, double& MaxLat) const;
};
