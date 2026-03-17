// GISWorldBuilder.h - 纯 C++ 版世界构建器 Actor
// 临时驱动入口，等 AngelScript 集成后可切换为 .as 版本
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Polygon/PolygonDeriver.h"
#include "Polygon/LandUseClassifier.h"
#include "GISWorldBuilder.generated.h"

class UGISPolygonComponent;

/**
 * GIS 世界构建器
 *
 * 纯 C++ 实现，放在关卡中即可自动从 GeoJSON 生成土地分类 Polygon。
 * 所有 UPROPERTY / UFUNCTION 已标记 BlueprintReadWrite / BlueprintCallable，
 * 等 AngelScript 就绪后，直接在 .as 中继承或调用即可，C++ 侧零改动。
 *
 * 用法：
 *   1. 拖到关卡中
 *   2. 在 Details 面板填 GeoJSON 路径、坐标原点
 *   3. 指定 ClassifyRules DataAsset
 *   4. Play → BeginPlay 自动生成
 *   或者在编辑器中点 "Generate In Editor" 按钮预览
 */
UCLASS(BlueprintType, Blueprintable)
class GISPROCEDURAL_API AGISWorldBuilder : public AActor
{
    GENERATED_BODY()

public:
    AGISWorldBuilder();

    // ============ 输入配置（Details 面板填写） ============

    /** 道路 GeoJSON 文件路径（相对于项目 Content 目录） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Input")
    FString RoadsGeoJsonPath = TEXT("GISData/Region_01/roads.geojson");

    /** DEM 高程文件路径（可选） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Input")
    FString DEMFilePath;

    /** 区域中心经度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Input")
    double OriginLongitude = 116.39;

    /** 区域中心纬度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Input")
    double OriginLatitude = 39.91;

    // ============ 算法配置 ============

    /** 分类规则（在编辑器中创建 ULandUseClassifyRules DataAsset 后拖入） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Config")
    ULandUseClassifyRules* ClassifyRules = nullptr;

    /** 最小 Polygon 面积（平方米） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Config", meta = (ClampMin = "100.0"))
    float MinPolygonArea = 800.0f;

    /** 道路等级权重配置 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Config")
    TMap<FString, int32> RoadClassWeights;

    // ============ 输出 ============

    /** 是否在 BeginPlay 时自动生成 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Runtime")
    bool bAutoGenerateOnPlay = true;

    /** 是否为每个 Polygon 生成独立的子 Actor */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Runtime")
    bool bSpawnPerPolygonActors = true;

    /** 是否绘制调试线框 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Debug")
    bool bDrawDebugPolygons = true;

    /** 调试线框持续时间（秒，-1 = 永久） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GIS|Debug", meta = (EditCondition = "bDrawDebugPolygons"))
    float DebugDrawDuration = -1.0f;

    /** 生成结果（运行时可读取） */
    UPROPERTY(BlueprintReadOnly, Category = "GIS|Output")
    TArray<FLandUsePolygon> GeneratedPolygons;

    // ============ 接口 ============

    /** 一键生成（BeginPlay 内部调用，也可手动调用） */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    void GenerateAll();

    /** 异步生成（大数据量用） */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    void GenerateAllAsync();

    /** 清除所有已生成的内容 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    void ClearGenerated();

    /** 获取指定类型的 Polygon 列表 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    TArray<FLandUsePolygon> GetPolygonsByType(ELandUseType Type) const;

    /** 打印生成统计信息 */
    UFUNCTION(BlueprintCallable, Category = "GIS|Debug")
    void PrintStatistics() const;

    // ============ 编辑器工具 ============

#if WITH_EDITOR
    /** 编辑器中预生成（不需要 Play） */
    UFUNCTION(CallInEditor, Category = "GIS")
    void GenerateInEditor();

    /** 编辑器中清除预览 */
    UFUNCTION(CallInEditor, Category = "GIS")
    void ClearEditorPreview();
#endif

protected:
    virtual void BeginPlay() override;

private:
    /** 内部 Polygon 推导器 */
    UPROPERTY()
    UPolygonDeriver* PolygonDeriver = nullptr;

    /** 已生成的子 Actor 列表（用于清除） */
    UPROPERTY()
    TArray<AActor*> SpawnedActors;

    /** 初始化推导器 */
    void InitDeriver();

    /** 为每个 Polygon 创建子 Actor + GISPolygonComponent */
    void SpawnPolygonActors(const TArray<FLandUsePolygon>& Polygons);

    /** 绘制调试可视化 */
    void DrawDebugPolygons(const TArray<FLandUsePolygon>& Polygons) const;

    /** 异步回调（UFUNCTION 因 Dynamic Delegate 需要） */
    UFUNCTION()
    void OnPolygonsGenerated(const TArray<FLandUsePolygon>& Polygons);

    /** 获取土地类型对应的调试颜色 */
    static FColor GetLandUseColor(ELandUseType Type);
};
