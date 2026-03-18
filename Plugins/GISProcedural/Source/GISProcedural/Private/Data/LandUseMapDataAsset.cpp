// LandUseMapDataAsset.cpp
#include "Data/LandUseMapDataAsset.h"

TArray<FLandUsePolygon> ULandUseMapDataAsset::GetPolygonsByType(ELandUseType Type) const
{
    TArray<FLandUsePolygon> Result;
    for (const FLandUsePolygon& Poly : Polygons)
    {
        if (Poly.LandUseType == Type)
        {
            Result.Add(Poly);
        }
    }
    return Result;
}

TArray<FLandUsePolygon> ULandUseMapDataAsset::GetPolygonsInWorldBounds(const FBox& WorldBounds) const
{
    TArray<FLandUsePolygon> Result;
    for (const FLandUsePolygon& Poly : Polygons)
    {
        if (WorldBounds.IsInsideOrOn(Poly.WorldCenter))
        {
            Result.Add(Poly);
        }
    }
    return Result;
}

float ULandUseMapDataAsset::GetTotalArea() const
{
    float Total = 0.0f;
    for (const FLandUsePolygon& Poly : Polygons)
    {
        Total += Poly.AreaSqM;
    }
    return Total;
}

int32 ULandUseMapDataAsset::GetCountByType(ELandUseType Type) const
{
    int32 Count = 0;
    for (const FLandUsePolygon& Poly : Polygons)
    {
        if (Poly.LandUseType == Type)
        {
            ++Count;
        }
    }
    return Count;
}
