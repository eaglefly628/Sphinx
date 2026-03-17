// PolygonDeriver.cpp - Polygon 推导算法核心实现
#include "Polygon/PolygonDeriver.h"
#include "Polygon/LandUseClassifier.h"
#include "Data/GeoJsonParser.h"
#include "Data/GISCoordinate.h"
#include "Async/Async.h"

TArray<FLandUsePolygon> UPolygonDeriver::GeneratePolygons(
    const FString& RoadsGeoJsonPath,
    const FString& DEMFilePath,
    double OriginLon,
    double OriginLat)
{
    TArray<FLandUsePolygon> Result;

    // Step 1: 加载道路网
    if (!LoadRoadNetwork(RoadsGeoJsonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("PolygonDeriver: Failed to load road network"));
        return Result;
    }

    // Step 2: 构建道路图
    if (!BuildRoadGraph())
    {
        UE_LOG(LogTemp, Error, TEXT("PolygonDeriver: Failed to build road graph"));
        return Result;
    }

    // Step 3: 提取 Polygon
    Result = ExtractPolygons();

    // Step 4: 分类
    ClassifyPolygons(Result);

    // Step 5: 填充 PCG 参数
    AssignPCGParams(Result);

    UE_LOG(LogTemp, Log, TEXT("PolygonDeriver: Generated %d polygons"), Result.Num());
    return Result;
}

void UPolygonDeriver::GeneratePolygonsAsync(
    const FString& RoadsGeoJsonPath,
    const FString& DEMFilePath,
    double OriginLon,
    double OriginLat,
    const FOnPolygonsGenerated& OnComplete)
{
    // 在后台线程执行
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, RoadsGeoJsonPath, DEMFilePath, OriginLon, OriginLat, OnComplete]()
    {
        TArray<FLandUsePolygon> Result = GeneratePolygons(RoadsGeoJsonPath, DEMFilePath, OriginLon, OriginLat);

        // 回到游戏线程执行回调
        AsyncTask(ENamedThreads::GameThread, [OnComplete, Result = MoveTemp(Result)]()
        {
            OnComplete.ExecuteIfBound(Result);
        });
    });
}

bool UPolygonDeriver::LoadRoadNetwork(const FString& GeoJsonPath)
{
    UGeoJsonParser* Parser = NewObject<UGeoJsonParser>();

    TArray<FGISFeature> AllFeatures;
    if (!Parser->ParseFile(GeoJsonPath, AllFeatures))
    {
        return false;
    }

    // 只保留 LineString 类型（道路）
    RoadFeatures = UGeoJsonParser::FilterByType(AllFeatures, EGISGeometryType::LineString);

    UE_LOG(LogTemp, Log, TEXT("PolygonDeriver: Loaded %d road features"), RoadFeatures.Num());
    return RoadFeatures.Num() > 0;
}

bool UPolygonDeriver::BuildRoadGraph()
{
    if (RoadFeatures.Num() == 0)
    {
        return false;
    }

    RoadGraph = MakeShared<FRoadNetworkGraph>();

    // 节点合并容差（厘米）— 约 1 米
    constexpr double NodeMergeTolerance = 100.0;

    for (const FGISFeature& Feature : RoadFeatures)
    {
        if (Feature.Coordinates.Num() < 2)
        {
            continue;
        }

        // 获取道路等级
        const FString* RoadClassPtr = Feature.Properties.Find(TEXT("highway"));
        const FString RoadClass = RoadClassPtr ? *RoadClassPtr : TEXT("unclassified");

        // 将线段上的每个点添加为节点（或复用已有的临近节点）
        TArray<int32> NodeIDs;
        for (const FVector2D& Coord : Feature.Coordinates)
        {
            // 简化坐标转换（这里用零点作为临时原点，实际使用时应配置）
            FVector WorldPos(Coord.X * 100000.0, Coord.Y * 100000.0, 0.0);

            int32 ExistingNodeID = RoadGraph->FindNearestNode(WorldPos, NodeMergeTolerance);
            if (ExistingNodeID != INDEX_NONE)
            {
                NodeIDs.Add(ExistingNodeID);
            }
            else
            {
                int32 NewNodeID = RoadGraph->AddNode(WorldPos, Coord);
                NodeIDs.Add(NewNodeID);
            }
        }

        // 相邻节点之间添加边
        for (int32 i = 0; i < NodeIDs.Num() - 1; ++i)
        {
            if (NodeIDs[i] != NodeIDs[i + 1]) // 避免自环
            {
                RoadGraph->AddEdge(NodeIDs[i], NodeIDs[i + 1], RoadClass);
            }
        }
    }

    // 计算交叉点并分割边
    RoadGraph->ComputeIntersectionsAndSplit();

    UE_LOG(LogTemp, Log, TEXT("PolygonDeriver: Built road graph with %d nodes, %d edges"),
        RoadGraph->NumNodes(), RoadGraph->NumEdges());

    return RoadGraph->NumEdges() > 0;
}

TArray<FLandUsePolygon> UPolygonDeriver::ExtractPolygons()
{
    TArray<FLandUsePolygon> Result;

    if (!RoadGraph.IsValid())
    {
        return Result;
    }

    // Planar Face Extraction
    TArray<TArray<FVector>> Faces = ExtractPlanarFaces();

    for (const TArray<FVector>& FaceVertices : Faces)
    {
        const float Area = ComputePolygonArea(FaceVertices);

        // 面积过滤（转换为平方米：UE5 用厘米，所以除以 10000）
        const float AreaSqM = FMath::Abs(Area) / 10000.0f;
        if (AreaSqM < MinPolygonArea)
        {
            continue;
        }

        FLandUsePolygon Polygon;
        Polygon.PolygonID = NextPolygonID++;
        Polygon.WorldVertices = FaceVertices;
        Polygon.AreaSqM = AreaSqM;
        Polygon.WorldCenter = ComputePolygonCenter(FaceVertices);

        Result.Add(MoveTemp(Polygon));
    }

    UE_LOG(LogTemp, Log, TEXT("PolygonDeriver: Extracted %d polygons (after area filter)"), Result.Num());
    return Result;
}

void UPolygonDeriver::ClassifyPolygons(TArray<FLandUsePolygon>& Polygons)
{
    if (ClassifyRules)
    {
        ULandUseClassifier::ClassifyAll(Polygons, ClassifyRules);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("PolygonDeriver: No ClassifyRules set, skipping classification"));
    }
}

void UPolygonDeriver::AssignPCGParams(TArray<FLandUsePolygon>& Polygons)
{
    if (ClassifyRules)
    {
        ULandUseClassifier::AssignPCGParameters(Polygons, ClassifyRules);
    }
}

TArray<TArray<FVector>> UPolygonDeriver::ExtractPlanarFaces() const
{
    TArray<TArray<FVector>> Faces;

    if (!RoadGraph.IsValid())
    {
        return Faces;
    }

    // Planar Face Extraction 算法
    // 对每条有向边执行 "最左转" 遍历，直到回到起始边
    TSet<TPair<int32, bool>> VisitedDirectedEdges;

    for (const FRoadEdge& Edge : RoadGraph->Edges)
    {
        // 每条边有正反两个方向
        for (int32 Dir = 0; Dir < 2; ++Dir)
        {
            const bool bForward = (Dir == 0);
            TPair<int32, bool> StartDirectedEdge(Edge.EdgeID, bForward);

            if (VisitedDirectedEdges.Contains(StartDirectedEdge))
            {
                continue;
            }

            // 从此有向边出发，沿最左转遍历
            TArray<FVector> FaceVertices;
            int32 CurrentEdgeID = Edge.EdgeID;
            bool bCurrentForward = bForward;
            int32 MaxIterations = RoadGraph->NumEdges() * 2; // 安全阀
            int32 Iterations = 0;

            do
            {
                TPair<int32, bool> CurrentDirected(CurrentEdgeID, bCurrentForward);
                if (VisitedDirectedEdges.Contains(CurrentDirected))
                {
                    break;
                }
                VisitedDirectedEdges.Add(CurrentDirected);

                // 添加当前边起点到面
                if (RoadGraph->Edges.IsValidIndex(CurrentEdgeID))
                {
                    const FRoadEdge& CurrEdge = RoadGraph->Edges[CurrentEdgeID];
                    const int32 StartNode = bCurrentForward ? CurrEdge.StartNodeID : CurrEdge.EndNodeID;
                    if (RoadGraph->Nodes.IsValidIndex(StartNode))
                    {
                        FaceVertices.Add(RoadGraph->Nodes[StartNode].Position);
                    }
                }

                // 获取最左转的下一条边
                TPair<int32, bool> Next = RoadGraph->GetLeftmostTurn(CurrentEdgeID, bCurrentForward);
                if (Next.Key == INDEX_NONE)
                {
                    break;
                }

                CurrentEdgeID = Next.Key;
                bCurrentForward = Next.Value;
                Iterations++;

            } while (!(CurrentEdgeID == Edge.EdgeID && bCurrentForward == bForward) && Iterations < MaxIterations);

            // 有效面需要至少 3 个顶点
            if (FaceVertices.Num() >= 3)
            {
                // 排除外围无界面（面积为负或过大）
                const float Area = ComputePolygonArea(FaceVertices);
                if (Area > 0) // 正面积 = 内部面
                {
                    Faces.Add(MoveTemp(FaceVertices));
                }
            }
        }
    }

    return Faces;
}

float UPolygonDeriver::ComputePolygonArea(const TArray<FVector>& Vertices)
{
    // Shoelace 公式（2D，使用 X 和 Y）
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
    if (Vertices.Num() == 0)
    {
        return FVector::ZeroVector;
    }

    FVector Sum = FVector::ZeroVector;
    for (const FVector& V : Vertices)
    {
        Sum += V;
    }

    return Sum / static_cast<float>(Vertices.Num());
}

bool UPolygonDeriver::IsClockwise(const TArray<FVector>& Vertices)
{
    return ComputePolygonArea(Vertices) < 0.0f;
}
