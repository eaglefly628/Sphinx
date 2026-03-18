// PCGLandUseData.cpp - PCG 自定义数据类型实现
#include "PCG/PCGLandUseData.h"

TArray<FLandUsePolygon> UPCGLandUseData::GetPolygonsByType(ELandUseType Type) const
{
    TArray<FLandUsePolygon> Result;
    for (const FLandUsePolygon& Polygon : Polygons)
    {
        if (Polygon.LandUseType == Type)
        {
            Result.Add(Polygon);
        }
    }
    return Result;
}

float UPCGLandUseData::GetTotalArea() const
{
    float Total = 0.0f;
    for (const FLandUsePolygon& Polygon : Polygons)
    {
        Total += Polygon.AreaSqM;
    }
    return Total;
}

int32 UPCGLandUseData::GetCountByType(ELandUseType Type) const
{
    int32 Count = 0;
    for (const FLandUsePolygon& Polygon : Polygons)
    {
        if (Polygon.LandUseType == Type)
        {
            Count++;
        }
    }
    return Count;
}
