// PCGDemoCreator.as — PCG 程序化生成演示 Actor
// 放置到关卡中 → 配置 mesh → 点击 Generate → 编辑器同步生成全部
// Play 时渐进式"长出来"展示过程
// 纯 Demo 用途，删除此 Actor 即清除所有生成内容，不影响主体

struct FDemoSpawnEntry
{
    FVector Location;
    FRotator Rotation;
    FVector Scale;
    UStaticMesh Mesh;
    bool bIsBuilding = false;
}

class APCGDemoCreator : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent Root;

    UPROPERTY(DefaultComponent, Attach = Root)
    USplineComponent TreeOutlineSpline;

    UPROPERTY(DefaultComponent, Attach = Root)
    USplineComponent BuildingOutlineSpline;

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
    bool bTraceToGround = true;

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Generation")
    float TraceHeight = 100000.0f;

    // ---- Play 时渐进生成 ----
    UPROPERTY(EditAnywhere, Category = "PCG Demo|Play Animation")
    float GrowInterval = 0.05f; // 每个实例"长出来"的间隔（秒）

    UPROPERTY(EditAnywhere, Category = "PCG Demo|Play Animation")
    float GrowDuration = 0.5f; // 从 0 缩放到目标缩放的时间（秒）

    // ---- 内部状态 ----
    private TArray<AActor> SpawnedActors;
    private TArray<FVector> TargetScales; // 每个 Actor 的目标缩放
    private int GrowIndex = 0;
    private bool bIsGrowing = false;

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

    // ======== 编辑器按钮：同步全部生成 ========
    UFUNCTION(CallInEditor, Category = "PCG Demo")
    void Generate()
    {
        if (TreeMeshes.Num() == 0 && BuildingMeshes.Num() == 0)
        {
            Print("[PCGDemo] ERROR: Assign meshes first!");
            return;
        }

        ClearGenerated();
        Print("[PCGDemo] ===== GENERATION START =====");

        TArray<FDemoSpawnEntry> allEntries;

        // 树木区
        if (TreeMeshes.Num() > 0)
        {
            TArray<FVector> outline = GetSplinePolygonPoints(TreeOutlineSpline);
            TArray<FVector> pts = SamplePointsInPolygon(outline, TreeMinSpacing);
            Print("[PCGDemo] Trees: " + pts.Num() + " points");
            for (int i = 0; i < pts.Num(); i++)
            {
                FDemoSpawnEntry e;
                e.Location = pts[i];
                e.Rotation = FRotator(0, Math::RandRange(0.0f, 360.0f), 0);
                float s = Math::RandRange(TreeScaleRange.X, TreeScaleRange.Y);
                e.Scale = FVector(s, s, s);
                e.Mesh = TreeMeshes[Math::RandRange(0, TreeMeshes.Num() - 1)];
                e.bIsBuilding = false;
                allEntries.Add(e);
            }
        }

        // 建筑区
        if (BuildingMeshes.Num() > 0)
        {
            TArray<FVector> outline = GetSplinePolygonPoints(BuildingOutlineSpline);
            TArray<FVector> pts = SamplePointsInPolygon(outline, BuildingSpacing);
            Print("[PCGDemo] Buildings: " + pts.Num() + " points");
            for (int i = 0; i < pts.Num(); i++)
            {
                FDemoSpawnEntry e;
                e.Location = pts[i];
                e.Rotation = FRotator(0, float(Math::RandRange(0, 3)) * 90.0f, 0);
                float sXY = Math::RandRange(BuildingScaleXYRange.X, BuildingScaleXYRange.Y);
                float sZ = Math::RandRange(BuildingScaleZRange.X, BuildingScaleZRange.Y);
                e.Scale = FVector(sXY, sXY, sZ);
                e.Mesh = BuildingMeshes[Math::RandRange(0, BuildingMeshes.Num() - 1)];
                e.bIsBuilding = true;
                allEntries.Add(e);
            }
        }

        // 同步生成全部（编辑器中）
        for (int i = 0; i < allEntries.Num(); i++)
        {
            SpawnOneInstance(allEntries[i]);
        }

        Print("[PCGDemo] ===== COMPLETE: " + SpawnedActors.Num() + " instances =====");
    }

    UFUNCTION(CallInEditor, Category = "PCG Demo")
    void Clear()
    {
        ClearGenerated();
        Print("[PCGDemo] Cleared.");
    }

    UFUNCTION(CallInEditor, Category = "PCG Demo")
    void RandomizeOutlines()
    {
        BuildOutlineSpline(TreeOutlineSpline, TreeZoneOffset, TreeZoneRadius, TreeOutlinePoints, TreeRandomJitter);
        BuildOutlineSpline(BuildingOutlineSpline, BuildingZoneOffset, BuildingZoneRadius, BuildingOutlinePoints, BuildingRandomJitter);
        Print("[PCGDemo] Outlines randomized.");
    }

    // ======== Play 时渐进"长出来" ========
    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        if (SpawnedActors.Num() == 0)
            return;

        // 记录目标缩放，先全部缩为 0
        TargetScales.Empty();
        for (int i = 0; i < SpawnedActors.Num(); i++)
        {
            if (SpawnedActors[i] != nullptr)
            {
                TargetScales.Add(SpawnedActors[i].GetActorScale3D());
                SpawnedActors[i].SetActorScale3D(FVector(0.01f, 0.01f, 0.01f));
                SpawnedActors[i].SetActorHiddenInGame(false);
            }
            else
            {
                TargetScales.Add(FVector(1, 1, 1));
            }
        }

        GrowIndex = 0;
        bIsGrowing = true;
        Print("[PCGDemo] Play: growing " + SpawnedActors.Num() + " instances...");
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (!bIsGrowing || SpawnedActors.Num() == 0)
            return;

        // 计算当前应该开始"长"的最大索引
        float elapsed = Gameplay::GetTimeSeconds();
        // 用简单计数：每 GrowInterval 秒启动下一个
        int maxVisible = Math::Min(
            Math::FloorToInt(Gameplay::GetTimeSeconds() / GrowInterval) + 1,
            SpawnedActors.Num()
        );

        // 对已启动的实例做缩放动画
        for (int i = 0; i < maxVisible; i++)
        {
            if (SpawnedActors[i] == nullptr)
                continue;

            FVector current = SpawnedActors[i].GetActorScale3D();
            FVector target = TargetScales[i];

            if (current.X < target.X * 0.99f)
            {
                // 平滑插值
                float alpha = Math::Min(1.0f, DeltaSeconds / GrowDuration * 3.0f);
                FVector newScale = Math::Lerp(current, target, alpha);
                SpawnedActors[i].SetActorScale3D(newScale);
            }
            else if (current.X < target.X)
            {
                SpawnedActors[i].SetActorScale3D(target);
            }
        }

        // 全部长完
        if (maxVisible >= SpawnedActors.Num())
        {
            // 检查是否全部到达目标
            bool allDone = true;
            for (int i = 0; i < SpawnedActors.Num(); i++)
            {
                if (SpawnedActors[i] != nullptr)
                {
                    FVector c = SpawnedActors[i].GetActorScale3D();
                    FVector t = TargetScales[i];
                    if (c.X < t.X * 0.99f)
                    {
                        allDone = false;
                        break;
                    }
                }
            }
            if (allDone)
            {
                bIsGrowing = false;
                Print("[PCGDemo] All instances fully grown!");
            }
        }
    }

    // ======== Spawn 单个实例 ========
    private void SpawnOneInstance(FDemoSpawnEntry Entry)
    {
        FVector loc = Entry.Location;

        // Ground trace — 只接受 Cesium 地形（Static mobility），跳过已生成的 Actor
        if (bTraceToGround)
        {
            FVector traceStart = FVector(loc.X, loc.Y, loc.Z + TraceHeight);
            FVector traceEnd = FVector(loc.X, loc.Y, loc.Z - TraceHeight);
            FHitResult hit;
            TArray<AActor> ignore;
            ignore.Add(this);
            if (System::LineTraceSingle(traceStart, traceEnd,
                ETraceTypeQuery::Visibility, false, ignore,
                EDrawDebugTrace::None, hit, true))
            {
                // 检查命中的是不是地形（非我们生成的 Movable Actor）
                UPrimitiveComponent hitComp = hit.Component;
                if (hitComp != nullptr && hitComp.Mobility == EComponentMobility::Movable)
                {
                    return; // 打到了已生成的树/建筑，跳过
                }
                loc = hit.Location;
            }
            else
            {
                return; // 地面没加载，跳过此点
            }
        }

        if (Entry.Mesh == nullptr)
            return;

        AActor spawned = SpawnActor(AActor);
        if (spawned == nullptr)
            return;

        // 先建 Root，再设位置
        UStaticMeshComponent meshComp = UStaticMeshComponent::Create(spawned);
        meshComp.SetStaticMesh(Entry.Mesh);
        meshComp.SetMobility(EComponentMobility::Movable);
        spawned.SetRootComponent(meshComp);

        spawned.SetActorLocation(loc);
        spawned.SetActorRotation(Entry.Rotation);
        spawned.SetActorScale3D(Entry.Scale);

        // Outliner 中按类型分组标签
        #if EDITOR
        FString label = Entry.bIsBuilding ? "PCG_Building_" : "PCG_Tree_";
        spawned.SetActorLabel(label + SpawnedActors.Num());
        spawned.SetFolderPath(n"PCGDemo");
        #endif

        SpawnedActors.Add(spawned);
    }

    // ======== 清理 ========
    private void ClearGenerated()
    {
        bIsGrowing = false;
        for (int i = SpawnedActors.Num() - 1; i >= 0; i--)
        {
            if (SpawnedActors[i] != nullptr)
                SpawnedActors[i].DestroyActor();
        }
        SpawnedActors.Empty();
        TargetScales.Empty();
        GrowIndex = 0;
    }

    // ======== Outline 构建 ========
    private void BuildOutlineSpline(USplineComponent Spline, FVector Offset, float Radius, int NumPoints, float Jitter)
    {
        Spline.ClearSplinePoints(false);
        for (int i = 0; i < NumPoints; i++)
        {
            float angle = (float(i) / float(NumPoints)) * 360.0f;
            float jR = Radius * (1.0f + Math::RandRange(-Jitter, Jitter));
            float rad = Math::DegreesToRadians(angle);
            FVector point = FVector(Math::Cos(rad) * jR, Math::Sin(rad) * jR, 0) + Offset;
            Spline.AddSplinePointAtIndex(point, i, ESplineCoordinateSpace::Local, false);
        }
        Spline.SetClosedLoop(true, false);
        for (int i = 0; i < NumPoints; i++)
            Spline.SetSplinePointType(i, ESplinePointType::CurveCustomTangent, false);
        Spline.UpdateSpline();
    }

    // ======== 多边形采样 ========
    private TArray<FVector> GetSplinePolygonPoints(USplineComponent Spline)
    {
        TArray<FVector> points;
        FVector actorLoc = GetActorLocation();
        int n = Spline.GetNumberOfSplinePoints();
        for (int i = 0; i < n; i++)
        {
            FVector localPt = Spline.GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
            points.Add(actorLoc + localPt);
        }
        return points;
    }

    private TArray<FVector> SamplePointsInPolygon(TArray<FVector> Polygon, float Spacing)
    {
        TArray<FVector> results;
        if (Polygon.Num() < 3) return results;

        FVector bMin = Polygon[0];
        FVector bMax = Polygon[0];
        for (int i = 1; i < Polygon.Num(); i++)
        {
            bMin.X = Math::Min(bMin.X, Polygon[i].X);
            bMin.Y = Math::Min(bMin.Y, Polygon[i].Y);
            bMax.X = Math::Max(bMax.X, Polygon[i].X);
            bMax.Y = Math::Max(bMax.Y, Polygon[i].Y);
        }

        float half = Spacing * 0.4f;
        for (float x = bMin.X; x <= bMax.X; x += Spacing)
        {
            for (float y = bMin.Y; y <= bMax.Y; y += Spacing)
            {
                float jx = x + Math::RandRange(-half, half);
                float jy = y + Math::RandRange(-half, half);
                FVector c = FVector(jx, jy, bMin.Z);
                if (IsPointInPolygon2D(c, Polygon))
                    results.Add(c);
            }
        }
        return results;
    }

    private bool IsPointInPolygon2D(FVector Point, TArray<FVector> Polygon)
    {
        int n = Polygon.Num();
        bool inside = false;
        int j = n - 1;
        for (int i = 0; i < n; i++)
        {
            if (((Polygon[i].Y > Point.Y) != (Polygon[j].Y > Point.Y)) &&
                (Point.X < (Polygon[j].X - Polygon[i].X) * (Point.Y - Polygon[i].Y)
                 / (Polygon[j].Y - Polygon[i].Y) + Polygon[i].X))
            {
                inside = !inside;
            }
            j = i;
        }
        return inside;
    }

    private void ShuffleSpawnList()
    {
        for (int i = SpawnedActors.Num() - 1; i > 0; i--)
        {
            int j = Math::RandRange(0, i);
            AActor temp = SpawnedActors[i];
            SpawnedActors[i] = SpawnedActors[j];
            SpawnedActors[j] = temp;
            // 同步交换 TargetScales
            FVector ts = TargetScales[i];
            TargetScales[i] = TargetScales[j];
            TargetScales[j] = ts;
        }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        bIsGrowing = false;
    }
}
