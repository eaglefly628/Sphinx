// GISCoordinate.h - 坐标转换工具
#pragma once

#include "CoreMinimal.h"
#include "GISCoordinate.generated.h"

/**
 * GIS 坐标转换工具类
 * 负责经纬度 ↔ UE5 世界坐标的相互转换
 * 使用简化的 Mercator 投影，以指定原点为中心
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API UGISCoordinate : public UObject
{
    GENERATED_BODY()

public:
    /**
     * 设置投影原点
     * @param OriginLon 原点经度
     * @param OriginLat 原点纬度
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Coordinate")
    void SetOrigin(double OriginLon, double OriginLat);

    /**
     * 经纬度 → UE5 世界坐标（厘米）
     * @param Lon 经度
     * @param Lat 纬度
     * @return UE5 世界坐标（X=东向, Y=北向, Z=0）
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Coordinate")
    FVector GeoToWorld(double Lon, double Lat) const;

    /**
     * UE5 世界坐标 → 经纬度
     * @param WorldPos UE5 世界坐标
     * @return FVector2D(Lon, Lat)
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Coordinate")
    FVector2D WorldToGeo(const FVector& WorldPos) const;

    /**
     * 批量转换经纬度数组 → 世界坐标数组
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Coordinate")
    TArray<FVector> GeoArrayToWorld(const TArray<FVector2D>& GeoCoords) const;

    /**
     * 计算两个经纬度点之间的距离（米）
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Coordinate")
    static double HaversineDistance(double Lon1, double Lat1, double Lon2, double Lat2);

    /**
     * 经纬度 → UTM 坐标（自动计算 Zone）
     * @param Lon            经度
     * @param Lat            纬度
     * @param OutZoneNumber  输出 UTM Zone (1-60)
     * @param OutEasting     输出 Easting (m)
     * @param OutNorthing    输出 Northing (m)
     * @return 是否成功
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Coordinate")
    static bool GeoToUTM(double Lon, double Lat, int32& OutZoneNumber, double& OutEasting, double& OutNorthing);

    /**
     * UTM 坐标 → 经纬度
     * @param ZoneNumber           UTM Zone (1-60)
     * @param Easting              Easting (m)
     * @param Northing             Northing (m)
     * @param bNorthernHemisphere  是否北半球
     * @param OutLon               输出经度
     * @param OutLat               输出纬度
     * @return 是否成功
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Coordinate")
    static bool UTMToGeo(int32 ZoneNumber, double Easting, double Northing, bool bNorthernHemisphere, double& OutLon, double& OutLat);

private:
    /** 投影原点经度 */
    double OriginLongitude = 0.0;

    /** 投影原点纬度 */
    double OriginLatitude = 0.0;

    /** 原点纬度处每度经度对应的米数 */
    double MetersPerDegreeLon = 0.0;

    /** 每度纬度对应的米数（近似常量） */
    double MetersPerDegreeLat = 0.0;

    /** 地球半径（米） */
    static constexpr double EarthRadius = 6371000.0;

    /** WGS84 椭球长轴（米） */
    static constexpr double WGS84_A = 6378137.0;

    /** WGS84 第一偏心率平方 */
    static constexpr double WGS84_E2 = 0.00669437999014;

    /** UE5 使用厘米为单位 */
    static constexpr double MetersToCm = 100.0;
};
