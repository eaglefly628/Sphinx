// CesiumBridgeComponent.cpp - Cesium 桥接组件实现
#include "Runtime/CesiumBridgeComponent.h"
#include "GISProceduralModule.h"
#include "Misc/FileHelper.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

#if WITH_CESIUM
#include "CesiumGeoreference.h"
#endif

UCesiumBridgeComponent::UCesiumBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCesiumBridgeComponent::UpdateCachedProjection()
{
	constexpr double EarthRadius = 6371000.0;
	const double LatRad = FMath::DegreesToRadians(OriginLatitude);
	CachedMetersPerDegreeLon = (PI / 180.0) * EarthRadius * FMath::Cos(LatRad);
	CachedMetersPerDegreeLat = (PI / 180.0) * EarthRadius;
}

// ============ 坐标桥接 ============

FVector UCesiumBridgeComponent::LLHToUnreal(double Longitude, double Latitude, double Height) const
{
#if WITH_CESIUM
	if (AActor* GeoRefActor = CesiumGeoreferenceActor.Get())
	{
		if (ACesiumGeoreference* GeoRef = Cast<ACesiumGeoreference>(GeoRefActor))
		{
			return GeoRef->TransformLongitudeLatitudeHeightPositionToUnreal(
				FVector(Longitude, Latitude, Height));
		}
	}
#endif

	// Cesium 不可用时回退到简化 Mercator
	return MercatorLLHToUnreal(Longitude, Latitude, Height);
}

FVector2D UCesiumBridgeComponent::UnrealToLLH(const FVector& WorldPos) const
{
#if WITH_CESIUM
	if (AActor* GeoRefActor = CesiumGeoreferenceActor.Get())
	{
		if (ACesiumGeoreference* GeoRef = Cast<ACesiumGeoreference>(GeoRefActor))
		{
			FVector LLH = GeoRef->TransformUnrealPositionToLongitudeLatitudeHeight(WorldPos);
			return FVector2D(LLH.X, LLH.Y);
		}
	}
#endif

	return MercatorUnrealToLLH(WorldPos);
}

FVector UCesiumBridgeComponent::MercatorLLHToUnreal(double Lon, double Lat, double Height) const
{
	// 使用缓存的投影参数（避免每次调用重算 cos(lat)）
	const double X = (Lon - OriginLongitude) * CachedMetersPerDegreeLon * MetersToCm;
	const double Y = (Lat - OriginLatitude) * CachedMetersPerDegreeLat * MetersToCm;
	const double Z = Height * MetersToCm;

	return FVector(X, Y, Z);
}

FVector2D UCesiumBridgeComponent::MercatorUnrealToLLH(const FVector& WorldPos) const
{
	if (CachedMetersPerDegreeLon == 0.0 || CachedMetersPerDegreeLat == 0.0)
	{
		return FVector2D(OriginLongitude, OriginLatitude);
	}

	const double Lon = OriginLongitude + (WorldPos.X / MetersToCm) / CachedMetersPerDegreeLon;
	const double Lat = OriginLatitude + (WorldPos.Y / MetersToCm) / CachedMetersPerDegreeLat;

	return FVector2D(Lon, Lat);
}

// ============ 离线 DEM 高程缓存 ============

bool UCesiumBridgeComponent::LoadDEMCache(const FString& FilePath, FIntPoint TileCoord)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		UE_LOG(LogGIS, Warning, TEXT("CesiumBridge: Failed to load DEM cache: %s"), *FilePath);
		return false;
	}

	// 文件格式：
	// [int32 GridSize][float CellSizeM]
	// [double MinLon][double MinLat][double MaxLon][double MaxLat]
	// [float × GridSize²]
	constexpr int32 HeaderSize = sizeof(int32) + sizeof(float) + 4 * sizeof(double);
	if (FileData.Num() < HeaderSize)
	{
		UE_LOG(LogGIS, Error, TEXT("CesiumBridge: DEM cache file too small: %s (%d bytes)"), *FilePath, FileData.Num());
		return false;
	}

	const uint8* Ptr = FileData.GetData();

	FDEMTileCache Cache;
	FMemory::Memcpy(&Cache.GridSize, Ptr, sizeof(int32)); Ptr += sizeof(int32);
	FMemory::Memcpy(&Cache.CellSizeM, Ptr, sizeof(float)); Ptr += sizeof(float);
	FMemory::Memcpy(&Cache.GeoBounds.MinLon, Ptr, sizeof(double)); Ptr += sizeof(double);
	FMemory::Memcpy(&Cache.GeoBounds.MinLat, Ptr, sizeof(double)); Ptr += sizeof(double);
	FMemory::Memcpy(&Cache.GeoBounds.MaxLon, Ptr, sizeof(double)); Ptr += sizeof(double);
	FMemory::Memcpy(&Cache.GeoBounds.MaxLat, Ptr, sizeof(double)); Ptr += sizeof(double);

	const int32 ExpectedFloats = Cache.GridSize * Cache.GridSize;
	const int32 ExpectedBytes = HeaderSize + ExpectedFloats * sizeof(float);
	if (FileData.Num() < ExpectedBytes)
	{
		UE_LOG(LogGIS, Error, TEXT("CesiumBridge: DEM cache truncated: expected %d bytes, got %d"), ExpectedBytes, FileData.Num());
		return false;
	}

	Cache.ElevationGrid.SetNumUninitialized(ExpectedFloats);
	FMemory::Memcpy(Cache.ElevationGrid.GetData(), Ptr, ExpectedFloats * sizeof(float));

	const int32 LogGridSize = Cache.GridSize;
	const float LogCellSize = Cache.CellSizeM;
	DEMCache.Add(TileCoord, MoveTemp(Cache));

	UE_LOG(LogGIS, Log, TEXT("CesiumBridge: Loaded DEM cache tile (%d,%d) → %dx%d grid, %.1fm cell"),
		TileCoord.X, TileCoord.Y, LogGridSize, LogGridSize, LogCellSize);

	return true;
}

float UCesiumBridgeComponent::QueryElevation(double Longitude, double Latitude) const
{
	const FDEMTileCache* Cache = FindTileCache(Longitude, Latitude);
	if (!Cache)
	{
		return 0.0f;
	}

	// 归一化坐标 [0,1]
	const double NormX = (Longitude - Cache->GeoBounds.MinLon) /
		(Cache->GeoBounds.MaxLon - Cache->GeoBounds.MinLon);
	const double NormY = (Latitude - Cache->GeoBounds.MinLat) /
		(Cache->GeoBounds.MaxLat - Cache->GeoBounds.MinLat);

	return BilinearSample(*Cache, NormX, NormY);
}

bool UCesiumBridgeComponent::BuildElevationGrid(
	const FGeoRect& Bounds,
	float Resolution,
	TArray<float>& OutGrid,
	int32& OutWidth,
	int32& OutHeight) const
{
	if (DEMCache.Num() == 0)
	{
		return false;
	}

	// 计算输出网格尺寸
	constexpr double EarthRadius = 6371000.0;
	const double LatRad = FMath::DegreesToRadians((Bounds.MinLat + Bounds.MaxLat) * 0.5);
	const double MetersPerDegreeLon = (PI / 180.0) * EarthRadius * FMath::Cos(LatRad);
	const double MetersPerDegreeLat = (PI / 180.0) * EarthRadius;

	const double WidthM = (Bounds.MaxLon - Bounds.MinLon) * MetersPerDegreeLon;
	const double HeightM = (Bounds.MaxLat - Bounds.MinLat) * MetersPerDegreeLat;

	OutWidth = FMath::Max(2, FMath::CeilToInt32(WidthM / Resolution));
	OutHeight = FMath::Max(2, FMath::CeilToInt32(HeightM / Resolution));

	OutGrid.SetNumZeroed(OutWidth * OutHeight);

	bool bAnyHit = false;
	for (int32 Row = 0; Row < OutHeight; ++Row)
	{
		const double Lat = Bounds.MinLat + (Bounds.MaxLat - Bounds.MinLat) * Row / (OutHeight - 1);
		for (int32 Col = 0; Col < OutWidth; ++Col)
		{
			const double Lon = Bounds.MinLon + (Bounds.MaxLon - Bounds.MinLon) * Col / (OutWidth - 1);
			const float Elev = QueryElevation(Lon, Lat);
			OutGrid[Row * OutWidth + Col] = Elev;
			if (Elev != 0.0f) bAnyHit = true;
		}
	}

	if (bAnyHit)
	{
		UE_LOG(LogGIS, Log, TEXT("CesiumBridge: Built elevation grid %dx%d from DEM cache"), OutWidth, OutHeight);
	}

	return bAnyHit;
}

void UCesiumBridgeComponent::ClearDEMCache()
{
	const int32 Count = DEMCache.Num();
	DEMCache.Empty();
	UE_LOG(LogGIS, Verbose, TEXT("CesiumBridge: Cleared %d DEM cache tiles"), Count);
}

const FDEMTileCache* UCesiumBridgeComponent::FindTileCache(double Lon, double Lat) const
{
	// 快速路径：如果只有一个 tile 或 tile 数量少，直接遍历
	// BuildElevationGrid 的内循环会频繁调用此方法（O(W*H) 次）
	// 所以缓存上一次命中的 tile，大多数情况下连续查询在同一 tile 内
	static thread_local FIntPoint LastHitTile = FIntPoint(-1, -1);

	if (const FDEMTileCache* CachedHit = DEMCache.Find(LastHitTile))
	{
		if (CachedHit->IsValid() &&
			Lon >= CachedHit->GeoBounds.MinLon && Lon <= CachedHit->GeoBounds.MaxLon &&
			Lat >= CachedHit->GeoBounds.MinLat && Lat <= CachedHit->GeoBounds.MaxLat)
		{
			return CachedHit;
		}
	}

	for (const auto& Pair : DEMCache)
	{
		const FDEMTileCache& Cache = Pair.Value;
		if (Cache.IsValid() &&
			Lon >= Cache.GeoBounds.MinLon && Lon <= Cache.GeoBounds.MaxLon &&
			Lat >= Cache.GeoBounds.MinLat && Lat <= Cache.GeoBounds.MaxLat)
		{
			LastHitTile = Pair.Key;
			return &Cache;
		}
	}
	return nullptr;
}

float UCesiumBridgeComponent::BilinearSample(const FDEMTileCache& Cache, double NormX, double NormY)
{
	const double GridX = FMath::Clamp(NormX, 0.0, 1.0) * (Cache.GridSize - 1);
	const double GridY = FMath::Clamp(NormY, 0.0, 1.0) * (Cache.GridSize - 1);

	const int32 X0 = FMath::FloorToInt32(GridX);
	const int32 Y0 = FMath::FloorToInt32(GridY);
	const int32 X1 = FMath::Min(X0 + 1, Cache.GridSize - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, Cache.GridSize - 1);

	const float FracX = static_cast<float>(GridX - X0);
	const float FracY = static_cast<float>(GridY - Y0);

	const float V00 = Cache.ElevationGrid[Y0 * Cache.GridSize + X0];
	const float V10 = Cache.ElevationGrid[Y0 * Cache.GridSize + X1];
	const float V01 = Cache.ElevationGrid[Y1 * Cache.GridSize + X0];
	const float V11 = Cache.ElevationGrid[Y1 * Cache.GridSize + X1];

	return FMath::Lerp(
		FMath::Lerp(V00, V10, FracX),
		FMath::Lerp(V01, V11, FracX),
		FracY);
}

// ============ PCG LOD 距离控制 ============

EPCGDetailLevel UCesiumBridgeComponent::GetDetailLevel(const FVector& WorldPos) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return EPCGDetailLevel::Full;
	}

	const APlayerController* PC = World->GetFirstPlayerController();
	if (!PC || !PC->GetPawn())
	{
		return EPCGDetailLevel::Full;
	}

	const float Dist = FVector::Dist(PC->GetPawn()->GetActorLocation(), WorldPos);

	if (Dist <= FullDetailDistance)
	{
		return EPCGDetailLevel::Full;
	}
	if (Dist <= MediumDetailDistance)
	{
		return EPCGDetailLevel::Medium;
	}
	if (Dist <= LowDetailDistance)
	{
		return EPCGDetailLevel::Low;
	}

	return EPCGDetailLevel::Culled;
}

float UCesiumBridgeComponent::GetSamplingIntervalForLevel(EPCGDetailLevel Level) const
{
	switch (Level)
	{
		case EPCGDetailLevel::Full:   return FullDetailInterval;
		case EPCGDetailLevel::Medium: return MediumDetailInterval;
		case EPCGDetailLevel::Low:    return LowDetailInterval;
		case EPCGDetailLevel::Culled: return 0.0f;  // 不采样
		default:                      return FullDetailInterval;
	}
}
