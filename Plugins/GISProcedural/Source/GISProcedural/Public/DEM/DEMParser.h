// DEMParser.h - DEM 瓦片解析器
#pragma once

#include "CoreMinimal.h"
#include "DEM/DEMTypes.h"
#include "DEMParser.generated.h"

/**
 * DEM 瓦片解析器
 * 支持 GeoTIFF（SRTM/ALOS）和 PNG Heightmap（Mapbox Terrain RGB / 灰度）
 *
 * 用法：
 *   UDEMParser* Parser = NewObject<UDEMParser>();
 *   Parser->Format = EDEMFormat::Auto;
 *   if (Parser->LoadTile("path/to/N39E116.tif"))
 *   {
 *       float Elev = Parser->SampleElevation(116.5, 39.5);
 *   }
 *   // 或加载多瓦片
 *   Parser->LoadTilesFromDirectory("path/to/tiles/");
 *   float Elev = Parser->SampleElevationMultiTile(116.5, 39.5);
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API UDEMParser : public UObject
{
    GENERATED_BODY()

public:
    // ============ 配置 ============

    /** DEM 文件格式 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEM")
    EDEMFormat Format = EDEMFormat::Auto;

    /** PNG 高程编码方式（仅 HeightmapPNG 格式时生效） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEM")
    EPNGHeightEncoding PNGEncoding = EPNGHeightEncoding::Grayscale16;

    /** 自定义线性映射参数：elevation = pixel * Scale + Offset */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEM",
        meta = (EditCondition = "PNGEncoding == EPNGHeightEncoding::CustomLinear"))
    float CustomScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEM",
        meta = (EditCondition = "PNGEncoding == EPNGHeightEncoding::CustomLinear"))
    float CustomOffset = 0.0f;

    /** SRTM 瓦片命名规则时，自动推断地理范围（如 N39E116.hgt → lon 116~117, lat 39~40） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEM")
    bool bAutoDetectSRTMBounds = true;

    /** 手动指定地理范围（当无法自动检测时） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEM")
    FDEMTileInfo ManualTileInfo;

    // ============ 加载接口 ============

    /**
     * 加载单个 DEM 瓦片
     * @param FilePath 文件路径
     * @return 是否成功
     */
    UFUNCTION(BlueprintCallable, Category = "DEM")
    bool LoadTile(const FString& FilePath);

    /**
     * 从目录加载所有 DEM 瓦片（支持 .tif / .png / .hgt / .r16）
     * @param DirectoryPath 目录路径
     * @return 加载的瓦片数量
     */
    UFUNCTION(BlueprintCallable, Category = "DEM")
    int32 LoadTilesFromDirectory(const FString& DirectoryPath);

    /** 已加载的瓦片数量 */
    UFUNCTION(BlueprintCallable, Category = "DEM")
    int32 GetTileCount() const { return Tiles.Num(); }

    // ============ 采样接口 ============

    /**
     * 在指定经纬度采样高程（单瓦片，使用第一个加载的瓦片）
     * @param Lon 经度
     * @param Lat 纬度
     * @return 高程值（米），无数据时返回 NoDataValue
     */
    UFUNCTION(BlueprintCallable, Category = "DEM")
    float SampleElevation(double Lon, double Lat) const;

    /**
     * 多瓦片采样（自动选择覆盖该经纬度的瓦片）
     */
    UFUNCTION(BlueprintCallable, Category = "DEM")
    float SampleElevationMultiTile(double Lon, double Lat) const;

    /**
     * 双线性插值采样（更平滑）
     */
    UFUNCTION(BlueprintCallable, Category = "DEM")
    float SampleElevationBilinear(double Lon, double Lat) const;

    /**
     * 获取指定范围的高程网格
     * @param MinLon/MinLat/MaxLon/MaxLat 地理范围
     * @param GridResolution 输出网格分辨率（米）
     * @param OutGrid 输出高程网格（行优先，左下角起始）
     * @param OutWidth/OutHeight 输出网格尺寸
     */
    UFUNCTION(BlueprintCallable, Category = "DEM")
    bool GetElevationGrid(
        double MinLon, double MinLat,
        double MaxLon, double MaxLat,
        float GridResolution,
        TArray<float>& OutGrid,
        int32& OutWidth, int32& OutHeight
    ) const;

    /** NODATA 标记值 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEM")
    float NoDataValue = -9999.0f;

private:
    /** 已加载的瓦片元数据 */
    TArray<FDEMTileInfo> Tiles;

    /** 高程数据（与 Tiles 一一对应，每个 TArray<float> 是行优先的栅格） */
    TArray<TArray<float>> TileData;

    // ---- 格式解析 ----

    /** 解析 GeoTIFF 文件 */
    bool ParseGeoTIFF(const FString& FilePath, FDEMTileInfo& OutInfo, TArray<float>& OutData);

    /** 解析 PNG Heightmap */
    bool ParseHeightmapPNG(const FString& FilePath, FDEMTileInfo& OutInfo, TArray<float>& OutData);

    /** 解析 RAW Heightmap (.r16 / .raw) */
    bool ParseHeightmapRAW(const FString& FilePath, FDEMTileInfo& OutInfo, TArray<float>& OutData);

    /** 从 SRTM 文件名推断地理范围（如 N39E116.hgt） */
    bool InferSRTMBounds(const FString& FileName, FDEMTileInfo& OutInfo) const;

    /** 自动检测格式 */
    EDEMFormat DetectFormat(const FString& FilePath) const;

    /** 查找覆盖指定经纬度的瓦片索引 */
    int32 FindTileForCoord(double Lon, double Lat) const;

    /** 在指定瓦片上采样 */
    float SampleTile(int32 TileIndex, double Lon, double Lat) const;

    /** 在指定瓦片上双线性插值采样 */
    float SampleTileBilinear(int32 TileIndex, double Lon, double Lat) const;
};
