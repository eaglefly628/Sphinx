#pragma once

#include "CoreMinimal.h"
#include "EagleRoadGraph.h"
#include "SplineRoadGraph.generated.h"

class USplineComponent;

/**
 * Road graph backed by a flat list of USplineComponent. Each spline is one edge.
 * Junctions are inferred by snapping spline endpoints within ConnectThreshold cm.
 *
 * Use this for prototyping or any project that has roads as splines. Call
 * BuildFromSplines() once after the splines are loaded; rebuild on streaming.
 */
UCLASS(BlueprintType)
class EAGLEWALKMASSAI_API USplineRoadGraph : public UEagleRoadGraph
{
	GENERATED_BODY()

public:
	/** Snap distance for treating two spline endpoints as the same junction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eagle MassAI")
	float ConnectThreshold = 200.f;

	/**
	 * Build the graph from a list of splines. Stores weak pointers so callers
	 * are free to destroy the splines later (rebuild required).
	 */
	UFUNCTION(BlueprintCallable, Category = "Eagle MassAI")
	void BuildFromSplines(const TArray<USplineComponent*>& InSplines);

	UFUNCTION(BlueprintCallable, Category = "Eagle MassAI")
	void Reset();

	// UEagleRoadGraph
	virtual int32 GetEdgeCount() const override { return Edges.Num(); }
	virtual float GetEdgeLength(int32 EdgeIdx) const override;
	virtual void SampleEdge(int32 EdgeIdx, float Distance, FVector& OutPosition, FVector& OutTangent) const override;
	virtual TArray<FEagleEdgeEndpoint> GetConnectedEdges(int32 EdgeIdx, bool bAtStart) const override;
	virtual int32 GetRandomEdge(FRandomStream& Rng) const override;
	virtual TArray<int32> GetEdgesInBounds(const FBox& Bounds) const override;
	virtual FBox GetEdgeBounds(int32 EdgeIdx) const override;

private:
	struct FEdge
	{
		TWeakObjectPtr<USplineComponent> Spline;
		float Length = 0.f;
		int32 StartNode = INDEX_NONE;
		int32 EndNode = INDEX_NONE;
		FBox Bounds = FBox(ForceInit);
	};

	struct FNode
	{
		FVector Position = FVector::ZeroVector;
		// Each connected edge stores (EdgeIdx, bAtStart=true if this node is at edge start)
		TArray<FEagleEdgeEndpoint> Connections;
	};

	TArray<FEdge> Edges;
	TArray<FNode> Nodes;

	int32 FindOrCreateNode(const FVector& Position);
};
