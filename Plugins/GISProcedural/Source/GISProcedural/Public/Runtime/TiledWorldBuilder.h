// TiledWorldBuilder.h - 编辑器批量 tile 生成器
// 遍历 tile_manifest → 逐 tile 生成 DataAsset → 注册到 TiledLandUseMapDataAsset
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/TileManifest.h"
#include "DEM/DEMTypes.h"
#include "TiledWorldBuilder.generated.h"

class UPolygonDeriver;
class ULandUseClassifyRules;
class UTiledLandUseMapDataAsset;
class UTiledFileProvider;

/**
 * 瓦片化世界构建器
 *
 * 编辑器工具 Actor，读取 tile_manifest.json 后逐 tile 执行：
 *   TiledFileProvider → PolygonDeriver → LandUseClassifier → DataAsset
 *
 * 所有 tile 的 DataAsset 注册到一个 UTiledLandUseMapDataAsset 清单中，
 * 运行时由 World Partition 按需加载。
 *
 * 用法：
 *   1. 将 ATiledWorldBuilder 拖入关卡
 *   2. 设置 ManifestPath 和分类规则
 *   3. 点击 "Generate All Tiles" 按钮
 *   4. 生成的清单 DataAsset 在 /Game/GISData/TiledMaps/
 */
UCLASS(BlueprintType, Blueprintable)
class GISPROCEDURAL_API ATiledWorldBuilder : public AActor
{
    GENERATED_BODY()

public:
    ATiledWorldBuilder();

    // ============ 配置 ============

    /** tile_manifest.json 路径（相对于 Content 目录） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|TiledBuild")
    FString ManifestPath = TEXT("GISData/Region_01/tile_manifest.json");

    /** 分类规则 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|TiledBuild")
    ULandUseClassifyRules* ClassifyRules = nullptr;

    /** 最小 Polygon 面积（平方米） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|TiledBuild", meta = (ClampMin = "100.0"))
    float MinPolygonArea = 800.0f;

    /** DEM 格式（与 GISWorldBuilder 一致） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|TiledBuild")
    EDEMFormat DEMFormat = EDEMFormat::Auto;

    /** 道路等级权重 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|TiledBuild")
    TMap<FString, int32> RoadClassWeights;

    /** 输出 DataAsset 的包路径 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|TiledBuild")
    FString OutputPackagePath = TEXT("/Game/GISData/TiledMaps/");

    // ============ 结果 ============

    /** 生成的瓦片清单 DataAsset */
    UPROPERTY(BlueprintReadOnly, Category = "GIS|TiledBuild")
    UTiledLandUseMapDataAsset* GeneratedTiledAsset = nullptr;

    /** 最后一次生成的统计 */
    UPROPERTY(BlueprintReadOnly, Category = "GIS|TiledBuild")
    int32 LastGeneratedTileCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GIS|TiledBuild")
    int32 LastTotalPolygonCount = 0;

    // ============ 接口 ============

#if WITH_EDITOR
    /** 编辑器按钮：生成所有 tile */
    UFUNCTION(CallInEditor, Category = "GIS|TiledBuild")
    void GenerateAllTiles();

    /** 编辑器按钮：生成单个 tile（测试用） */
    UFUNCTION(CallInEditor, Category = "GIS|TiledBuild")
    void GenerateSingleTestTile();
#endif

private:
    /** Polygon 推导器 */
    UPROPERTY()
    UPolygonDeriver* PolygonDeriver = nullptr;

    /** TiledFile 数据源 */
    UPROPERTY()
    UTiledFileProvider* FileProvider = nullptr;

    /** 初始化构建组件 */
    bool InitBuildComponents();

    /** 为单个 tile 生成 DataAsset */
    ULandUseMapDataAsset* GenerateTileDataAsset(
        const FTileEntry& Entry,
        const FString& TileID);
};
