// LandCoverGrid.h - 栅格 LandCover 数据共享类型
// Phase 0 接口约定：用于 IGISDataProvider::QueryLandCover 输出
#pragma once

#include "CoreMinimal.h"
#include "Data/GeoRect.h"
#include "LandCoverGrid.generated.h"

/**
 * ESA WorldCover v200 分类码枚举
 * 参考: https://esa-worldcover.org/en
 */
UENUM(BlueprintType)
enum class EWorldCoverClass : uint8
{
    TreeCover    = 10  UMETA(DisplayName = "Tree Cover"),
    Shrubland    = 20  UMETA(DisplayName = "Shrubland"),
    Grassland    = 30  UMETA(DisplayName = "Grassland"),
    Cropland     = 40  UMETA(DisplayName = "Cropland"),
    BuiltUp      = 50  UMETA(DisplayName = "Built-up"),
    BareSparse   = 60  UMETA(DisplayName = "Bare / Sparse Vegetation"),
    SnowIce      = 70  UMETA(DisplayName = "Snow and Ice"),
    Water        = 80  UMETA(DisplayName = "Permanent Water Bodies"),
    Wetland      = 90  UMETA(DisplayName = "Herbaceous Wetland"),
    Mangroves    = 95  UMETA(DisplayName = "Mangroves"),
    MossLichen   = 100 UMETA(DisplayName = "Moss and Lichen"),
    NoData       = 0   UMETA(DisplayName = "No Data"),
};

/**
 * LandCover 栅格数据块
 * 一个瓦片或查询区域的栅格分类结果
 */
USTRUCT(BlueprintType)
struct GISPROCEDURAL_API FLandCoverGrid
{
    GENERATED_BODY()

    /** 栅格数据（ESA WorldCover 类码，行优先） */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|LandCover")
    TArray<uint8> ClassGrid;

    /** 栅格宽度（列数） */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|LandCover")
    int32 Width = 0;

    /** 栅格高度（行数） */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|LandCover")
    int32 Height = 0;

    /** 栅格覆盖的地理范围 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|LandCover")
    FGeoRect GeoBounds;

    /** 栅格分辨率（米） */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|LandCover")
    float ResolutionM = 10.0f;

    bool IsValid() const
    {
        return Width > 0 && Height > 0 && ClassGrid.Num() == Width * Height;
    }
};
