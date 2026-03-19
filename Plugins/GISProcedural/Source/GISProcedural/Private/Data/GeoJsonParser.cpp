// GeoJsonParser.cpp - GeoJSON 解析器实现
#include "Data/GeoJsonParser.h"
#include "GISProceduralModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

bool UGeoJsonParser::ParseFile(const FString& FilePath, TArray<FGISFeature>& OutFeatures)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
    {
        UE_LOG(LogGIS, Error, TEXT("GeoJsonParser: Failed to load file: %s"), *FilePath);
        return false;
    }

    return ParseString(JsonString, OutFeatures);
}

bool UGeoJsonParser::ParseString(const FString& JsonString, TArray<FGISFeature>& OutFeatures)
{
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogGIS, Error, TEXT("GeoJsonParser: Failed to parse JSON"));
        return false;
    }

    // 检查是否为 FeatureCollection
    FString Type;
    if (!RootObject->TryGetStringField(TEXT("type"), Type))
    {
        UE_LOG(LogGIS, Error, TEXT("GeoJsonParser: Missing 'type' field"));
        return false;
    }

    if (Type == TEXT("FeatureCollection"))
    {
        const TArray<TSharedPtr<FJsonValue>>* FeaturesArray;
        if (!RootObject->TryGetArrayField(TEXT("features"), FeaturesArray))
        {
            UE_LOG(LogGIS, Error, TEXT("GeoJsonParser: Missing 'features' array"));
            return false;
        }

        OutFeatures.Reserve(FeaturesArray->Num());

        for (const TSharedPtr<FJsonValue>& FeatureValue : *FeaturesArray)
        {
            const TSharedPtr<FJsonObject>* FeatureObject;
            if (FeatureValue->TryGetObject(FeatureObject))
            {
                FGISFeature Feature;
                if (ParseFeature(*FeatureObject, Feature))
                {
                    Feature.Category = InferCategory(Feature.Properties);
                    OutFeatures.Add(MoveTemp(Feature));
                }
            }
        }
    }
    else if (Type == TEXT("Feature"))
    {
        // 单个 Feature
        FGISFeature Feature;
        if (ParseFeature(RootObject, Feature))
        {
            Feature.Category = InferCategory(Feature.Properties);
            OutFeatures.Add(MoveTemp(Feature));
        }
    }
    else
    {
        UE_LOG(LogGIS, Warning, TEXT("GeoJsonParser: Unsupported type: %s"), *Type);
        return false;
    }

    UE_LOG(LogGIS, Log, TEXT("GeoJsonParser: Parsed %d features"), OutFeatures.Num());
    return OutFeatures.Num() > 0;
}

TArray<FGISFeature> UGeoJsonParser::FilterByType(const TArray<FGISFeature>& Features, EGISGeometryType Type)
{
    TArray<FGISFeature> Result;
    for (const FGISFeature& Feature : Features)
    {
        if (Feature.GeometryType == Type)
        {
            Result.Add(Feature);
        }
    }
    return Result;
}

TArray<FGISFeature> UGeoJsonParser::FilterByProperty(
    const TArray<FGISFeature>& Features,
    const FString& Key,
    const FString& Value)
{
    TArray<FGISFeature> Result;
    for (const FGISFeature& Feature : Features)
    {
        const FString* FoundValue = Feature.Properties.Find(Key);
        if (FoundValue && *FoundValue == Value)
        {
            Result.Add(Feature);
        }
    }
    return Result;
}

TArray<FGISFeature> UGeoJsonParser::FilterByCategory(
    const TArray<FGISFeature>& Features,
    EGISFeatureCategory Category)
{
    TArray<FGISFeature> Result;
    for (const FGISFeature& Feature : Features)
    {
        if (Feature.Category == Category)
        {
            Result.Add(Feature);
        }
    }
    return Result;
}

EGISFeatureCategory UGeoJsonParser::InferCategory(const TMap<FString, FString>& Properties)
{
    // OSM tag 优先级：先检查最具体的 tag

    // 1. highway=* → Road
    if (Properties.Contains(TEXT("highway")))
    {
        return EGISFeatureCategory::Road;
    }

    // 2. waterway=river/stream/canal/drain → River
    if (const FString* Waterway = Properties.Find(TEXT("waterway")))
    {
        if (*Waterway == TEXT("river") || *Waterway == TEXT("stream") ||
            *Waterway == TEXT("canal") || *Waterway == TEXT("drain") ||
            *Waterway == TEXT("ditch"))
        {
            return EGISFeatureCategory::River;
        }
        // waterway=riverbank → WaterBody (面域)
        if (*Waterway == TEXT("riverbank") || *Waterway == TEXT("dock") ||
            *Waterway == TEXT("boatyard"))
        {
            return EGISFeatureCategory::WaterBody;
        }
    }

    // 3. natural=coastline → Coastline
    if (const FString* Natural = Properties.Find(TEXT("natural")))
    {
        if (*Natural == TEXT("coastline"))
        {
            return EGISFeatureCategory::Coastline;
        }
        if (*Natural == TEXT("water") || *Natural == TEXT("bay") || *Natural == TEXT("strait"))
        {
            return EGISFeatureCategory::WaterBody;
        }
        if (*Natural == TEXT("wood") || *Natural == TEXT("scrub") ||
            *Natural == TEXT("grassland") || *Natural == TEXT("heath") ||
            *Natural == TEXT("wetland") || *Natural == TEXT("beach") ||
            *Natural == TEXT("cliff") || *Natural == TEXT("peak"))
        {
            return EGISFeatureCategory::Natural;
        }
    }

    // 4. water=* → WaterBody
    if (Properties.Contains(TEXT("water")))
    {
        return EGISFeatureCategory::WaterBody;
    }

    // 5. landuse=reservoir/basin → WaterBody
    if (const FString* LandUse = Properties.Find(TEXT("landuse")))
    {
        if (*LandUse == TEXT("reservoir") || *LandUse == TEXT("basin"))
        {
            return EGISFeatureCategory::WaterBody;
        }
        // 其他 landuse → LandUse
        return EGISFeatureCategory::LandUse;
    }

    // 6. building=* → Building
    if (Properties.Contains(TEXT("building")))
    {
        return EGISFeatureCategory::Building;
    }

    return EGISFeatureCategory::Other;
}

bool UGeoJsonParser::ParseFeature(const TSharedPtr<FJsonObject>& FeatureObject, FGISFeature& OutFeature)
{
    // 解析 Geometry
    const TSharedPtr<FJsonObject>* GeometryObject;
    if (FeatureObject->TryGetObjectField(TEXT("geometry"), GeometryObject))
    {
        if (!ParseGeometry(*GeometryObject, OutFeature))
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    // 解析 Properties
    const TSharedPtr<FJsonObject>* PropertiesObject;
    if (FeatureObject->TryGetObjectField(TEXT("properties"), PropertiesObject))
    {
        ParseProperties(*PropertiesObject, OutFeature.Properties);
    }

    return true;
}

bool UGeoJsonParser::ParseGeometry(const TSharedPtr<FJsonObject>& GeometryObject, FGISFeature& OutFeature)
{
    FString GeomType;
    if (!GeometryObject->TryGetStringField(TEXT("type"), GeomType))
    {
        return false;
    }

    if (GeomType == TEXT("Point"))
    {
        OutFeature.GeometryType = EGISGeometryType::Point;
    }
    else if (GeomType == TEXT("LineString"))
    {
        OutFeature.GeometryType = EGISGeometryType::LineString;
    }
    else if (GeomType == TEXT("Polygon"))
    {
        OutFeature.GeometryType = EGISGeometryType::Polygon;
    }
    else if (GeomType == TEXT("MultiPolygon"))
    {
        OutFeature.GeometryType = EGISGeometryType::MultiPolygon;
    }
    else
    {
        UE_LOG(LogGIS, Warning, TEXT("GeoJsonParser: Unsupported geometry type: %s"), *GeomType);
        return false;
    }

    // 解析坐标
    const TArray<TSharedPtr<FJsonValue>>* CoordinatesArray;
    if (!GeometryObject->TryGetArrayField(TEXT("coordinates"), CoordinatesArray))
    {
        return false;
    }

    if (OutFeature.GeometryType == EGISGeometryType::Point)
    {
        // Point: [lon, lat]
        if (CoordinatesArray->Num() >= 2)
        {
            double Lon = (*CoordinatesArray)[0]->AsNumber();
            double Lat = (*CoordinatesArray)[1]->AsNumber();
            OutFeature.Coordinates.Add(FVector2D(Lon, Lat));
        }
    }
    else if (OutFeature.GeometryType == EGISGeometryType::LineString)
    {
        // LineString: [[lon, lat], [lon, lat], ...]
        ParseCoordinateArray(*CoordinatesArray, OutFeature.Coordinates);
    }
    else if (OutFeature.GeometryType == EGISGeometryType::Polygon)
    {
        // Polygon: [[[lon, lat], ...], ...] — 取第一个 ring（外环）
        if (CoordinatesArray->Num() > 0)
        {
            const TArray<TSharedPtr<FJsonValue>>* OuterRing;
            if ((*CoordinatesArray)[0]->TryGetArray(OuterRing))
            {
                ParseCoordinateArray(*OuterRing, OutFeature.Coordinates);
            }
        }
    }
    else if (OutFeature.GeometryType == EGISGeometryType::MultiPolygon)
    {
        // MultiPolygon: [[[[lon, lat], ...], ...], ...] — 取第一个 Polygon 的外环
        if (CoordinatesArray->Num() > 0)
        {
            const TArray<TSharedPtr<FJsonValue>>* FirstPolygon;
            if ((*CoordinatesArray)[0]->TryGetArray(FirstPolygon) && FirstPolygon->Num() > 0)
            {
                const TArray<TSharedPtr<FJsonValue>>* OuterRing;
                if ((*FirstPolygon)[0]->TryGetArray(OuterRing))
                {
                    ParseCoordinateArray(*OuterRing, OutFeature.Coordinates);
                }
            }
        }
    }

    return OutFeature.Coordinates.Num() > 0;
}

bool UGeoJsonParser::ParseCoordinateArray(const TArray<TSharedPtr<FJsonValue>>& JsonArray, TArray<FVector2D>& OutCoords)
{
    OutCoords.Reserve(JsonArray.Num());

    for (const TSharedPtr<FJsonValue>& CoordValue : JsonArray)
    {
        const TArray<TSharedPtr<FJsonValue>>* CoordPair;
        if (CoordValue->TryGetArray(CoordPair) && CoordPair->Num() >= 2)
        {
            double Lon = (*CoordPair)[0]->AsNumber();
            double Lat = (*CoordPair)[1]->AsNumber();
            OutCoords.Add(FVector2D(Lon, Lat));
        }
    }

    return OutCoords.Num() > 0;
}

void UGeoJsonParser::ParseProperties(const TSharedPtr<FJsonObject>& PropertiesObject, TMap<FString, FString>& OutProperties)
{
    for (const auto& Pair : PropertiesObject->Values)
    {
        FString Value;
        if (Pair.Value->TryGetString(Value))
        {
            OutProperties.Add(Pair.Key, Value);
        }
        else
        {
            // 非字符串类型转为字符串存储
            double NumValue;
            if (Pair.Value->TryGetNumber(NumValue))
            {
                OutProperties.Add(Pair.Key, FString::SanitizeFloat(NumValue));
            }
            else
            {
                bool BoolValue;
                if (Pair.Value->TryGetBool(BoolValue))
                {
                    OutProperties.Add(Pair.Key, BoolValue ? TEXT("true") : TEXT("false"));
                }
            }
        }
    }
}
