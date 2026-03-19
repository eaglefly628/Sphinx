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

bool UGISCoordinate::GeoToUTM(double Lon, double Lat, int32& OutZoneNumber, double& OutEasting, double& OutNorthing)
{
    // UTM Zone 计算
    OutZoneNumber = static_cast<int32>(FMath::Floor((Lon + 180.0) / 6.0)) + 1;
    if (OutZoneNumber < 1 || OutZoneNumber > 60) return false;

    const double CentralMeridian = -180.0 + (OutZoneNumber - 1) * 6.0 + 3.0;
    const double LatRad = FMath::DegreesToRadians(Lat);
    const double DeltaLonRad = FMath::DegreesToRadians(Lon - CentralMeridian);

    const double SinLat = FMath::Sin(LatRad);
    const double CosLat = FMath::Cos(LatRad);
    const double TanLat = FMath::Tan(LatRad);

    const double EP2 = WGS84_E2 / (1.0 - WGS84_E2);  // 第二偏心率平方
    const double N = WGS84_A / FMath::Sqrt(1.0 - WGS84_E2 * SinLat * SinLat);
    const double T = TanLat * TanLat;
    const double CC = EP2 * CosLat * CosLat;
    const double AA = CosLat * DeltaLonRad;

    // 子午线弧长
    const double M = WGS84_A * (
        (1.0 - WGS84_E2 / 4.0 - 3.0 * WGS84_E2 * WGS84_E2 / 64.0) * LatRad
        - (3.0 * WGS84_E2 / 8.0 + 3.0 * WGS84_E2 * WGS84_E2 / 32.0) * FMath::Sin(2.0 * LatRad)
        + (15.0 * WGS84_E2 * WGS84_E2 / 256.0) * FMath::Sin(4.0 * LatRad));

    constexpr double K0 = 0.9996;  // UTM 缩放因子

    OutEasting = K0 * N * (AA
        + AA * AA * AA / 6.0 * (1.0 - T + CC)
        + AA * AA * AA * AA * AA / 120.0 * (5.0 - 18.0 * T + T * T + 72.0 * CC - 58.0 * EP2))
        + 500000.0;  // 假东向

    OutNorthing = K0 * (M + N * TanLat * (
        AA * AA / 2.0
        + AA * AA * AA * AA / 24.0 * (5.0 - T + 9.0 * CC + 4.0 * CC * CC)
        + AA * AA * AA * AA * AA * AA / 720.0 * (61.0 - 58.0 * T + T * T + 600.0 * CC - 330.0 * EP2)));

    if (Lat < 0.0)
    {
        OutNorthing += 10000000.0;  // 南半球假北向
    }

    return true;
}

bool UGISCoordinate::UTMToGeo(int32 ZoneNumber, double Easting, double Northing, bool bNorthernHemisphere, double& OutLon, double& OutLat)
{
    if (ZoneNumber < 1 || ZoneNumber > 60) return false;

    const double EP2 = WGS84_E2 / (1.0 - WGS84_E2);
    const double CentralMeridian = -180.0 + (ZoneNumber - 1) * 6.0 + 3.0;

    constexpr double K0 = 0.9996;
    double AdjE = Easting - 500000.0;
    double AdjN = Northing;
    if (!bNorthernHemisphere) AdjN -= 10000000.0;

    const double Mu = AdjN / K0 / (WGS84_A * (1.0 - WGS84_E2 / 4.0 - 3.0 * WGS84_E2 * WGS84_E2 / 64.0));
    const double E1 = (1.0 - FMath::Sqrt(1.0 - WGS84_E2)) / (1.0 + FMath::Sqrt(1.0 - WGS84_E2));

    const double Lat1 = Mu
        + (3.0 * E1 / 2.0 - 27.0 * E1 * E1 * E1 / 32.0) * FMath::Sin(2.0 * Mu)
        + (21.0 * E1 * E1 / 16.0 - 55.0 * E1 * E1 * E1 * E1 / 32.0) * FMath::Sin(4.0 * Mu)
        + (151.0 * E1 * E1 * E1 / 96.0) * FMath::Sin(6.0 * Mu);

    const double SinLat1 = FMath::Sin(Lat1);
    const double CosLat1 = FMath::Cos(Lat1);
    const double TanLat1 = FMath::Tan(Lat1);
    const double N1 = WGS84_A / FMath::Sqrt(1.0 - WGS84_E2 * SinLat1 * SinLat1);
    const double R1 = WGS84_A * (1.0 - WGS84_E2) / FMath::Pow(1.0 - WGS84_E2 * SinLat1 * SinLat1, 1.5);
    const double T1 = TanLat1 * TanLat1;
    const double C1 = EP2 * CosLat1 * CosLat1;
    const double D = AdjE / (N1 * K0);

    OutLat = FMath::RadiansToDegrees(Lat1
        - (N1 * TanLat1 / R1) * (
            D * D / 2.0
            - D * D * D * D / 24.0 * (5.0 + 3.0 * T1 + 10.0 * C1 - 4.0 * C1 * C1 - 9.0 * EP2)
            + D * D * D * D * D * D / 720.0 * (61.0 + 90.0 * T1 + 298.0 * C1 + 45.0 * T1 * T1 - 252.0 * EP2 - 3.0 * C1 * C1)));

    OutLon = CentralMeridian + FMath::RadiansToDegrees(
        (D - D * D * D / 6.0 * (1.0 + 2.0 * T1 + C1)
        + D * D * D * D * D / 120.0 * (5.0 - 2.0 * C1 + 28.0 * T1 - 3.0 * C1 * C1 + 8.0 * EP2 + 24.0 * T1 * T1))
        / CosLat1);

    return true;
}
