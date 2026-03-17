// LandUseClassifier.h - 土地分类器
#pragma once

#include "CoreMinimal.h"
#include "Polygon/LandUsePolygon.h"
#include "LandUseClassifier.generated.h"

/**
 * 分类规则配置资产（DataAsset，可在编辑器中调参）
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API ULandUseClassifyRules : public UDataAsset
{
    GENERATED_BODY()

public:
    // ---- 面积阈值 ----

    /** < SmallBlockMaxArea = 小地块（倾向住宅） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area")
    float SmallBlockMaxArea = 5000.0f;

    /** > LargeBlockMinArea = 大地块（可能是开阔地/工业） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area")
    float LargeBlockMinArea = 20000.0f;

    // ---- 坡度阈值 ----

    /** < FlatMaxSlope = 平地 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float FlatMaxSlope = 5.0f;

    /** > HillMinSlope = 山地/林地 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float HillMinSlope = 15.0f;

    // ---- 海拔阈值 ----

    /** < LowElevationMax = 沿海/平原 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float LowElevationMax = 50.0f;

    /** > HighElevationMin = 山区 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float HighElevationMin = 500.0f;

    // ---- 道路相关 ----

    /** 主干道缓冲区宽度（米） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
    float MainRoadBufferM = 30.0f;

    // ---- PCG 参数映射 ----

    /** 各土地类型的建筑密度 (0-1) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
    TMap<ELandUseType, float> BuildingDensityMap;

    /** 各土地类型的植被密度 (0-1) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
    TMap<ELandUseType, float> VegetationDensityMap;

    /** 各土地类型的楼层范围 X=Min, Y=Max */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG")
    TMap<ELandUseType, FIntPoint> FloorRangeMap;
};

/**
 * 土地分类器
 * 根据 Polygon 的面积、坡度、海拔、道路关系等属性判定土地类型
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API ULandUseClassifier : public UObject
{
    GENERATED_BODY()

public:
    /**
     * 对单个 Polygon 进行分类
     * @param Polygon 待分类的 Polygon（会被修改 LandUseType 字段）
     * @param Rules 分类规则
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Classify")
    static void ClassifySingle(UPARAM(ref) FLandUsePolygon& Polygon, const ULandUseClassifyRules* Rules);

    /**
     * 批量分类
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Classify")
    static void ClassifyAll(UPARAM(ref) TArray<FLandUsePolygon>& Polygons, const ULandUseClassifyRules* Rules);

    /**
     * 根据分类结果填充 PCG 参数
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Classify")
    static void AssignPCGParameters(UPARAM(ref) TArray<FLandUsePolygon>& Polygons, const ULandUseClassifyRules* Rules);

private:
    /**
     * 分类算法核心逻辑（按优先级排列）：
     *
     * 1. if (Polygon 被水体矢量覆盖) → Water
     * 2. if (AvgSlope > HillMinSlope) → Forest
     * 3. if (AvgElevation > HighElevationMin) → Forest
     * 4. if (面积 < SmallBlockMaxArea && 临主干道) → Commercial
     * 5. if (面积 < SmallBlockMaxArea && 不临主干道) → Residential
     * 6. if (面积在中间 && 临主干道) → Commercial
     * 7. if (面积在中间 && 不临主干道) → Residential
     * 8. if (面积 > LargeBlockMinArea && 平地) → Farmland 或 Industrial
     * 9. else → OpenSpace
     */
    static ELandUseType DetermineType(const FLandUsePolygon& Polygon, const ULandUseClassifyRules* Rules);
};
