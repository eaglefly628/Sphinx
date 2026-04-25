#include "EagleTrafficManager.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EagleRoadGraph.h"
#include "EagleWalkMassAI.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AEagleTrafficManager::AEagleTrafficManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Mesh = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("VehicleHISM"));
	RootComponent = Mesh;
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(true);
	Mesh->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		VehicleMesh = CubeFinder.Object;
	}
}

void AEagleTrafficManager::BeginPlay()
{
	Super::BeginPlay();

	Rng.Initialize(FDateTime::Now().GetTicks());

	if (VehicleMesh && Mesh)
	{
		Mesh->SetStaticMesh(VehicleMesh);
	}

	if (DespawnRadius < SpawnRadius)
	{
		DespawnRadius = SpawnRadius * 1.2f;
	}

	UE_LOG(LogEagleMassAI, Log, TEXT("===== EagleTrafficManager BeginPlay ====="));
	UE_LOG(LogEagleMassAI, Log, TEXT("RoadGraph=%s MaxAgents=%d Spawn=%.0f Despawn=%.0f"),
		*GetNameSafe(RoadGraph), MaxAgents, SpawnRadius, DespawnRadius);
}

void AEagleTrafficManager::SetRoadGraph(UEagleRoadGraph* InGraph)
{
	RoadGraph = InGraph;
	ClearAgents();
}

void AEagleTrafficManager::ClearAgents()
{
	Agents.Reset();
	if (Mesh)
	{
		Mesh->ClearInstances();
	}
}

void AEagleTrafficManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!RoadGraph || RoadGraph->GetEdgeCount() == 0 || !Mesh || !VehicleMesh)
	{
		return;
	}

	const FVector Focus = GetFocusLocation();
	const float DespawnSq = DespawnRadius * DespawnRadius;

	// Step + cull pass. Iterate backwards so RemoveAgentAt doesn't disturb indexing.
	for (int32 i = Agents.Num() - 1; i >= 0; --i)
	{
		StepAgent(Agents[i], DeltaTime);

		FVector Pos, Tan;
		RoadGraph->SampleEdge(Agents[i].EdgeIdx, Agents[i].DistanceAlongEdge, Pos, Tan);
		if (FVector::DistSquared(Pos, Focus) > DespawnSq)
		{
			RemoveAgentAt(i);
			continue;
		}
		RefreshInstance(Agents[i]);
	}

	// Spawn fill. Bounded per-tick to avoid hitches when warming up.
	int32 Attempts = 0;
	while (Agents.Num() < MaxAgents && Attempts < MaxSpawnAttemptsPerTick)
	{
		++Attempts;
		if (!TrySpawnOne(Focus))
		{
			// no valid spot this attempt; keep trying within budget
		}
	}

	if (Mesh->GetInstanceCount() > 0)
	{
		Mesh->MarkRenderStateDirty();
	}
}

FVector AEagleTrafficManager::GetFocusLocation() const
{
	if (FocusActor.IsValid())
	{
		return FocusActor->GetActorLocation();
	}
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			FVector Loc;
			FRotator Rot;
			PC->GetPlayerViewPoint(Loc, Rot);
			return Loc;
		}
	}
	return GetActorLocation();
}

bool AEagleTrafficManager::TrySpawnOne(const FVector& Focus)
{
	const FBox SpawnBox(Focus - FVector(SpawnRadius), Focus + FVector(SpawnRadius));
	const TArray<int32> Candidates = RoadGraph->GetEdgesInBounds(SpawnBox);
	if (Candidates.Num() == 0)
	{
		return false;
	}

	const int32 EdgeIdx = Candidates[Rng.RandRange(0, Candidates.Num() - 1)];
	const float Length = RoadGraph->GetEdgeLength(EdgeIdx);
	if (Length <= 0.f)
	{
		return false;
	}

	FEagleVehicleAgent Agent;
	Agent.EdgeIdx = EdgeIdx;
	Agent.DistanceAlongEdge = Rng.FRandRange(0.f, Length);
	Agent.Speed = FMath::Max(50.f, DefaultSpeed + Rng.FRandRange(-SpeedJitter, SpeedJitter));
	Agent.YawOffsetDeg = 0.f;

	// Position check vs spawn radius (edge bbox could be huge).
	FVector Pos, Tan;
	RoadGraph->SampleEdge(EdgeIdx, Agent.DistanceAlongEdge, Pos, Tan);
	if (FVector::DistSquared(Pos, Focus) > SpawnRadius * SpawnRadius)
	{
		return false;
	}

	const FTransform Xform(
		FRotationMatrix::MakeFromX(Tan).Rotator() + FRotator(0.f, Agent.YawOffsetDeg, 0.f),
		Pos + FVector(0, 0, MeshZOffset),
		FVector::OneVector);
	Agent.InstanceIdx = Mesh->AddInstance(Xform, /*bWorldSpace=*/true);
	Agents.Add(Agent);
	return true;
}

void AEagleTrafficManager::StepAgent(FEagleVehicleAgent& Agent, float DeltaTime)
{
	Agent.DistanceAlongEdge += Agent.Speed * DeltaTime;

	float Length = RoadGraph->GetEdgeLength(Agent.EdgeIdx);
	while (Agent.DistanceAlongEdge >= Length && Length > 0.f)
	{
		const float Overshoot = Agent.DistanceAlongEdge - Length;
		// At the end node, pick a random connected edge that isn't this one.
		const TArray<FEagleEdgeEndpoint> Conns = RoadGraph->GetConnectedEdges(Agent.EdgeIdx, /*bAtStart=*/false);
		if (Conns.Num() == 0)
		{
			// dead end: U-turn on the same edge
			Agent.DistanceAlongEdge = FMath::Max(0.f, Length - Overshoot);
			Agent.YawOffsetDeg = (Agent.YawOffsetDeg == 0.f) ? 180.f : 0.f;
			return;
		}
		const FEagleEdgeEndpoint Next = Conns[Rng.RandRange(0, Conns.Num() - 1)];
		Agent.EdgeIdx = Next.EdgeIdx;
		const float NextLength = RoadGraph->GetEdgeLength(Agent.EdgeIdx);
		// If we entered the new edge from its end, walk backwards (distance from end = overshoot).
		Agent.DistanceAlongEdge = Next.bAtStart ? Overshoot : FMath::Max(0.f, NextLength - Overshoot);
		Length = NextLength;
		// Flip facing if we're going against the spline direction.
		Agent.YawOffsetDeg = Next.bAtStart ? 0.f : 180.f;
	}
}

void AEagleTrafficManager::RefreshInstance(FEagleVehicleAgent& Agent)
{
	if (!Mesh || Agent.InstanceIdx == INDEX_NONE)
	{
		return;
	}
	FVector Pos, Tan;
	RoadGraph->SampleEdge(Agent.EdgeIdx, Agent.DistanceAlongEdge, Pos, Tan);
	const FTransform Xform(
		FRotationMatrix::MakeFromX(Tan).Rotator() + FRotator(0.f, Agent.YawOffsetDeg, 0.f),
		Pos + FVector(0, 0, MeshZOffset),
		FVector::OneVector);
	Mesh->UpdateInstanceTransform(Agent.InstanceIdx, Xform, /*bWorldSpace=*/true,
		/*bMarkRenderStateDirty=*/false, /*bTeleport=*/true);
}

void AEagleTrafficManager::RemoveAgentAt(int32 AgentIdx)
{
	if (!Agents.IsValidIndex(AgentIdx))
	{
		return;
	}
	const int32 InstIdx = Agents[AgentIdx].InstanceIdx;
	if (Mesh && InstIdx != INDEX_NONE)
	{
		// HISM RemoveInstance reorders the last instance into the removed slot.
		// Patch any agent that referenced the moved instance.
		const int32 LastInst = Mesh->GetInstanceCount() - 1;
		Mesh->RemoveInstance(InstIdx);
		if (InstIdx != LastInst)
		{
			for (FEagleVehicleAgent& Other : Agents)
			{
				if (Other.InstanceIdx == LastInst)
				{
					Other.InstanceIdx = InstIdx;
					break;
				}
			}
		}
	}
	Agents.RemoveAtSwap(AgentIdx, 1, EAllowShrinking::No);
}
