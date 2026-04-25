#include "SplineRoadGraph.h"

#include "Components/SplineComponent.h"
#include "EagleWalkMassAI.h"

void USplineRoadGraph::BuildFromSplines(const TArray<USplineComponent*>& InSplines)
{
	Reset();

	for (USplineComponent* Spline : InSplines)
	{
		if (!IsValid(Spline) || Spline->GetNumberOfSplinePoints() < 2)
		{
			continue;
		}

		const float Length = Spline->GetSplineLength();
		if (Length <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector Start = Spline->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World);
		const FVector End = Spline->GetLocationAtDistanceAlongSpline(Length, ESplineCoordinateSpace::World);

		FEdge Edge;
		Edge.Spline = Spline;
		Edge.Length = Length;
		Edge.StartNode = FindOrCreateNode(Start);
		Edge.EndNode = FindOrCreateNode(End);
		Edge.Bounds = Spline->Bounds.GetBox();

		const int32 EdgeIdx = Edges.Add(Edge);
		Nodes[Edge.StartNode].Connections.Add({EdgeIdx, true});
		Nodes[Edge.EndNode].Connections.Add({EdgeIdx, false});
	}

	UE_LOG(LogEagleMassAI, Log, TEXT("SplineRoadGraph built: %d edges, %d nodes"), Edges.Num(), Nodes.Num());
}

void USplineRoadGraph::Reset()
{
	Edges.Reset();
	Nodes.Reset();
}

int32 USplineRoadGraph::FindOrCreateNode(const FVector& Position)
{
	const float ThresholdSq = ConnectThreshold * ConnectThreshold;
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		if (FVector::DistSquared(Nodes[i].Position, Position) <= ThresholdSq)
		{
			return i;
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
	const FEdge& Edge = Edges[EdgeIdx];
	const USplineComponent* Spline = Edge.Spline.Get();
	if (!Spline)
	{
		return;
	}

	const float D = FMath::Clamp(Distance, 0.f, Edge.Length);
	OutPosition = Spline->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
	OutTangent = Spline->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
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
	return Rng.RandRange(0, Edges.Num() - 1);
}

TArray<int32> USplineRoadGraph::GetEdgesInBounds(const FBox& Bounds) const
{
	TArray<int32> Result;
	for (int32 i = 0; i < Edges.Num(); ++i)
	{
		if (Bounds.Intersect(Edges[i].Bounds))
		{
			Result.Add(i);
		}
	}
	return Result;
}

FBox USplineRoadGraph::GetEdgeBounds(int32 EdgeIdx) const
{
	return Edges.IsValidIndex(EdgeIdx) ? Edges[EdgeIdx].Bounds : FBox(ForceInit);
}
