// ArcGISRestProvider.h - ArcGIS REST API 数据源
// 通过 HTTP 查询 ArcGIS Feature Service，返回 GeoJSON 格式数据
#pragma once

#include "CoreMinimal.h"
#include "Data/IGISDataProvider.h"
#include "ArcGISRestProvider.generated.h"

/**
 * ArcGIS REST API 数据源
 *
 * 直接调用 ArcGIS Feature Service REST endpoint 获取矢量要素。
 * 返回 GeoJSON 格式，复用现有 GeoJsonParser 解析。
 *
 * Feature Service URL 格式：
 *   https://services.arcgis.com/{orgId}/arcgis/rest/services/{name}/FeatureServer/{layerId}
 *
 * 查询示例：
 *   {url}/query?where=1=1&geometry={bbox}&geometryType=esriGeometryEnvelope
 *   &spatialRel=esriSpatialRelIntersects&outFields=*&f=geojson
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API UArcGISRestProvider : public UObject, public IGISDataProvider
{
    GENERATED_BODY()

public:

    /** Feature Service Layer URL（不含 /query） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArcGIS")
    FString FeatureServiceUrl;

    /** ArcGIS API Key（用于认证） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArcGIS")
    FString ApiKey;

    /** 每次查询的最大记录数（Feature Service 有上限，默认 2000） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArcGIS", meta = (ClampMin = "100", ClampMax = "10000"))
    int32 MaxRecordCount = 2000;

    /** WHERE 过滤条件（默认查全部） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArcGIS")
    FString WhereClause = TEXT("1=1");

    /** 要返回的字段（* = 全部） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArcGIS")
    FString OutFields = TEXT("*");

    /** 多个 Feature Service Layer URL（可选，用于同时查多个图层） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArcGIS")
    TArray<FString> AdditionalLayerUrls;

    // --- IGISDataProvider ---
    virtual bool QueryFeatures(const FGeoRect& Bounds, TArray<FGISFeature>& OutFeatures) override;
    virtual FString GetProviderName() const override { return TEXT("ArcGISRest"); }
    virtual bool IsAvailable() const override;

private:
    /** 同步查询单个图层 */
    bool QuerySingleLayer(
        const FString& LayerUrl,
        const FGeoRect& Bounds,
        TArray<FGISFeature>& OutFeatures);

    /** 构建查询 URL */
    FString BuildQueryUrl(const FString& LayerUrl, const FGeoRect& Bounds, int32 ResultOffset) const;

    /** 同步 HTTP GET（Editor 时可用，Runtime 应改为异步） */
    bool HttpGetSync(const FString& Url, FString& OutResponse) const;
};
