// IGISDataProvider.h - GIS 数据源抽象接口
// 支持本地文件、ArcGIS REST API、Shapefile 等不同数据源
#pragma once

#include "CoreMinimal.h"
#include "Data/GISFeature.h"
#include "Data/GeoRect.h"

/**
 * GIS 数据源抽象接口
 *
 * 插件通过此接口获取矢量要素，不关心数据来自哪里。
 * 内置实现：ULocalFileProvider（读 GeoJSON 文件）
 * 内置实现：UArcGISRestProvider（HTTP 查 ArcGIS Feature Service）
 * 项目层可自定义实现
 */
class GISPROCEDURAL_API IGISDataProvider
{
public:
    virtual ~IGISDataProvider() = default;

    /**
     * 查询指定范围内的矢量要素
     * @param Bounds       查询的地理范围
     * @param OutFeatures  输出要素数组
     * @return 是否成功
     */
    virtual bool QueryFeatures(
        const FGeoRect& Bounds,
        TArray<FGISFeature>& OutFeatures) = 0;

    /**
     * 查询指定范围内的高程数据（可选实现）
     * @param Bounds       查询的地理范围
     * @param Resolution   网格分辨率（米）
     * @param OutGrid      输出高程网格（行优先）
     * @param OutWidth     输出网格宽度
     * @param OutHeight    输出网格高度
     * @return 是否成功（未实现时返回 false）
     */
    virtual bool QueryElevation(
        const FGeoRect& Bounds,
        float Resolution,
        TArray<float>& OutGrid,
        int32& OutWidth, int32& OutHeight)
    {
        return false;
    }

    /** 数据源名称（用于日志） */
    virtual FString GetProviderName() const = 0;

    /** 数据源是否可用 */
    virtual bool IsAvailable() const = 0;
};
