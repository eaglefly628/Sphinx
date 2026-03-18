// GISFeature.h - GIS 要素数据结构
#pragma once

#include "CoreMinimal.h"
#include "GISFeature.generated.h"

/** GIS 几何类型 */
UENUM(BlueprintType)
enum class EGISGeometryType : uint8
{
    Point,
    LineString,
    Polygon,
    MultiPolygon,
};

/**
 * OSM 要素分类（从 properties 中的 highway/waterway/natural 等 tag 推断）
 * 用于区分道路、河流、海岸线等不同类型的矢量数据
 */
UENUM(BlueprintType)
enum class EGISFeatureCategory : uint8
{
    /** 道路（highway=*） */
    Road            UMETA(DisplayName = "道路"),

    /** 河流/溪流（waterway=river/stream/canal） */
    River           UMETA(DisplayName = "河流"),

    /** 海岸线（natural=coastline） */
    Coastline       UMETA(DisplayName = "海岸线"),

    /** 水体面域（natural=water / waterway=riverbank / landuse=reservoir） */
    WaterBody       UMETA(DisplayName = "水体"),

    /** 建筑（building=*） */
    Building        UMETA(DisplayName = "建筑"),

    /** 已有土地分类（landuse=*） */
    LandUse         UMETA(DisplayName = "土地利用"),

    /** 自然要素（natural=wood/scrub/grassland 等） */
    Natural         UMETA(DisplayName = "自然要素"),

    /** 其他/未分类 */
    Other           UMETA(DisplayName = "其他"),
};

/** 单条 GIS 要素 */
USTRUCT(BlueprintType)
struct GISPROCEDURAL_API FGISFeature
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    EGISGeometryType GeometryType = EGISGeometryType::Point;

    /** OSM 要素分类（自动从 Properties 推断） */
    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    EGISFeatureCategory Category = EGISFeatureCategory::Other;

    /** 经纬度点序列 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    TArray<FVector2D> Coordinates;

    /** 属性字段 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    TMap<FString, FString> Properties;
};
