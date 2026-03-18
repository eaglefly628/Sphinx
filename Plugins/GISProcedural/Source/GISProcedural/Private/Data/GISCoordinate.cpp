// GISCoordinate.cpp - 坐标转换实现
#include "Data/GISCoordinate.h"

void UGISCoordinate::SetOrigin(double OriginLon, double OriginLat)
{
    OriginLongitude = OriginLon;
    OriginLatitude = OriginLat;

    // 计算该纬度处每度经度/纬度对应的米数
    const double LatRad = FMath::DegreesToRadians(OriginLat);
    MetersPerDegreeLon = (PI / 180.0) * EarthRadius * FMath::Cos(LatRad);
    MetersPerDegreeLat = (PI / 180.0) * EarthRadius;
}

FVector UGISCoordinate::GeoToWorld(double Lon, double Lat) const
{
    const double DeltaLon = Lon - OriginLongitude;
    const double DeltaLat = Lat - OriginLatitude;

    // X = 东向, Y = 北向, Z = 0（高程由 DEM 另外填充）
    const double X = DeltaLon * MetersPerDegreeLon * MetersToCm;
    const double Y = DeltaLat * MetersPerDegreeLat * MetersToCm;

    return FVector(X, Y, 0.0);
}

FVector2D UGISCoordinate::WorldToGeo(const FVector& WorldPos) const
{
    if (MetersPerDegreeLon == 0.0 || MetersPerDegreeLat == 0.0)
    {
        return FVector2D(OriginLongitude, OriginLatitude);
    }

    const double Lon = OriginLongitude + (WorldPos.X / MetersToCm) / MetersPerDegreeLon;
    const double Lat = OriginLatitude + (WorldPos.Y / MetersToCm) / MetersPerDegreeLat;

    return FVector2D(Lon, Lat);
}

TArray<FVector> UGISCoordinate::GeoArrayToWorld(const TArray<FVector2D>& GeoCoords) const
{
    TArray<FVector> Result;
    Result.Reserve(GeoCoords.Num());

    for (const FVector2D& Coord : GeoCoords)
    {
        Result.Add(GeoToWorld(Coord.X, Coord.Y));
    }

    return Result;
}

double UGISCoordinate::HaversineDistance(double Lon1, double Lat1, double Lon2, double Lat2)
{
    const double DLat = FMath::DegreesToRadians(Lat2 - Lat1);
    const double DLon = FMath::DegreesToRadians(Lon2 - Lon1);

    const double Lat1Rad = FMath::DegreesToRadians(Lat1);
    const double Lat2Rad = FMath::DegreesToRadians(Lat2);

    const double A = FMath::Sin(DLat / 2.0) * FMath::Sin(DLat / 2.0)
        + FMath::Cos(Lat1Rad) * FMath::Cos(Lat2Rad)
        * FMath::Sin(DLon / 2.0) * FMath::Sin(DLon / 2.0);

    const double C = 2.0 * FMath::Atan2(FMath::Sqrt(A), FMath::Sqrt(1.0 - A));

    return EarthRadius * C;
}
