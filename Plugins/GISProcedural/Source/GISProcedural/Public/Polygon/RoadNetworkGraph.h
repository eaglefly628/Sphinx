// RoadNetworkGraph.h - 道路网图结构
#pragma once

#include "CoreMinimal.h"

/**
 * 道路网络中的节点（交叉口或端点）
 */
struct GISPROCEDURAL_API FRoadNode
{
    /** 节点 ID */
    int32 NodeID = INDEX_NONE;

    /** 节点位置（UE5 世界坐标） */
    FVector Position = FVector::ZeroVector;

    /** 节点位置（经纬度） */
    FVector2D GeoPosition = FVector2D::ZeroVector;

    /** 连接的边 ID 列表 */
    TArray<int32> ConnectedEdges;
};

/**
 * 道路网络中的边（道路段）
 */
struct GISPROCEDURAL_API FRoadEdge
{
    /** 边 ID */
    int32 EdgeID = INDEX_NONE;

    /** 起始节点 ID */
    int32 StartNodeID = INDEX_NONE;

    /** 终止节点 ID */
    int32 EndNodeID = INDEX_NONE;

    /** 边上的中间点序列（用于弯曲道路） */
    TArray<FVector> IntermediatePoints;

    /** 道路等级（如 highway, primary, secondary, residential 等） */
    FString RoadClass;

    /** 道路等级权重（数值越大越重要） */
    int32 RoadWeight = 0;

    /** 道路名称 */
    FString RoadName;
};

/**
 * 道路网络平面图
 * 用于 Planar Face Extraction 算法的基础数据结构
 */
class GISPROCEDURAL_API FRoadNetworkGraph
{
public:
    /** 所有节点 */
    TArray<FRoadNode> Nodes;

    /** 所有边 */
    TArray<FRoadEdge> Edges;

    /** 添加节点，返回节点 ID */
    int32 AddNode(const FVector& Position, const FVector2D& GeoPosition);

    /** 添加边，返回边 ID */
    int32 AddEdge(int32 StartNodeID, int32 EndNodeID, const FString& RoadClass);

    /** 查找距离给定位置最近的节点（用于合并临近节点） */
    int32 FindNearestNode(const FVector& Position, double Tolerance) const;

    /**
     * 计算从指定有向边出发的"最左转"下一条边
     * 这是 Planar Face Extraction 的核心操作
     * @param CurrentEdgeID 当前边 ID
     * @param bForward 当前是否沿正方向行走
     * @return 下一条有向边的 ID 和方向
     */
    TPair<int32, bool> GetLeftmostTurn(int32 CurrentEdgeID, bool bForward) const;

    /** 获取节点数量 */
    int32 NumNodes() const { return Nodes.Num(); }

    /** 获取边数量 */
    int32 NumEdges() const { return Edges.Num(); }

    /** 清空图 */
    void Reset();

    /**
     * 在所有边的交叉点处插入新节点并分割边
     * 构建真正的平面图
     */
    void ComputeIntersectionsAndSplit();

private:
    /** 节点 ID 计数器 */
    int32 NextNodeID = 0;

    /** 边 ID 计数器 */
    int32 NextEdgeID = 0;

    /** 计算从节点出发的各边的角度排序 */
    TArray<TPair<int32, double>> GetSortedEdgeAngles(int32 NodeID) const;
};
