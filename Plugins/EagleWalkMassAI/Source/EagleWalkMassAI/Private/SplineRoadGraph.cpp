#include "SplineRoadGraph.h"

#include "Components/SplineComponent.h"
#include "EagleWalkMassAI.h"

namespace
{
	void BuildCumDist(const TArray<FVector>& Points, TArray<float>& OutCumDist, float& OutLength)
	{
		OutCumDist.Reset(Points.Num());
		OutCumDist.Add(0.f);
		float Acc = 0.f;
		for (int32 i = 1; i < Points.Num(); ++i)
		{
			Acc += FVector::Dist(Points[i - 1], Points[i]);
			OutCumDist.Add(Acc);
		}
		OutLength = Acc;
	}

	FBox PolylineBounds(const TArray<FVector>& Points)
	{
		FBox Box(ForceInit);
		for (const FVector& P : Points)
		{
			Box += P;
		}
		// Inflate slightly so a perfectly axis-aligned polyline still has volume.
		return Box.ExpandBy(50.f);
	}
}

int32 USplineRoadGraph::AddPolyline(const TArray<FVector>& Points, FName GroupId)
{
	if (Points.Num() < 2)
	{
		return INDEX_NONE;
	}
	TArray<FVector> Copy = Points;
	return InsertEdgeFromPoints(MoveTemp(Copy), GroupId);
}

int32 USplineRoadGraph::AddSpline(USplineComponent* Spline, FName GroupId)
{
	if (!IsValid(Spline) || Spline->GetNumberOfSplinePoints() < 2)
	{
		return INDEX_NONE;
	}
	const float Length = Spline->GetSplineLength();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return INDEX_NONE;
	}
	const float Step = FMath::Max(50.f, SplineSampleStep);
	const int32 Samples = FMath::Max(2, FMath::CeilToInt(Length / Step) + 1);
	TArray<FVector> Points;
	Points.Reserve(Samples);
	for (int32 i = 0; i < Samples; ++i)
	{
		const float D = (i == Samples - 1) ? Length : (i * Step);
		Points.Add(Spline->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World));
	}
	return InsertEdgeFromPoints(MoveTemp(Points), GroupId);
}

TArray<int32> USplineRoadGraph::AddPolylines(const TArray<FEaglePolyline>& Polylines, FName GroupId)
{
	TArray<int32> Result;
	Result.Reserve(Polylines.Num());
	for (const FEaglePolyline& P : Polylines)
	{
		const int32 Idx = AddPolyline(P.Points, GroupId);
		if (Idx != INDEX_NONE)
		{
			Result.Add(Idx);
		}
	}
	return Result;
}

int32 USplineRoadGraph::InsertEdgeFromPoints(TArray<FVector>&& Points, FName GroupId)
{
	FEdge Edge;
	Edge.Points = MoveTemp(Points);
	BuildCumDist(Edge.Points, Edge.CumDist, Edge.Length);
	if (Edge.Length <= KINDA_SMALL_NUMBER)
	{
		return INDEX_NONE;
	}
	Edge.Bounds = PolylineBounds(Edge.Points);
	Edge.StartNode = FindOrCreateNode(Edge.Points[0]);
	Edge.EndNode = FindOrCreateNode(Edge.Points.Last());
	Edge.GroupId = GroupId;

	const int32 EdgeIdx = Edges.Add(Edge);
	Nodes[Edge.StartNode].Connections.Add({EdgeIdx, true});
	Nodes[Edge.EndNode].Connections.Add({EdgeIdx, false});
	if (!GroupId.IsNone())
	{
		GroupToEdges.Add(GroupId, EdgeIdx);
	}
	return EdgeIdx;
}

void USplineRoadGraph::RemoveEdge(int32 EdgeIdx)
{
	if (!Edges.IsValidIndex(EdgeIdx))
	{
		return;
	}
	const FEdge Edge = Edges[EdgeIdx];
	if (Nodes.IsValidIndex(Edge.StartNode))
	{
		DetachEdgeFromNode(Edge.StartNode, EdgeIdx);
	}
	if (Nodes.IsValidIndex(Edge.EndNode) && Edge.EndNode != Edge.StartNode)
	{
		DetachEdgeFromNode(Edge.EndNode, EdgeIdx);
	}
	if (!Edge.GroupId.IsNone())
	{
		GroupToEdges.RemoveSingle(Edge.GroupId, EdgeIdx);
	}
	Edges.RemoveAt(EdgeIdx);
}

int32 USplineRoadGraph::RemoveGroup(FName GroupId)
{
	if (GroupId.IsNone())
	{
		return 0;
	}
	TArray<int32> ToRemove;
	GroupToEdges.MultiFind(GroupId, ToRemove);
	for (int32 EdgeIdx : ToRemove)
	{
		// RemoveEdge also removes from GroupToEdges, but iterating a snapshot is safe.
		RemoveEdge(EdgeIdx);
	}
	GroupToEdges.Remove(GroupId);
	return ToRemove.Num();
}

void USplineRoadGraph::ResetAll()
{
	Edges.Empty();
	Nodes.Empty();
	GroupToEdges.Empty();
}

void USplineRoadGraph::DetachEdgeFromNode(int32 NodeIdx, int32 EdgeIdx)
{
	if (!Nodes.IsValidIndex(NodeIdx))
	{
		return;
	}
	FNode& Node = Nodes[NodeIdx];
	for (int32 i = Node.Connections.Num() - 1; i >= 0; --i)
	{
		if (Node.Connections[i].EdgeIdx == EdgeIdx)
		{
			Node.Connections.RemoveAtSwap(i);
		}
	}
	if (Node.Connections.Num() == 0)
	{
		Nodes.RemoveAt(NodeIdx);
	}
}

int32 USplineRoadGraph::FindOrCreateNode(const FVector& Position)
{
	const float ThresholdSq = ConnectThreshold * ConnectThreshold;
	for (auto It = Nodes.CreateConstIterator(); It; ++It)
	{
		if (FVector::DistSquared(It->Position, Position) <= ThresholdSq)
		{
			return It.GetIndex();
		}
	}
	FNode Node;
	Node.Position = Position;
	return Nodes.Add(Node);
}

float USplineRoadGraph::GetEdgeLength(int32 EdgeIdx) const
{
	return Edges.IsValidIndex(EdgeIdx) ? Edges[EdgeIdx].Length : 0.f;
}

void USplineRoadGraph::SampleEdge(int32 EdgeIdx, float Distance, FVector& OutPosition, FVector& OutTangent) const
{
	OutPosition = FVector::ZeroVector;
	OutTangent = FVector::ForwardVector;
	if (!Edges.IsValidIndex(EdgeIdx))
	{
		return;
	}
	SampleAtDistance(Edges[EdgeIdx], Distance, OutPosition, OutTangent);
}

void USplineRoadGraph::SampleAtDistance(const FEdge& Edge, float Distance, FVector& OutPos, FVector& OutTangent)
{
	const float D = FMath::Clamp(Distance, 0.f, Edge.Length);
	// Locate segment via binary search on CumDist.
	int32 Lo = 0, Hi = Edge.CumDist.Num() - 1;
	while (Lo < Hi - 1)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (Edge.CumDist[Mid] <= D)
		{
			Lo = Mid;
		}
		else
		{
			Hi = Mid;
		}
	}
	const float SegLen = FMath::Max(KINDA_SMALL_NUMBER, Edge.CumDist[Hi] - Edge.CumDist[Lo]);
	const float Alpha = (D - Edge.CumDist[Lo]) / SegLen;
	OutPos = FMath::Lerp(Edge.Points[Lo], Edge.Points[Hi], Alpha);
	const FVector Dir = Edge.Points[Hi] - Edge.Points[Lo];
	OutTangent = Dir.GetSafeNormal();
	if (OutTangent.IsNearlyZero())
	{
		OutTangent = FVector::ForwardVector;
	}
}

TArray<FEagleEdgeEndpoint> USplineRoadGraph::GetConnectedEdges(int32 EdgeIdx, bool bAtStart) const
{
	TArray<FEagleEdgeEndpoint> Result;
	if (!Edges.IsValidIndex(EdgeIdx))
	{
		return Result;
	}
	const int32 NodeIdx = bAtStart ? Edges[EdgeIdx].StartNode : Edges[EdgeIdx].EndNode;
	if (!Nodes.IsValidIndex(NodeIdx))
	{
		return Result;
	}
	for (const FEagleEdgeEndpoint& Conn : Nodes[NodeIdx].Connections)
	{
		if (Conn.EdgeIdx != EdgeIdx)
		{
			Result.Add(Conn);
		}
	}
	return Result;
}

int32 USplineRoadGraph::GetRandomEdge(FRandomStream& Rng) const
{
	if (Edges.Num() == 0)
	{
		return INDEX_NONE;
	}
	// TSparseArray indices may have holes; sample by walking the iterator after
	// skipping a random count of allocated entries.
	const int32 Skip = Rng.RandRange(0, Edges.Num() - 1);
	int32 N = 0;
	for (auto It = Edges.CreateConstIterator(); It; ++It)
	{
		if (N++ == Skip)
		{
			return It.GetIndex();
		}
	}
	return INDEX_NONE;
}

TArray<int32> USplineRoadGraph::GetEdgesInBounds(const FBox& Bounds) const
{
	TArray<int32> Result;
	for (auto It = Edges.CreateConstIterator(); It; ++It)
	{
		if (Bounds.Intersect(It->Bounds))
		{
			Result.Add(It.GetIndex());
		}
	}
	return Result;
}

FBox USplineRoadGraph::GetEdgeBounds(int32 EdgeIdx) const
{
	return Edges.IsValidIndex(EdgeIdx) ? Edges[EdgeIdx].Bounds : FBox(ForceInit);
}
