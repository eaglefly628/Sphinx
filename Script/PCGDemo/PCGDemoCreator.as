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
    float TreeZoneRadius = 30000.0f;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Tree Zone")
    FVector TreeZoneOffset = FVector(0, 0, 0);

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Tree Zone")
    int TreeOutlinePoints = 8;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Tree Zone")
    float TreeRandomJitter = 0.3f;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Tree Zone")
    TArray<UStaticMesh> TreeMeshes;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Tree Zone")
    float TreeMinSpacing = 500.0f;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Tree Zone")
    FVector2D TreeScaleRange = FVector2D(0.7f, 1.4f);

    // ---- 建筑区 ----
    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    float BuildingZoneRadius = 20000.0f;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    FVector BuildingZoneOffset = FVector(50000, 0, 0);

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    int BuildingOutlinePoints = 6;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    float BuildingRandomJitter = 0.2f;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    TArray<UStaticMesh> BuildingMeshes;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    float BuildingSpacing = 3000.0f;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    FVector2D BuildingScaleXYRange = FVector2D(0.8f, 1.2f);

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Building Zone")
    FVector2D BuildingScaleZRange = FVector2D(0.8f, 1.5f);

    // ---- 生成控制 ----
    UPROPERTY(EditAnywhere, Category = "PCG Demo|Generation")
    float SpawnInterval = 0.02f;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Generation")
    bool bTraceToGround = true;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Generation")
    float TraceHeight = 100000.0f;

    // ======== 内部状态 ========
    private TArray<AActor> SpawnedActors;
    private TArray<FDemoSpawnEntry> PendingSpawns;
    private int CurrentSpawnIndex = 0;
    private bool bIsGenerating = false;

    // ======== 构造 ========
    UFUNCTION(BlueprintOverride)
    void ConstructionScript()
    {
        TreeOutlineSpline.SetDrawDebug(true);
        TreeOutlineSpline.SetUnselectedSplineSegmentColor(FLinearColor(0.1f, 0.8f, 0.2f, 1.0f));
        BuildingOutlineSpline.SetDrawDebug(true);
        BuildingOutlineSpline.SetUnselectedSplineSegmentColor(FLinearColor(0.9f, 0.6f, 0.1f, 1.0f));

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

        ClearGenerated();

        Print("[PCGDemo] ===== GENERATION START =====");

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
                entry.Rotation = FRotator(0, Math::RandRange(0.0f, 360.0f), 0);
                float s = Math::RandRange(TreeScaleRange.X, TreeScaleRange.Y);
                entry.Scale = FVector(s, s, s);
                entry.Mesh = TreeMeshes[Math::RandRange(0, TreeMeshes.Num() - 1)];
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
                float yaw = float(Math::RandRange(0, 3)) * 90.0f;
                entry.Rotation = FRotator(0, yaw, 0);
                float sXY = Math::RandRange(BuildingScaleXYRange.X, BuildingScaleXYRange.Y);
                float sZ = Math::RandRange(BuildingScaleZRange.X, BuildingScaleZRange.Y);
                entry.Scale = FVector(sXY, sXY, sZ);
                entry.Mesh = BuildingMeshes[Math::RandRange(0, BuildingMeshes.Num() - 1)];
                PendingSpawns.Add(entry);
            }
        }

        if (PendingSpawns.Num() == 0)
        {
            Print("[PCGDemo] No points to spawn. Check outline size and spacing.");
            return;
        }

        ShuffleSpawnList();

        Print("[PCGDemo] Total: " + PendingSpawns.Num() + " instances, spawning...");

        Print("[PCGDemo] Starting spawn loop...");

        // 先只生成 3 个测试
        int spawnCount = Math::Min(3, PendingSpawns.Num());
        for (int i = 0; i < spawnCount; i++)
        {
            Print("[PCGDemo] Spawning #" + i);
            FDemoSpawnEntry entry = PendingSpawns[i];
            FVector loc = entry.Location;
            Print("[PCGDemo]   loc=(" + loc.X + ", " + loc.Y + ", " + loc.Z + ")");
            Print("[PCGDemo]   mesh=" + (entry.Mesh != nullptr ? "valid" : "NULL"));

            AActor spawned = SpawnActor(AActor, loc, entry.Rotation);
            Print("[PCGDemo]   SpawnActor=" + (spawned != nullptr ? "OK" : "FAILED"));

            if (spawned != nullptr)
            {
                UStaticMeshComponent meshComp = UStaticMeshComponent::Create(spawned);
                Print("[PCGDemo]   MeshComp=" + (meshComp != nullptr ? "OK" : "FAILED"));
                if (meshComp != nullptr && entry.Mesh != nullptr)
                {
                    meshComp.SetStaticMesh(entry.Mesh);
                    meshComp.SetWorldScale3D(entry.Scale);
                    meshComp.SetMobility(EComponentMobility::Movable);
                    meshComp.RegisterComponent();
                    spawned.SetRootComponent(meshComp);
                }
                spawned.AttachToActor(this, NAME_None, EAttachmentRule::KeepWorld);
                SpawnedActors.Add(spawned);
            }
        }

        Print("[PCGDemo] ===== TEST COMPLETE =====");
        Print("[PCGDemo] " + SpawnedActors.Num() + " instances spawned.");
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
    void SpawnNextBatch()
    {
        int batchSize = Math::Max(1, Math::CeilToInt(1.0f / (SpawnInterval * 30.0f)));
        int endIndex = Math::Min(CurrentSpawnIndex + batchSize, PendingSpawns.Num());

        for (int i = CurrentSpawnIndex; i < endIndex; i++)
        {
            SpawnSingleInstance(PendingSpawns[i]);
        }

        CurrentSpawnIndex = endIndex;

        // 进度日志（每 10% 打印一次）
        float progressF = float(CurrentSpawnIndex) / float(PendingSpawns.Num()) * 100.0f;
        int progress = Math::FloorToInt(progressF);
        float prevF = float(CurrentSpawnIndex - batchSize) / float(PendingSpawns.Num()) * 100.0f;
        int prevProgress = Math::FloorToInt(prevF);
        if (Math::IntegerDivisionTrunc(progress, 10) > Math::IntegerDivisionTrunc(prevProgress, 10))
        {
            Print("[PCGDemo] Progress: " + progress + "% (" + CurrentSpawnIndex + "/" + PendingSpawns.Num() + ")");
        }

        if (CurrentSpawnIndex >= PendingSpawns.Num())
        {
            System::ClearTimer(this, "SpawnNextBatch");
            bIsGenerating = false;
            Print("[PCGDemo] ===== GENERATION COMPLETE =====");
            Print("[PCGDemo] " + SpawnedActors.Num() + " instances spawned.");
        }
    }

    // ======== 核心函数 ========

    private void SpawnSingleInstance(FDemoSpawnEntry Entry)
    {
        FVector spawnLoc = Entry.Location;

        if (bTraceToGround)
        {
            FVector traceStart = FVector(spawnLoc.X, spawnLoc.Y, spawnLoc.Z + TraceHeight);
            FVector traceEnd = FVector(spawnLoc.X, spawnLoc.Y, spawnLoc.Z - TraceHeight);
            FHitResult hit;
            TArray<AActor> ignore;
            ignore.Add(this);

            if (System::LineTraceSingle(traceStart, traceEnd,
                ETraceTypeQuery::Visibility, false, ignore,
                EDrawDebugTrace::None, hit, true))
            {
                spawnLoc = hit.Location;
            }
        }

        // 调试：打印前 3 个实例的位置
        if (SpawnedActors.Num() < 3)
        {
            Print("[PCGDemo] DEBUG spawn #" + SpawnedActors.Num()
                + " at (" + spawnLoc.X + ", " + spawnLoc.Y + ", " + spawnLoc.Z + ")"
                + " mesh=" + (Entry.Mesh != nullptr ? "valid" : "NULL"));
        }

        if (Entry.Mesh == nullptr)
        {
            Print("[PCGDemo] ERROR: Mesh is null, skipping");
            return;
        }

        // 用 ISMC 方式：直接创建带 StaticMeshComponent 的 Actor
        AActor spawned = SpawnActor(AActor, spawnLoc, Entry.Rotation);
        if (spawned == nullptr)
        {
            if (SpawnedActors.Num() < 3)
                Print("[PCGDemo] ERROR: SpawnActor returned null");
            return;
        }

        UStaticMeshComponent meshComp = UStaticMeshComponent::Create(spawned);
        meshComp.SetStaticMesh(Entry.Mesh);
        meshComp.SetWorldLocation(spawnLoc);
        meshComp.SetWorldRotation(Entry.Rotation);
        meshComp.SetWorldScale3D(Entry.Scale);
        meshComp.SetMobility(EComponentMobility::Movable);
        meshComp.RegisterComponent();
        spawned.SetRootComponent(meshComp);

        spawned.AttachToActor(this, NAME_None, EAttachmentRule::KeepWorld);

        #if EDITOR
        spawned.SetActorLabel("PCGDemo_" + SpawnedActors.Num());
        #endif

        SpawnedActors.Add(spawned);
    }

    private void ClearGenerated()
    {
        if (bIsGenerating)
        {
            System::ClearTimer(this, "SpawnNextBatch");
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
            float jitteredRadius = Radius * (1.0f + Math::RandRange(-Jitter, Jitter));
            float rad = Math::DegreesToRadians(angle);

            FVector point = FVector(
                Math::Cos(rad) * jitteredRadius,
                Math::Sin(rad) * jitteredRadius,
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

    private TArray<FVector> SamplePointsInPolygon(TArray<FVector> Polygon, float Spacing)
    {
        TArray<FVector> results;
        if (Polygon.Num() < 3)
            return results;

        FVector bMin = Polygon[0];
        FVector bMax = Polygon[0];
        for (int i = 1; i < Polygon.Num(); i++)
        {
            bMin.X = Math::Min(bMin.X, Polygon[i].X);
            bMin.Y = Math::Min(bMin.Y, Polygon[i].Y);
            bMax.X = Math::Max(bMax.X, Polygon[i].X);
            bMax.Y = Math::Max(bMax.Y, Polygon[i].Y);
        }

        float halfSpacing = Spacing * 0.4f;
        for (float x = bMin.X; x <= bMax.X; x += Spacing)
        {
            for (float y = bMin.Y; y <= bMax.Y; y += Spacing)
            {
                float jx = x + Math::RandRange(-halfSpacing, halfSpacing);
                float jy = y + Math::RandRange(-halfSpacing, halfSpacing);
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

    private bool IsPointInPolygon2D(FVector Point, TArray<FVector> Polygon)
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
            int j = Math::RandRange(0, i);
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
            System::ClearTimer(this, "SpawnNextBatch");
        }
    }
}
