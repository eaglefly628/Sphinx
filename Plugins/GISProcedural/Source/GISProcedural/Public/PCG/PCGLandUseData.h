// PCGLandUseData.h - PCG 自定义数据类型
#pragma once

#include "CoreMinimal.h"
#include "PCGData.h"
#include "Polygon/LandUsePolygon.h"
#include "PCGLandUseData.generated.h"

/**
 * 自定义 PCG 数据类型：承载土地分类 Polygon 信息
 * 可在 PCG Graph 中作为输入/输出数据传递
 */
UCLASS(BlueprintType, ClassGroup = "GISProcedural")
class GISPROCEDURAL_API UPCGLandUseData : public UPCGData
{
    GENERATED_BODY()

public:
    /** 所有土地分类 Polygon */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GIS")
    TArray<FLandUsePolygon> Polygons;

    /** 根据 LandUseType 过滤 Polygon */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    TArray<FLandUsePolygon> GetPolygonsByType(ELandUseType Type) const;

    /** 获取所有 Polygon 的总面积 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    float GetTotalArea() const;

    /** 获取指定类型的 Polygon 数量 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    int32 GetCountByType(ELandUseType Type) const;
};
