// PCGGISNode.cpp - 自定义 PCG 节点实现
// 从 DataAsset 读取 Polygon → 网格采样 → 输出 PCG 点
#include "PCG/PCGGISNode.h"
#include "PCG/PCGLandUseData.h"
#include "Data/LandUseMapDataAsset.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAccessor.h"

UPCGGISLandUseSampler::UPCGGISLandUseSampler()
{
    SamplingInterval = 10.0f;
    bJitterPoints = true;
    JitterAmount = 2.0f;
}

#if WITH_EDITOR
FName UPCGGISLandUseSampler::GetDefaultNodeName() const
{
    return FName(TEXT("GISLandUseSampler"));
}

FText UPCGGISLandUseSampler::GetDefaultNodeTitle() const
{
    return NSLOCTEXT("PCGGISNode", "NodeTitle", "GIS Land Use Sampler");
}

FText UPCGGISLandUseSampler::GetNodeTooltipText() const
{
    return NSLOCTEXT("PCGGISNode", "NodeTooltip",
        "Samples points within GIS land use polygons from a DataAsset.\nEach point carries LandUseType attribute for downstream filtering.");
}

EPCGSettingsType UPCGGISLandUseSampler::GetType() const
{
    return EPCGSettingsType::Sampler;
}
#endif

TArray<FPCGPinProperties> UPCGGISLandUseSampler::InputPinProperties() const
{
    TArray<FPCGPinProperties> Properties;
    Properties.Emplace(
        PCGPinConstants::DefaultInputLabel,
        EPCGDataType::Any,
        false,
        false
    );
    return Properties;
}

TArray<FPCGPinProperties> UPCGGISLandUseSampler::OutputPinProperties() const
{
    TArray<FPCGPinProperties> Properties;
    Properties.Emplace(
        PCGPinConstants::DefaultOutputLabel,
        EPCGDataType::Point
    );
    return Properties;
}

FPCGElementPtr UPCGGISLandUseSampler::CreateElement() const
{
    return MakeShared<FPCGGISLandUseSamplerElement>();
}

bool FPCGGISLandUseSamplerElement::ExecuteInternal(FPCGContext* Context) const
{
    check(Context);

    const UPCGGISLandUseSampler* Settings = Context->GetInputSettings<UPCGGISLandUseSampler>();
    if (!Settings)
    {
        return true;
    }

    // 加载 DataAsset
    ULandUseMapDataAsset* DataAsset = Settings->LandUseDataAsset.LoadSynchronous();
    if (!DataAsset || DataAsset->Polygons.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("PCGGISLandUseSampler: No DataAsset or empty polygons"));
        return true;
    }

    // 确保空间索引已建立（首次访问时自动构建）
    if (!DataAsset->HasSpatialIndex() && DataAsset->Polygons.Num() > 100)
    {
        DataAsset->BuildSpatialIndex();
    }

    // 瓦片感知：当启用 tiling 时，只采样当前 PCG 组件周围的 polygon
    const TArray<FLandUsePolygon>* PolygonsToSample = &DataAsset->Polygons;
    TArray<FLandUsePolygon> TileFilteredPolygons;

    if (Settings->bEnableTiling && Context->SourceComponent.IsValid())
    {
        // 获取当前 PCG 组件的世界 bounds
        const FBox ComponentBounds = Context->SourceComponent->Bounds.GetBox();
        const float TileHalfExtent = Settings->TileSizeM * 100.0f * Settings->LoadRadius;
        const FVector Center = ComponentBounds.GetCenter();
        const FBox LoadBounds(
            Center - FVector(TileHalfExtent, TileHalfExtent, HALF_WORLD_MAX),
            Center + FVector(TileHalfExtent, TileHalfExtent, HALF_WORLD_MAX));

        TileFilteredPolygons = DataAsset->GetPolygonsInWorldBounds(LoadBounds);
        PolygonsToSample = &TileFilteredPolygons;

        UE_LOG(LogTemp, Verbose, TEXT("PCGGISLandUseSampler: Tiling enabled, sampling %d/%d polygons in load radius"),
            TileFilteredPolygons.Num(), DataAsset->Polygons.Num());
    }

    // 创建输出
    UPCGPointData* OutputPointData = NewObject<UPCGPointData>();
    TArray<FPCGPoint>& OutputPoints = OutputPointData->GetMutablePoints();

    // 创建 metadata 属性
    UPCGMetadata* Metadata = OutputPointData->MutableMetadata();
    FPCGMetadataAttribute<int32>* LandUseAttr = Metadata->CreateAttribute<int32>(
        FName(TEXT("LandUseType")), 0, true, true);
    FPCGMetadataAttribute<int32>* PolygonIDAttr = Metadata->CreateAttribute<int32>(
        FName(TEXT("PolygonID")), 0, true, true);
    FPCGMetadataAttribute<float>* BuildingDensityAttr = Metadata->CreateAttribute<float>(
        FName(TEXT("BuildingDensity")), 0.0f, true, true);
    FPCGMetadataAttribute<float>* VegetationDensityAttr = Metadata->CreateAttribute<float>(
        FName(TEXT("VegetationDensity")), 0.0f, true, true);

    const float IntervalCm = Settings->SamplingInterval * 100.0f; // 米 → 厘米
    const float JitterCm = Settings->JitterAmount * 100.0f;
    FRandomStream RNG(42);

    for (const FLandUsePolygon& Poly : *PolygonsToSample)
    {
        // 类型过滤
        if (Settings->FilterTypes.Num() > 0 && !Settings->FilterTypes.Contains(Poly.LandUseType))
        {
            continue;
        }

        if (Poly.WorldVertices.Num() < 3)
        {
            continue;
        }

        // 计算 AABB
        FBox PolyBounds(ForceInit);
        for (const FVector& V : Poly.WorldVertices)
        {
            PolyBounds += V;
        }

        // 网格采样
        for (float X = PolyBounds.Min.X; X <= PolyBounds.Max.X; X += IntervalCm)
        {
            for (float Y = PolyBounds.Min.Y; Y <= PolyBounds.Max.Y; Y += IntervalCm)
            {
                FVector SamplePos(X, Y, Poly.WorldCenter.Z);

                // 可选抖动
                if (Settings->bJitterPoints)
                {
                    SamplePos.X += RNG.FRandRange(-JitterCm, JitterCm);
                    SamplePos.Y += RNG.FRandRange(-JitterCm, JitterCm);
                }

                // 点在多边形内测试
                if (!IsPointInPolygon(SamplePos, Poly.WorldVertices))
                {
                    continue;
                }

                FPCGPoint& Point = OutputPoints.Emplace_GetRef();
                Point.Transform.SetLocation(SamplePos);
                Point.SetLocalBounds(FBox(FVector(-50, -50, -50), FVector(50, 50, 50)));
                Point.Density = 1.0f;
                Point.Seed = RNG.GetCurrentSeed();

                // 设置 metadata
                const int64 MetadataEntry = Metadata->AddEntry();
                Point.MetadataEntry = MetadataEntry;

                LandUseAttr->SetValue(MetadataEntry, static_cast<int32>(Poly.LandUseType));
                PolygonIDAttr->SetValue(MetadataEntry, Poly.PolygonID);
                BuildingDensityAttr->SetValue(MetadataEntry, Poly.BuildingDensity);
                VegetationDensityAttr->SetValue(MetadataEntry, Poly.VegetationDensity);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("PCGGISLandUseSampler: Generated %d sample points from %d polygons (total in asset: %d)"),
        OutputPoints.Num(), PolygonsToSample->Num(), DataAsset->Polygons.Num());

    // 输出到 PCG Graph
    TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
    FPCGTaggedData& Output = Outputs.Emplace_GetRef();
    Output.Data = OutputPointData;

    return true;
}

bool FPCGGISLandUseSamplerElement::IsPointInPolygon(const FVector& Point, const TArray<FVector>& PolygonVerts)
{
    const int32 N = PolygonVerts.Num();
    if (N < 3) return false;

    bool bInside = false;
    for (int32 i = 0, j = N - 1; i < N; j = i++)
    {
        if (((PolygonVerts[i].Y > Point.Y) != (PolygonVerts[j].Y > Point.Y)) &&
            (Point.X < (PolygonVerts[j].X - PolygonVerts[i].X) *
             (Point.Y - PolygonVerts[i].Y) / (PolygonVerts[j].Y - PolygonVerts[i].Y) +
             PolygonVerts[i].X))
        {
            bInside = !bInside;
        }
    }
    return bInside;
}
