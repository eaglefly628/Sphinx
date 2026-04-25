#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "EagleRoadGraph.generated.h"

/**
 * A directed reference to one end of an edge.
 * EdgeIdx + bAtStart fully identifies "which junction" the vehicle is heading
 * into. Indices are stable across edits when backed by a sparse store.
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
 * Designed for streamed worlds: edges can appear and disappear at runtime as
 * 3D tiles or vector tiles load/unload. Indices returned from add operations
 * MUST stay stable for the lifetime of the edge (use a sparse store), so that
 * agents can keep referring to them. Removed edges return invalid from
 * IsValidEdge() and the traffic manager will despawn affected agents.
 *
 * Coordinates are world-space cm.
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class EAGLEWALKMASSAI_API UEagleRoadGraph : public UObject
{
	GENERATED_BODY()

public:
	/** Total live edges. */
	virtual int32 GetEdgeCount() const { return 0; }

	/** False if the edge has been removed or never existed. */
	virtual bool IsValidEdge(int32 EdgeIdx) const { return false; }

	/** Length of an edge in cm. 0 if invalid. */
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
	 * itself. bAtStart in the returned endpoints describes which end of the
	 * neighbor sits at the junction (so caller knows which way to traverse).
	 */
	virtual TArray<FEagleEdgeEndpoint> GetConnectedEdges(int32 EdgeIdx, bool bAtStart) const { return {}; }

	/** Pick any valid edge index uniformly at random, or INDEX_NONE if empty. */
	virtual int32 GetRandomEdge(FRandomStream& Rng) const { return INDEX_NONE; }

	/**
	 * Edges whose AABB overlaps Bounds. Default impl is O(N); override for
	 * spatial index if needed.
	 */
	virtual TArray<int32> GetEdgesInBounds(const FBox& Bounds) const { return {}; }

	/** World-space AABB of an edge. */
	virtual FBox GetEdgeBounds(int32 EdgeIdx) const { return FBox(ForceInit); }
};
