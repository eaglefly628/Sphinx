// GISWorldBuilder.h - 世界构建器 Actor
// 支持两种数据模式：本地文件 / DataProvider（ArcGIS REST 等）
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Polygon/PolygonDeriver.h"
#include "Polygon/LandUseClassifier.h"
#include "Data/IGISDataProvider.h"
#include "GISWorldBuilder.generated.h"

class UGISPolygonComponent;
class ULandUseMapDataAsset;
class ULocalFileProvider;
class UArcGISRestProvider;
class UTiledFileProvider;

/** 数据源类型 */
UENUM(BlueprintType)
enum class EGISDataSourceType : uint8
{
    /** 本地文件（GeoJSON + DEM） */
    LocalFile     UMETA(DisplayName = "Local File"),

    /** ArcGIS REST API */
    ArcGISRest    UMETA(DisplayName = "ArcGIS REST API"),

    /** 已有 DataAsset（跳过生成，直接加载） */
    DataAsset     UMETA(DisplayName = "DataAsset"),

    /** 预处理瓦片集（tile_manifest.json，支持大规模区域） */
    TiledFile     UMETA(DisplayName = "Tiled File"),
};

/**
 * GIS 世界构建器
 *
 * 离线/在线分离架构：
 *   Editor 时：从数据源生成 Polygon → 存为 ULandUseMapDataAsset
 *   Runtime 时：从 DataAsset 加载 → 驱动 PCG
 *
 * 数据源支持：
 *   - LocalFile：读磁盘 GeoJSON + DEM
 *   - ArcGISRest：HTTP 查 Feature Service
 *   - DataAsset：直接读已生成的 DataAsset
 */
UCLASS(BlueprintType, Blueprintable)
class GISPROCEDURAL_API AGISWorldBuilder : public AActor
{
    GENERATED_BODY()

public:
    AGISWorldBuilder();

    // ============ 数据源选择 ============

    /** 数据源类型 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|DataSource")
    EGISDataSourceType DataSourceType = EGISDataSourceType::LocalFile;

    // ---- LocalFile 模式 ----

    /** DEM 瓦片路径（文件或目录） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|DataSource|LocalFile",
        meta = (EditCondition = "DataSourceType == EGISDataSourceType::LocalFile"))
    FString DEMPath = TEXT("GISData/Region_01/DEM/");

    /** GeoJSON 文件路径 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|DataSource|LocalFile",
        meta = (EditCondition = "DataSourceType == EGISDataSourceType::LocalFile"))
    FString GeoJsonPath = TEXT("GISData/Region_01/osm_data.geojson");

    /** DEM 格式 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|DataSource|LocalFile",
        meta = (EditCondition = "DataSourceType == EGISDataSourceType::LocalFile"))
    EDEMFormat DEMFormat = EDEMFormat::Auto;

    // ---- ArcGIS REST 模式 ----

    /** Feature Service URL */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|DataSource|ArcGIS",
        meta = (EditCondition = "DataSourceType == EGISDataSourceType::ArcGISRest"))
    FString FeatureServiceUrl;

    /** ArcGIS API Key */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|DataSource|ArcGIS",
        meta = (EditCondition = "DataSourceType == EGISDataSourceType::ArcGISRest"))
    FString ArcGISApiKey;

    /** 附加图层 URL（可选） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|DataSource|ArcGIS",
        meta = (EditCondition = "DataSourceType == EGISDataSourceType::ArcGISRest"))
    TArray<FString> AdditionalLayerUrls;

    // ---- TiledFile 模式 ----

    /** Tile Manifest 路径（tile_manifest.json） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|DataSource|TiledFile",
        meta = (EditCondition = "DataSourceType == EGISDataSourceType::TiledFile"))
    FString TileManifestPath = TEXT("GISData/Region_01/tile_manifest.json");

    // ---- DataAsset 模式 ----

    /** 已生成的 DataAsset（直接加载） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|DataSource|DataAsset")
    ULandUseMapDataAsset* LandUseDataAsset = nullptr;

    // ============ 通用配置 ============

    /** 区域中心经度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Input")
    double OriginLongitude = 116.39;

    /** 区域中心纬度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Input")
    double OriginLatitude = 39.91;

    /** 查询范围（经纬度，默认空 = 不限） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Input")
    FGeoRect QueryBounds;

    /** 地形分析分辨率（米） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Input", meta = (ClampMin = "5.0", ClampMax = "500.0"))
    float TerrainAnalysisResolution = 30.0f;

    // ============ 算法配置 ============

    /** 分类规则 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Config")
    ULandUseClassifyRules* ClassifyRules = nullptr;

    /** 最小 Polygon 面积（平方米） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Config", meta = (ClampMin = "100.0"))
    float MinPolygonArea = 800.0f;

    /** 道路等级权重 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Config")
    TMap<FString, int32> RoadClassWeights;

    // ============ 输出 ============

    /** 是否在 BeginPlay 时自动生成 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Runtime")
    bool bAutoGenerateOnPlay = false;

    /** 是否为每个 Polygon 生成独立的子 Actor */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Runtime")
    bool bSpawnPerPolygonActors = true;

    /** 是否绘制调试线框 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Debug")
    bool bDrawDebugPolygons = true;

    /** 调试线框持续时间（秒，-1 = 永久） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Debug", meta = (EditCondition = "bDrawDebugPolygons"))
    float DebugDrawDuration = -1.0f;

    /** 生成结果 */
    UPROPERTY(BlueprintReadOnly, Category = "GIS|Output")
    TArray<FLandUsePolygon> GeneratedPolygons;

    // ============ 接口 ============

    /** 一键生成 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    void GenerateAll();

    /** 异步生成 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    void GenerateAllAsync();

    /** 清除已生成内容 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    void ClearGenerated();

    /** 获取指定类型的 Polygon */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    TArray<FLandUsePolygon> GetPolygonsByType(ELandUseType Type) const;

    /** 打印统计 */
    UFUNCTION(BlueprintCallable, Category = "GIS|Debug")
    void PrintStatistics() const;

    // ============ 编辑器工具 ============

#if WITH_EDITOR
    /** 编辑器中生成 */
    UFUNCTION(CallInEditor, Category = "GIS")
    void GenerateInEditor();

    /** 编辑器中清除 */
    UFUNCTION(CallInEditor, Category = "GIS")
    void ClearEditorPreview();

    /** 生成并保存为 DataAsset */
    UFUNCTION(CallInEditor, Category = "GIS")
    void GenerateAndSaveDataAsset();
#endif

protected:
    virtual void BeginPlay() override;

private:
    /** Polygon 推导器 */
    UPROPERTY()
    UPolygonDeriver* PolygonDeriver = nullptr;

    /** 数据源实例（由 WorldBuilder 持有） */
    UPROPERTY()
    ULocalFileProvider* LocalFileProviderInstance = nullptr;

    UPROPERTY()
    UArcGISRestProvider* ArcGISRestProviderInstance = nullptr;

    /** TiledFile 数据源实例（Phase 2，由小城实现 UTiledFileProvider） */
    UPROPERTY()
    TObjectPtr<UObject> TiledFileProviderInstance = nullptr;

    /** 已生成的子 Actor */
    UPROPERTY()
    TArray<AActor*> SpawnedActors;

    /** 初始化推导器 */
    void InitDeriver();

    /** 创建并配置 DataProvider */
    IGISDataProvider* CreateDataProvider();

    /** 从 DataAsset 加载 polygon */
    void LoadFromDataAsset();

    /** 生成后处理 */
    void PostGenerate();

    /** 为每个 Polygon 创建子 Actor */
    void SpawnPolygonActors(const TArray<FLandUsePolygon>& Polygons);

    /** 绘制调试可视化 */
    void DrawDebugPolygons(const TArray<FLandUsePolygon>& Polygons) const;

    /** 异步回调 */
    UFUNCTION()
    void OnPolygonsGenerated(const TArray<FLandUsePolygon>& Polygons);

    /** 获取土地类型颜色 */
    static FColor GetLandUseColor(ELandUseType Type);
};
