// LandUseClassifier.cpp - 土地分类器实现
#include "Polygon/LandUseClassifier.h"

void ULandUseClassifier::ClassifySingle(FLandUsePolygon& Polygon, const ULandUseClassifyRules* Rules)
{
    if (!Rules)
    {
        Polygon.LandUseType = ELandUseType::Unknown;
        return;
    }

    Polygon.LandUseType = DetermineType(Polygon, Rules);
}

void ULandUseClassifier::ClassifyAll(TArray<FLandUsePolygon>& Polygons, const ULandUseClassifyRules* Rules)
{
    if (!Rules)
    {
        return;
    }

    for (FLandUsePolygon& Polygon : Polygons)
    {
        ClassifySingle(Polygon, Rules);
    }
}

void ULandUseClassifier::AssignPCGParameters(TArray<FLandUsePolygon>& Polygons, const ULandUseClassifyRules* Rules)
{
    if (!Rules)
    {
        return;
    }

    for (FLandUsePolygon& Polygon : Polygons)
    {
        const ELandUseType Type = Polygon.LandUseType;

        // 建筑密度
        if (const float* Density = Rules->BuildingDensityMap.Find(Type))
        {
            Polygon.BuildingDensity = *Density;
        }

        // 植被密度
        if (const float* VegDensity = Rules->VegetationDensityMap.Find(Type))
        {
            Polygon.VegetationDensity = *VegDensity;
        }

        // 楼层范围
        if (const FIntPoint* FloorRange = Rules->FloorRangeMap.Find(Type))
        {
            Polygon.MinFloors = FloorRange->X;
            Polygon.MaxFloors = FloorRange->Y;
        }

        // 退让距离：商业区临主干道时退让更多
        if (Polygon.bAdjacentToMainRoad && Type == ELandUseType::Commercial)
        {
            Polygon.BuildingSetback = 10.0f;
        }
        else if (Polygon.bAdjacentToMainRoad)
        {
            Polygon.BuildingSetback = 8.0f;
        }
        else
        {
            Polygon.BuildingSetback = 5.0f;
        }
    }
}

ELandUseType ULandUseClassifier::DetermineType(const FLandUsePolygon& Polygon, const ULandUseClassifyRules* Rules)
{
    // 分类规则按优先级排列

    // 1. 水体检测（需要额外的水体矢量数据，此处预留）
    // TODO: 如果有水体矢量数据，检测 Polygon 是否与水体重叠

    // 2. 高坡度 → 林地
    if (Polygon.AvgSlope > Rules->HillMinSlope)
    {
        return ELandUseType::Forest;
    }

    // 3. 高海拔 → 林地
    if (Polygon.AvgElevation > Rules->HighElevationMin)
    {
        return ELandUseType::Forest;
    }

    // 4-5. 小地块
    if (Polygon.AreaSqM < Rules->SmallBlockMaxArea)
    {
        if (Polygon.bAdjacentToMainRoad)
        {
            return ELandUseType::Commercial;
        }
        else
        {
            return ELandUseType::Residential;
        }
    }

    // 6-7. 中等地块
    if (Polygon.AreaSqM < Rules->LargeBlockMinArea)
    {
        if (Polygon.bAdjacentToMainRoad)
        {
            return ELandUseType::Commercial;
        }
        else
        {
            return ELandUseType::Residential;
        }
    }

    // 8. 大地块 + 平地
    if (Polygon.AreaSqM >= Rules->LargeBlockMinArea && Polygon.AvgSlope <= Rules->FlatMaxSlope)
    {
        // 低海拔倾向农田，否则工业
        if (Polygon.AvgElevation < Rules->LowElevationMax)
        {
            return ELandUseType::Farmland;
        }
        else
        {
            return ELandUseType::Industrial;
        }
    }

    // 9. 默认
    return ELandUseType::OpenSpace;
}
