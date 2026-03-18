// GeoJsonParser.h - GeoJSON 解析器
#pragma once

#include "CoreMinimal.h"
#include "Data/GISFeature.h"
#include "GeoJsonParser.generated.h"

/**
 * GeoJSON 文件解析器
 * 支持 Point, LineString, Polygon, MultiPolygon 类型
 * 解析结果为 FGISFeature 数组
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API UGeoJsonParser : public UObject
{
    GENERATED_BODY()

public:
    /**
     * 从文件路径解析 GeoJSON
     * @param FilePath GeoJSON 文件的绝对路径或相对于项目的路径
     * @param OutFeatures 解析出的要素数组
     * @return 是否解析成功
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Data")
    bool ParseFile(const FString& FilePath, TArray<FGISFeature>& OutFeatures);

    /**
     * 从 JSON 字符串解析 GeoJSON
     * @param JsonString GeoJSON 字符串内容
     * @param OutFeatures 解析出的要素数组
     * @return 是否解析成功
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Data")
    bool ParseString(const FString& JsonString, TArray<FGISFeature>& OutFeatures);

    /**
     * 按几何类型过滤要素
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Data")
    static TArray<FGISFeature> FilterByType(const TArray<FGISFeature>& Features, EGISGeometryType Type);

    /**
     * 按属性值过滤要素
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Data")
    static TArray<FGISFeature> FilterByProperty(
        const TArray<FGISFeature>& Features,
        const FString& Key,
        const FString& Value
    );

    /**
     * 按 OSM 要素分类过滤
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Data")
    static TArray<FGISFeature> FilterByCategory(
        const TArray<FGISFeature>& Features,
        EGISFeatureCategory Category
    );

    /**
     * 从 OSM properties 自动推断要素分类
     * 解析后自动调用，也可手动重新分类
     */
    UFUNCTION(BlueprintCallable, Category = "GISProcedural|Data")
    static EGISFeatureCategory InferCategory(const TMap<FString, FString>& Properties);

private:
    /** 解析单个 Feature 对象 */
    bool ParseFeature(const TSharedPtr<class FJsonObject>& FeatureObject, FGISFeature& OutFeature);

    /** 解析 Geometry 对象中的坐标 */
    bool ParseGeometry(const TSharedPtr<class FJsonObject>& GeometryObject, FGISFeature& OutFeature);

    /** 解析坐标数组 [lon, lat] */
    bool ParseCoordinateArray(const TArray<TSharedPtr<class FJsonValue>>& JsonArray, TArray<FVector2D>& OutCoords);

    /** 解析 Properties 对象 */
    void ParseProperties(const TSharedPtr<class FJsonObject>& PropertiesObject, TMap<FString, FString>& OutProperties);
};
