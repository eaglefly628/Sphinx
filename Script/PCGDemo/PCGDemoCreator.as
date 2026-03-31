// PCGDemoCreator.as — PCG 程序化生成演示 Actor
// 放置到关卡中 → 配置 mesh → 点击 Generate → 渐进式生成树木和建筑
// 纯 Demo 用途，删除此 Actor 即清除所有生成内容，不影响主体

// ============================================================
// 生成的单个实例数据
// ============================================================
struct FDemoSpawnEntry
{
    FVector Location;
    FRotator Rotation;
    FVector Scale;
    UStaticMesh Mesh;
}

// ============================================================
// PCG Demo Creator
// ============================================================
class APCGDemoCreator : AActor
{
    // -------- 根组件 --------
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent Root;

    // -------- Outline Spline（编辑器中可见） --------
    UPROPERTY(DefaultComponent, Attach = Root)
    USplineComponent TreeOutlineSpline;

    UPROPERTY(DefaultComponent, Attach = Root)
    USplineComponent BuildingOutlineSpline;

    // ======== 可配置参数 ========

    // ---- 树木区 ----
    UPROPERTY(EditAnywhere, Category = "PCG Demo|Tree Zone")
    float TreeZoneRadius = 30000.0f; // 300m (cm)

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Tree Zone")
    FVector TreeZoneOffset = FVector(0, 0, 0);

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Tree Zone")
    int TreeOutlinePoints = 8;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Tree Zone")
    float TreeRandomJitter = 0.3f; // outline 顶点随机扰动比例

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Tree Zone")
    TArray<UStaticMesh> TreeMeshes;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Tree Zone", Meta = (ClampMin = "0.001", ClampMax = "1.0"))
    float TreeDensity = 0.03f; // 每平米棵数

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Tree Zone", Meta = (ClampMin = "100", ClampMax = "5000"))
    float TreeMinSpacing = 500.0f; // 最小间距 5m (cm)

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Tree Zone")
    FVector2D TreeScaleRange = FVector2D(0.7f, 1.4f);

    // ---- 建筑区 ----
    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    float BuildingZoneRadius = 20000.0f; // 200m (cm)

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    FVector BuildingZoneOffset = FVector(50000, 0, 0); // 默认偏移 500m

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    int BuildingOutlinePoints = 6;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    float BuildingRandomJitter = 0.2f;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    TArray<UStaticMesh> BuildingMeshes;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone", Meta = (ClampMin = "1000", ClampMax = "10000"))
    float BuildingSpacing = 3000.0f; // 30m (cm)

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    FVector2D BuildingScaleXYRange = FVector2D(0.8f, 1.2f);

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    FVector2D BuildingScaleZRange = FVector2D(0.8f, 1.5f);

    // ---- 生成控制 ----
    UPROPERTY(EditAnywhere, Category = "PCG Demo|Generation", Meta = (ClampMin = "0.005", ClampMax = "0.5"))
    float SpawnInterval = 0.02f; // 渐进生成间隔（秒/个）

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Generation")
    bool bTraceToGround = true; // Line Trace 投影到地面

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Generation", Meta = (ClampMin = "10000", ClampMax = "500000"))
    float TraceHeight = 100000.0f; // Line Trace 起始高度 1000m (cm)

    // ======== 内部状态 ========
    private TArray<AActor> SpawnedActors;
    private TArray<FDemoSpawnEntry> PendingSpawns;
    private int CurrentSpawnIndex = 0;
    private bool bIsGenerating = false;

    // ======== 构造 ========
    UFUNCTION(BlueprintOverride)
    void ConstructionScript()
    {
        // 设置 Spline 外观
        TreeOutlineSpline.SetDrawDebug(true);
        TreeOutlineSpline.SetUnselectedSplineSegmentColor(FLinearColor(0.1f, 0.8f, 0.2f, 1.0f)); // 绿色
        BuildingOutlineSpline.SetDrawDebug(true);
        BuildingOutlineSpline.SetUnselectedSplineSegmentColor(FLinearColor(0.9f, 0.6f, 0.1f, 1.0f)); // 橙色

        // 构建 outline
        BuildOutlineSpline(TreeOutlineSpline, TreeZoneOffset, TreeZoneRadius, TreeOutlinePoints, TreeRandomJitter);
        BuildOutlineSpline(BuildingOutlineSpline, BuildingZoneOffset, BuildingZoneRadius, BuildingOutlinePoints, BuildingRandomJitter);
    }

    // ======== 编辑器按钮 ========

    UFUNCTION(CallInEditor, Category = "PCG Demo")
    void Generate()
    {
        if (bIsGenerating)
        {
            Print("[PCGDemo] Already generating, please wait...");
            return;
        }

        if (TreeMeshes.Num() == 0 && BuildingMeshes.Num() == 0)
        {
            Print("[PCGDemo] ERROR: Please assign at least one mesh in TreeMeshes or BuildingMeshes!");
            return;
        }

        // 清除旧的
        ClearGenerated();

        Print("[PCGDemo] ===== GENERATION START =====");

        // 收集所有待生成点
        PendingSpawns.Empty();
        CurrentSpawnIndex = 0;

        // 树木区采样
        if (TreeMeshes.Num() > 0)
        {
            TArray<FVector> treeOutline = GetSplinePolygonPoints(TreeOutlineSpline);
            TArray<FVector> treePoints = SamplePointsInPolygon(treeOutline, TreeMinSpacing);
            Print("[PCGDemo] Tree zone: " + treePoints.Num() + " points sampled");

            for (int i = 0; i < treePoints.Num(); i++)
            {
                FDemoSpawnEntry entry;
                entry.Location = treePoints[i];
                entry.Rotation = FRotator(0, FMath::RandRange(0.0f, 360.0f), 0);
                float s = FMath::RandRange(TreeScaleRange.X, TreeScaleRange.Y);
                entry.Scale = FVector(s, s, s);
                entry.Mesh = TreeMeshes[FMath::RandRange(0, TreeMeshes.Num() - 1)];
                PendingSpawns.Add(entry);
            }
        }

        // 建筑区采样
        if (BuildingMeshes.Num() > 0)
        {
            TArray<FVector> buildOutline = GetSplinePolygonPoints(BuildingOutlineSpline);
            TArray<FVector> buildPoints = SamplePointsInPolygon(buildOutline, BuildingSpacing);
            Print("[PCGDemo] Building zone: " + buildPoints.Num() + " points sampled");

            for (int i = 0; i < buildPoints.Num(); i++)
            {
                FDemoSpawnEntry entry;
                entry.Location = buildPoints[i];
                // 正交朝向: 0/90/180/270
                float yaw = float(FMath::RandRange(0, 3)) * 90.0f;
                entry.Rotation = FRotator(0, yaw, 0);
                float sXY = FMath::RandRange(BuildingScaleXYRange.X, BuildingScaleXYRange.Y);
                float sZ = FMath::RandRange(BuildingScaleZRange.X, BuildingScaleZRange.Y);
                entry.Scale = FVector(sXY, sXY, sZ);
                entry.Mesh = BuildingMeshes[FMath::RandRange(0, BuildingMeshes.Num() - 1)];
                PendingSpawns.Add(entry);
            }
        }

        if (PendingSpawns.Num() == 0)
        {
            Print("[PCGDemo] No points to spawn. Check outline size and spacing.");
            return;
        }

        // 随机打乱顺序（混合树木和建筑，视觉更好）
        ShuffleSpawnList();

        Print("[PCGDemo] Total: " + PendingSpawns.Num() + " instances, generating at " + SpawnInterval + "s intervals...");
        bIsGenerating = true;

        // 启动渐进式生成 Timer
        System::SetTimer(this, n"SpawnNextBatch", SpawnInterval, true);
    }

    UFUNCTION(CallInEditor, Category = "PCG Demo")
    void Clear()
    {
        ClearGenerated();
        Print("[PCGDemo] All generated content cleared.");
    }

    UFUNCTION(CallInEditor, Category = "PCG Demo")
    void RandomizeOutlines()
    {
        BuildOutlineSpline(TreeOutlineSpline, TreeZoneOffset, TreeZoneRadius, TreeOutlinePoints, TreeRandomJitter);
        BuildOutlineSpline(BuildingOutlineSpline, BuildingZoneOffset, BuildingZoneRadius, BuildingOutlinePoints, BuildingRandomJitter);
        Print("[PCGDemo] Outlines randomized.");
    }

    // ======== 渐进式生成 ========

    UFUNCTION()
    private void SpawnNextBatch()
    {
        int batchSize = FMath::Max(1, FMath::CeilToInt(1.0f / (SpawnInterval * 30.0f)));
        int endIndex = FMath::Min(CurrentSpawnIndex + batchSize, PendingSpawns.Num());

        for (int i = CurrentSpawnIndex; i < endIndex; i++)
        {
            SpawnSingleInstance(PendingSpawns[i]);
        }

        CurrentSpawnIndex = endIndex;

        // 进度日志（每 10% 打印一次）
        int progress = FMath::FloorToInt(float(CurrentSpawnIndex) / float(PendingSpawns.Num()) * 100.0f);
        int prevProgress = FMath::FloorToInt(float(CurrentSpawnIndex - batchSize) / float(PendingSpawns.Num()) * 100.0f);
        if (progress / 10 > prevProgress / 10)
        {
            Print("[PCGDemo] Progress: " + progress + "% (" + CurrentSpawnIndex + "/" + PendingSpawns.Num() + ")");
        }

        if (CurrentSpawnIndex >= PendingSpawns.Num())
        {
            System::ClearTimer(this, n"SpawnNextBatch");
            bIsGenerating = false;
            Print("[PCGDemo] ===== GENERATION COMPLETE =====");
            Print("[PCGDemo] " + SpawnedActors.Num() + " instances spawned.");
        }
    }

    // ======== 核心函数 ========

    private void SpawnSingleInstance(const FDemoSpawnEntry& Entry)
    {
        FVector spawnLoc = Entry.Location;

        if (bTraceToGround)
        {
            FVector traceStart = FVector(spawnLoc.X, spawnLoc.Y, spawnLoc.Z + TraceHeight);
            FVector traceEnd = FVector(spawnLoc.X, spawnLoc.Y, spawnLoc.Z - TraceHeight);
            FHitResult hit;
            TArray<AActor> ignore;
            ignore.Add(this);
            for (AActor a : SpawnedActors)
            {
                if (a != nullptr)
                    ignore.Add(a);
            }

            if (System::LineTraceSingle(traceStart, traceEnd,
                ETraceTypeQuery::Visibility, false, ignore,
                EDrawDebugTrace::None, hit, true))
            {
                spawnLoc = hit.Location;
            }
        }

        AActor spawned = SpawnActor(AStaticMeshActor, spawnLoc, Entry.Rotation);
        if (spawned == nullptr)
            return;

        AStaticMeshActor meshActor = Cast<AStaticMeshActor>(spawned);
        if (meshActor != nullptr)
        {
            meshActor.StaticMeshComponent.SetStaticMesh(Entry.Mesh);
            meshActor.SetActorScale3D(Entry.Scale);
            meshActor.SetMobility(EComponentMobility::Movable);
            meshActor.AttachToActor(this, NAME_None, EAttachmentRule::KeepWorld);

            #if EDITOR
            meshActor.SetActorLabel("PCGDemo_" + SpawnedActors.Num());
            #endif
        }

        SpawnedActors.Add(spawned);
    }

    private void ClearGenerated()
    {
        if (bIsGenerating)
        {
            System::ClearTimer(this, n"SpawnNextBatch");
            bIsGenerating = false;
        }

        for (int i = SpawnedActors.Num() - 1; i >= 0; i--)
        {
            if (SpawnedActors[i] != nullptr)
            {
                SpawnedActors[i].DestroyActor();
            }
        }
        SpawnedActors.Empty();
        PendingSpawns.Empty();
        CurrentSpawnIndex = 0;
    }

    // ======== Outline 构建 ========

    private void BuildOutlineSpline(USplineComponent Spline, FVector Offset, float Radius, int NumPoints, float Jitter)
    {
        Spline.ClearSplinePoints(false);

        for (int i = 0; i < NumPoints; i++)
        {
            float angle = (float(i) / float(NumPoints)) * 360.0f;
            float jitteredRadius = Radius * (1.0f + FMath::RandRange(-Jitter, Jitter));
            float rad = FMath::DegreesToRadians(angle);

            FVector point = FVector(
                FMath::Cos(rad) * jitteredRadius,
                FMath::Sin(rad) * jitteredRadius,
                0
            ) + Offset;

            Spline.AddSplinePointAtIndex(point, i, ESplineCoordinateSpace::Local, false);
        }

        Spline.SetClosedLoop(true, false);
        for (int i = 0; i < NumPoints; i++)
        {
            Spline.SetSplinePointType(i, ESplinePointType::CurveCustomTangent, false);
        }
        Spline.UpdateSpline();
    }

    // ======== 多边形内采样 ========

    private TArray<FVector> GetSplinePolygonPoints(USplineComponent Spline)
    {
        TArray<FVector> points;
        int numPoints = Spline.GetNumberOfSplinePoints();
        for (int i = 0; i < numPoints; i++)
        {
            points.Add(Spline.GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World));
        }
        return points;
    }

    private TArray<FVector> SamplePointsInPolygon(const TArray<FVector>& Polygon, float Spacing)
    {
        TArray<FVector> results;
        if (Polygon.Num() < 3)
            return results;

        FVector bMin = Polygon[0];
        FVector bMax = Polygon[0];
        for (int i = 1; i < Polygon.Num(); i++)
        {
            bMin.X = FMath::Min(bMin.X, Polygon[i].X);
            bMin.Y = FMath::Min(bMin.Y, Polygon[i].Y);
            bMax.X = FMath::Max(bMax.X, Polygon[i].X);
            bMax.Y = FMath::Max(bMax.Y, Polygon[i].Y);
        }

        float halfSpacing = Spacing * 0.4f;
        for (float x = bMin.X; x <= bMax.X; x += Spacing)
        {
            for (float y = bMin.Y; y <= bMax.Y; y += Spacing)
            {
                float jx = x + FMath::RandRange(-halfSpacing, halfSpacing);
                float jy = y + FMath::RandRange(-halfSpacing, halfSpacing);
                FVector candidate = FVector(jx, jy, bMin.Z);

                if (IsPointInPolygon2D(candidate, Polygon))
                {
                    results.Add(candidate);
                }
            }
        }

        return results;
    }

    // ======== 点在多边形内测试 (Ray Casting) ========

    private bool IsPointInPolygon2D(const FVector& Point, const TArray<FVector>& Polygon)
    {
        int n = Polygon.Num();
        bool inside = false;

        int j = n - 1;
        for (int i = 0; i < n; i++)
        {
            float yi = Polygon[i].Y;
            float yj = Polygon[j].Y;
            float xi = Polygon[i].X;
            float xj = Polygon[j].X;

            if (((yi > Point.Y) != (yj > Point.Y)) &&
                (Point.X < (xj - xi) * (Point.Y - yi) / (yj - yi) + xi))
            {
                inside = !inside;
            }
            j = i;
        }

        return inside;
    }

    // ======== 工具 ========

    private void ShuffleSpawnList()
    {
        for (int i = PendingSpawns.Num() - 1; i > 0; i--)
        {
            int j = FMath::RandRange(0, i);
            FDemoSpawnEntry temp = PendingSpawns[i];
            PendingSpawns[i] = PendingSpawns[j];
            PendingSpawns[j] = temp;
        }
    }

    // ======== 清理保障 ========

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        if (bIsGenerating)
        {
            System::ClearTimer(this, n"SpawnNextBatch");
        }
    }
}
