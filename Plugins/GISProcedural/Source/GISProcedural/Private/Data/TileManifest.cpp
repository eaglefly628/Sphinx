// TileManifest.cpp - tile_manifest.json 解析实现
#include "Data/TileManifest.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FTileManifest::LoadFromFile(const FString& FilePath, FTileManifest& OutManifest)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("TileManifest: Failed to load file: %s"), *FilePath);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("TileManifest: Loaded %d chars from %s"), JsonString.Len(), *FilePath);
    return ParseFromJson(JsonString, OutManifest);
}

bool FTileManifest::ParseFromJson(const FString& JsonString, FTileManifest& OutManifest)
{
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("TileManifest: Failed to parse JSON"));
        return false;
    }

    // 投影信息
    FString Projection;
    RootObject->TryGetStringField(TEXT("projection"), Projection);

    OutManifest.UTMZone = 0;
    OutManifest.bNorthernHemisphere = true;

    // 解析 "UTM51N" 格式
    if (Projection.StartsWith(TEXT("UTM")))
    {
        FString ZoneStr = Projection.Mid(3);
        if (ZoneStr.EndsWith(TEXT("N")))
        {
            OutManifest.bNorthernHemisphere = true;
            ZoneStr = ZoneStr.LeftChop(1);
        }
        else if (ZoneStr.EndsWith(TEXT("S")))
        {
            OutManifest.bNorthernHemisphere = false;
            ZoneStr = ZoneStr.LeftChop(1);
        }
        OutManifest.UTMZone = FCString::Atoi(*ZoneStr);
    }

    OutManifest.OriginLongitude = RootObject->GetNumberField(TEXT("origin_lon"));
    OutManifest.OriginLatitude = RootObject->GetNumberField(TEXT("origin_lat"));
    OutManifest.TileSizeM = RootObject->GetNumberField(TEXT("tile_size_m"));
    OutManifest.NumCols = RootObject->GetIntegerField(TEXT("num_cols"));
    OutManifest.NumRows = RootObject->GetIntegerField(TEXT("num_rows"));

    // 总范围
    const TSharedPtr<FJsonObject>* TotalBoundsObj = nullptr;
    if (RootObject->TryGetObjectField(TEXT("total_bounds"), TotalBoundsObj))
    {
        OutManifest.TotalBounds.MinLon = (*TotalBoundsObj)->GetNumberField(TEXT("min_lon"));
        OutManifest.TotalBounds.MinLat = (*TotalBoundsObj)->GetNumberField(TEXT("min_lat"));
        OutManifest.TotalBounds.MaxLon = (*TotalBoundsObj)->GetNumberField(TEXT("max_lon"));
        OutManifest.TotalBounds.MaxLat = (*TotalBoundsObj)->GetNumberField(TEXT("max_lat"));
    }

    // 瓦片数组
    const TArray<TSharedPtr<FJsonValue>>* TilesArray = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("tiles"), TilesArray))
    {
        UE_LOG(LogTemp, Error, TEXT("TileManifest: No 'tiles' array in JSON"));
        return false;
    }

    OutManifest.Tiles.Reserve(TilesArray->Num());

    for (const TSharedPtr<FJsonValue>& TileValue : *TilesArray)
    {
        const TSharedPtr<FJsonObject>& TileObj = TileValue->AsObject();
        if (!TileObj.IsValid())
        {
            continue;
        }

        FTileEntry Entry;
        Entry.Col = TileObj->GetIntegerField(TEXT("x"));
        Entry.Row = TileObj->GetIntegerField(TEXT("y"));

        TileObj->TryGetStringField(TEXT("geojson"), Entry.GeoJsonRelPath);
        TileObj->TryGetStringField(TEXT("dem"), Entry.DEMRelPath);
        TileObj->TryGetStringField(TEXT("landcover"), Entry.LandCoverRelPath);

        Entry.FeatureCount = TileObj->HasField(TEXT("polygon_count"))
            ? static_cast<int32>(TileObj->GetNumberField(TEXT("polygon_count")))
            : 0;

        // 地理范围
        const TSharedPtr<FJsonObject>* BoundsObj = nullptr;
        if (TileObj->TryGetObjectField(TEXT("bounds_geo"), BoundsObj))
        {
            Entry.GeoBounds.MinLon = (*BoundsObj)->GetNumberField(TEXT("min_lon"));
            Entry.GeoBounds.MinLat = (*BoundsObj)->GetNumberField(TEXT("min_lat"));
            Entry.GeoBounds.MaxLon = (*BoundsObj)->GetNumberField(TEXT("max_lon"));
            Entry.GeoBounds.MaxLat = (*BoundsObj)->GetNumberField(TEXT("max_lat"));
        }

        OutManifest.Tiles.Add(MoveTemp(Entry));
    }

    UE_LOG(LogTemp, Log, TEXT("TileManifest: Parsed %d tiles, grid %dx%d, origin (%.4f, %.4f)"),
        OutManifest.Tiles.Num(), OutManifest.NumCols, OutManifest.NumRows,
        OutManifest.OriginLongitude, OutManifest.OriginLatitude);

    return true;
}

const FTileEntry* FTileManifest::FindTile(int32 Col, int32 Row) const
{
    for (const FTileEntry& Entry : Tiles)
    {
        if (Entry.Col == Col && Entry.Row == Row)
        {
            return &Entry;
        }
    }
    return nullptr;
}

TArray<const FTileEntry*> FTileManifest::FindTilesInBounds(const FGeoRect& Bounds) const
{
    TArray<const FTileEntry*> Result;

    for (const FTileEntry& Entry : Tiles)
    {
        // AABB 相交测试
        if (Entry.GeoBounds.MaxLon < Bounds.MinLon ||
            Entry.GeoBounds.MinLon > Bounds.MaxLon ||
            Entry.GeoBounds.MaxLat < Bounds.MinLat ||
            Entry.GeoBounds.MinLat > Bounds.MaxLat)
        {
            continue;
        }
        Result.Add(&Entry);
    }

    return Result;
}
