// MockPolygonGenerator.as — 生成假的 FLandUsePolygon 数据
// 模拟矢量数据库查询结果，验证 Polygon → PCG 完整链路

class AMockPolygonGenerator : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent Root;

    // ======== 区域配置 ========
    UPROPERTY(EditAnywhere, Category = "Mock Data|Area")
    float AreaRadius = 50000.0f;

    UPROPERTY(EditAnywhere, Category = "Mock Data|Area")
    float PolygonMinRadius = 2000.0f;

    UPROPERTY(EditAnywhere, Category = "Mock Data|Area")
    float PolygonMaxRadius = 8000.0f;

    // ======== 数量配置 ========
    UPROPERTY(EditAnywhere, Category = "Mock Data|Counts")
    int ForestCount = 8;

    UPROPERTY(EditAnywhere, Category = "Mock Data|Counts")
    int ResidentialCount = 5;

    UPROPERTY(EditAnywhere, Category = "Mock Data|Counts")
    int CommercialCount = 3;

    UPROPERTY(EditAnywhere, Category = "Mock Data|Counts")
    int IndustrialCount = 2;

    UPROPERTY(EditAnywhere, Category = "Mock Data|Counts")
    int FarmlandCount = 3;

    UPROPERTY(EditAnywhere, Category = "Mock Data|Counts")
    int OpenSpaceCount = 2;

    // ======== 输出 ========
    UPROPERTY(EditAnywhere, Category = "Mock Data|Output")
    ULandUseMapDataAsset DataAsset;

    UPROPERTY(EditAnywhere, Category = "Mock Data|Debug")
    bool bDrawDebugPolygons = true;

    UPROPERTY(EditAnywhere, Category = "Mock Data|Debug")
    float DebugDrawDuration = 30.0f;

    // ======== 内部临时存储 ========
    private TArray<FLandUsePolygon> TempPolygons;
    private int NextID = 0;

    // ======== 编辑器按钮 ========

    UFUNCTION(CallInEditor, Category = "Mock Data")
    void GenerateMockData()
    {
        if (DataAsset == nullptr)
        {
            Print("[MockGen] ERROR: Assign a DataAsset first!");
            return;
        }

        Print("[MockGen] ===== GENERATING =====");

        TempPolygons.Empty();
        NextID = 0;
        FVector center = GetActorLocation();

        AddPolygonsOfType(ELandUseType::Forest, ForestCount, center, 0.0f, 0.8f, 1, 1, 0.0f);
        AddPolygonsOfType(ELandUseType::Residential, ResidentialCount, center, 0.4f, 0.3f, 2, 5, 8.0f);
        AddPolygonsOfType(ELandUseType::Commercial, CommercialCount, center, 0.7f, 0.1f, 3, 12, 5.0f);
        AddPolygonsOfType(ELandUseType::Industrial, IndustrialCount, center, 0.3f, 0.1f, 1, 3, 15.0f);
        AddPolygonsOfType(ELandUseType::Farmland, FarmlandCount, center, 0.0f, 0.6f, 1, 1, 0.0f);
        AddPolygonsOfType(ELandUseType::OpenSpace, OpenSpaceCount, center, 0.0f, 0.4f, 1, 1, 0.0f);

        Print("[MockGen] TempPolygons count: " + TempPolygons.Num());

        // 验证第一个
        if (TempPolygons.Num() > 0)
        {
            Print("[MockGen] poly[0]: verts=" + TempPolygons[0].WorldVertices.Num()
                + " type=" + int(TempPolygons[0].LandUseType)
                + " area=" + TempPolygons[0].AreaSqM);
        }

        // 赋值给 DataAsset
        DataAsset.Polygons = TempPolygons;

        Print("[MockGen] DataAsset.Polygons count: " + DataAsset.Polygons.Num());

        if (DataAsset.Polygons.Num() > 0)
        {
            Print("[MockGen] DA poly[0] verts=" + DataAsset.Polygons[0].WorldVertices.Num());
        }

        DataAsset.BuildSpatialIndex();

        if (bDrawDebugPolygons)
            DrawAllPolygons();

        Print("[MockGen] ===== DONE: " + DataAsset.Polygons.Num() + " polygons =====");
    }

    UFUNCTION(CallInEditor, Category = "Mock Data")
    void DrawPolygons()
    {
        if (DataAsset == nullptr || DataAsset.Polygons.Num() == 0)
        {
            Print("[MockGen] No data.");
            return;
        }
        DrawAllPolygons();
    }

    UFUNCTION(CallInEditor, Category = "Mock Data")
    void ClearData()
    {
        if (DataAsset != nullptr)
        {
            DataAsset.Polygons.Empty();
            DataAsset.ClearSpatialIndex();
        }
        TempPolygons.Empty();
        Print("[MockGen] Cleared.");
    }

    // ======== 生成逻辑（用成员变量，不传引用） ========

    private void AddPolygonsOfType(
        ELandUseType Type, int Count, FVector AreaCenter,
        float BuildDensity, float VegDensity,
        int MinFloors, int MaxFloors, float Setback)
    {
        for (int i = 0; i < Count; i++)
        {
            float angle = Math::RandRange(0.0f, 360.0f);
            float dist = Math::RandRange(AreaRadius * 0.1f, AreaRadius);
            float rad = Math::DegreesToRadians(angle);
            FVector polyCenter = AreaCenter + FVector(
                Math::Cos(rad) * dist,
                Math::Sin(rad) * dist,
                0
            );

            // Ground trace
            FVector traceStart = FVector(polyCenter.X, polyCenter.Y, polyCenter.Z + 100000.0f);
            FVector traceEnd = FVector(polyCenter.X, polyCenter.Y, polyCenter.Z - 100000.0f);
            FHitResult hit;
            TArray<AActor> ignore;
            ignore.Add(this);
            if (System::LineTraceSingle(traceStart, traceEnd,
                ETraceTypeQuery::Visibility, false, ignore,
                EDrawDebugTrace::None, hit, true))
            {
                polyCenter = hit.Location;
            }

            // 生成多边形顶点
            float polyRadius = Math::RandRange(PolygonMinRadius, PolygonMaxRadius);
            int numVerts = Math::RandRange(5, 10);
            TArray<FVector> verts;
            for (int v = 0; v < numVerts; v++)
            {
                float vAngle = (float(v) / float(numVerts)) * 360.0f;
                float vRad = Math::DegreesToRadians(vAngle);
                float jitter = Math::RandRange(0.7f, 1.3f);
                verts.Add(polyCenter + FVector(
                    Math::Cos(vRad) * polyRadius * jitter,
                    Math::Sin(vRad) * polyRadius * jitter,
                    0
                ));
            }

            // AABB
            FVector bMin = verts[0];
            FVector bMax = verts[0];
            for (int v = 1; v < verts.Num(); v++)
            {
                bMin.X = Math::Min(bMin.X, verts[v].X);
                bMin.Y = Math::Min(bMin.Y, verts[v].Y);
                bMin.Z = Math::Min(bMin.Z, verts[v].Z);
                bMax.X = Math::Max(bMax.X, verts[v].X);
                bMax.Y = Math::Max(bMax.Y, verts[v].Y);
                bMax.Z = Math::Max(bMax.Z, verts[v].Z);
            }

            float areaSqM = 3.14159f * (polyRadius / 100.0f) * (polyRadius / 100.0f);

            FLandUsePolygon poly;
            poly.PolygonID = NextID;
            poly.TileCoord = FIntPoint(0, 0);
            poly.LandUseType = Type;
            poly.WorldVertices = verts;
            poly.AreaSqM = areaSqM;
            poly.WorldCenter = polyCenter;
            poly.WorldBounds = FBox(bMin, bMax);
            poly.AvgElevation = polyCenter.Z / 100.0f;
            poly.AvgSlope = Math::RandRange(0.0f, 15.0f);
            poly.bAdjacentToMainRoad = (Math::RandRange(0, 3) == 0);
            poly.BuildingDensity = BuildDensity + Math::RandRange(-0.1f, 0.1f);
            poly.VegetationDensity = VegDensity + Math::RandRange(-0.1f, 0.1f);
            poly.MinFloors = MinFloors;
            poly.MaxFloors = MaxFloors;
            poly.BuildingSetback = Setback;

            TempPolygons.Add(poly);
            NextID++;
        }
    }

    // ======== Debug 可视化 ========

    private void DrawAllPolygons()
    {
        for (int i = 0; i < DataAsset.Polygons.Num(); i++)
        {
            FLandUsePolygon poly = DataAsset.Polygons[i];
            FLinearColor color = GetColorForType(poly.LandUseType);

            for (int v = 0; v < poly.WorldVertices.Num(); v++)
            {
                int next = (v + 1) % poly.WorldVertices.Num();
                FVector from = poly.WorldVertices[v] + FVector(0, 0, 50);
                FVector to = poly.WorldVertices[next] + FVector(0, 0, 50);
                System::DrawDebugLine(from, to, color, DebugDrawDuration, 3.0f);
            }
            System::DrawDebugPoint(poly.WorldCenter + FVector(0, 0, 100), 10.0f, color, DebugDrawDuration);
        }
    }

    private FLinearColor GetColorForType(ELandUseType Type)
    {
        switch (Type)
        {
            case ELandUseType::Forest:      return FLinearColor(0.1f, 0.8f, 0.1f, 1.0f);
            case ELandUseType::Residential: return FLinearColor(0.9f, 0.7f, 0.2f, 1.0f);
            case ELandUseType::Commercial:  return FLinearColor(0.2f, 0.4f, 0.9f, 1.0f);
            case ELandUseType::Industrial:  return FLinearColor(0.6f, 0.3f, 0.6f, 1.0f);
            case ELandUseType::Farmland:    return FLinearColor(0.8f, 0.8f, 0.2f, 1.0f);
            case ELandUseType::OpenSpace:   return FLinearColor(0.5f, 0.9f, 0.5f, 1.0f);
            case ELandUseType::Water:       return FLinearColor(0.1f, 0.3f, 0.9f, 1.0f);
            case ELandUseType::Road:        return FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
            case ELandUseType::Military:    return FLinearColor(0.9f, 0.1f, 0.1f, 1.0f);
        }
        return FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
    }
}
