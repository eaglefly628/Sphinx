// LandUsePolygon.h - 输出 Polygon 数据结构
#pragma once

#include "CoreMinimal.h"
#include "LandUsePolygon.generated.h"

/** 土地分类枚举 */
UENUM(BlueprintType)
enum class ELandUseType : uint8
{
    Residential     UMETA(DisplayName = "居住区"),
    Commercial      UMETA(DisplayName = "商业区"),
    Industrial      UMETA(DisplayName = "工业区"),
    Forest          UMETA(DisplayName = "林地"),
    Farmland        UMETA(DisplayName = "农田"),
    Water           UMETA(DisplayName = "水体"),
    Road            UMETA(DisplayName = "道路"),
    OpenSpace       UMETA(DisplayName = "开阔地"),
    Military        UMETA(DisplayName = "军事区"),
    Unknown         UMETA(DisplayName = "未知"),
};

/** 推导出的土地分类多边形 */
USTRUCT(BlueprintType)
struct GISPROCEDURAL_API FLandUsePolygon
{
    GENERATED_BODY()

    /** 多边形 ID */
    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    int32 PolygonID = 0;

    /** 土地分类 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    ELandUseType LandUseType = ELandUseType::Unknown;

    /** 多边形顶点（UE5 世界坐标，已转换） */
    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    TArray<FVector> WorldVertices;

    /** 多边形顶点（原始经纬度） */
    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    TArray<FVector2D> GeoVertices;

    /** 面积（平方米） */
    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    float AreaSqM = 0.0f;

    /** 中心点 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    FVector WorldCenter = FVector::ZeroVector;

    /** 平均海拔 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    float AvgElevation = 0.0f;

    /** 平均坡度 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    float AvgSlope = 0.0f;

    /** 是否临主干道 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    bool bAdjacentToMainRoad = false;

    /** PCG 生成参数（由分类器填充） */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|PCG")
    float BuildingDensity = 0.0f;

    UPROPERTY(BlueprintReadWrite, Category = "GIS|PCG")
    float VegetationDensity = 0.0f;

    UPROPERTY(BlueprintReadWrite, Category = "GIS|PCG")
    int32 MinFloors = 1;

    UPROPERTY(BlueprintReadWrite, Category = "GIS|PCG")
    int32 MaxFloors = 3;

    UPROPERTY(BlueprintReadWrite, Category = "GIS|PCG")
    float BuildingSetback = 5.0f;
};
