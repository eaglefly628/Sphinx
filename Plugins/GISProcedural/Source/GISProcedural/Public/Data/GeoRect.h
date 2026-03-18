// GeoRect.h - 地理矩形范围
#pragma once

#include "CoreMinimal.h"
#include "GeoRect.generated.h"

/** 地理矩形范围（经纬度） */
USTRUCT(BlueprintType)
struct GISPROCEDURAL_API FGeoRect
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS")
    double MinLon = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS")
    double MinLat = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS")
    double MaxLon = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS")
    double MaxLat = 0.0;

    bool IsValid() const
    {
        return MaxLon > MinLon && MaxLat > MinLat;
    }

    FVector2D GetCenter() const
    {
        return FVector2D((MinLon + MaxLon) * 0.5, (MinLat + MaxLat) * 0.5);
    }
};
