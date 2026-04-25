#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EagleTrafficManager.generated.h"

class UEagleRoadGraph;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * One agent traversing the road graph. POD; lives in TrafficManager.Agents.
 */
USTRUCT()
struct EAGLEWALKMASSAI_API FEagleVehicleAgent
{
	GENERATED_BODY()

	/** Current edge in the road graph. */
	int32 EdgeIdx = INDEX_NONE;

	/** Distance from the start of EdgeIdx in cm. */
	float DistanceAlongEdge = 0.f;

	/** cm/s along the edge. */
	float Speed = 800.f;

	/** Index in the HISM. INDEX_NONE while not visualized. */
	int32 InstanceIdx = INDEX_NONE;

	/** Per-agent yaw offset, useful if you ever want lane offset / wobble. */
	float YawOffsetDeg = 0.f;
};

/**
 * Lightweight traffic system for streamed road networks.
 *
 * - Pull a UEagleRoadGraph (any subclass) at BeginPlay or call SetRoadGraph().
 * - Each tick: advance every agent along its edge; pick a random connected edge
 *   at junctions; rebatch HISM transforms.
 * - Spawn density follows a focus actor (player camera by default). Agents
 *   outside DespawnRadius are recycled.
 *
 * Tested target: ~500 agents, 1 draw call, sub-millisecond tick.
 */
UCLASS(Blueprintable)
class EAGLEWALKMASSAI_API AEagleTrafficManager : public AActor
{
	GENERATED_BODY()

public:
	AEagleTrafficManager();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eagle MassAI")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Mesh;

	/** Vehicle mesh. Set in BP/details; cube placeholder if null. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eagle MassAI")
	TObjectPtr<UStaticMesh> VehicleMesh;

	/** Road graph. Subclass UEagleRoadGraph or use USplineRoadGraph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eagle MassAI",
		meta = (DisplayName = "Road Graph"))
	TObjectPtr<UEagleRoadGraph> RoadGraph;

	/** Max concurrent agents. Hard cap, regardless of demand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eagle MassAI", meta = (ClampMin = "0"))
	int32 MaxAgents = 200;

	/** Spawn agents within this radius (cm) of the focus point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eagle MassAI", meta = (ClampMin = "100"))
	float SpawnRadius = 50000.f;

	/** Despawn agents past this radius (cm). Should be > SpawnRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eagle MassAI", meta = (ClampMin = "100"))
	float DespawnRadius = 80000.f;

	/** Default forward speed (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eagle MassAI", meta = (ClampMin = "0"))
	float DefaultSpeed = 800.f;

	/** Random ± noise applied to DefaultSpeed at spawn (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eagle MassAI", meta = (ClampMin = "0"))
	float SpeedJitter = 200.f;

	/** Vertical offset added to spawned mesh transforms (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eagle MassAI")
	float MeshZOffset = 0.f;

	/** Max attempts per spawn before giving up that tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eagle MassAI", meta = (ClampMin = "1"))
	int32 MaxSpawnAttemptsPerTick = 16;

	/** Optional override for the focus actor. Falls back to player camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eagle MassAI")
	TWeakObjectPtr<AActor> FocusActor;

	/** Replace the road graph at runtime. Wipes all agents. */
	UFUNCTION(BlueprintCallable, Category = "Eagle MassAI")
	void SetRoadGraph(UEagleRoadGraph* InGraph);

	/** Force-clear all agents and HISM instances. */
	UFUNCTION(BlueprintCallable, Category = "Eagle MassAI")
	void ClearAgents();

	UFUNCTION(BlueprintCallable, Category = "Eagle MassAI")
	int32 GetAgentCount() const { return Agents.Num(); }

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

protected:
	UPROPERTY()
	TArray<FEagleVehicleAgent> Agents;

	FRandomStream Rng;

	FVector GetFocusLocation() const;

	/** Try to add one new agent on a random edge near the focus. */
	bool TrySpawnOne(const FVector& Focus);

	/** Advance an agent; on edge end, hop to a random connected edge. */
	void StepAgent(FEagleVehicleAgent& Agent, float DeltaTime);

	/** Refresh HISM transform for one agent. */
	void RefreshInstance(FEagleVehicleAgent& Agent);

	/** Remove agent at array index, fixup HISM. */
	void RemoveAgentAt(int32 AgentIdx);
};
