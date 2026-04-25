#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "EagleRoadGraph.generated.h"

/**
 * A directed reference to one end of an edge.
 * EdgeIdx + bAtStart fully identifies "which junction" the vehicle is heading into.
 */
USTRUCT(BlueprintType)
struct EAGLEWALKMASSAI_API FEagleEdgeEndpoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int32 EdgeIdx = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bAtStart = false;
};

/**
 * Abstract pluggable road graph.
 *
 * Implement this to feed AEagleTrafficManager from any road source:
 *  - bag of USplineComponents (USplineRoadGraph, default impl)
 *  - GIS road network (write a subclass that wraps your data)
 *  - hand-authored arrays
 *
 * Coordinates are world-space cm. Edge indices are stable until the graph is
 * rebuilt; prefer rebuilding the whole graph on streaming events rather than
 * incremental edits in v0.
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class EAGLEWALKMASSAI_API UEagleRoadGraph : public UObject
{
	GENERATED_BODY()

public:
	/** Total edges currently in the graph. */
	virtual int32 GetEdgeCount() const { return 0; }

	/** Length of an edge in cm. Returns 0 for invalid index. */
	virtual float GetEdgeLength(int32 EdgeIdx) const { return 0.f; }

	/**
	 * Sample a point along an edge.
	 * @param EdgeIdx       edge index
	 * @param Distance      distance from start in cm, clamped to [0, length]
	 * @param OutPosition   world position
	 * @param OutTangent    unit tangent in the start->end direction
	 */
	virtual void SampleEdge(int32 EdgeIdx, float Distance, FVector& OutPosition, FVector& OutTangent) const {}

	/**
	 * Edges connected at the given endpoint of EdgeIdx, excluding the edge
	 * itself. Each result includes a bAtStart flag indicating which end of the
	 * neighbor is at the junction (so the caller knows which way to traverse).
	 */
	virtual TArray<FEagleEdgeEndpoint> GetConnectedEdges(int32 EdgeIdx, bool bAtStart) const { return {}; }

	/** Pick any edge index uniformly at random, or INDEX_NONE if empty. */
	virtual int32 GetRandomEdge(FRandomStream& Rng) const { return INDEX_NONE; }

	/**
	 * Edges whose AABB overlaps Bounds. Used by spawning/despawning.
	 * Default impl is O(N) brute force; override for spatial index.
	 */
	virtual TArray<int32> GetEdgesInBounds(const FBox& Bounds) const { return {}; }

	/** World-space AABB of an edge (cheap, used by GetEdgesInBounds default). */
	virtual FBox GetEdgeBounds(int32 EdgeIdx) const { return FBox(ForceInit); }
};
