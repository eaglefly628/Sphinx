// TileManifest.h - 瓦片清单共享类型
// Phase 0 接口约定：小城（TiledFileProvider）和鹰飞（WorldBuilder/PCG）共用
#pragma once

#include "CoreMinimal.h"
#include "Data/GeoRect.h"
#include "TileManifest.generated.h"

/**
 * 单个瓦片的元数据
 * 对应预处理管线输出的 tile_manifest.json 中每条记录
 */
USTRUCT(BlueprintType)
struct GISPROCEDURAL_API FTileEntry
{
    GENERATED_BODY()

    /** 瓦片列号 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    int32 Col = 0;

    /** 瓦片行号 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    int32 Row = 0;

    /** 瓦片地理范围 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    FGeoRect GeoBounds;

    /** GeoJSON 文件相对路径 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    FString GeoJsonRelPath;

    /** DEM 文件相对路径 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    FString DEMRelPath;

    /** LandCover 栅格文件相对路径（可选） */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    FString LandCoverRelPath;

    /** 该瓦片的要素数量（预处理时统计） */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    int32 FeatureCount = 0;

    /** 唯一键 */
    FIntPoint GetCoord() const { return FIntPoint(Col, Row); }
};

/**
 * 瓦片清单（整个预处理区域）
 * 由 Python/GDAL 管线生成 tile_manifest.json，运行时由 TiledFileProvider 解析
 */
USTRUCT(BlueprintType)
struct GISPROCEDURAL_API FTileManifest
{
    GENERATED_BODY()

    /** 整体地理范围 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    FGeoRect TotalBounds;

    /** 瓦片尺寸（米） */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    float TileSizeM = 1024.0f;

    /** UTM Zone（所有瓦片统一） */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    int32 UTMZone = 0;

    /** 是否北半球 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    bool bNorthernHemisphere = true;

    /** 投影原点经度 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    double OriginLongitude = 0.0;

    /** 投影原点纬度 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    double OriginLatitude = 0.0;

    /** 所有瓦片条目 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    TArray<FTileEntry> Tiles;

    /** 网格列数 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    int32 NumCols = 0;

    /** 网格行数 */
    UPROPERTY(BlueprintReadWrite, Category = "GIS|Tile")
    int32 NumRows = 0;

    /** 从 JSON 文件解析 tile_manifest.json */
    static bool LoadFromFile(const FString& FilePath, FTileManifest& OutManifest);

    /** 从 JSON 字符串解析 */
    static bool ParseFromJson(const FString& JsonString, FTileManifest& OutManifest);

    /** 按坐标查找 tile（线性搜索，小规模使用） */
    const FTileEntry* FindTile(int32 Col, int32 Row) const;

    /** 查找与给定地理范围相交的所有 tile */
    TArray<const FTileEntry*> FindTilesInBounds(const FGeoRect& Bounds) const;

    /** 是否有效 */
    bool IsValid() const { return Tiles.Num() > 0 && TileSizeM > 0.0f; }
};
