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

/** 单条 GIS 要素 */
USTRUCT(BlueprintType)
struct GISPROCEDURAL_API FGISFeature
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    EGISGeometryType GeometryType = EGISGeometryType::Point;

    /** 经纬度点序列 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    TArray<FVector2D> Coordinates;

    /** 属性字段 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    TMap<FString, FString> Properties;
};
