// PCGGISNode.h - 自定义 PCG 节点
// 从 ULandUseMapDataAsset 读取 Polygon 数据，生成采样点
#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "Polygon/LandUsePolygon.h"
#include "PCGGISNode.generated.h"

class ULandUseMapDataAsset;

/**
 * 自定义 PCG 节点：从 GIS LandUse DataAsset 生成采样点
 * 每个点携带 LandUseType 属性，供下游节点过滤
 *
 * 用法（PCG Graph 中）：
 * [GIS Land Use Sampler] → [Attribute Filter: LandUseType == Residential] → 建筑生成子图
 *                        → [Attribute Filter: LandUseType == Forest]      → 植被生成子图
 *                        → [Attribute Filter: LandUseType == Water]       → 水体生成子图
 */
UCLASS(BlueprintType, ClassGroup = "GISProcedural")
class GISPROCEDURAL_API UPCGGISLandUseSampler : public UPCGSettings
{
    GENERATED_BODY()

public:
    UPCGGISLandUseSampler();

    //~ Begin UPCGSettings Interface
#if WITH_EDITOR
    virtual FName GetDefaultNodeName() const override;
    virtual FText GetDefaultNodeTitle() const override;
    virtual FText GetNodeTooltipText() const override;
    virtual EPCGSettingsType GetType() const override;
#endif

protected:
    virtual TArray<FPCGPinProperties> InputPinProperties() const override;
    virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
    virtual FPCGElementPtr CreateElement() const override;
    //~ End UPCGSettings Interface

public:
    /** LandUse DataAsset（离线生成的数据） */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GIS")
    TSoftObjectPtr<ULandUseMapDataAsset> LandUseDataAsset;

    /** 采样间距（米） */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GIS", meta = (ClampMin = "1.0"))
    float SamplingInterval = 10.0f;

    /** 是否添加随机抖动 */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GIS")
    bool bJitterPoints = true;

    /** 抖动量（米） */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GIS", meta = (ClampMin = "0.0", EditCondition = "bJitterPoints"))
    float JitterAmount = 2.0f;

    /** 只采样指定类型（Empty = 全部） */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GIS")
    TArray<ELandUseType> FilterTypes;

    // ============ 瓦片流式加载配置（Phase 2+） ============

    /** 是否启用瓦片流式加载（大规模地图时开启） */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GIS|Tiling")
    bool bEnableTiling = false;

    /** 瓦片尺寸（米），与 World Partition 网格对齐 */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GIS|Tiling",
        meta = (ClampMin = "256.0", ClampMax = "4096.0", EditCondition = "bEnableTiling"))
    float TileSizeM = 1024.0f;

    /** 加载半径（瓦片数），玩家周围加载 N 圈瓦片 */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GIS|Tiling",
        meta = (ClampMin = "1", ClampMax = "8", EditCondition = "bEnableTiling"))
    int32 LoadRadius = 3;
};

/**
 * PCG 执行元素：实际执行采样逻辑
 */
class GISPROCEDURAL_API FPCGGISLandUseSamplerElement : public IPCGElement
{
protected:
    virtual bool ExecuteInternal(FPCGContext* Context) const override;

private:
    /** 点在多边形内测试 */
    static bool IsPointInPolygon(const FVector& Point, const TArray<FVector>& PolygonVerts);
};
