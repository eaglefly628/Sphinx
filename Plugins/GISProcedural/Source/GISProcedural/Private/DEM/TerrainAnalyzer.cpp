// TerrainAnalyzer.cpp - 地形分析器实现
#include "DEM/TerrainAnalyzer.h"
#include "DEM/DEMParser.h"
#include "Data/GISCoordinate.h"

TArray<FTerrainZone> UTerrainAnalyzer::AnalyzeTerrain(
    UDEMParser* DEMParser,
    double MinLon, double MinLat,
    double MaxLon, double MaxLat,
    double OriginLon, double OriginLat)
{
    TArray<FTerrainZone> Result;

    if (!BuildElevationGrid(DEMParser, MinLon, MinLat, MaxLon, MaxLat))
    {
        UE_LOG(LogTemp, Error, TEXT("TerrainAnalyzer: Failed to build elevation grid"));
        return Result;
    }

    if (!ComputeSlopeAspect())
    {
        UE_LOG(LogTemp, Error, TEXT("TerrainAnalyzer: Failed to compute slope/aspect"));
        return Result;
    }

    if (!ClassifyTerrainGrid())
    {
        UE_LOG(LogTemp, Error, TEXT("TerrainAnalyzer: Failed to classify terrain"));
        return Result;
    }

    if (!LabelConnectedZones())
    {
        UE_LOG(LogTemp, Error, TEXT("TerrainAnalyzer: Failed to label zones"));
        return Result;
    }

    Result = ExtractZoneBoundaries(OriginLon, OriginLat);

    UE_LOG(LogTemp, Log, TEXT("TerrainAnalyzer: Generated %d terrain zones"), Result.Num());
    return Result;
}

// ============ Step 1: 高程网格 ============

bool UTerrainAnalyzer::BuildElevationGrid(
    UDEMParser* DEMParser,
    double MinLon, double MinLat,
    double MaxLon, double MaxLat)
{
    if (!DEMParser || DEMParser->GetTileCount() == 0)
    {
        return false;
    }

    AnalysisMinLon = MinLon;
    AnalysisMinLat = MinLat;
    AnalysisMaxLon = MaxLon;
    AnalysisMaxLat = MaxLat;

    return DEMParser->GetElevationGrid(
        MinLon, MinLat, MaxLon, MaxLat,
        Config.AnalysisResolution,
        ElevationGrid,
        GridWidth, GridHeight
    );
}

// ============ Step 2: 坡度/坡向 ============

bool UTerrainAnalyzer::ComputeSlopeAspect()
{
    if (ElevationGrid.Num() == 0 || GridWidth < 3 || GridHeight < 3)
    {
        return false;
    }

    const int32 N = GridWidth * GridHeight;
    SlopeGrid.SetNumUninitialized(N);
    AspectGrid.SetNumUninitialized(N);

    // 计算该纬度的单元格实际大小（米）
    const double CenterLat = (AnalysisMinLat + AnalysisMaxLat) / 2.0;
    const float CellSizeMeters = Config.AnalysisResolution;

    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            SlopeGrid[Y * GridWidth + X] = ComputeSlopeAtPixel(X, Y, CellSizeMeters);
            AspectGrid[Y * GridWidth + X] = ComputeAspectAtPixel(X, Y);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("TerrainAnalyzer: Computed slope/aspect for %dx%d grid"), GridWidth, GridHeight);
    return true;
}

float UTerrainAnalyzer::ComputeSlopeAtPixel(int32 X, int32 Y, float CellSizeMeters) const
{
    // Horn 算法 (3x3 窗口)
    // dz/dx = ((c + 2f + i) - (a + 2d + g)) / (8 * cellsize)
    // dz/dy = ((g + 2h + i) - (a + 2b + c)) / (8 * cellsize)
    // slope = atan(sqrt(dzdx^2 + dzdy^2))

    const float A = GetElevation(X - 1, Y - 1);
    const float B = GetElevation(X,     Y - 1);
    const float C = GetElevation(X + 1, Y - 1);
    const float D = GetElevation(X - 1, Y);
    // E = center (unused)
    const float F = GetElevation(X + 1, Y);
    const float G = GetElevation(X - 1, Y + 1);
    const float H = GetElevation(X,     Y + 1);
    const float I = GetElevation(X + 1, Y + 1);

    const float DZDX = ((C + 2.0f * F + I) - (A + 2.0f * D + G)) / (8.0f * CellSizeMeters);
    const float DZDY = ((G + 2.0f * H + I) - (A + 2.0f * B + C)) / (8.0f * CellSizeMeters);

    const float SlopeRad = FMath::Atan(FMath::Sqrt(DZDX * DZDX + DZDY * DZDY));
    return FMath::RadiansToDegrees(SlopeRad);
}

float UTerrainAnalyzer::ComputeAspectAtPixel(int32 X, int32 Y) const
{
    const float A = GetElevation(X - 1, Y - 1);
    const float C = GetElevation(X + 1, Y - 1);
    const float D = GetElevation(X - 1, Y);
    const float F = GetElevation(X + 1, Y);
    const float G = GetElevation(X - 1, Y + 1);
    const float I = GetElevation(X + 1, Y + 1);

    const float DZDX = ((C + 2.0f * F + I) - (A + 2.0f * D + G));
    const float DZDY = ((G + 2.0f * GetElevation(X, Y + 1) + I) -
                         (A + 2.0f * GetElevation(X, Y - 1) + C));

    float AspectDeg = FMath::RadiansToDegrees(FMath::Atan2(DZDY, -DZDX));
    if (AspectDeg < 0.0f)
    {
        AspectDeg += 360.0f;
    }
    return AspectDeg;
}

float UTerrainAnalyzer::GetElevation(int32 X, int32 Y) const
{
    X = FMath::Clamp(X, 0, GridWidth - 1);
    Y = FMath::Clamp(Y, 0, GridHeight - 1);
    return ElevationGrid[Y * GridWidth + X];
}

// ============ Step 3: 栅格分类 ============

bool UTerrainAnalyzer::ClassifyTerrainGrid()
{
    if (ElevationGrid.Num() == 0 || SlopeGrid.Num() == 0)
    {
        return false;
    }

    const int32 N = GridWidth * GridHeight;
    ClassGrid.SetNumUninitialized(N);

    for (int32 i = 0; i < N; ++i)
    {
        ClassGrid[i] = ClassifyPixel(ElevationGrid[i], SlopeGrid[i]);
    }

    UE_LOG(LogTemp, Log, TEXT("TerrainAnalyzer: Classified %d pixels"), N);
    return true;
}

int32 UTerrainAnalyzer::ClassifyPixel(float Elevation, float Slope) const
{
    // 水体候选：非常低的高程 + 几乎无坡度
    if (Elevation < 0.5f && Slope < 1.0f)
    {
        return 5; // Water Candidate
    }

    const bool bLow = (Elevation <= Config.LowElevationMax);
    const bool bHigh = (Elevation >= Config.HighElevationMin);
    const bool bFlat = (Slope <= Config.FlatMaxSlope);
    const bool bSteep = (Slope >= Config.SteepMinSlope);

    if (bHigh && bSteep)  return 4; // High Steep → 山地/林地
    if (bHigh && !bSteep) return 2; // Mid/High Flat → 高原/台地
    if (bLow && bFlat)    return 0; // Low Flat → 平原/农田/沿海
    if (bLow && !bFlat)   return 1; // Low Gentle → 缓坡/可居住

    // 中间情况
    if (bFlat)  return 2; // Mid Flat
    if (bSteep) return 4; // Steep anywhere → 林地候选
    return 3;             // Mid Gentle → 丘陵
}

// ============ Step 4: 连通区域标记 ============

bool UTerrainAnalyzer::LabelConnectedZones()
{
    if (ClassGrid.Num() == 0)
    {
        return false;
    }

    const int32 NumZones = RunCCL();

    // 计算每像素对应面积
    const double CenterLat = (AnalysisMinLat + AnalysisMaxLat) / 2.0;
    const float CellAreaSqM = Config.AnalysisResolution * Config.AnalysisResolution;

    MergeSmallZones(NumZones, CellAreaSqM);

    UE_LOG(LogTemp, Log, TEXT("TerrainAnalyzer: Labeled %d connected zones"), NumZones);
    return true;
}

int32 UTerrainAnalyzer::RunCCL()
{
    // Two-Pass Connected Component Labeling (4-连通)
    const int32 N = GridWidth * GridHeight;
    LabelGrid.SetNumZeroed(N);

    TArray<int32> Parent; // Union-Find 父数组
    int32 NextLabel = 1;

    Parent.Add(0); // 占位，label 从 1 开始

    // Pass 1: 初始标记
    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            const int32 Idx = Y * GridWidth + X;
            const int32 CurClass = ClassGrid[Idx];

            int32 LeftLabel = 0, UpLabel = 0;

            if (X > 0 && ClassGrid[Idx - 1] == CurClass)
            {
                LeftLabel = LabelGrid[Idx - 1];
            }
            if (Y > 0 && ClassGrid[Idx - GridWidth] == CurClass)
            {
                UpLabel = LabelGrid[Idx - GridWidth];
            }

            if (LeftLabel == 0 && UpLabel == 0)
            {
                // 新标签
                LabelGrid[Idx] = NextLabel;
                Parent.Add(NextLabel);
                NextLabel++;
            }
            else if (LeftLabel != 0 && UpLabel == 0)
            {
                LabelGrid[Idx] = LeftLabel;
            }
            else if (LeftLabel == 0 && UpLabel != 0)
            {
                LabelGrid[Idx] = UpLabel;
            }
            else
            {
                // 两个邻居都有标签 → Union
                LabelGrid[Idx] = FMath::Min(LeftLabel, UpLabel);

                // Find roots and union
                int32 RootL = LeftLabel;
                while (Parent[RootL] != RootL) RootL = Parent[RootL];
                int32 RootU = UpLabel;
                while (Parent[RootU] != RootU) RootU = Parent[RootU];

                if (RootL != RootU)
                {
                    Parent[FMath::Max(RootL, RootU)] = FMath::Min(RootL, RootU);
                }
            }
        }
    }

    // Pass 2: 扁平化标签
    // 先将所有标签指向根
    for (int32 i = 1; i < Parent.Num(); ++i)
    {
        int32 Root = i;
        while (Parent[Root] != Root) Root = Parent[Root];
        Parent[i] = Root;
    }

    // 重编号为连续 ID
    TMap<int32, int32> Remap;
    int32 FinalCount = 0;
    for (int32 i = 0; i < N; ++i)
    {
        int32 Root = Parent[LabelGrid[i]];
        if (!Remap.Contains(Root))
        {
            Remap.Add(Root, FinalCount++);
        }
        LabelGrid[i] = Remap[Root];
    }

    return FinalCount;
}

void UTerrainAnalyzer::MergeSmallZones(int32 NumZones, float CellAreaSqM)
{
    if (NumZones <= 1)
    {
        return;
    }

    // 统计每个 zone 的面积
    TArray<int32> ZoneCounts;
    ZoneCounts.SetNumZeroed(NumZones);
    for (int32 Label : LabelGrid)
    {
        if (Label >= 0 && Label < NumZones)
        {
            ZoneCounts[Label]++;
        }
    }

    const float MinPixels = Config.MinZoneAreaSqM / CellAreaSqM;

    // 对每个小区域，找到最大邻居并合并
    bool bChanged = true;
    int32 Iterations = 0;
    const int32 MaxIterations = 50;

    while (bChanged && Iterations < MaxIterations)
    {
        bChanged = false;
        Iterations++;

        for (int32 ZoneID = 0; ZoneID < NumZones; ++ZoneID)
        {
            if (ZoneCounts[ZoneID] >= MinPixels || ZoneCounts[ZoneID] == 0)
            {
                continue;
            }

            // 找这个 zone 边界上的最大邻居
            TMap<int32, int32> NeighborCounts;
            for (int32 Y = 0; Y < GridHeight; ++Y)
            {
                for (int32 X = 0; X < GridWidth; ++X)
                {
                    if (LabelGrid[Y * GridWidth + X] != ZoneID)
                    {
                        continue;
                    }

                    // 检查 4 邻居
                    const int32 Neighbors[4][2] = {{X-1,Y},{X+1,Y},{X,Y-1},{X,Y+1}};
                    for (const auto& N : Neighbors)
                    {
                        if (N[0] >= 0 && N[0] < GridWidth && N[1] >= 0 && N[1] < GridHeight)
                        {
                            const int32 NLabel = LabelGrid[N[1] * GridWidth + N[0]];
                            if (NLabel != ZoneID)
                            {
                                NeighborCounts.FindOrAdd(NLabel)++;
                            }
                        }
                    }
                }
            }

            if (NeighborCounts.Num() == 0)
            {
                continue;
            }

            // 找最大邻居
            int32 BestNeighbor = INDEX_NONE;
            int32 BestCount = 0;
            for (const auto& Pair : NeighborCounts)
            {
                if (Pair.Value > BestCount)
                {
                    BestCount = Pair.Value;
                    BestNeighbor = Pair.Key;
                }
            }

            if (BestNeighbor == INDEX_NONE)
            {
                continue;
            }

            // 合并：将 ZoneID 重标签为 BestNeighbor
            for (int32& Label : LabelGrid)
            {
                if (Label == ZoneID)
                {
                    Label = BestNeighbor;
                }
            }
            ZoneCounts[BestNeighbor] += ZoneCounts[ZoneID];
            ZoneCounts[ZoneID] = 0;
            bChanged = true;
        }
    }
}

// ============ Step 5: 提取边界 ============

TArray<FTerrainZone> UTerrainAnalyzer::ExtractZoneBoundaries(double OriginLon, double OriginLat)
{
    TArray<FTerrainZone> Result;

    if (LabelGrid.Num() == 0)
    {
        return Result;
    }

    // 找出所有唯一 zone ID 及其统计
    TMap<int32, FTerrainZone> ZoneMap;

    const double StepLon = (AnalysisMaxLon - AnalysisMinLon) / GridWidth;
    const double StepLat = (AnalysisMaxLat - AnalysisMinLat) / GridHeight;
    const float CellAreaSqM = Config.AnalysisResolution * Config.AnalysisResolution;

    // 坐标转换器
    UGISCoordinate* Coord = NewObject<UGISCoordinate>();
    Coord->SetOrigin(OriginLon, OriginLat);

    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            const int32 Idx = Y * GridWidth + X;
            const int32 ZoneID = LabelGrid[Idx];

            FTerrainZone& Zone = ZoneMap.FindOrAdd(ZoneID);
            if (Zone.ZoneID == 0 && ZoneID != 0)
            {
                Zone.ZoneID = ZoneID;
            }
            else if (Zone.ZoneID == 0 && ZoneID == 0)
            {
                Zone.ZoneID = 0;
            }

            Zone.AvgElevation += ElevationGrid[Idx];
            Zone.AvgSlope += SlopeGrid[Idx];
            Zone.AreaSqM += CellAreaSqM;
            Zone.TerrainClass = ClassGrid[Idx]; // 最后一个像素的分类（后面取众数）
        }
    }

    // 计算平均值并提取边界
    for (auto& Pair : ZoneMap)
    {
        FTerrainZone& Zone = Pair.Value;
        const int32 PixelCount = FMath::RoundToInt(Zone.AreaSqM / CellAreaSqM);
        if (PixelCount > 0)
        {
            Zone.AvgElevation /= PixelCount;
            Zone.AvgSlope /= PixelCount;
        }

        // 取地形类别众数
        TMap<int32, int32> ClassCounts;
        for (int32 Y = 0; Y < GridHeight; ++Y)
        {
            for (int32 X = 0; X < GridWidth; ++X)
            {
                if (LabelGrid[Y * GridWidth + X] == Pair.Key)
                {
                    ClassCounts.FindOrAdd(ClassGrid[Y * GridWidth + X])++;
                }
            }
        }
        int32 MaxCount = 0;
        for (const auto& CC : ClassCounts)
        {
            if (CC.Value > MaxCount)
            {
                MaxCount = CC.Value;
                Zone.TerrainClass = CC.Key;
            }
        }
    }

    // 提取边界轮廓（简化版：提取每个 zone 的凸包边界像素）
    TArray<TArray<FVector2D>> BoundaryPixels = TraceBoundaries(ZoneMap.Num());

    // 转换到最终结果
    for (auto& Pair : ZoneMap)
    {
        FTerrainZone& Zone = Pair.Value;

        // 跳过过小的区域
        if (Zone.AreaSqM < Config.MinZoneAreaSqM)
        {
            continue;
        }

        // 提取边界像素 → 经纬度 → 世界坐标
        for (int32 Y = 0; Y < GridHeight; ++Y)
        {
            for (int32 X = 0; X < GridWidth; ++X)
            {
                if (LabelGrid[Y * GridWidth + X] != Pair.Key)
                {
                    continue;
                }

                // 检查是否是边界像素（至少一个邻居不同）
                bool bIsBorder = false;
                const int32 Dirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
                for (const auto& D : Dirs)
                {
                    const int32 NX = X + D[0], NY = Y + D[1];
                    if (NX < 0 || NX >= GridWidth || NY < 0 || NY >= GridHeight ||
                        LabelGrid[NY * GridWidth + NX] != Pair.Key)
                    {
                        bIsBorder = true;
                        break;
                    }
                }

                if (bIsBorder)
                {
                    const double Lon = AnalysisMinLon + (X + 0.5) * StepLon;
                    const double Lat = AnalysisMinLat + (Y + 0.5) * StepLat;
                    Zone.BoundaryGeoCoords.Add(FVector2D(Lon, Lat));
                    Zone.BoundaryVertices.Add(Coord->GeoToWorld(Lon, Lat));
                }
            }
        }

        // TODO: 对边界点做排序（凸包或轮廓追踪排序），现在是扫描顺序
        // 后续由 PolygonDeriver 做矢量切割时会重新整理

        Result.Add(Zone);
    }

    UE_LOG(LogTemp, Log, TEXT("TerrainAnalyzer: Extracted %d zone boundaries"), Result.Num());
    return Result;
}

TArray<TArray<FVector2D>> UTerrainAnalyzer::TraceBoundaries(int32 NumZones) const
{
    // 预留：完整的 Marching Squares 轮廓追踪
    // 当前版本由 ExtractZoneBoundaries 中内联处理边界像素提取
    TArray<TArray<FVector2D>> Result;
    Result.SetNum(NumZones);
    return Result;
}

void UTerrainAnalyzer::GetSlopeGrid(TArray<float>& OutSlope, int32& OutWidth, int32& OutHeight) const
{
    OutSlope = SlopeGrid;
    OutWidth = GridWidth;
    OutHeight = GridHeight;
}

void UTerrainAnalyzer::GetClassGrid(TArray<int32>& OutClass, int32& OutWidth, int32& OutHeight) const
{
    OutClass = ClassGrid;
    OutWidth = GridWidth;
    OutHeight = GridHeight;
}
