// DEMTypes.h - DEM 数据结构定义
#pragma once

#include "CoreMinimal.h"
#include "DEMTypes.generated.h"

/**
 * 单个 DEM 瓦片的元数据
 */
USTRUCT(BlueprintType)
struct GISPROCEDURAL_API FDEMTileInfo
{
    GENERATED_BODY()

    /** 瓦片文件路径 */
    UPROPERTY(BlueprintReadWrite, Category = "DEM")
    FString FilePath;

    /** 左下角经度 */
    UPROPERTY(BlueprintReadWrite, Category = "DEM")
    double MinLon = 0.0;

    /** 左下角纬度 */
    UPROPERTY(BlueprintReadWrite, Category = "DEM")
    double MinLat = 0.0;

    /** 右上角经度 */
    UPROPERTY(BlueprintReadWrite, Category = "DEM")
    double MaxLon = 0.0;

    /** 右上角纬度 */
    UPROPERTY(BlueprintReadWrite, Category = "DEM")
    double MaxLat = 0.0;

    /** 像素宽度 */
    UPROPERTY(BlueprintReadWrite, Category = "DEM")
    int32 Width = 0;

    /** 像素高度 */
    UPROPERTY(BlueprintReadWrite, Category = "DEM")
    int32 Height = 0;

    /** 每像素对应的经度跨度 */
    double PixelSizeLon() const { return (Width > 0) ? (MaxLon - MinLon) / Width : 0.0; }

    /** 每像素对应的纬度跨度 */
    double PixelSizeLat() const { return (Height > 0) ? (MaxLat - MinLat) / Height : 0.0; }
};

/**
 * DEM 来源格式
 */
UENUM(BlueprintType)
enum class EDEMFormat : uint8
{
    /** SRTM / ALOS GeoTIFF (.tif) — 单波段 int16/float 高程值 */
    GeoTIFF     UMETA(DisplayName = "GeoTIFF"),

    /** PNG Heightmap — 灰度 16bit 或 Mapbox Terrain RGB 编码 */
    HeightmapPNG UMETA(DisplayName = "Heightmap PNG"),

    /** RAW Heightmap — UE Landscape 常用的 r16/r32 格式 */
    HeightmapRAW UMETA(DisplayName = "Heightmap RAW"),

    /** 自动检测（按扩展名） */
    Auto        UMETA(DisplayName = "Auto Detect"),
};

/**
 * Mapbox Terrain RGB 解码方式
 * elevation = -10000 + ((R * 256 * 256 + G * 256 + B) * 0.1)
 */
UENUM(BlueprintType)
enum class EPNGHeightEncoding : uint8
{
    /** 灰度图：像素值直接映射高程（需指定范围） */
    Grayscale16     UMETA(DisplayName = "Grayscale 16bit"),

    /** Mapbox Terrain RGB 编码 */
    MapboxTerrainRGB UMETA(DisplayName = "Mapbox Terrain RGB"),

    /** 自定义线性映射：elevation = pixel * Scale + Offset */
    CustomLinear     UMETA(DisplayName = "Custom Linear"),
};

/**
 * 地形分析结果（每个栅格单元）
 */
USTRUCT(BlueprintType)
struct GISPROCEDURAL_API FTerrainCell
{
    GENERATED_BODY()

    /** 高程值（米） */
    UPROPERTY(BlueprintReadOnly, Category = "DEM")
    float Elevation = 0.0f;

    /** 坡度（度，0-90） */
    UPROPERTY(BlueprintReadOnly, Category = "DEM")
    float Slope = 0.0f;

    /** 坡向（度，0-360，北=0，顺时针） */
    UPROPERTY(BlueprintReadOnly, Category = "DEM")
    float Aspect = 0.0f;

    /** 地形分区 ID（聚类后赋值） */
    UPROPERTY(BlueprintReadOnly, Category = "DEM")
    int32 ZoneID = INDEX_NONE;
};
