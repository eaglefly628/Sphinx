#pragma once

#include "CoreMinimal.h"
#include "EagleRoadGraph.h"
#include "SplineRoadGraph.generated.h"

class USplineComponent;

/** Helper struct so AddPolylines can be exposed to BP (TArray<TArray<>> isn't BP-friendly). */
USTRUCT(BlueprintType)
struct EAGLEWALKMASSAI_API FEaglePolyline
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<FVector> Points;
};

/**
 * Road graph backed by polylines. Edges are added/removed dynamically as
 * tiles stream in/out. Indices are stable: removing edge N never invalidates
 * any other edge's index.
 *
 * Two ingestion paths:
 *  - AddPolyline(points): raw points, the cheap path. Use when tile data is
 *    plain polylines (GeoJSON, vector tiles, custom 3D tile metadata).
 *  - AddSpline(USplineComponent*): convenience wrapper that samples the
 *    spline; the component does NOT need to stay alive afterwards.
 *
 * Junction detection: endpoints within ConnectThreshold cm are merged into
 * one node. Newly added edges snap to existing nodes when in range.
 *
 * Group ids let you remove a whole tile's edges in one call:
 *   Graph->AddPolyline(Points, TileId);
 *   ...later...
 *   Graph->RemoveGroup(TileId);
 */
UCLASS(BlueprintType)
class EAGLEWALKMASSAI_API USplineRoadGraph : public UEagleRoadGraph
{
	GENERATED_BODY()

public:
	/** Snap distance for treating two polyline endpoints as the same junction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eagle MassAI")
	float ConnectThreshold = 200.f;

	/** Distance between sampled points when ingesting a USplineComponent (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eagle MassAI")
	float SplineSampleStep = 200.f;

	UFUNCTION(BlueprintCallable, Category = "Eagle MassAI")
	int32 AddPolyline(const TArray<FVector>& Points, FName GroupId);

	UFUNCTION(BlueprintCallable, Category = "Eagle MassAI")
	int32 AddSpline(USplineComponent* Spline, FName GroupId);

	/** Convenience: add many polylines under the same group. */
	UFUNCTION(BlueprintCallable, Category = "Eagle MassAI")
	TArray<int32> AddPolylines(const TArray<FEaglePolyline>& Polylines, FName GroupId);

	/** Remove a single edge. Cleans up node connections; orphan nodes stay. */
	UFUNCTION(BlueprintCallable, Category = "Eagle MassAI")
	void RemoveEdge(int32 EdgeIdx);

	/** Remove every edge added under GroupId. Typical use: tile unload. */
	UFUNCTION(BlueprintCallable, Category = "Eagle MassAI")
	int32 RemoveGroup(FName GroupId);

	UFUNCTION(BlueprintCallable, Category = "Eagle MassAI")
	void ResetAll();

	// UEagleRoadGraph
	virtual int32 GetEdgeCount() const override { return Edges.Num(); }
	virtual bool IsValidEdge(int32 EdgeIdx) const override { return Edges.IsValidIndex(EdgeIdx); }
	virtual float GetEdgeLength(int32 EdgeIdx) const override;
	virtual void SampleEdge(int32 EdgeIdx, float Distance, FVector& OutPosition, FVector& OutTangent) const override;
	virtual TArray<FEagleEdgeEndpoint> GetConnectedEdges(int32 EdgeIdx, bool bAtStart) const override;
	virtual int32 GetRandomEdge(FRandomStream& Rng) const override;
	virtual TArray<int32> GetEdgesInBounds(const FBox& Bounds) const override;
	virtual FBox GetEdgeBounds(int32 EdgeIdx) const override;

private:
	struct FEdge
	{
		// Sampled polyline points; at least 2.
		TArray<FVector> Points;
		// Cumulative distance from Points[0] to Points[i], same length as Points.
		TArray<float> CumDist;
		float Length = 0.f;
		FBox Bounds = FBox(ForceInit);
		int32 StartNode = INDEX_NONE;
		int32 EndNode = INDEX_NONE;
		FName GroupId = NAME_None;
	};

	struct FNode
	{
		FVector Position = FVector::ZeroVector;
		// Connections at this node. bAtStart=true means EdgeIdx's start sits here.
		TArray<FEagleEdgeEndpoint> Connections;
	};

	TSparseArray<FEdge> Edges;
	TSparseArray<FNode> Nodes;
	TMultiMap<FName, int32> GroupToEdges;

	int32 FindOrCreateNode(const FVector& Position);
	int32 InsertEdgeFromPoints(TArray<FVector>&& Points, FName GroupId);
	void DetachEdgeFromNode(int32 NodeIdx, int32 EdgeIdx);

	static void SampleAtDistance(const FEdge& Edge, float Distance, FVector& OutPos, FVector& OutTangent);
};
