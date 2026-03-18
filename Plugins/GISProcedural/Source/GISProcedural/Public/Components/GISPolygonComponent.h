// GISPolygonComponent.h - 可挂载到 Actor 的 GIS Polygon 组件
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Polygon/LandUsePolygon.h"
#include "GISPolygonComponent.generated.h"

/**
 * GIS Polygon 组件
 * 挂载到 Actor 上，承载一个或多个土地分类 Polygon 的数据
 * 可供 PCG Component 或 AngelScript 读取使用
 */
UCLASS(ClassGroup = "GISProcedural", meta = (BlueprintSpawnableComponent))
class GISPROCEDURAL_API UGISPolygonComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGISPolygonComponent();

    /** 该组件承载的 Polygon 数据 */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GIS")
    FLandUsePolygon PolygonData;

    /** 设置 Polygon 数据 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    void SetPolygonData(const FLandUsePolygon& InPolygon);

    /** 获取土地分类类型 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    ELandUseType GetLandUseType() const;

    /** 获取 Polygon 面积 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    float GetArea() const;

    /** 判断世界坐标点是否在此 Polygon 内部 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    bool IsPointInside(const FVector& WorldPoint) const;

    /** 获取 Polygon 的包围盒 */
    UFUNCTION(BlueprintCallable, Category = "GIS")
    FBox GetBoundingBox() const;

#if WITH_EDITOR
    /** 在编辑器中绘制 Polygon 边界（调试用） */
    virtual void OnComponentCreated() override;
#endif

private:
    /** 点在多边形内部测试（Ray Casting 算法） */
    static bool PointInPolygon2D(const FVector2D& Point, const TArray<FVector>& Vertices);
};
