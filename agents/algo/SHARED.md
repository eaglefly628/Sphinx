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
