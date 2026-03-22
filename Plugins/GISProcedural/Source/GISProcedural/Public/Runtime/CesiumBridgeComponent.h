// CesiumBridgeComponent.h - Cesium ↔ GIS 桥接组件
// 坐标转换、离线 DEM 高程缓存、PCG LOD 距离控制
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/GeoRect.h"
#include "CesiumBridgeComponent.generated.h"

/** PCG 细节级别 */
UENUM(BlueprintType)
enum class EPCGDetailLevel : uint8
{
	Full     UMETA(DisplayName = "Full Detail"),
	Medium   UMETA(DisplayName = "Medium Detail"),
	Low      UMETA(DisplayName = "Low Detail"),
	Culled   UMETA(DisplayName = "Culled"),
};

/** 单个 tile 的高程缓存 */
USTRUCT()
struct FDEMTileCache
{
	GENERATED_BODY()

	/** 高程值网格（行优先，GridSize × GridSize） */
	TArray<float> ElevationGrid;

	/** 网格尺寸（正方形边长） */
	int32 GridSize = 0;

	/** 该 tile 对应的地理范围 */
	FGeoRect GeoBounds;

	/** 网格间距（米） */
	float CellSizeM = 10.0f;

	bool IsValid() const { return GridSize > 0 && ElevationGrid.Num() == GridSize * GridSize; }
};

/**
 * Cesium 桥接组件
 *
 * 职责：
 * 1. 坐标转换：LLH(经纬高) ↔ UE5 世界坐标（Cesium 模式委托 ACesiumGeoreference）
 * 2. 离线 DEM 高程缓存：预处理阶段生成的高程网格，运行时 O(1) 双线性插值查表
 * 3. PCG LOD 控制：根据相机距离决定 PCG 采样密度级别
 */
UCLASS(BlueprintType, ClassGroup = "GIS", meta = (BlueprintSpawnableComponent))
class GISPROCEDURAL_API UCesiumBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCesiumBridgeComponent();

	// ============ 坐标桥接 ============

	/**
	 * 经纬高 → UE5 世界坐标
	 * Cesium 模式下委托 CesiumGeoreference 做 ECEF 变换
	 * 无 Cesium 时回退到简化 Mercator
	 */
	UFUNCTION(BlueprintCallable, Category = "GIS|Cesium")
	FVector LLHToUnreal(double Longitude, double Latitude, double Height) const;

	/**
	 * UE5 世界坐标 → 经纬度
	 */
	UFUNCTION(BlueprintCallable, Category = "GIS|Cesium")
	FVector2D UnrealToLLH(const FVector& WorldPos) const;

	// ============ 离线 DEM 高程缓存 ============

	/**
	 * 从预处理的二进制文件加载高程缓存
	 * 文件格式：[int32 GridSize][float CellSizeM][double MinLon][double MinLat][double MaxLon][double MaxLat][float × GridSize²]
	 * @param FilePath 高程缓存文件路径
	 * @param TileCoord tile 坐标（用作缓存 key）
	 * @return 是否加载成功
	 */
	UFUNCTION(BlueprintCallable, Category = "GIS|Cesium")
	bool LoadDEMCache(const FString& FilePath, FIntPoint TileCoord);

	/**
	 * 查询指定经纬度的高程值（双线性插值）
	 * @return 高程（米），未命中缓存返回 0.0
	 */
	UFUNCTION(BlueprintCallable, Category = "GIS|Cesium")
	float QueryElevation(double Longitude, double Latitude) const;

	/**
	 * 从缓存生成高程网格（供 TerrainAnalyzer 使用）
	 * 代替 DataProvider::QueryElevation 的运行时 Line Trace
	 */
	bool BuildElevationGrid(
		const FGeoRect& Bounds,
		float Resolution,
		TArray<float>& OutGrid,
		int32& OutWidth,
		int32& OutHeight) const;

	/** 清空所有 DEM 缓存 */
	UFUNCTION(BlueprintCallable, Category = "GIS|Cesium")
	void ClearDEMCache();

	/** 当前缓存的 tile 数量 */
	UFUNCTION(BlueprintPure, Category = "GIS|Cesium")
	int32 GetCachedTileCount() const { return DEMCache.Num(); }

	// ============ PCG LOD 距离控制 ============

	/**
	 * 根据世界坐标到相机的距离，返回 PCG 细节级别
	 */
	UFUNCTION(BlueprintCallable, Category = "GIS|Cesium")
	EPCGDetailLevel GetDetailLevel(const FVector& WorldPos) const;

	/**
	 * 根据细节级别返回建议的 PCG 采样间隔（米）
	 */
	UFUNCTION(BlueprintPure, Category = "GIS|Cesium")
	float GetSamplingIntervalForLevel(EPCGDetailLevel Level) const;

	// ============ 配置 ============

	/** 全细节距离（相机距离 < 此值时采样间隔最小） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Cesium|LOD",
		meta = (ClampMin = "100.0", ClampMax = "10000.0"))
	float FullDetailDistance = 1000.0f;

	/** 中等距离 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Cesium|LOD",
		meta = (ClampMin = "500.0", ClampMax = "20000.0"))
	float MediumDetailDistance = 3000.0f;

	/** 低细节距离 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Cesium|LOD",
		meta = (ClampMin = "1000.0", ClampMax = "50000.0"))
	float LowDetailDistance = 7000.0f;

	/** 全细节采样间隔（米） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Cesium|LOD",
		meta = (ClampMin = "1.0", ClampMax = "50.0"))
	float FullDetailInterval = 10.0f;

	/** 中等采样间隔（米） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Cesium|LOD",
		meta = (ClampMin = "10.0", ClampMax = "100.0"))
	float MediumDetailInterval = 30.0f;

	/** 低细节采样间隔（米） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Cesium|LOD",
		meta = (ClampMin = "50.0", ClampMax = "500.0"))
	float LowDetailInterval = 100.0f;

	/** Cesium Georeference Actor 软引用（避免硬依赖） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Cesium",
		meta = (DisplayName = "Cesium Georeference Actor"))
	TSoftObjectPtr<AActor> CesiumGeoreferenceActor;

	/** 投影原点经度（无 Cesium 时用于 Mercator 回退） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Cesium")
	double OriginLongitude = 0.0;

	/** 投影原点纬度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Cesium")
	double OriginLatitude = 0.0;

	/** 更新缓存的投影参数（Origin 变更后调用） */
	void UpdateCachedProjection();

private:
	/** 缓存的 Mercator 投影参数（避免每次调用重算 cos） */
	double CachedMetersPerDegreeLon = 0.0;
	double CachedMetersPerDegreeLat = 0.0;
	static constexpr double MetersToCm = 100.0;

	/** DEM 高程缓存：tile 坐标 → 缓存数据 */
	TMap<FIntPoint, FDEMTileCache> DEMCache;

	/** 根据经纬度找到对应的 tile 缓存 */
	const FDEMTileCache* FindTileCache(double Lon, double Lat) const;

	/** 双线性插值 */
	static float BilinearSample(const FDEMTileCache& Cache, double NormX, double NormY);

	/** Mercator 回退计算（无 Cesium 时） */
	FVector MercatorLLHToUnreal(double Lon, double Lat, double Height) const;
	FVector2D MercatorUnrealToLLH(const FVector& WorldPos) const;
};
