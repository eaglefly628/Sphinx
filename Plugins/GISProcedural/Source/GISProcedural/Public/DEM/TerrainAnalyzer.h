// TerrainAnalyzer.h - 地形分析器
#pragma once

#include "CoreMinimal.h"
#include "DEM/DEMTypes.h"
#include "TerrainAnalyzer.generated.h"

class UDEMParser;

/**
 * 地形分区配置
 */
USTRUCT(BlueprintType)
struct GISPROCEDURAL_API FTerrainZoneConfig
{
    GENERATED_BODY()

    /** 分析区域的网格分辨率（米），越小越精细但越慢 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float AnalysisResolution = 30.0f;

    /** 坡度计算的邻域窗口大小（像素，3 = 3x3） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    int32 SlopeKernelSize = 3;

    // ---- 高程分区阈值 ----

    /** 海拔分级（米）：低于此值 = 低地 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Elevation")
    float LowElevationMax = 50.0f;

    /** 海拔分级（米）：高于此值 = 高地/山区 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Elevation")
    float HighElevationMin = 300.0f;

    // ---- 坡度分区阈值 ----

    /** 坡度分级（度）：低于此值 = 平地 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Slope")
    float FlatMaxSlope = 5.0f;

    /** 坡度分级（度）：高于此值 = 陡坡 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Slope")
    float SteepMinSlope = 15.0f;

    // ---- 聚类参数 ----

    /** 最小区域面积（平方米），小于此值的区域合并到邻居 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Clustering")
    float MinZoneAreaSqM = 2000.0f;

    /** 区域合并时的形态学膨胀半径（像素） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Clustering")
    int32 MorphologyRadius = 2;
};

/**
 * 地形分区结果
 */
USTRUCT(BlueprintType)
struct GISPROCEDURAL_API FTerrainZone
{
    GENERATED_BODY()

    /** 分区 ID */
    UPROPERTY(BlueprintReadOnly, Category = "Terrain")
    int32 ZoneID = 0;

    /** 分区内的平均高程 */
    UPROPERTY(BlueprintReadOnly, Category = "Terrain")
    float AvgElevation = 0.0f;

    /** 分区内的平均坡度 */
    UPROPERTY(BlueprintReadOnly, Category = "Terrain")
    float AvgSlope = 0.0f;

    /** 分区面积（平方米） */
    UPROPERTY(BlueprintReadOnly, Category = "Terrain")
    float AreaSqM = 0.0f;

    /** 分区边界多边形（世界坐标） */
    UPROPERTY(BlueprintReadOnly, Category = "Terrain")
    TArray<FVector> BoundaryVertices;

    /** 分区边界多边形（经纬度） */
    UPROPERTY(BlueprintReadOnly, Category = "Terrain")
    TArray<FVector2D> BoundaryGeoCoords;

    /**
     * 地形类别：
     * 0 = 低平地 (Low Flat)      → 可能是农田/平原/沿海
     * 1 = 低缓坡 (Low Gentle)    → 可能是居住区/商业区
     * 2 = 中高程平地 (Mid Flat)   → 可能是高原/台地
     * 3 = 中缓坡 (Mid Gentle)    → 可能是丘陵住宅
     * 4 = 高陡坡 (High Steep)    → 可能是山地/林地
     * 5 = 水体候选 (Water Candidate) → 由 DEM 低洼 + 平坦检测
     */
    UPROPERTY(BlueprintReadOnly, Category = "Terrain")
    int32 TerrainClass = 0;
};

/**
 * 地形分析器
 *
 * 从 DEM 数据中分析出地形分区：
 * 1. 计算坡度图/坡向图
 * 2. 根据高程+坡度做栅格分类
 * 3. 连通区域标记 (Connected Component Labeling)
 * 4. 小区域合并
 * 5. 提取分区边界为多边形 (Marching Squares / Contour Tracing)
 *
 * 这些分区是 "自然边界"，后续被道路/河流/海岸线 "切割" 成最终 Polygon
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API UTerrainAnalyzer : public UObject
{
    GENERATED_BODY()

public:
    // ============ 配置 ============

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    FTerrainZoneConfig Config;

    // ============ 主接口 ============

    /**
     * 从 DEM 数据执行完整的地形分析
     * @param DEMParser 已加载数据的 DEM 解析器
     * @param MinLon/MinLat/MaxLon/MaxLat 分析区域范围
     * @param OriginLon/OriginLat 坐标转换原点
     * @return 地形分区数组
     */
    UFUNCTION(BlueprintCallable, Category = "Terrain")
    TArray<FTerrainZone> AnalyzeTerrain(
        UDEMParser* DEMParser,
        double MinLon, double MinLat,
        double MaxLon, double MaxLat,
        double OriginLon, double OriginLat
    );

    // ============ 分步接口 ============

    /** Step 1: 从 DEM 生成高程网格 */
    UFUNCTION(BlueprintCallable, Category = "Terrain|Steps")
    bool BuildElevationGrid(
        UDEMParser* DEMParser,
        double MinLon, double MinLat,
        double MaxLon, double MaxLat
    );

    /** Step 2: 计算坡度图和坡向图 */
    UFUNCTION(BlueprintCallable, Category = "Terrain|Steps")
    bool ComputeSlopeAspect();

    /** Step 3: 根据高程+坡度做栅格分类 */
    UFUNCTION(BlueprintCallable, Category = "Terrain|Steps")
    bool ClassifyTerrainGrid();

    /** Step 4: 连通区域标记 + 小区域合并 */
    UFUNCTION(BlueprintCallable, Category = "Terrain|Steps")
    bool LabelConnectedZones();

    /** Step 5: 提取分区边界为多边形 */
    UFUNCTION(BlueprintCallable, Category = "Terrain|Steps")
    TArray<FTerrainZone> ExtractZoneBoundaries(double OriginLon, double OriginLat);

    // ============ 调试 ============

    /** 获取坡度图数据（调试可视化用） */
    UFUNCTION(BlueprintCallable, Category = "Terrain|Debug")
    void GetSlopeGrid(TArray<float>& OutSlope, int32& OutWidth, int32& OutHeight) const;

    /** 获取分类图数据（调试可视化用） */
    UFUNCTION(BlueprintCallable, Category = "Terrain|Debug")
    void GetClassGrid(TArray<int32>& OutClass, int32& OutWidth, int32& OutHeight) const;

private:
    // ---- 内部栅格数据 ----

    /** 高程网格 */
    TArray<float> ElevationGrid;

    /** 坡度网格 */
    TArray<float> SlopeGrid;

    /** 坡向网格 */
    TArray<float> AspectGrid;

    /** 地形分类网格（每个像素的地形类别 ID） */
    TArray<int32> ClassGrid;

    /** 连通区域标签网格（每个像素的 ZoneID） */
    TArray<int32> LabelGrid;

    /** 网格尺寸 */
    int32 GridWidth = 0;
    int32 GridHeight = 0;

    /** 分析区域的地理范围 */
    double AnalysisMinLon = 0.0;
    double AnalysisMinLat = 0.0;
    double AnalysisMaxLon = 0.0;
    double AnalysisMaxLat = 0.0;

    // ---- 算法 ----

    /** 计算单像素坡度（3x3 Horn 算法） */
    float ComputeSlopeAtPixel(int32 X, int32 Y, float CellSizeMeters) const;

    /** 计算单像素坡向 */
    float ComputeAspectAtPixel(int32 X, int32 Y) const;

    /** 将高程+坡度映射到地形类别 */
    int32 ClassifyPixel(float Elevation, float Slope) const;

    /**
     * Connected Component Labeling (CCL)
     * 使用 Two-Pass 算法标记连通区域
     */
    int32 RunCCL();

    /** 合并面积过小的区域到最大邻居 */
    void MergeSmallZones(int32 NumZones, float CellAreaSqM);

    /**
     * 从标签网格提取分区边界
     * 使用简化的 Contour Tracing 算法
     */
    TArray<TArray<FVector2D>> TraceBoundaries(int32 NumZones) const;

    /** 获取栅格中的高程值（带边界检查） */
    float GetElevation(int32 X, int32 Y) const;
};
