// LandUseClassifier.cpp - 土地分类器实现
#include "Polygon/LandUseClassifier.h"
#include "GISProceduralModule.h"

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

/** ESA WorldCover 类码 → ELandUseType 映射 */
static ELandUseType WorldCoverCodeToLandUse(uint8 Code)
{
    // ESA WorldCover v200 类码:
    // 10=Tree cover, 20=Shrubland, 30=Grassland, 40=Cropland
    // 50=Built-up, 60=Bare/sparse, 70=Snow/ice, 80=Water, 90=Wetland, 95=Mangroves, 100=Moss/lichen
    switch (Code)
    {
        case 10: return ELandUseType::Forest;
        case 20: return ELandUseType::Forest;
        case 30: return ELandUseType::OpenSpace;
        case 40: return ELandUseType::Farmland;
        case 50: return ELandUseType::Residential;  // Built-up → 默认住宅，后续由面积/道路细分
        case 60: return ELandUseType::OpenSpace;
        case 70: return ELandUseType::OpenSpace;
        case 80: return ELandUseType::Water;
        case 90: return ELandUseType::Water;
        case 95: return ELandUseType::Forest;
        case 100: return ELandUseType::OpenSpace;
        default: return ELandUseType::Unknown;
    }
}

void ULandUseClassifier::FuseLandCoverData(
    TArray<FLandUsePolygon>& Polygons,
    const TArray<uint8>& ClassGrid,
    int32 GridWidth, int32 GridHeight,
    const FBox2D& GridBounds)
{
    if (ClassGrid.Num() == 0 || GridWidth <= 0 || GridHeight <= 0)
    {
        return;
    }

    const FVector2D GridSize = GridBounds.Max - GridBounds.Min;
    if (GridSize.X <= 0.0 || GridSize.Y <= 0.0)
    {
        return;
    }

    const double CellWidth = GridSize.X / GridWidth;
    const double CellHeight = GridSize.Y / GridHeight;

    int32 FusedCount = 0;

    for (FLandUsePolygon& Poly : Polygons)
    {
        if (Poly.GeoVertices.Num() == 0)
        {
            continue;
        }

        // 计算 Polygon 的地理 AABB
        FVector2D GeoMin(MAX_dbl, MAX_dbl);
        FVector2D GeoMax(-MAX_dbl, -MAX_dbl);
        for (const FVector2D& V : Poly.GeoVertices)
        {
            GeoMin.X = FMath::Min(GeoMin.X, V.X);
            GeoMin.Y = FMath::Min(GeoMin.Y, V.Y);
            GeoMax.X = FMath::Max(GeoMax.X, V.X);
            GeoMax.Y = FMath::Max(GeoMax.Y, V.Y);
        }

        // 栅格中对应的行列范围
        const int32 ColMin = FMath::Clamp(FMath::FloorToInt32((GeoMin.X - GridBounds.Min.X) / CellWidth), 0, GridWidth - 1);
        const int32 ColMax = FMath::Clamp(FMath::FloorToInt32((GeoMax.X - GridBounds.Min.X) / CellWidth), 0, GridWidth - 1);
        const int32 RowMin = FMath::Clamp(FMath::FloorToInt32((GeoMin.Y - GridBounds.Min.Y) / CellHeight), 0, GridHeight - 1);
        const int32 RowMax = FMath::Clamp(FMath::FloorToInt32((GeoMax.Y - GridBounds.Min.Y) / CellHeight), 0, GridHeight - 1);

        // 多数投票：统计覆盖区域内各 LandUseType 的像素数
        TMap<ELandUseType, int32> VoteMap;
        int32 TotalVotes = 0;

        for (int32 Row = RowMin; Row <= RowMax; ++Row)
        {
            for (int32 Col = ColMin; Col <= ColMax; ++Col)
            {
                const int32 Idx = Row * GridWidth + Col;
                if (Idx < 0 || Idx >= ClassGrid.Num()) continue;

                const ELandUseType RasterType = WorldCoverCodeToLandUse(ClassGrid[Idx]);
                if (RasterType != ELandUseType::Unknown)
                {
                    VoteMap.FindOrAdd(RasterType) += 1;
                    TotalVotes++;
                }
            }
        }

        if (TotalVotes == 0) continue;

        // 找到票数最多的类型
        ELandUseType WinnerType = ELandUseType::Unknown;
        int32 MaxVotes = 0;
        for (const auto& Pair : VoteMap)
        {
            if (Pair.Value > MaxVotes)
            {
                MaxVotes = Pair.Value;
                WinnerType = Pair.Key;
            }
        }

        // 水体栅格优先级最高（遥感水体检测可靠度高）
        if (const int32* WaterVotes = VoteMap.Find(ELandUseType::Water))
        {
            if (*WaterVotes > TotalVotes / 3)  // 超过 1/3 像素为水体
            {
                WinnerType = ELandUseType::Water;
            }
        }

        // 仅当栅格结果与矢量推导不同时修正
        if (WinnerType != ELandUseType::Unknown && WinnerType != Poly.LandUseType)
        {
            Poly.LandUseType = WinnerType;
            FusedCount++;
        }
    }

    UE_LOG(LogGIS, Log, TEXT("LandUseClassifier: Fused LandCover data, corrected %d/%d polygons"), FusedCount, Polygons.Num());
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
