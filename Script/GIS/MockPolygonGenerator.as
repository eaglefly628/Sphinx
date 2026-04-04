// MockPolygonGenerator.as — 生成假的 FLandUsePolygon 数据
// 模拟矢量数据库查询结果，验证 Polygon → PCG 完整链路
//
// 用法：
//   1. 编辑器中放置 AMockPolygonGenerator 到场景
//   2. 配置 CenterLocation（放到 Cesium 地面上）和区域大小
//   3. 点击 "GenerateAndSave" 生成假多边形并保存为 DataAsset
//   4. 在 PCG Graph 中用 GIS Land Use Sampler 节点引用该 DataAsset
//   5. PCG 按 LandUseType 过滤 → 分流到树木/建筑 Mesh Spawner

class AMockPolygonGenerator : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent Root;

    // ======== 区域配置 ========

    // 生成区域半径（cm）
    UPROPERTY(EditAnywhere, Category = "Mock Data|Area")
    float AreaRadius = 50000.0f; // 500m

    // 多边形最小/最大半径（cm）
    UPROPERTY(EditAnywhere, Category = "Mock Data|Area")
    float PolygonMinRadius = 2000.0f; // 20m

    UPROPERTY(EditAnywhere, Category = "Mock Data|Area")
    float PolygonMaxRadius = 8000.0f; // 80m

    // ======== 生成数量（按类型） ========

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

    // 生成的 DataAsset 引用（Generate 后自动填充）
    UPROPERTY(EditAnywhere, Category = "Mock Data|Output")
    ULandUseMapDataAsset DataAsset;

    // ======== 可视化 ========
    UPROPERTY(EditAnywhere, Category = "Mock Data|Debug")
    bool bDrawDebugPolygons = true;

    UPROPERTY(EditAnywhere, Category = "Mock Data|Debug")
    float DebugDrawDuration = 30.0f;

    // ======== 编辑器按钮 ========

    UFUNCTION(CallInEditor, Category = "Mock Data")
    void GenerateMockData()
    {
        if (DataAsset == nullptr)
        {
            Print("[MockGen] ERROR: Assign a DataAsset first! (Create one in Content Browser: Right-click → GIS → LandUseMapDataAsset)");
            return;
        }

        Print("[MockGen] ===== GENERATING MOCK POLYGON DATA =====");

        DataAsset.Polygons.Empty();
        FVector center = GetActorLocation();
        int nextID = 0;

        // 按类型生成
        nextID = GeneratePolygonsOfType(ELandUseType::Forest, ForestCount, center, nextID,
            0.0f, 0.1f, 0.8f, 1, 1, 0.0f);

        nextID = GeneratePolygonsOfType(ELandUseType::Residential, ResidentialCount, center, nextID,
            0.4f, 0.3f, 0.3f, 2, 5, 8.0f);

        nextID = GeneratePolygonsOfType(ELandUseType::Commercial, CommercialCount, center, nextID,
            0.7f, 0.1f, 0.1f, 3, 12, 5.0f);

        nextID = GeneratePolygonsOfType(ELandUseType::Industrial, IndustrialCount, center, nextID,
            0.3f, 0.05f, 0.1f, 1, 3, 15.0f);

        nextID = GeneratePolygonsOfType(ELandUseType::Farmland, FarmlandCount, center, nextID,
            0.0f, 0.0f, 0.6f, 1, 1, 0.0f);

        nextID = GeneratePolygonsOfType(ELandUseType::OpenSpace, OpenSpaceCount, center, nextID,
            0.0f, 0.0f, 0.4f, 1, 1, 0.0f);

        // 构建空间索引
        DataAsset.BuildSpatialIndex();

        Print("[MockGen] Total: " + DataAsset.Polygons.Num() + " polygons generated");
        Print("[MockGen] Forest=" + ForestCount
            + " Residential=" + ResidentialCount
            + " Commercial=" + CommercialCount
            + " Industrial=" + IndustrialCount
            + " Farmland=" + FarmlandCount
            + " OpenSpace=" + OpenSpaceCount);

        // 可视化
        if (bDrawDebugPolygons)
        {
            DrawAllPolygons();
        }

        Print("[MockGen] ===== DONE =====");
        Print("[MockGen] DataAsset ready. Use 'GIS Land Use Sampler' PCG node to consume it.");
    }

    UFUNCTION(CallInEditor, Category = "Mock Data")
    void DrawPolygons()
    {
        if (DataAsset == nullptr || DataAsset.Polygons.Num() == 0)
        {
            Print("[MockGen] No data to draw.");
            return;
        }
        DrawAllPolygons();
        Print("[MockGen] Drawing " + DataAsset.Polygons.Num() + " polygons for " + DebugDrawDuration + "s");
    }

    UFUNCTION(CallInEditor, Category = "Mock Data")
    void ClearData()
    {
        if (DataAsset != nullptr)
        {
            DataAsset.Polygons.Empty();
            DataAsset.ClearSpatialIndex();
            Print("[MockGen] DataAsset cleared.");
        }
    }

    // ======== 核心生成逻辑 ========

    private int GeneratePolygonsOfType(
        ELandUseType Type, int Count, FVector AreaCenter, int StartID,
        float BuildingDensity, float BldDensityVariance,
        float VegetationDensity,
        int MinFloors, int MaxFloors,
        float Setback)
    {
        int id = StartID;
        for (int i = 0; i < Count; i++)
        {
            // 随机位置（在区域内）
            float angle = Math::RandRange(0.0f, 360.0f);
            float dist = Math::RandRange(0.0f, AreaRadius);
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

            // 生成不规则多边形顶点
            float polyRadius = Math::RandRange(PolygonMinRadius, PolygonMaxRadius);
            int numVerts = Math::RandRange(5, 10);
            TArray<FVector> verts;
            for (int v = 0; v < numVerts; v++)
            {
                float vAngle = (float(v) / float(numVerts)) * 360.0f;
                float vRad = Math::DegreesToRadians(vAngle);
                float jitter = Math::RandRange(0.7f, 1.3f);
                FVector vert = polyCenter + FVector(
                    Math::Cos(vRad) * polyRadius * jitter,
                    Math::Sin(vRad) * polyRadius * jitter,
                    0
                );
                verts.Add(vert);
            }

            // 计算面积（简化：用半径近似圆面积）
            float areaSqM = 3.14159f * (polyRadius / 100.0f) * (polyRadius / 100.0f);

            // 计算 AABB
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

            // 填充 FLandUsePolygon
            FLandUsePolygon poly;
            poly.PolygonID = id;
            poly.TileCoord = FIntPoint(0, 0);
            poly.LandUseType = Type;
            poly.WorldVertices = verts;
            poly.AreaSqM = areaSqM;
            poly.WorldCenter = polyCenter;
            poly.WorldBounds = FBox(bMin, bMax);
            poly.AvgElevation = polyCenter.Z / 100.0f;
            poly.AvgSlope = Math::RandRange(0.0f, 15.0f);
            poly.bAdjacentToMainRoad = (Math::RandRange(0, 3) == 0);
            poly.BuildingDensity = BuildingDensity + Math::RandRange(-BldDensityVariance, BldDensityVariance);
            poly.VegetationDensity = VegetationDensity + Math::RandRange(-0.1f, 0.1f);
            poly.MinFloors = MinFloors;
            poly.MaxFloors = MaxFloors;
            poly.BuildingSetback = Setback;

            DataAsset.Polygons.Add(poly);
            id++;
        }
        return id;
    }

    // ======== Debug 可视化 ========

    private void DrawAllPolygons()
    {
        for (int i = 0; i < DataAsset.Polygons.Num(); i++)
        {
            FLandUsePolygon poly = DataAsset.Polygons[i];
            FLinearColor color = GetColorForType(poly.LandUseType);

            // 画多边形边
            for (int v = 0; v < poly.WorldVertices.Num(); v++)
            {
                int next = (v + 1) % poly.WorldVertices.Num();
                FVector from = poly.WorldVertices[v] + FVector(0, 0, 50); // 稍微抬高
                FVector to = poly.WorldVertices[next] + FVector(0, 0, 50);
                System::DrawDebugLine(from, to, color, DebugDrawDuration, 3.0f);
            }

            // 标注中心
            System::DrawDebugPoint(poly.WorldCenter + FVector(0, 0, 100),
                10.0f, color, DebugDrawDuration);
        }
    }

    private FLinearColor GetColorForType(ELandUseType Type)
    {
        switch (Type)
        {
            case ELandUseType::Forest:      return FLinearColor(0.1f, 0.8f, 0.1f, 1.0f); // 绿
            case ELandUseType::Residential: return FLinearColor(0.9f, 0.7f, 0.2f, 1.0f); // 橙
            case ELandUseType::Commercial:  return FLinearColor(0.2f, 0.4f, 0.9f, 1.0f); // 蓝
            case ELandUseType::Industrial:  return FLinearColor(0.6f, 0.3f, 0.6f, 1.0f); // 紫
            case ELandUseType::Farmland:    return FLinearColor(0.8f, 0.8f, 0.2f, 1.0f); // 黄
            case ELandUseType::OpenSpace:   return FLinearColor(0.5f, 0.9f, 0.5f, 1.0f); // 浅绿
            case ELandUseType::Water:       return FLinearColor(0.1f, 0.3f, 0.9f, 1.0f); // 深蓝
            case ELandUseType::Road:        return FLinearColor(0.5f, 0.5f, 0.5f, 1.0f); // 灰
            case ELandUseType::Military:    return FLinearColor(0.9f, 0.1f, 0.1f, 1.0f); // 红
        }
        return FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
    }
}
