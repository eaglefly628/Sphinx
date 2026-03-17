// GISPolygonComponent.cpp - GIS Polygon 组件实现
#include "Components/GISPolygonComponent.h"
#include "DrawDebugHelpers.h"

UGISPolygonComponent::UGISPolygonComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    bWantsInitializeComponent = true;
}

void UGISPolygonComponent::SetPolygonData(const FLandUsePolygon& InPolygon)
{
    PolygonData = InPolygon;
}

ELandUseType UGISPolygonComponent::GetLandUseType() const
{
    return PolygonData.LandUseType;
}

float UGISPolygonComponent::GetArea() const
{
    return PolygonData.AreaSqM;
}

bool UGISPolygonComponent::IsPointInside(const FVector& WorldPoint) const
{
    return PointInPolygon2D(FVector2D(WorldPoint.X, WorldPoint.Y), PolygonData.WorldVertices);
}

FBox UGISPolygonComponent::GetBoundingBox() const
{
    FBox Box(ForceInit);
    for (const FVector& V : PolygonData.WorldVertices)
    {
        Box += V;
    }
    return Box;
}

#if WITH_EDITOR
void UGISPolygonComponent::OnComponentCreated()
{
    Super::OnComponentCreated();

    // 编辑器中自动绘制 Polygon 轮廓（方便调试）
    if (PolygonData.WorldVertices.Num() < 3)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FColor Color;
    switch (PolygonData.LandUseType)
    {
        case ELandUseType::Residential: Color = FColor::Yellow;  break;
        case ELandUseType::Commercial:  Color = FColor::Red;     break;
        case ELandUseType::Industrial:  Color = FColor::Orange;  break;
        case ELandUseType::Forest:      Color = FColor::Green;   break;
        case ELandUseType::Farmland:    Color = FColor(139, 195, 74); break;
        case ELandUseType::Water:       Color = FColor::Blue;    break;
        case ELandUseType::Road:        Color = FColor(158, 158, 158); break;
        case ELandUseType::Military:    Color = FColor(120, 0, 0); break;
        default:                        Color = FColor(200, 200, 200); break;
    }

    for (int32 i = 0; i < PolygonData.WorldVertices.Num(); ++i)
    {
        const int32 Next = (i + 1) % PolygonData.WorldVertices.Num();
        DrawDebugLine(
            World,
            PolygonData.WorldVertices[i],
            PolygonData.WorldVertices[Next],
            Color,
            true,  // persistent
            -1.0f,
            0,
            3.0f
        );
    }
}
#endif

bool UGISPolygonComponent::PointInPolygon2D(const FVector2D& Point, const TArray<FVector>& Vertices)
{
    // Ray Casting 算法
    const int32 N = Vertices.Num();
    if (N < 3)
    {
        return false;
    }

    bool bInside = false;
    for (int32 i = 0, j = N - 1; i < N; j = i++)
    {
        const FVector2D Vi(Vertices[i].X, Vertices[i].Y);
        const FVector2D Vj(Vertices[j].X, Vertices[j].Y);

        if (((Vi.Y > Point.Y) != (Vj.Y > Point.Y)) &&
            (Point.X < (Vj.X - Vi.X) * (Point.Y - Vi.Y) / (Vj.Y - Vi.Y) + Vi.X))
        {
            bInside = !bInside;
        }
    }

    return bInside;
}
