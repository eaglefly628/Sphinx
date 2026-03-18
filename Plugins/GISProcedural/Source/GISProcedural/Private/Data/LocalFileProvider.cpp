// LocalFileProvider.cpp
#include "Data/LocalFileProvider.h"
#include "Data/GeoJsonParser.h"
#include "DEM/DEMParser.h"

bool ULocalFileProvider::QueryFeatures(const FGeoRect& Bounds, TArray<FGISFeature>& OutFeatures)
{
    // 首次调用时解析文件并缓存
    if (!bFeaturesCached)
    {
        if (GeoJsonFilePath.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("LocalFileProvider: GeoJsonFilePath is empty"));
            return false;
        }

        UGeoJsonParser* Parser = NewObject<UGeoJsonParser>();
        if (!Parser->ParseFile(GeoJsonFilePath, CachedFeatures))
        {
            UE_LOG(LogTemp, Error, TEXT("LocalFileProvider: Failed to parse %s"), *GeoJsonFilePath);
            return false;
        }

        bFeaturesCached = true;
        UE_LOG(LogTemp, Log, TEXT("LocalFileProvider: Cached %d features from %s"),
            CachedFeatures.Num(), *GeoJsonFilePath);
    }

    // 如果有有效范围，做空间过滤
    if (Bounds.IsValid())
    {
        for (const FGISFeature& Feature : CachedFeatures)
        {
            bool bInBounds = false;
            for (const FVector2D& Coord : Feature.Coordinates)
            {
                if (Coord.X >= Bounds.MinLon && Coord.X <= Bounds.MaxLon &&
                    Coord.Y >= Bounds.MinLat && Coord.Y <= Bounds.MaxLat)
                {
                    bInBounds = true;
                    break;
                }
            }
            if (bInBounds)
            {
                OutFeatures.Add(Feature);
            }
        }
    }
    else
    {
        OutFeatures = CachedFeatures;
    }

    return true;
}

bool ULocalFileProvider::QueryElevation(
    const FGeoRect& Bounds, float Resolution,
    TArray<float>& OutGrid, int32& OutWidth, int32& OutHeight)
{
    if (!EnsureDEMLoaded())
    {
        return false;
    }

    return DEMParserInstance->GetElevationGrid(
        Bounds.MinLon, Bounds.MinLat,
        Bounds.MaxLon, Bounds.MaxLat,
        Resolution,
        OutGrid, OutWidth, OutHeight
    );
}

bool ULocalFileProvider::IsAvailable() const
{
    if (GeoJsonFilePath.IsEmpty())
    {
        return false;
    }
    return FPaths::FileExists(GeoJsonFilePath);
}

bool ULocalFileProvider::EnsureDEMLoaded()
{
    if (bDEMLoaded)
    {
        return DEMParserInstance != nullptr && DEMParserInstance->GetTileCount() > 0;
    }

    if (DEMPath.IsEmpty())
    {
        return false;
    }

    if (!DEMParserInstance)
    {
        DEMParserInstance = NewObject<UDEMParser>(this);
    }

    DEMParserInstance->Format = DEMFormat;

    if (FPaths::DirectoryExists(DEMPath))
    {
        bDEMLoaded = DEMParserInstance->LoadTilesFromDirectory(DEMPath) > 0;
    }
    else
    {
        bDEMLoaded = DEMParserInstance->LoadTile(DEMPath);
    }

    return bDEMLoaded;
}
