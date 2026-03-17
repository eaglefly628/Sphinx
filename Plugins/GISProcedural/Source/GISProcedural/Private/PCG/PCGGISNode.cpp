// PCGGISNode.cpp - 自定义 PCG 节点实现
#include "PCG/PCGGISNode.h"
#include "PCG/PCGLandUseData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGHelpers.h"

UPCGGISLandUseSampler::UPCGGISLandUseSampler()
{
    // 默认配置
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
        "Samples points within GIS land use polygons.\nEach point carries LandUseType attribute for downstream filtering.");
}

EPCGSettingsType UPCGGISLandUseSampler::GetType() const
{
    return EPCGSettingsType::Sampler;
}
#endif

TArray<FPCGPinProperties> UPCGGISLandUseSampler::InputPinProperties() const
{
    TArray<FPCGPinProperties> Properties;
    // 输入: 可选的 LandUse 数据
    Properties.Emplace(
        PCGPinConstants::DefaultInputLabel,
        EPCGDataType::Any,
        false /*bAllowMultipleConnections*/,
        false /*bAllowMultipleData*/
    );
    return Properties;
}

TArray<FPCGPinProperties> UPCGGISLandUseSampler::OutputPinProperties() const
{
    TArray<FPCGPinProperties> Properties;
    // 输出: Point Data（带 LandUseType 属性）
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

    // 创建输出 Point Data
    UPCGPointData* OutputPointData = NewObject<UPCGPointData>();
    TArray<FPCGPoint>& OutputPoints = OutputPointData->GetMutablePoints();

    // TODO: 从 PolygonDataPath 加载 Polygon 数据
    // TODO: 对每个 Polygon 内部做网格采样
    // TODO: 每个采样点设置 LandUseType 属性
    //
    // 采样逻辑伪代码：
    // for each Polygon:
    //   BBox = Polygon.GetBoundingBox()
    //   for x = BBox.Min.X to BBox.Max.X step SamplingInterval:
    //     for y = BBox.Min.Y to BBox.Max.Y step SamplingInterval:
    //       Point = (x, y) + optional jitter
    //       if PointInPolygon(Point, Polygon):
    //         PCGPoint.Transform.SetLocation(Point)
    //         PCGPoint.SetAttribute("LandUseType", Polygon.LandUseType)
    //         OutputPoints.Add(PCGPoint)

    UE_LOG(LogTemp, Log, TEXT("PCGGISLandUseSampler: Generated %d sample points"), OutputPoints.Num());

    // 输出到 PCG Graph
    TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
    FPCGTaggedData& Output = Outputs.Emplace_GetRef();
    Output.Data = OutputPointData;

    return true;
}
