// LocalFileProvider.h - 本地文件数据源（GeoJSON + DEM）
#pragma once

#include "CoreMinimal.h"
#include "Data/IGISDataProvider.h"
#include "LocalFileProvider.generated.h"

class UGeoJsonParser;
class UDEMParser;
enum class EDEMFormat : uint8;

/**
 * 本地文件数据源
 *
 * 从磁盘 GeoJSON 文件读取矢量要素，可选从 DEM 文件读取高程。
 * 是插件内置的默认 DataProvider。
 */
UCLASS(BlueprintType)
class GISPROCEDURAL_API ULocalFileProvider : public UObject, public IGISDataProvider
{
    GENERATED_BODY()

public:

    /** GeoJSON 文件路径 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DataProvider")
    FString GeoJsonFilePath;

    /** DEM 文件或目录路径（可选） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DataProvider")
    FString DEMPath;

    /** DEM 格式 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DataProvider")
    EDEMFormat DEMFormat;

    // --- IGISDataProvider ---
    virtual bool QueryFeatures(const FGeoRect& Bounds, TArray<FGISFeature>& OutFeatures) override;
    virtual bool QueryElevation(const FGeoRect& Bounds, float Resolution, TArray<float>& OutGrid, int32& OutWidth, int32& OutHeight) override;
    virtual FString GetProviderName() const override { return TEXT("LocalFile"); }
    virtual bool IsAvailable() const override;

private:
    /** 缓存的要素（避免重复解析） */
    TArray<FGISFeature> CachedFeatures;
    bool bFeaturesCached = false;

    /** DEM 解析器实例 */
    UPROPERTY()
    UDEMParser* DEMParserInstance = nullptr;
    bool bDEMLoaded = false;

    /** 确保 DEM 已加载 */
    bool EnsureDEMLoaded();
};
