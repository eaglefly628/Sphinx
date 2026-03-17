// RoadNetworkGraph.cpp - 道路网图结构实现
#include "Polygon/RoadNetworkGraph.h"

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
    // TODO: 实现线段交叉检测和分割
    // 1. 遍历所有边对，检测线段交叉
    // 2. 在交叉点处插入新节点
    // 3. 将原始边分割为两条新边
    // 这是 Planar Face Extraction 的前提条件
    UE_LOG(LogTemp, Log, TEXT("RoadNetworkGraph: ComputeIntersectionsAndSplit - %d nodes, %d edges"), Nodes.Num(), Edges.Num());
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
