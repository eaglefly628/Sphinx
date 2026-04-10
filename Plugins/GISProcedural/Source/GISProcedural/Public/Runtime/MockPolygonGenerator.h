// MockPolygonGenerator.h - C++ port of MockPolygonGenerator.as
// Generates fake FLandUsePolygon data for validating Polygon -> PCG pipeline
// Supports SavePackage persistence (unlike AngelScript version)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Polygon/LandUsePolygon.h"
#include "MockPolygonGenerator.generated.h"

class ULandUseMapDataAsset;

/**
 * Mock Polygon Generator (Editor Tool)
 *
 * Generates fake LandUsePolygon data around the Actor's location,
 * writes to a ULandUseMapDataAsset, and persists to disk via SavePackage.
 *
 * Usage:
 *   1. Place in level, assign a DataAsset (or use "Create & Save New DataAsset")
 *   2. Click "Generate Mock Data" in Details panel
 *   3. DataAsset is saved to Content/GISData/MockLandUseMap.uasset
 *   4. PCG Graph references this DataAsset -> PCGGISNode samples it
 */
UCLASS(BlueprintType, Blueprintable)
class GISPROCEDURAL_API AMockPolygonGenerator : public AActor
{
	GENERATED_BODY()

public:
	AMockPolygonGenerator();

	// ======== Area Config ========

	UPROPERTY(EditAnywhere, Category = "Mock Data|Area")
	float AreaRadius = 50000.0f;

	UPROPERTY(EditAnywhere, Category = "Mock Data|Area")
	float PolygonMinRadius = 2000.0f;

	UPROPERTY(EditAnywhere, Category = "Mock Data|Area")
	float PolygonMaxRadius = 8000.0f;

	// ======== Count Config ========

	UPROPERTY(EditAnywhere, Category = "Mock Data|Counts")
	int32 ForestCount = 8;

	UPROPERTY(EditAnywhere, Category = "Mock Data|Counts")
	int32 ResidentialCount = 5;

	UPROPERTY(EditAnywhere, Category = "Mock Data|Counts")
	int32 CommercialCount = 3;

	UPROPERTY(EditAnywhere, Category = "Mock Data|Counts")
	int32 IndustrialCount = 2;

	UPROPERTY(EditAnywhere, Category = "Mock Data|Counts")
	int32 FarmlandCount = 3;

	UPROPERTY(EditAnywhere, Category = "Mock Data|Counts")
	int32 OpenSpaceCount = 2;

	// ======== Output ========

	UPROPERTY(EditAnywhere, Category = "Mock Data|Output")
	ULandUseMapDataAsset* DataAsset = nullptr;

	/** Save path under /Game/ (without extension) */
	UPROPERTY(EditAnywhere, Category = "Mock Data|Output")
	FString SavePath = TEXT("/Game/GISData/MockLandUseMap");

	// ======== Debug ========

	UPROPERTY(EditAnywhere, Category = "Mock Data|Debug")
	bool bDrawDebugPolygons = true;

	UPROPERTY(EditAnywhere, Category = "Mock Data|Debug")
	float DebugDrawDuration = 30.0f;

	// ======== Editor Buttons ========

	/** Generate mock polygons and save DataAsset to disk */
	UFUNCTION(CallInEditor, Category = "Mock Data")
	void GenerateMockData();

	/** Create a new DataAsset and save it (if no DataAsset assigned) */
	UFUNCTION(CallInEditor, Category = "Mock Data")
	void CreateAndSaveNewDataAsset();

	/** Draw debug visualization of all polygons */
	UFUNCTION(CallInEditor, Category = "Mock Data")
	void DrawPolygons();

	/** Clear all polygon data */
	UFUNCTION(CallInEditor, Category = "Mock Data")
	void ClearData();

private:
	/** Temporary polygon storage during generation */
	TArray<FLandUsePolygon> TempPolygons;

	/** Auto-increment polygon ID */
	int32 NextID = 0;

	/** Generate polygons of a specific type */
	void AddPolygonsOfType(
		ELandUseType Type, int32 Count, const FVector& AreaCenter,
		float BuildDensity, float VegDensity,
		int32 MinFloors, int32 MaxFloors, float Setback);

	/** Draw all polygons with debug lines */
	void DrawAllPolygons();

	/** Get color for land use type */
	static FLinearColor GetColorForType(ELandUseType Type);

	/** Save DataAsset package to disk (editor only) */
	bool SaveDataAssetToDisk(ULandUseMapDataAsset* Asset);
};
