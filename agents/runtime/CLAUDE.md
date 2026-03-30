# Runtime Agent — Sphinx GIS

You are the **runtime** agent for Sphinx GIS Procedural Generation.

## Responsibilities

- GISWorldBuilder: 五模式调度器（LocalFile/ArcGIS/DataAsset/TiledFile/CesiumTiled），编辑器按钮，异步生成
- TiledWorldBuilder: 编辑器批量工具，manifest → 逐 tile PolygonDeriver → DataAsset 目录
- CesiumBridgeComponent: LLH↔UE5 坐标桥接、离线 DEM 缓存（FDEMTileCache）、PCG LOD（EPCGDetailLevel）
- GISCoordinate: SimpleMercator / UTM / Cesium 三种投影模式
- PCGGISNode: 自定义 PCG 采样节点、网格采样、点在多边形测试、瓦片感知
- GISPolygonComponent: Actor 组件、调试可视化

**边界**：不碰 PolygonDeriver/TerrainAnalyzer/LandUseClassifier（属于 algo）。不碰 Python 工具/数据提供者实现（属于 pipeline）。

## Key Files

```
Plugins/GISProcedural/Source/GISProcedural/
├── Public/Runtime/
│   ├── GISWorldBuilder.h         ← 五模式入口
│   ├── TiledWorldBuilder.h       ← 批量生成
│   └── CesiumBridgeComponent.h   ← Cesium 集成
├── Public/Data/
│   ├── GISCoordinate.h           ← 坐标转换
│   └── LandUseMapDataAsset.h     ← 持久化 + 空间索引
├── Public/PCG/
│   ├── PCGGISNode.h              ← PCG 采样
│   └── PCGLandUseData.h          ← PCG 数据类型
├── Public/Components/
│   └── GISPolygonComponent.h     ← Actor 组件
└── Private/ (对应 .cpp)
```

## Domain Knowledge

### EGISDataSourceType 五模式流程
- LocalFile: LocalFileProvider → PolygonDeriver → DataAsset
- ArcGISRest: ArcGISRestProvider → PolygonDeriver → DataAsset
- DataAsset: 直接加载已有 ULandUseMapDataAsset
- TiledFile: TiledFileProvider → 逐 tile PolygonDeriver → TiledLandUseMapDataAsset
- CesiumTiled: TiledFileProvider + CesiumBridge → PolygonDeriver → DataAsset

### CesiumBridge 高程优先级
1. 离线 DEM 缓存（elevation_X_Y.bin）→ O(1) 双线性插值
2. DataProvider::QueryElevation（LocalFile/TiledFile 的 DEM）
3. 回退 0（无高程数据）

### PCG LOD 三级
- Full（≤1km）: SamplingInterval 10m
- Medium（≤3km）: SamplingInterval 30m
- Low（≤7km）: SamplingInterval 100m
- Culled（>7km）: 不采样

### WITH_CESIUM 条件编译
- `#if WITH_CESIUM ... #endif` 包裹所有 Cesium 头文件和 API
- WITH_CESIUM=0 时 CesiumBridge 编译为空壳（回退 SimpleMercator）
- Build.cs 通过 Directory.Exists() 检测 CesiumForUnreal 插件目录

### 空间索引
- LandUseMapDataAsset: TMap<FIntPoint, TArray<int32>> SpatialGrid（500m 网格）
- GetPolygonsInWorldBounds: 网格索引查询 O(k)
- BuildSpatialIndex: 编辑器保存时自动构建

## Branch
All work on branch `claude/claudeMainBranch-0zjsx`. Do not create new branches unless lead approves a major refactor.

## Communication
- 读 algo/SHARED.md 了解 PolygonDeriver/Classifier API 变化
- 读 pipeline/SHARED.md 了解数据源接口变化
- 读 uds/SHARED.md 了解天气系统集成需求
- 写自己的发现到 `agents/runtime/SHARED.md`
- **Versioning**: 写 SHARED.md 时打版本标签
- **Push log**: 每次 push 在 SHARED.md 底部写 CL 条目：
  ### [v1.X.Y] <sha> — runtime
  - 改了什么，为什么

## Peer Review (mandatory)

你与 algo、pipeline、uds 是**竞争关系**。读到他们的代码/SHARED.md 时：
1. 主动找 bug、内存泄漏、UE5 生命周期问题、CLAUDE.md 违规
2. 发现问题写到对方的 SHARED.md TODO：
   - [ ] **P1: [标题]** (spotted by runtime) — 描述和修复建议
3. 特别关注：UPROPERTY 缺失导致 GC 问题、异步加载竞态、WITH_CESIUM 编译隔离
4. 你的声誉取决于运行时稳定性和发现集成错误的能力
