# Algorithm Agent (algo) — Shared Notes

## [v1.0.0] Initial State

### 已完成的算法模块

- **PolygonDeriver**: 6 步管线完整实现，支持 DataProvider 模式和 Legacy 模式
- **TerrainAnalyzer**: 5 步流水线（高程网格→坡度→分类→CCL→边界），Horn 3x3 Sobel
- **LandUseClassifier**: 9 规则分类 + ESA WorldCover 融合，PCG 参数自动填充
- **RoadNetworkGraph**: O(n²) 交叉检测 + 边分割 + GetLeftmostTurn 面提取
- **DEMParser**: PNG/RAW/HGT 完整支持，GeoTIFF 简化版

### 当前 API 接口

```cpp
// PolygonDeriver 主入口
void GenerateFromProvider(IGISDataProvider* Provider, UGISCoordinate* Coord,
                          TArray<FLandUsePolygon>& OutPolygons);

// LandUseClassifier 单多边形分类
ELandUseType ClassifySingle(const FLandUsePolygon& Poly,
                            const TArray<FGISFeature>& NearbyFeatures,
                            const FLandCoverGrid* LandCover = nullptr);
```

### 待实现功能

- bridge/tunnel 标签提取 → EStructureLayer 枚举
- RoadNetworkGraph LayerIndex 分离（立交桥不交叉分割）
- 道路逐顶点高程（当前只有 AvgElevation）

### 技能清单

| 模块 | 文件数 | 状态 | 复杂度 |
|------|--------|------|--------|
| PolygonDeriver | 2 (.h/.cpp) | ✅ 完成 | 6 步管线，~800 行 |
| TerrainAnalyzer | 2 | ✅ 完成 | Horn Sobel + Union-Find CCL |
| LandUseClassifier | 2 | ✅ 完成 | 9 规则 + ESA 融合 |
| RoadNetworkGraph | 2 | ✅ 完成 | O(n²) 交叉 + 面提取 |
| DEMParser | 3 (.h+types.h+.cpp) | ✅ 完成 | PNG/RAW/HGT/GeoTIFF |

### 与其他 agent 的依赖

- **← pipeline**: GeoJsonParser 提供 FGISFeature（含 OSM 标签），DEMParser 消费 pipeline 预处理的 DEM
- **→ runtime**: PolygonDeriver 输出 TArray<FLandUsePolygon>，runtime 的 GISWorldBuilder 调用
- **→ runtime**: RoadNetworkGraph 输出道路结构，未来 runtime 做 Spline Mesh 消费

## TODO (from lead review)

- [ ] **P2: EStructureLayer 枚举定义** (from lead) — 新增 `EStructureLayer { Ground, Bridge, Tunnel }` 到 `LandUsePolygon.h`，供 pipeline 的 GeoJsonParser 填充、runtime 的 Spline Mesh 消费
- [ ] **P2: RoadNetworkGraph LayerIndex 分层** (from lead) — 按 OSM `layer=*` 标签分层，同层内做交叉检测+边分割，跨层不分割。依赖 pipeline 先完成 bridge/tunnel 标签提取
- [ ] **P2: 道路逐顶点高程** (from lead) — FRoadEdge 每个顶点独立高程，替代当前的 AvgElevation。消费 DEMParser 或 CesiumBridge 高程数据

## Changelog

### [v1.0.0] — algo
- 初始状态记录
- Lead review: 补充技能清单和依赖关系

### [v1.0.1] 4009556 — algo
- fix: ArcGISRestProvider FGenericPlatformHttp::UrlEncode → FPlatformHttp::UrlEncode（UE5 新版 API）
- fix: PCGGISNode SourceComponent 废弃 API → .Get() + PCGComponent.h include
- fix: RoadNetworkGraph 局部变量 NextEdgeID 遮蔽类成员 → 重命名 FoundEdgeID

### [v1.0.2] Session 01AX8SpXvMkCNzwKftaQ3tE7 — algo (UE 5.7 全面适配)

#### 编译修复
- fix: GISWorldBuilder ActorLabel → PolyLabel（UE 5.7 AActor::ActorLabel 变 private）
- fix: TiledFileProvider OldestKey 未初始化 → (0,0)
- fix: GISProcedural.uplugin 声明 CesiumForUnreal 可选依赖

#### CesiumForUnreal 集成
- CesiumForUnreal v2.24.1 UE 5.7 预编译版替换（源码 + ThirdParty 匹配）
- Git LFS 配置 .lib/.uasset
- CesiumRuntime.Build.cs ThirdParty 检测

#### AngelScript 踩坑记录 (AS-1 ~ AS-7)
- docs/AngelScript_Guide.md 更新
- 关键发现：FMath→Math, SetTimer用FName/ClearTimer用FString, 无RegisterComponent, Cesium下SpawnActor坐标反转

#### Phase A 验证（进行中）
- MockPolygonGenerator.as: 成功生成 23 个假多边形到 DataAsset 内存
- **问题**: AngelScript 无法持久化 DataAsset 到磁盘
- **结论**: 需移植到 C++ 实现（支持 Live Coding + SavePackage）

#### 下一步（新 Session）
1. C++ 版 AMockPolygonGenerator（CallInEditor + SavePackage 持久化）
2. 编辑器搭 PCG Graph 验证 Polygon → PCGGISNode → Spawn 完整链路
3. 按 LandUseType 分流到不同 Mesh Spawner

关键参考文件：
- `Public/Polygon/LandUsePolygon.h` — 数据结构
- `Public/Data/LandUseMapDataAsset.h` — 存储 + 空间索引
- `Public/PCG/PCGGISNode.h` — PCG 采样节点
- `Script/GIS/MockPolygonGenerator.as` — AngelScript 版逻辑参考

**新 Session 继续在 `claude/claudeMainBranch-0zjsx` 分支工作。**

### [v1.0.3] — algo (Phase A: MockPolygonGenerator C++ 移植)

#### C++ 移植
- feat: `AMockPolygonGenerator` C++ 版 (`.h` + `.cpp`)
  - 完整移植 `MockPolygonGenerator.as` 所有逻辑
  - CallInEditor: GenerateMockData / CreateAndSaveNewDataAsset / DrawPolygons / ClearData
  - `UPackage::SavePackage` 持久化 DataAsset 到磁盘（AS 版无法做到）
  - `FAssetRegistryModule::AssetCreated` 注册到资产浏览器
  - `#if WITH_EDITOR` 保护所有编辑器专用代码
  - Ground trace 贴地（LineTraceSingleByChannel）
  - BuildingDensity/VegetationDensity 带 FMath::Clamp 防止越界

#### 文件清单
- `Public/Runtime/MockPolygonGenerator.h` — 新增
- `Private/Runtime/MockPolygonGenerator.cpp` — 新增
- Build.cs 无需修改（SavePackage 在 CoreUObject，已有依赖）

#### 与 AS 版差异
- SavePackage 持久化 ✅（AS 版 ❌）
- CreateAndSaveNewDataAsset 一键创建 + 保存 ✅（AS 版无）
- SavePath 可配置（默认 `/Game/GISData/MockLandUseMap`）
- Density 值 Clamp 到 [0,1]（AS 版可能产生负值）

#### 下一步
1. 编辑器内验证：放置 AMockPolygonGenerator → CreateAndSaveNewDataAsset → GenerateMockData
2. 搭 PCG Graph：PCGGISNode 引用生成的 DataAsset → Attribute Filter → Static Mesh Spawner
3. 验证完整链路：MockPolygon → DataAsset → PCGGISNode → Spawn
