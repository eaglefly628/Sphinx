// PolygonDeriver.cpp - Polygon 推导实现
// 支持两种模式：DataProvider 模式 和 传统文件路径模式
#include "Polygon/PolygonDeriver.h"
#include "Polygon/LandUseClassifier.h"
#include "Data/GeoJsonParser.h"
#include "Data/GISCoordinate.h"
#include "DEM/DEMParser.h"
#include "DEM/TerrainAnalyzer.h"
#include "Async/Async.h"

// ============ DataProvider 模式 ============

TArray<FLandUsePolygon> UPolygonDeriver::GenerateFromProvider(
    double OriginLon,
    double OriginLat)
{
    TArray<FLandUsePolygon> Result;

    if (!DataProvider)
    {
        UE_LOG(LogTemp, Error, TEXT("PolygonDeriver: No DataProvider set"));
        return Result;
    }

    if (!DataProvider->IsAvailable())
    {
        UE_LOG(LogTemp, Error, TEXT("PolygonDeriver: DataProvider '%s' is not available"),
            *DataProvider->GetProviderName());
        return Result;
    }

    UE_LOG(LogTemp, Log, TEXT("PolygonDeriver: Using DataProvider '%s'"),
        *DataProvider->GetProviderName());

    // Step 1: 从 DataProvider 查询矢量数据
    if (!LoadVectorDataFromProvider(QueryBounds))
    {
        UE_LOG(LogTemp, Warning, TEXT("PolygonDeriver: No vector data from provider"));
        return Result;
    }

    // Step 2: 尝试从 DataProvider 获取高程数据做地形分区
    bool bHasTerrain = false;
    FGeoRect EffectiveBounds = QueryBounds.IsValid() ? QueryBounds : InferBoundsFromFeatures();

    if (EffectiveBounds.IsValid())
    {
        TArray<float> ElevGrid;
        int32 GridW = 0, GridH = 0;
        if (DataProvider->QueryElevation(EffectiveBounds, TerrainAnalysisResolution, ElevGrid, GridW, GridH))
        {
            // 有高程数据 → 走完整的 DEM 分析 + 矢量切割流程
            if (!TerrainAnalyzerInstance)
            {
                TerrainAnalyzerInstance = NewObject<UTerrainAnalyzer>(this);
            }
            TerrainAnalyzerInstance->Config.AnalysisResolution = TerrainAnalysisResolution;

            if (ClassifyRules)
            {
                TerrainAnalyzerInstance->Config.LowElevationMax = ClassifyRules->LowElevationMax;
                TerrainAnalyzerInstance->Config.HighElevationMin = ClassifyRules->HighElevationMin;
                TerrainAnalyzerInstance->Config.FlatMaxSlope = ClassifyRules->FlatMaxSlope;
                TerrainAnalyzerInstance->Config.SteepMinSlope = ClassifyRules->HillMinSlope;
            }

            TerrainZones = TerrainAnalyzerInstance->AnalyzeFromGrid(
                ElevGrid, GridW, GridH,
                EffectiveBounds.MinLon, EffectiveBounds.MinLat,
                EffectiveBounds.MaxLon, EffectiveBounds.MaxLat,
                OriginLon, OriginLat
            );

            bHasTerrain = TerrainZones.Num() > 0;
            UE_LOG(LogTemp, Log, TEXT("PolygonDeriver: Terrain analysis produced %d zones"), TerrainZones.Num());
        }
    }

    // Step 3: 生成 Polygon
    if (bHasTerrain)
    {
        // 有地形分区 → 矢量切割
        Result = CutZonesWithVectors(OriginLon, OriginLat);
    }
    else
    {
        // 无地形数据 → 仅从矢量数据生成
        Result = GenerateFromVectorsOnly(OriginLon, OriginLat);
    }

    // Step 4: 分类 + PCG 参数
    ClassifyPolygons(Result);
    AssignPCGParams(Result);

    UE_LOG(LogTemp, Log, TEXT("PolygonDeriver: Generated %d polygons via DataProvider"), Result.Num());
    return Result;
}

bool UPolygonDeriver::LoadVectorDataFromProvider(const FGeoRect& Bounds)
{
    if (!DataProvider)
    {
        return false;
    }

    AllFeatures.Empty();
    if (!DataProvider->QueryFeatures(Bounds, AllFeatures))
    {
        return false;
    }

    CategorizeFeatures();
    return AllFeatures.Num() > 0;
}

TArray<FLandUsePolygon> UPolygonDeriver::GenerateFromVectorsOnly(double OriginLon, double OriginLat)
{
    TArray<FLandUsePolygon> Result;

    UGISCoordinate* Coord = NewObject<UGISCoordinate>();
    Coord->SetOrigin(OriginLon, OriginLat);

    // 无 DEM 地形分区时，从矢量面域（WaterBody, LandUse, Building, Natural）直接创建 Polygon
    for (const FGISFeature& Feature : AllFeatures)
    {
        if (Feature.GeometryType != EGISGeometryType::Polygon &&
            Feature.GeometryType != EGISGeometryType::MultiPolygon)
        {
            continue;
        }

        if (Feature.Coordinates.Num() < 3)
        {
            continue;
        }

        // 转为世界坐标
        TArray<FVector> WorldVerts = Coord->GeoArrayToWorld(Feature.Coordinates);

        const float AreaSqM = FMath::Abs(ComputePolygonArea(WorldVerts)) / 10000.0f;
        if (AreaSqM < MinPolygonArea)
        {
            continue;
        }

        FLandUsePolygon Poly;
        Poly.PolygonID = NextPolygonID++;
        Poly.WorldVertices = WorldVerts;
        Poly.GeoVertices = Feature.Coordinates;
        Poly.AreaSqM = AreaSqM;
        Poly.WorldCenter = ComputePolygonCenter(WorldVerts);

        // 从要素类别推断初步分类
        switch (Feature.Category)
        {
            case EGISFeatureCategory::WaterBody:
                Poly.LandUseType = ELandUseType::Water;
                break;
            case EGISFeatureCategory::Building:
                Poly.LandUseType = ELandUseType::Residential;
                break;
            case EGISFeatureCategory::Natural:
                Poly.LandUseType = ELandUseType::Forest;
                break;
            case EGISFeatureCategory::LandUse:
            {
                const FString* LandUse = Feature.Properties.Find(TEXT("landuse"));
                if (LandUse)
                {
                    if (*LandUse == TEXT("residential")) Poly.LandUseType = ELandUseType::Residential;
                    else if (*LandUse == TEXT("commercial") || *LandUse == TEXT("retail")) Poly.LandUseType = ELandUseType::Commercial;
                    else if (*LandUse == TEXT("industrial")) Poly.LandUseType = ELandUseType::Industrial;
                    else if (*LandUse == TEXT("forest")) Poly.LandUseType = ELandUseType::Forest;
                    else if (*LandUse == TEXT("farmland") || *LandUse == TEXT("farm")) Poly.LandUseType = ELandUseType::Farmland;
                    else if (*LandUse == TEXT("military")) Poly.LandUseType = ELandUseType::Military;
                    else Poly.LandUseType = ELandUseType::OpenSpace;
                }
                break;
            }
            default:
                Poly.LandUseType = ELandUseType::Unknown;
                break;
        }

        Result.Add(MoveTemp(Poly));
    }

    // 标记临主干道
    for (FLandUsePolygon& Poly : Result)
    {
        for (const FGISFeature& Road : RoadFeatures)
        {
            const FString* Highway = Road.Properties.Find(TEXT("highway"));
            if (!Highway) continue;
            const int32* Weight = RoadClassWeights.Find(*Highway);
            if (Weight && *Weight >= 40)
            {
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
    }

    MarkWaterBodies(Result);

    UE_LOG(LogTemp, Log, TEXT("PolygonDeriver: Generated %d polygons from vectors only"), Result.Num());
    return Result;
}

void UPolygonDeriver::CategorizeFeatures()
{
    RoadFeatures = UGeoJsonParser::FilterByCategory(AllFeatures, EGISFeatureCategory::Road);
    RiverFeatures = UGeoJsonParser::FilterByCategory(AllFeatures, EGISFeatureCategory::River);
    CoastlineFeatures = UGeoJsonParser::FilterByCategory(AllFeatures, EGISFeatureCategory::Coastline);
    WaterBodyFeatures = UGeoJsonParser::FilterByCategory(AllFeatures, EGISFeatureCategory::WaterBody);

    UE_LOG(LogTemp, Log, TEXT("PolygonDeriver: Categorized - Roads: %d, Rivers: %d, Coastlines: %d, WaterBodies: %d"),
        RoadFeatures.Num(), RiverFeatures.Num(), CoastlineFeatures.Num(), WaterBodyFeatures.Num());
}

FGeoRect UPolygonDeriver::InferBoundsFromFeatures() const
{
    FGeoRect Bounds;
    if (AllFeatures.Num() == 0) return Bounds;

    Bounds.MinLon = MAX_dbl;
    Bounds.MinLat = MAX_dbl;
    Bounds.MaxLon = -MAX_dbl;
    Bounds.MaxLat = -MAX_dbl;

    for (const FGISFeature& Feature : AllFeatures)
    {
        for (const FVector2D& Coord : Feature.Coordinates)
        {
            Bounds.MinLon = FMath::Min(Bounds.MinLon, Coord.X);
            Bounds.MinLat = FMath::Min(Bounds.MinLat, Coord.Y);
            Bounds.MaxLon = FMath::Max(Bounds.MaxLon, Coord.X);
            Bounds.MaxLat = FMath::Max(Bounds.MaxLat, Coord.Y);
        }
    }

    // 外扩 10%
    const double PadLon = (Bounds.MaxLon - Bounds.MinLon) * 0.1;
    const double PadLat = (Bounds.MaxLat - Bounds.MinLat) * 0.1;
    Bounds.MinLon -= PadLon;
    Bounds.MinLat -= PadLat;
    Bounds.MaxLon += PadLon;
    Bounds.MaxLat += PadLat;

    return Bounds;
}

// ============ 传统模式（向后兼容） ============

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

    CategorizeFeatures();
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

        TArray<TArray<FVector>> SubPolygons;
        if (CutLines.Num() > 0)
        {
            SubPolygons = CutPolygonWithLines(Zone.BoundaryVertices, CutLines);
        }

        if (SubPolygons.Num() == 0)
        {
            SubPolygons.Add(Zone.BoundaryVertices);
        }

        for (const TArray<FVector>& SubPoly : SubPolygons)
        {
            const float AreaSqM = FMath::Abs(ComputePolygonArea(SubPoly)) / 10000.0f;
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
            Poly.LandUseType = TerrainClassToLandUse(Zone.TerrainClass);

            for (const FVector& V : SubPoly)
            {
                Poly.GeoVertices.Add(Coord->WorldToGeo(V));
            }

            // 判断是否临主干道
            for (const FGISFeature& Road : RoadFeatures)
            {
                const FString* Highway = Road.Properties.Find(TEXT("highway"));
                if (!Highway) continue;
                const int32* Weight = RoadClassWeights.Find(*Highway);
                if (Weight && *Weight >= 40)
                {
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

    constexpr double NodeMergeTolerance = 100.0;

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
    // 当前简化版：直接返回原多边形
    Result.Add(PolygonVerts);
    return Result;
}

void UPolygonDeriver::MarkWaterBodies(TArray<FLandUsePolygon>& Polygons) const
{
    for (FLandUsePolygon& Poly : Polygons)
    {
        if (Poly.LandUseType == ELandUseType::Water)
        {
            continue;
        }

        for (const FGISFeature& WaterFeature : WaterBodyFeatures)
        {
            if (WaterFeature.GeometryType != EGISGeometryType::Polygon &&
                WaterFeature.GeometryType != EGISGeometryType::MultiPolygon)
            {
                continue;
            }

            if (Poly.GeoVertices.Num() > 0)
            {
                FVector2D GeoCenter(0, 0);
                for (const FVector2D& GV : Poly.GeoVertices)
                {
                    GeoCenter += GV;
                }
                GeoCenter /= Poly.GeoVertices.Num();

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
        case 0: return ELandUseType::Farmland;
        case 1: return ELandUseType::Residential;
        case 2: return ELandUseType::OpenSpace;
        case 3: return ELandUseType::Residential;
        case 4: return ELandUseType::Forest;
        case 5: return ELandUseType::Water;
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

    if (AllFeatures.Num() > 0)
    {
        FGeoRect Bounds = InferBoundsFromFeatures();
        MinLon = Bounds.MinLon;
        MinLat = Bounds.MinLat;
        MaxLon = Bounds.MaxLon;
        MaxLat = Bounds.MaxLat;
        return Bounds.IsValid();
    }

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
