// MockPolygonGenerator.cpp - C++ port of MockPolygonGenerator.as
// Generates fake FLandUsePolygon data with SavePackage persistence
#include "Runtime/MockPolygonGenerator.h"
#include "Data/LandUseMapDataAsset.h"
#include "GISProceduralModule.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

#if WITH_EDITOR
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif

AMockPolygonGenerator::AMockPolygonGenerator()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AMockPolygonGenerator::GenerateMockData()
{
	if (!DataAsset)
	{
		UE_LOG(LogGIS, Error, TEXT("[MockGen] No DataAsset assigned! Use 'Create & Save New DataAsset' first."));
		return;
	}

	UE_LOG(LogGIS, Log, TEXT("[MockGen] ===== GENERATING ====="));

	TempPolygons.Empty();
	NextID = 0;
	const FVector Center = GetActorLocation();

	// Generate polygons for each land use type
	AddPolygonsOfType(ELandUseType::Forest,      ForestCount,      Center, 0.0f, 0.8f, 1, 1,  0.0f);
	AddPolygonsOfType(ELandUseType::Residential,  ResidentialCount, Center, 0.4f, 0.3f, 2, 5,  8.0f);
	AddPolygonsOfType(ELandUseType::Commercial,   CommercialCount,  Center, 0.7f, 0.1f, 3, 12, 5.0f);
	AddPolygonsOfType(ELandUseType::Industrial,   IndustrialCount,  Center, 0.3f, 0.1f, 1, 3,  15.0f);
	AddPolygonsOfType(ELandUseType::Farmland,     FarmlandCount,    Center, 0.0f, 0.6f, 1, 1,  0.0f);
	AddPolygonsOfType(ELandUseType::OpenSpace,    OpenSpaceCount,   Center, 0.0f, 0.4f, 1, 1,  0.0f);

	UE_LOG(LogGIS, Log, TEXT("[MockGen] TempPolygons count: %d"), TempPolygons.Num());

	// Debug first polygon
	if (TempPolygons.Num() > 0)
	{
		const FLandUsePolygon& First = TempPolygons[0];
		UE_LOG(LogGIS, Log, TEXT("[MockGen] poly[0]: verts=%d type=%d area=%.1f"),
			First.WorldVertices.Num(), static_cast<int32>(First.LandUseType), First.AreaSqM);
	}

	// Assign to DataAsset
	DataAsset->Polygons = TempPolygons;
	DataAsset->SourceProvider = TEXT("MockPolygonGenerator");
	DataAsset->GeneratedTime = FDateTime::Now();
	DataAsset->BuildSpatialIndex();

	UE_LOG(LogGIS, Log, TEXT("[MockGen] DataAsset.Polygons count: %d"), DataAsset->Polygons.Num());

	// Save to disk
	if (SaveDataAssetToDisk(DataAsset))
	{
		UE_LOG(LogGIS, Log, TEXT("[MockGen] DataAsset saved to disk successfully."));
	}
	else
	{
		UE_LOG(LogGIS, Warning, TEXT("[MockGen] DataAsset save to disk failed (in-memory only)."));
	}

	if (bDrawDebugPolygons)
	{
		DrawAllPolygons();
	}

	UE_LOG(LogGIS, Log, TEXT("[MockGen] ===== DONE: %d polygons ====="), DataAsset->Polygons.Num());
}

void AMockPolygonGenerator::CreateAndSaveNewDataAsset()
{
#if WITH_EDITOR
	const FString PackagePath = SavePath;
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogGIS, Error, TEXT("[MockGen] Failed to create package: %s"), *PackagePath);
		return;
	}

	ULandUseMapDataAsset* NewAsset = NewObject<ULandUseMapDataAsset>(
		Package,
		ULandUseMapDataAsset::StaticClass(),
		FName(TEXT("MockLandUseMap")),
		RF_Public | RF_Standalone);

	if (!NewAsset)
	{
		UE_LOG(LogGIS, Error, TEXT("[MockGen] Failed to create DataAsset object."));
		return;
	}

	// Save immediately
	if (SaveDataAssetToDisk(NewAsset))
	{
		DataAsset = NewAsset;
		UE_LOG(LogGIS, Log, TEXT("[MockGen] Created & saved new DataAsset: %s"), *PackagePath);
	}
	else
	{
		UE_LOG(LogGIS, Error, TEXT("[MockGen] Created DataAsset but failed to save."));
		DataAsset = NewAsset; // Still assign for in-memory use
	}
#else
	UE_LOG(LogGIS, Error, TEXT("[MockGen] CreateAndSaveNewDataAsset is editor-only."));
#endif
}

void AMockPolygonGenerator::DrawPolygons()
{
	if (!DataAsset || DataAsset->Polygons.Num() == 0)
	{
		UE_LOG(LogGIS, Warning, TEXT("[MockGen] No data to draw."));
		return;
	}
	DrawAllPolygons();
}

void AMockPolygonGenerator::ClearData()
{
	if (DataAsset)
	{
		DataAsset->Polygons.Empty();
		DataAsset->ClearSpatialIndex();
	}
	TempPolygons.Empty();
	UE_LOG(LogGIS, Log, TEXT("[MockGen] Cleared."));
}

void AMockPolygonGenerator::AddPolygonsOfType(
	ELandUseType Type, int32 Count, const FVector& AreaCenter,
	float BuildDensity, float VegDensity,
	int32 MinFloors, int32 MaxFloors, float Setback)
{
	UWorld* World = GetWorld();

	for (int32 i = 0; i < Count; ++i)
	{
		const float PolyRadius = FMath::FRandRange(PolygonMinRadius, PolygonMaxRadius);

		// Try to find a non-overlapping position (rejection sampling)
		FVector PolyCenter = FVector::ZeroVector;
		bool bPlaced = false;
		constexpr int32 MaxAttempts = 50;

		for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
		{
			const float Angle = FMath::FRandRange(0.0f, 360.0f);
			const float Dist = FMath::FRandRange(AreaRadius * 0.1f, AreaRadius);
			const float Rad = FMath::DegreesToRadians(Angle);
			FVector Candidate = AreaCenter + FVector(
				FMath::Cos(Rad) * Dist,
				FMath::Sin(Rad) * Dist,
				0.0f);

			if (!OverlapsExisting(Candidate, PolyRadius))
			{
				PolyCenter = Candidate;
				bPlaced = true;
				break;
			}
		}

		if (!bPlaced)
		{
			UE_LOG(LogGIS, Warning, TEXT("[MockGen] Could not place poly %d (type=%d) without overlap after %d attempts, skipping."),
				NextID, static_cast<int32>(Type), MaxAttempts);
			continue;
		}

		// Ground trace to snap to terrain
		if (World)
		{
			const FVector TraceStart(PolyCenter.X, PolyCenter.Y, PolyCenter.Z + 100000.0f);
			const FVector TraceEnd(PolyCenter.X, PolyCenter.Y, PolyCenter.Z - 100000.0f);
			FHitResult HitResult;
			FCollisionQueryParams QueryParams;
			QueryParams.AddIgnoredActor(this);
			if (World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd,
				ECC_Visibility, QueryParams))
			{
				PolyCenter = HitResult.Location;
			}
		}
		const int32 NumVerts = FMath::RandRange(5, 10);
		TArray<FVector> Verts;
		Verts.Reserve(NumVerts);

		for (int32 v = 0; v < NumVerts; ++v)
		{
			const float VAngle = (static_cast<float>(v) / static_cast<float>(NumVerts)) * 360.0f;
			const float VRad = FMath::DegreesToRadians(VAngle);
			const float Jitter = FMath::FRandRange(0.7f, 1.3f);
			Verts.Add(PolyCenter + FVector(
				FMath::Cos(VRad) * PolyRadius * Jitter,
				FMath::Sin(VRad) * PolyRadius * Jitter,
				0.0f));
		}

		// Compute AABB
		FVector BMin = Verts[0];
		FVector BMax = Verts[0];
		for (int32 v = 1; v < Verts.Num(); ++v)
		{
			BMin.X = FMath::Min(BMin.X, Verts[v].X);
			BMin.Y = FMath::Min(BMin.Y, Verts[v].Y);
			BMin.Z = FMath::Min(BMin.Z, Verts[v].Z);
			BMax.X = FMath::Max(BMax.X, Verts[v].X);
			BMax.Y = FMath::Max(BMax.Y, Verts[v].Y);
			BMax.Z = FMath::Max(BMax.Z, Verts[v].Z);
		}

		// Approximate area (pi * r^2, radius in meters = radius/100)
		const float AreaSqM = PI * (PolyRadius / 100.0f) * (PolyRadius / 100.0f);

		// Build polygon struct
		FLandUsePolygon Poly;
		Poly.PolygonID = NextID;
		Poly.TileCoord = FIntPoint(0, 0);
		Poly.LandUseType = Type;
		Poly.WorldVertices = MoveTemp(Verts);
		Poly.AreaSqM = AreaSqM;
		Poly.WorldCenter = PolyCenter;
		Poly.WorldBounds = FBox(BMin, BMax);
		Poly.AvgElevation = PolyCenter.Z / 100.0f;
		Poly.AvgSlope = FMath::FRandRange(0.0f, 15.0f);
		Poly.bAdjacentToMainRoad = (FMath::RandRange(0, 3) == 0);
		Poly.BuildingDensity = FMath::Clamp(BuildDensity + FMath::FRandRange(-0.1f, 0.1f), 0.0f, 1.0f);
		Poly.VegetationDensity = FMath::Clamp(VegDensity + FMath::FRandRange(-0.1f, 0.1f), 0.0f, 1.0f);
		Poly.MinFloors = MinFloors;
		Poly.MaxFloors = MaxFloors;
		Poly.BuildingSetback = Setback;

		TempPolygons.Add(MoveTemp(Poly));
		NextID++;
	}
}

void AMockPolygonGenerator::DrawAllPolygons()
{
	const UWorld* World = GetWorld();
	if (!World || !DataAsset) return;

	for (int32 i = 0; i < DataAsset->Polygons.Num(); ++i)
	{
		const FLandUsePolygon& Poly = DataAsset->Polygons[i];
		const FColor Color = GetColorForType(Poly.LandUseType).ToFColor(true);
		const int32 VertCount = Poly.WorldVertices.Num();

		// Draw polygon edges with thick lines
		for (int32 v = 0; v < VertCount; ++v)
		{
			const int32 Next = (v + 1) % VertCount;
			const FVector From = Poly.WorldVertices[v] + FVector(0, 0, 50);
			const FVector To = Poly.WorldVertices[Next] + FVector(0, 0, 50);
			DrawDebugLine(World, From, To, Color, false, DebugDrawDuration, 0, DebugLineThickness);
		}

		// Draw center point
		DrawDebugPoint(World, Poly.WorldCenter + FVector(0, 0, 100), 15.0f, Color, false, DebugDrawDuration);

		// Draw text label: "[index] TypeName"
		const FString Label = FString::Printf(TEXT("[%d] %s"), i, *GetTypeDisplayName(Poly.LandUseType));
		DrawDebugString(World, Poly.WorldCenter + FVector(0, 0, 200), Label, nullptr,
			Color, DebugDrawDuration, false, DebugTextScale);
	}
}

FLinearColor AMockPolygonGenerator::GetColorForType(ELandUseType Type)
{
	switch (Type)
	{
	case ELandUseType::Forest:      return FLinearColor(0.1f, 0.8f, 0.1f, 1.0f);
	case ELandUseType::Residential: return FLinearColor(0.9f, 0.7f, 0.2f, 1.0f);
	case ELandUseType::Commercial:  return FLinearColor(0.2f, 0.4f, 0.9f, 1.0f);
	case ELandUseType::Industrial:  return FLinearColor(0.6f, 0.3f, 0.6f, 1.0f);
	case ELandUseType::Farmland:    return FLinearColor(0.8f, 0.8f, 0.2f, 1.0f);
	case ELandUseType::OpenSpace:   return FLinearColor(0.5f, 0.9f, 0.5f, 1.0f);
	case ELandUseType::Water:       return FLinearColor(0.1f, 0.3f, 0.9f, 1.0f);
	case ELandUseType::Road:        return FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
	case ELandUseType::Military:    return FLinearColor(0.9f, 0.1f, 0.1f, 1.0f);
	default:                        return FLinearColor::White;
	}
}

FString AMockPolygonGenerator::GetTypeDisplayName(ELandUseType Type)
{
	switch (Type)
	{
	case ELandUseType::Forest:      return TEXT("Forest");
	case ELandUseType::Residential: return TEXT("Residential");
	case ELandUseType::Commercial:  return TEXT("Commercial");
	case ELandUseType::Industrial:  return TEXT("Industrial");
	case ELandUseType::Farmland:    return TEXT("Farmland");
	case ELandUseType::OpenSpace:   return TEXT("OpenSpace");
	case ELandUseType::Water:       return TEXT("Water");
	case ELandUseType::Road:        return TEXT("Road");
	case ELandUseType::Military:    return TEXT("Military");
	default:                        return TEXT("Unknown");
	}
}

bool AMockPolygonGenerator::OverlapsExisting(const FVector& Center, float Radius) const
{
	for (const FLandUsePolygon& Existing : TempPolygons)
	{
		// 2D distance check (ignore Z)
		const float Dist2D = FVector::Dist2D(Center, Existing.WorldCenter);
		// Estimate existing polygon's radius from its area: r = sqrt(area_cm2 / pi)
		// AreaSqM is in m^2, WorldVertices are in cm, so convert: area_cm2 = AreaSqM * 10000
		const float ExistingRadius = FMath::Sqrt(Existing.AreaSqM * 10000.0f / PI);
		if (Dist2D < (Radius + ExistingRadius) * 0.85f)
		{
			return true;
		}
	}
	return false;
}

bool AMockPolygonGenerator::SaveDataAssetToDisk(ULandUseMapDataAsset* Asset)
{
#if WITH_EDITOR
	if (!Asset)
	{
		return false;
	}

	UPackage* Package = Asset->GetOutermost();
	if (!Package)
	{
		UE_LOG(LogGIS, Error, TEXT("[MockGen] Asset has no outer package."));
		return false;
	}

	// Register with asset registry and mark dirty
	FAssetRegistryModule::AssetCreated(Asset);
	Asset->MarkPackageDirty();

	// Resolve file path from package name
	const FString PackageFilePath = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const bool bSaved = UPackage::SavePackage(Package, Asset, *PackageFilePath, SaveArgs);

	if (bSaved)
	{
		UE_LOG(LogGIS, Log, TEXT("[MockGen] Saved: %s"), *PackageFilePath);
	}
	else
	{
		UE_LOG(LogGIS, Error, TEXT("[MockGen] Save failed: %s"), *PackageFilePath);
	}

	return bSaved;
#else
	UE_LOG(LogGIS, Warning, TEXT("[MockGen] SavePackage is editor-only."));
	return false;
#endif
}
