// PolygonDeriver.cpp - DEM 主导 + 矢量切割的 Polygon 推导实现
#include "Polygon/PolygonDeriver.h"
#include "Polygon/LandUseClassifier.h"
#include "Data/GeoJsonParser.h"
#include "Data/GISCoordinate.h"
#include "DEM/DEMParser.h"
#include "DEM/TerrainAnalyzer.h"
#include "Async/Async.h"

TArray<FLandUsePolygon> UPolygonDeriver::GeneratePolygons(
    const FString& DEMPath,
    const FString& GeoJsonPath,
    double OriginLon,
    double OriginLat)
{
    TArray<FLandUsePolygon> Result;

    // Step 1: 加载 DEM
    if (!LoadDEM(DEMPath))
    {
        UE_LOG(LogTemp, Error, TEXT("PolygonDeriver: Failed to load DEM from %s"), *DEMPath);
        return Result;
    }

    // Step 2: 地形分析
    double MinLon, MinLat, MaxLon, MaxLat;
    if (!InferAnalysisBounds(MinLon, MinLat, MaxLon, MaxLat))
    {
        UE_LOG(LogTemp, Error, TEXT("PolygonDeriver: Failed to infer analysis bounds from DEM"));
        return Result;
    }

    if (!AnalyzeTerrain(MinLon, MinLat, MaxLon, MaxLat, OriginLon, OriginLat))
    {
        UE_LOG(LogTemp, Error, TEXT("PolygonDeriver: Terrain analysis failed"));
        return Result;
    }

    // Step 3: 加载矢量数据
    if (!LoadVectorData(GeoJsonPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("PolygonDeriver: No vector data loaded, using terrain zones only"));
    }

    // Step 4: 矢量切割
    Result = CutZonesWithVectors(OriginLon, OriginLat);

    // Step 5: 综合分类
    ClassifyPolygons(Result);

    // Step 6: PCG 参数
    AssignPCGParams(Result);

    UE_LOG(LogTemp, Log, TEXT("PolygonDeriver: Generated %d polygons"), Result.Num());
    return Result;
}

void UPolygonDeriver::GeneratePolygonsAsync(
    const FString& DEMPath,
    const FString& GeoJsonPath,
    double OriginLon,
    double OriginLat,
    const FOnPolygonsGenerated& OnComplete)
{
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
        [this, DEMPath, GeoJsonPath, OriginLon, OriginLat, OnComplete]()
    {
        TArray<FLandUsePolygon> Result = GeneratePolygons(DEMPath, GeoJsonPath, OriginLon, OriginLat);

        AsyncTask(ENamedThreads::GameThread, [OnComplete, Result = MoveTemp(Result)]()
        {
            OnComplete.ExecuteIfBound(Result);
        });
    });
}

// ============ Step 1: 加载 DEM ============

bool UPolygonDeriver::LoadDEM(const FString& DEMPath)
{
    if (!DEMParserInstance)
    {
        DEMParserInstance = NewObject<UDEMParser>(this);
    }

    DEMParserInstance->Format = DEMFormat;

    // 判断是文件还是目录
    if (FPaths::DirectoryExists(DEMPath))
    {
        const int32 Count = DEMParserInstance->LoadTilesFromDirectory(DEMPath);
        return Count > 0;
    }
    else
    {
        return DEMParserInstance->LoadTile(DEMPath);
    }
}

// ============ Step 2: 地形分析 ============

bool UPolygonDeriver::AnalyzeTerrain(
    double MinLon, double MinLat,
    double MaxLon, double MaxLat,
    double OriginLon, double OriginLat)
{
    if (!DEMParserInstance || DEMParserInstance->GetTileCount() == 0)
    {
        return false;
    }

    if (!TerrainAnalyzerInstance)
    {
        TerrainAnalyzerInstance = NewObject<UTerrainAnalyzer>(this);
    }

    TerrainAnalyzerInstance->Config.AnalysisResolution = TerrainAnalysisResolution;

    // 如果有分类规则，同步阈值
    if (ClassifyRules)
    {
        TerrainAnalyzerInstance->Config.LowElevationMax = ClassifyRules->LowElevationMax;
        TerrainAnalyzerInstance->Config.HighElevationMin = ClassifyRules->HighElevationMin;
        TerrainAnalyzerInstance->Config.FlatMaxSlope = ClassifyRules->FlatMaxSlope;
        TerrainAnalyzerInstance->Config.SteepMinSlope = ClassifyRules->HillMinSlope;
    }

    TerrainZones = TerrainAnalyzerInstance->AnalyzeTerrain(
        DEMParserInstance,
        MinLon, MinLat, MaxLon, MaxLat,
        OriginLon, OriginLat
    );

    UE_LOG(LogTemp, Log, TEXT("PolygonDeriver: Terrain analysis produced %d zones"), TerrainZones.Num());
    return TerrainZones.Num() > 0;
}

// ============ Step 3: 加载矢量数据 ============

bool UPolygonDeriver::LoadVectorData(const FString& GeoJsonPath)
{
    UGeoJsonParser* Parser = NewObject<UGeoJsonParser>();

    AllFeatures.Empty();
    if (!Parser->ParseFile(GeoJsonPath, AllFeatures))
    {
        return false;
    }

    // 按 OSM 分类拆分
    RoadFeatures = UGeoJsonParser::FilterByCategory(AllFeatures, EGISFeatureCategory::Road);
    RiverFeatures = UGeoJsonParser::FilterByCategory(AllFeatures, EGISFeatureCategory::River);
    CoastlineFeatures = UGeoJsonParser::FilterByCategory(AllFeatures, EGISFeatureCategory::Coastline);
    WaterBodyFeatures = UGeoJsonParser::FilterByCategory(AllFeatures, EGISFeatureCategory::WaterBody);

    UE_LOG(LogTemp, Log, TEXT("PolygonDeriver: Loaded vectors - Roads: %d, Rivers: %d, Coastlines: %d, WaterBodies: %d"),
        RoadFeatures.Num(), RiverFeatures.Num(), CoastlineFeatures.Num(), WaterBodyFeatures.Num());

    return true;
}

// ============ Step 4: 矢量切割 ============

TArray<FLandUsePolygon> UPolygonDeriver::CutZonesWithVectors(double OriginLon, double OriginLat)
{
    TArray<FLandUsePolygon> Result;

    UGISCoordinate* Coord = NewObject<UGISCoordinate>();
    Coord->SetOrigin(OriginLon, OriginLat);

    // 收集所有切割线（道路 + 河流 + 海岸线）→ 世界坐标
    TArray<TArray<FVector>> CutLines;

    auto AddFeaturesToCutLines = [&](const TArray<FGISFeature>& Features)
    {
        for (const FGISFeature& Feature : Features)
        {
            if (Feature.GeometryType == EGISGeometryType::LineString && Feature.Coordinates.Num() >= 2)
            {
                TArray<FVector> WorldLine = Coord->GeoArrayToWorld(Feature.Coordinates);
                CutLines.Add(MoveTemp(WorldLine));
            }
        }
    };

    AddFeaturesToCutLines(RoadFeatures);
    AddFeaturesToCutLines(RiverFeatures);
    AddFeaturesToCutLines(CoastlineFeatures);

    UE_LOG(LogTemp, Log, TEXT("PolygonDeriver: %d cut lines collected"), CutLines.Num());

    // 对每个地形分区执行切割
    for (const FTerrainZone& Zone : TerrainZones)
    {
        if (Zone.BoundaryVertices.Num() < 3)
        {
            continue;
        }

        // 切割
        TArray<TArray<FVector>> SubPolygons;
        if (CutLines.Num() > 0)
        {
            SubPolygons = CutPolygonWithLines(Zone.BoundaryVertices, CutLines);
        }

        // 如果切割没有产生结果，保留原始分区
        if (SubPolygons.Num() == 0)
        {
            SubPolygons.Add(Zone.BoundaryVertices);
        }

        // 转为 FLandUsePolygon
        for (const TArray<FVector>& SubPoly : SubPolygons)
        {
            const float AreaSqM = FMath::Abs(ComputePolygonArea(SubPoly)) / 10000.0f; // cm² → m²
            if (AreaSqM < MinPolygonArea)
            {
                continue;
            }

            FLandUsePolygon Poly;
            Poly.PolygonID = NextPolygonID++;
            Poly.WorldVertices = SubPoly;
            Poly.AreaSqM = AreaSqM;
            Poly.WorldCenter = ComputePolygonCenter(SubPoly);
            Poly.AvgElevation = Zone.AvgElevation;
            Poly.AvgSlope = Zone.AvgSlope;

            // 从地形类别做初步分类
            Poly.LandUseType = TerrainClassToLandUse(Zone.TerrainClass);

            // 反算经纬度
            for (const FVector& V : SubPoly)
            {
                Poly.GeoVertices.Add(Coord->WorldToGeo(V));
            }

            // 判断是否临主干道
            for (const FGISFeature& Road : RoadFeatures)
            {
                const FString* Highway = Road.Properties.Find(TEXT("highway"));
                if (!Highway)
                {
                    continue;
                }
                const int32* Weight = RoadClassWeights.Find(*Highway);
                if (Weight && *Weight >= 40) // secondary 及以上
                {
                    // 简化检测：道路任一点是否在 Polygon 附近
                    for (const FVector2D& RoadCoord : Road.Coordinates)
                    {
                        FVector RoadWorld = Coord->GeoToWorld(RoadCoord.X, RoadCoord.Y);
                        if (FVector::Dist(RoadWorld, Poly.WorldCenter) < FMath::Sqrt(Poly.AreaSqM) * 100.0f * 1.5f)
                        {
                            Poly.bAdjacentToMainRoad = true;
                            break;
                        }
                    }
                    if (Poly.bAdjacentToMainRoad) break;
                }
            }

            Result.Add(MoveTemp(Poly));
        }
    }

    // 标记水体
    MarkWaterBodies(Result);

    UE_LOG(LogTemp, Log, TEXT("PolygonDeriver: Cut zones into %d polygons"), Result.Num());
    return Result;
}

// ============ Step 5: 综合分类 ============

void UPolygonDeriver::ClassifyPolygons(TArray<FLandUsePolygon>& Polygons)
{
    if (ClassifyRules)
    {
        ULandUseClassifier::ClassifyAll(Polygons, ClassifyRules);
    }
    // 如果没有 ClassifyRules，Step 4 中已从 TerrainClass 做了初步分类
}

// ============ Step 6: PCG 参数 ============

void UPolygonDeriver::AssignPCGParams(TArray<FLandUsePolygon>& Polygons)
{
    if (ClassifyRules)
    {
        ULandUseClassifier::AssignPCGParameters(Polygons, ClassifyRules);
    }
}

// ============ 旧接口兼容 ============

bool UPolygonDeriver::BuildRoadGraph()
{
    if (RoadFeatures.Num() == 0)
    {
        return false;
    }

    RoadGraph = MakeShared<FRoadNetworkGraph>();

    constexpr double NodeMergeTolerance = 100.0; // 1 米

    for (const FGISFeature& Feature : RoadFeatures)
    {
        if (Feature.Coordinates.Num() < 2) continue;

        const FString* RoadClassPtr = Feature.Properties.Find(TEXT("highway"));
        const FString RoadClass = RoadClassPtr ? *RoadClassPtr : TEXT("unclassified");

        TArray<int32> NodeIDs;
        for (const FVector2D& Coord : Feature.Coordinates)
        {
            FVector WorldPos(Coord.X * 100000.0, Coord.Y * 100000.0, 0.0);
            int32 ExistingNodeID = RoadGraph->FindNearestNode(WorldPos, NodeMergeTolerance);
            if (ExistingNodeID != INDEX_NONE)
            {
                NodeIDs.Add(ExistingNodeID);
            }
            else
            {
                NodeIDs.Add(RoadGraph->AddNode(WorldPos, Coord));
            }
        }

        for (int32 i = 0; i < NodeIDs.Num() - 1; ++i)
        {
            if (NodeIDs[i] != NodeIDs[i + 1])
            {
                RoadGraph->AddEdge(NodeIDs[i], NodeIDs[i + 1], RoadClass);
            }
        }
    }

    RoadGraph->ComputeIntersectionsAndSplit();
    return RoadGraph->NumEdges() > 0;
}

// ============ 内部算法 ============

TArray<TArray<FVector>> UPolygonDeriver::CutPolygonWithLines(
    const TArray<FVector>& PolygonVerts,
    const TArray<TArray<FVector>>& CutLines) const
{
    TArray<TArray<FVector>> Result;

    // TODO: 实现完整的多边形-线段切割算法
    // 算法思路：
    //   1. 对每条切割线，计算与多边形边界的交点
    //   2. 如果切割线穿过多边形（有进入点和离开点）
    //   3. 沿切割线分割多边形为两半
    //   4. 递归对子多边形继续切割
    //
    // 当前简化版：直接返回原多边形，等集成 GeometryCore 后用 ClipPolygon
    //
    // 完整实现可用：
    //   - Sutherland-Hodgman 算法（凸多边形切割）
    //   - Weiler-Atherton 算法（任意多边形切割）
    //   - 或直接使用 UE5 GeometryCore 的 FPolygon2 操作

    Result.Add(PolygonVerts);
    return Result;
}

void UPolygonDeriver::MarkWaterBodies(TArray<FLandUsePolygon>& Polygons) const
{
    // 将水体面域覆盖的 Polygon 标记为 Water
    for (FLandUsePolygon& Poly : Polygons)
    {
        // 已经由地形分析标记为水体候选
        if (Poly.LandUseType == ELandUseType::Water)
        {
            continue;
        }

        // 检查是否与水体面域重叠（简化：中心点在水体面域内）
        for (const FGISFeature& WaterFeature : WaterBodyFeatures)
        {
            if (WaterFeature.GeometryType != EGISGeometryType::Polygon &&
                WaterFeature.GeometryType != EGISGeometryType::MultiPolygon)
            {
                continue;
            }

            // 简化的点-在-多边形测试（经纬度空间）
            const FVector2D CenterGeo = Poly.GeoVertices.Num() > 0
                ? FVector2D(0, 0) // 计算 GeoVertices 的中心
                : FVector2D(0, 0);

            if (Poly.GeoVertices.Num() > 0)
            {
                FVector2D GeoCenter(0, 0);
                for (const FVector2D& GV : Poly.GeoVertices)
                {
                    GeoCenter += GV;
                }
                GeoCenter /= Poly.GeoVertices.Num();

                // Ray casting test in geo coordinates
                const TArray<FVector2D>& WaterCoords = WaterFeature.Coordinates;
                bool bInside = false;
                for (int32 i = 0, j = WaterCoords.Num() - 1; i < WaterCoords.Num(); j = i++)
                {
                    if (((WaterCoords[i].Y > GeoCenter.Y) != (WaterCoords[j].Y > GeoCenter.Y)) &&
                        (GeoCenter.X < (WaterCoords[j].X - WaterCoords[i].X) *
                         (GeoCenter.Y - WaterCoords[i].Y) / (WaterCoords[j].Y - WaterCoords[i].Y) +
                         WaterCoords[i].X))
                    {
                        bInside = !bInside;
                    }
                }

                if (bInside)
                {
                    Poly.LandUseType = ELandUseType::Water;
                    Poly.BuildingDensity = 0.0f;
                    Poly.VegetationDensity = 0.0f;
                    break;
                }
            }
        }
    }
}

ELandUseType UPolygonDeriver::TerrainClassToLandUse(int32 TerrainClass)
{
    switch (TerrainClass)
    {
        case 0: return ELandUseType::Farmland;      // Low Flat → 农田/平原
        case 1: return ELandUseType::Residential;    // Low Gentle → 缓坡可居住
        case 2: return ELandUseType::OpenSpace;      // Mid Flat → 高原/台地
        case 3: return ELandUseType::Residential;    // Mid Gentle → 丘陵住宅
        case 4: return ELandUseType::Forest;         // High Steep → 山地/林地
        case 5: return ELandUseType::Water;          // Water Candidate
        default: return ELandUseType::Unknown;
    }
}

FString UPolygonDeriver::GetVectorStats() const
{
    return FString::Printf(
        TEXT("Roads: %d | Rivers: %d | Coastlines: %d | WaterBodies: %d | Total: %d"),
        RoadFeatures.Num(), RiverFeatures.Num(),
        CoastlineFeatures.Num(), WaterBodyFeatures.Num(),
        AllFeatures.Num()
    );
}

bool UPolygonDeriver::InferAnalysisBounds(double& MinLon, double& MinLat, double& MaxLon, double& MaxLat) const
{
    if (!DEMParserInstance || DEMParserInstance->GetTileCount() == 0)
    {
        return false;
    }

    // 从所有已加载瓦片的范围推断
    // 使用第一个瓦片作为初始值
    // DEMParser 不直接暴露 tile info，通过矢量数据范围补充
    // 简化：使用矢量数据的范围
    if (AllFeatures.Num() > 0)
    {
        MinLon = MAX_dbl;
        MinLat = MAX_dbl;
        MaxLon = -MAX_dbl;
        MaxLat = -MAX_dbl;

        for (const FGISFeature& Feature : AllFeatures)
        {
            for (const FVector2D& Coord : Feature.Coordinates)
            {
                MinLon = FMath::Min(MinLon, Coord.X);
                MinLat = FMath::Min(MinLat, Coord.Y);
                MaxLon = FMath::Max(MaxLon, Coord.X);
                MaxLat = FMath::Max(MaxLat, Coord.Y);
            }
        }

        // 外扩 10%
        const double PadLon = (MaxLon - MinLon) * 0.1;
        const double PadLat = (MaxLat - MinLat) * 0.1;
        MinLon -= PadLon;
        MinLat -= PadLat;
        MaxLon += PadLon;
        MaxLat += PadLat;

        return true;
    }

    // 没有矢量数据时使用默认 1 度范围
    // 这会在后续步骤中从 DEM 瓦片元数据中获取
    MinLon = -1.0;
    MinLat = -1.0;
    MaxLon = 1.0;
    MaxLat = 1.0;
    return false;
}

float UPolygonDeriver::ComputePolygonArea(const TArray<FVector>& Vertices)
{
    float Area = 0.0f;
    const int32 N = Vertices.Num();
    for (int32 i = 0; i < N; ++i)
    {
        const int32 j = (i + 1) % N;
        Area += Vertices[i].X * Vertices[j].Y;
        Area -= Vertices[j].X * Vertices[i].Y;
    }
    return Area / 2.0f;
}

FVector UPolygonDeriver::ComputePolygonCenter(const TArray<FVector>& Vertices)
{
    if (Vertices.Num() == 0) return FVector::ZeroVector;
    FVector Sum = FVector::ZeroVector;
    for (const FVector& V : Vertices) Sum += V;
    return Sum / static_cast<float>(Vertices.Num());
}

bool UPolygonDeriver::IsClockwise(const TArray<FVector>& Vertices)
{
    return ComputePolygonArea(Vertices) < 0.0f;
}
