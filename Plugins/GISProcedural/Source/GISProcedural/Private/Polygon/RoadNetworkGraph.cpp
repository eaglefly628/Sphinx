// RoadNetworkGraph.cpp - 道路网图结构实现
#include "Polygon/RoadNetworkGraph.h"
#include "GISProceduralModule.h"

int32 FRoadNetworkGraph::AddNode(const FVector& Position, const FVector2D& GeoPosition)
{
    FRoadNode Node;
    Node.NodeID = NextNodeID++;
    Node.Position = Position;
    Node.GeoPosition = GeoPosition;

    Nodes.Add(MoveTemp(Node));
    return Node.NodeID;
}

int32 FRoadNetworkGraph::AddEdge(int32 StartNodeID, int32 EndNodeID, const FString& RoadClass)
{
    FRoadEdge Edge;
    Edge.EdgeID = NextEdgeID++;
    Edge.StartNodeID = StartNodeID;
    Edge.EndNodeID = EndNodeID;
    Edge.RoadClass = RoadClass;

    // 将边 ID 添加到起止节点的连接列表
    if (Nodes.IsValidIndex(StartNodeID))
    {
        Nodes[StartNodeID].ConnectedEdges.Add(Edge.EdgeID);
    }
    if (Nodes.IsValidIndex(EndNodeID))
    {
        Nodes[EndNodeID].ConnectedEdges.Add(Edge.EdgeID);
    }

    Edges.Add(MoveTemp(Edge));
    return Edge.EdgeID;
}

int32 FRoadNetworkGraph::FindNearestNode(const FVector& Position, double Tolerance) const
{
    int32 NearestID = INDEX_NONE;
    double NearestDistSq = Tolerance * Tolerance;

    for (const FRoadNode& Node : Nodes)
    {
        const double DistSq = FVector::DistSquared(Node.Position, Position);
        if (DistSq < NearestDistSq)
        {
            NearestDistSq = DistSq;
            NearestID = Node.NodeID;
        }
    }

    return NearestID;
}

TPair<int32, bool> FRoadNetworkGraph::GetLeftmostTurn(int32 CurrentEdgeID, bool bForward) const
{
    if (!Edges.IsValidIndex(CurrentEdgeID))
    {
        return TPair<int32, bool>(INDEX_NONE, false);
    }

    const FRoadEdge& CurrentEdge = Edges[CurrentEdgeID];
    // 当前行走到达的节点
    const int32 ArrivalNodeID = bForward ? CurrentEdge.EndNodeID : CurrentEdge.StartNodeID;
    // 来时的节点
    const int32 DepartureNodeID = bForward ? CurrentEdge.StartNodeID : CurrentEdge.EndNodeID;

    if (!Nodes.IsValidIndex(ArrivalNodeID) || !Nodes.IsValidIndex(DepartureNodeID))
    {
        return TPair<int32, bool>(INDEX_NONE, false);
    }

    const FVector& ArrivalPos = Nodes[ArrivalNodeID].Position;
    const FVector& DeparturePos = Nodes[DepartureNodeID].Position;

    // 来时方向的角度
    const FVector IncomingDir = (ArrivalPos - DeparturePos).GetSafeNormal();
    const double IncomingAngle = FMath::Atan2(IncomingDir.Y, IncomingDir.X);

    // 获取到达节点上所有可走的边（排除当前边）
    TArray<TPair<int32, double>> SortedEdges = GetSortedEdgeAngles(ArrivalNodeID);

    // 找到当前边在排序列表中的位置，然后取下一条（最左转）
    int32 CurrentIndex = INDEX_NONE;
    for (int32 i = 0; i < SortedEdges.Num(); ++i)
    {
        if (SortedEdges[i].Key == CurrentEdgeID)
        {
            CurrentIndex = i;
            break;
        }
    }

    if (CurrentIndex == INDEX_NONE || SortedEdges.Num() < 2)
    {
        return TPair<int32, bool>(INDEX_NONE, false);
    }

    // 最左转 = 角度排序中的下一条边
    const int32 NextIndex = (CurrentIndex + 1) % SortedEdges.Num();
    const int32 NextEdgeID = SortedEdges[NextIndex].Key;

    if (!Edges.IsValidIndex(NextEdgeID))
    {
        return TPair<int32, bool>(INDEX_NONE, false);
    }

    // 判断下一条边的行走方向
    const FRoadEdge& NextEdge = Edges[NextEdgeID];
    const bool bNextForward = (NextEdge.StartNodeID == ArrivalNodeID);

    return TPair<int32, bool>(NextEdgeID, bNextForward);
}

void FRoadNetworkGraph::Reset()
{
    Nodes.Empty();
    Edges.Empty();
    NextNodeID = 0;
    NextEdgeID = 0;
}

void FRoadNetworkGraph::ComputeIntersectionsAndSplit()
{
    UE_LOG(LogGIS, Log, TEXT("RoadNetworkGraph: ComputeIntersectionsAndSplit - %d nodes, %d edges (before)"),
        Nodes.Num(), Edges.Num());

    // 收集所有需要分割的信息：{ EdgeID → 排序后的 t 参数列表 + 对应交点 }
    struct FSplitInfo
    {
        float T;
        FVector Position;
        FVector2D GeoPosition;
    };
    TMap<int32, TArray<FSplitInfo>> EdgeSplits;

    const int32 OriginalEdgeCount = Edges.Num();

    // 1. O(n²) 遍历所有边对，检测线段交叉
    for (int32 i = 0; i < OriginalEdgeCount; ++i)
    {
        const FRoadEdge& EdgeA = Edges[i];
        if (EdgeA.EdgeID == INDEX_NONE) continue;
        if (!Nodes.IsValidIndex(EdgeA.StartNodeID) || !Nodes.IsValidIndex(EdgeA.EndNodeID)) continue;

        const FVector& A1 = Nodes[EdgeA.StartNodeID].Position;
        const FVector& A2 = Nodes[EdgeA.EndNodeID].Position;

        for (int32 j = i + 1; j < OriginalEdgeCount; ++j)
        {
            const FRoadEdge& EdgeB = Edges[j];
            if (EdgeB.EdgeID == INDEX_NONE) continue;
            if (!Nodes.IsValidIndex(EdgeB.StartNodeID) || !Nodes.IsValidIndex(EdgeB.EndNodeID)) continue;

            // 共享端点的边不算交叉
            if (EdgeA.StartNodeID == EdgeB.StartNodeID || EdgeA.StartNodeID == EdgeB.EndNodeID ||
                EdgeA.EndNodeID == EdgeB.StartNodeID || EdgeA.EndNodeID == EdgeB.EndNodeID)
            {
                continue;
            }

            const FVector& B1 = Nodes[EdgeB.StartNodeID].Position;
            const FVector& B2 = Nodes[EdgeB.EndNodeID].Position;

            // 2D 线段求交
            const float Dx1 = A2.X - A1.X;
            const float Dy1 = A2.Y - A1.Y;
            const float Dx2 = B2.X - B1.X;
            const float Dy2 = B2.Y - B1.Y;

            const float Denom = Dx1 * Dy2 - Dy1 * Dx2;
            if (FMath::Abs(Denom) < KINDA_SMALL_NUMBER)
            {
                continue; // 平行
            }

            const float Dx3 = B1.X - A1.X;
            const float Dy3 = B1.Y - A1.Y;

            const float T = (Dx3 * Dy2 - Dy3 * Dx2) / Denom;
            const float U = (Dx3 * Dy1 - Dy3 * Dx1) / Denom;

            // 严格内部交点（排除端点）
            constexpr float Eps = 0.001f;
            if (T > Eps && T < 1.0f - Eps && U > Eps && U < 1.0f - Eps)
            {
                const FVector IntersectPos = A1 + (A2 - A1) * T;

                // 对两条边都记录分割信息
                // 插值 GeoPosition
                const FVector2D GeoA = FMath::Lerp(
                    Nodes[EdgeA.StartNodeID].GeoPosition,
                    Nodes[EdgeA.EndNodeID].GeoPosition, T);

                FSplitInfo SplitA;
                SplitA.T = T;
                SplitA.Position = IntersectPos;
                SplitA.GeoPosition = GeoA;
                EdgeSplits.FindOrAdd(i).Add(SplitA);

                const FVector2D GeoB = FMath::Lerp(
                    Nodes[EdgeB.StartNodeID].GeoPosition,
                    Nodes[EdgeB.EndNodeID].GeoPosition, U);

                FSplitInfo SplitB;
                SplitB.T = U;
                SplitB.Position = IntersectPos;
                SplitB.GeoPosition = GeoB;
                EdgeSplits.FindOrAdd(j).Add(SplitB);
            }
        }
    }

    if (EdgeSplits.Num() == 0)
    {
        UE_LOG(LogGIS, Log, TEXT("RoadNetworkGraph: No intersections found"));
        return;
    }

    // 2. 对每条需要分割的边，按 T 排序后创建新节点和新边
    int32 TotalSplits = 0;

    for (auto& Pair : EdgeSplits)
    {
        const int32 EdgeIdx = Pair.Key;
        TArray<FSplitInfo>& Splits = Pair.Value;

        if (!Edges.IsValidIndex(EdgeIdx)) continue;

        // 按 T 排序
        Splits.Sort([](const FSplitInfo& A, const FSplitInfo& B) { return A.T < B.T; });

        // 去重（同一位置的交点只保留一个）
        for (int32 k = Splits.Num() - 1; k > 0; --k)
        {
            if (FVector::DistSquared(Splits[k].Position, Splits[k - 1].Position) < 1.0f)
            {
                Splits.RemoveAt(k);
            }
        }

        const FRoadEdge OriginalEdge = Edges[EdgeIdx]; // copy
        const int32 OrigStartID = OriginalEdge.StartNodeID;
        const int32 OrigEndID = OriginalEdge.EndNodeID;
        const FString& RoadClass = OriginalEdge.RoadClass;

        // 创建分割点节点
        TArray<int32> SplitNodeIDs;
        for (const FSplitInfo& Split : Splits)
        {
            // 检查是否已有非常接近的节点（因为多条边可能在同一点交叉）
            int32 ExistingNode = FindNearestNode(Split.Position, 50.0);
            if (ExistingNode != INDEX_NONE)
            {
                SplitNodeIDs.Add(ExistingNode);
            }
            else
            {
                SplitNodeIDs.Add(AddNode(Split.Position, Split.GeoPosition));
            }
        }

        // 从原始起点的 ConnectedEdges 中移除原始边
        if (Nodes.IsValidIndex(OrigStartID))
        {
            Nodes[OrigStartID].ConnectedEdges.Remove(OriginalEdge.EdgeID);
        }
        if (Nodes.IsValidIndex(OrigEndID))
        {
            Nodes[OrigEndID].ConnectedEdges.Remove(OriginalEdge.EdgeID);
        }

        // 标记原始边为无效（不实际删除以保持索引稳定）
        Edges[EdgeIdx].StartNodeID = INDEX_NONE;
        Edges[EdgeIdx].EndNodeID = INDEX_NONE;

        // 创建新的子边链：Start → Split1 → Split2 → ... → End
        TArray<int32> ChainNodes;
        ChainNodes.Add(OrigStartID);
        ChainNodes.Append(SplitNodeIDs);
        ChainNodes.Add(OrigEndID);

        for (int32 k = 0; k < ChainNodes.Num() - 1; ++k)
        {
            if (ChainNodes[k] != ChainNodes[k + 1])
            {
                AddEdge(ChainNodes[k], ChainNodes[k + 1], RoadClass);
            }
        }

        TotalSplits += Splits.Num();
    }

    UE_LOG(LogGIS, Log, TEXT("RoadNetworkGraph: Split %d edges at %d intersection points → %d nodes, %d edges (after)"),
        EdgeSplits.Num(), TotalSplits, Nodes.Num(), Edges.Num());
}

TArray<TPair<int32, double>> FRoadNetworkGraph::GetSortedEdgeAngles(int32 NodeID) const
{
    TArray<TPair<int32, double>> Result;

    if (!Nodes.IsValidIndex(NodeID))
    {
        return Result;
    }

    const FRoadNode& Node = Nodes[NodeID];

    for (int32 EdgeID : Node.ConnectedEdges)
    {
        if (!Edges.IsValidIndex(EdgeID))
        {
            continue;
        }

        const FRoadEdge& Edge = Edges[EdgeID];
        // 确定边的另一端节点
        const int32 OtherNodeID = (Edge.StartNodeID == NodeID) ? Edge.EndNodeID : Edge.StartNodeID;

        if (!Nodes.IsValidIndex(OtherNodeID))
        {
            continue;
        }

        const FVector Dir = (Nodes[OtherNodeID].Position - Node.Position).GetSafeNormal();
        const double Angle = FMath::Atan2(Dir.Y, Dir.X);

        Result.Add(TPair<int32, double>(EdgeID, Angle));
    }

    // 按角度排序
    Result.Sort([](const TPair<int32, double>& A, const TPair<int32, double>& B)
    {
        return A.Value < B.Value;
    });

    return Result;
}
