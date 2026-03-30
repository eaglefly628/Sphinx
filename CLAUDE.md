# Sphinx — GIS Procedural Generation for UE5

军事仿真用全球 GIS 程序化世界生成系统。UE5 C++ 插件 + Python 预处理管线。

## Version

**Current: v1.0.0** (Phase 0-4 complete)

### Changelog
| Version | Date | Summary |
|---------|------|---------|
| v1.0.0 | 2026-03-30 | Phase 0-4 全部完成：五模式数据源、瓦片流式、Cesium 集成、PCG、71+ 测试 |

### Versioning Rules
- Bump minor (v1.X.0) for features/API changes, patch (v1.X.Y) for bug fixes
- 只有主程序员在此文件 bump 版本，agent 不能自行升版本
- SHARED.md 每个更新段落必须打版本标签：`## [v1.X.Y] Feature Name`
- Agent 之间互相引用用版本号："see algo SHARED.md [v1.1.0]"
- **每次 push 必须包含 changelog 条目**，无例外

### Agent Names (canonical)
| Agent | Name | Domain |
|-------|------|--------|
| algo | 算法 | 多边形推导、地形分析、分类器、道路网络、DEM 解析 |
| runtime | 运行时 | GISWorldBuilder 五模式、Cesium 桥接、PCG 节点、坐标转换 |
| pipeline | 管线 | Python 预处理、数据提供者、瓦片系统、GeoJSON/Raster 解析 |
| uds | 天气 | Ultra Dynamic Sky 集成、天气系统、云层/雾/雨雪、昼夜循环 |

使用这些精确名字（algo / runtime / pipeline / uds），不许变体。

## 快速参考

- **插件**: `Plugins/GISProcedural/` (UE5 C++, Runtime module)
- **工具**: `Tools/GISPreprocess/` (Python 3.10+)
- **测试**: `python3 Tests/test_pipeline.py && python3 Tests/test_cesium_bridge.py`
- **文档**: `Plugins/GISProcedural/README.md` (EN) / `README_CN.md` (CN) / `GISProcedural_使用手册.md`
- **计划**: `PLAN.md` (Phase 0-4 全部完成)

## 架构

五种数据源模式 (EGISDataSourceType):
- LocalFile: 本地 GeoJSON + DEM（小区域 ~10km²）
- ArcGISRest: HTTP Feature Service
- DataAsset: 已生成的 ULandUseMapDataAsset
- TiledFile: tile_manifest.json + 预切割瓦片（大规模 ~100km²）
- CesiumTiled: Cesium 3D 地球 + 本地瓦片 + DEM 缓存（全球级）

分层架构:
- Data 层: IGISDataProvider 接口 → LocalFile / ArcGIS / TiledFile 实现
- DEM 层: DEMParser → TerrainAnalyzer（坡度/分类/连通分量）
- Polygon 层: PolygonDeriver（6步管线）→ LandUseClassifier → FLandUsePolygon
- Runtime 层: GISWorldBuilder（5模式）/ TiledWorldBuilder（批量）/ CesiumBridgeComponent
- PCG 层: PCGGISNode 采样 → LandUseType/BuildingDensity 等属性

## 构建

### UE5 插件
- Build.cs 依赖: Core, Engine, Json, PCG, GeometryCore, HTTP
- Cesium 软依赖: WITH_CESIUM=0/1（自动检测 CesiumForUnreal 插件目录）
- 无 GDAL 依赖（GDAL 仅在 Python 预处理中使用）

### Python 测试
```bash
# 全部测试（71+ 测试, 629 断言）
python3 Tests/test_pipeline.py      # T1-T7: UTM/瓦片/清单/验证/LandCover/空间索引/端到端
python3 Tests/test_cesium_bridge.py  # T1-T7: DEM缓存/插值/Mercator/LOD/高程
```

### Python 预处理
```bash
pip install -r Tools/GISPreprocess/requirements.txt
python Tools/GISPreprocess/preprocess.py --input ./RawData --output ./Output \
    --tile-size 1024 --origin-lon 121.47 --origin-lat 31.23
```

## 关键文件

| 文件 | 职责 | Owner |
|------|------|-------|
| `GISWorldBuilder.h/.cpp` | 五模式入口，编辑器生成按钮 | runtime |
| `PolygonDeriver.h/.cpp` | 核心算法：DEM→地形→矢量切割→分类→PCG参数 | algo |
| `GISCoordinate.h/.cpp` | 坐标转换（SimpleMercator/UTM/Cesium） | runtime |
| `CesiumBridgeComponent.h/.cpp` | Cesium 桥接、离线 DEM 缓存、PCG LOD | runtime |
| `TiledFileProvider.h/.cpp` | 瓦片数据源、LRU 缓存、manifest 读取 | pipeline |
| `LandUseClassifier.h/.cpp` | 9 规则分类 + ESA WorldCover 融合 | algo |
| `TerrainAnalyzer.h/.cpp` | 5 步地形分析流水线 | algo |
| `RoadNetworkGraph.h/.cpp` | 平面图、交叉检测、边分割 | algo |
| `DEMParser.h/.cpp` | 多格式 DEM 解析 | algo |
| `PCGGISNode.h/.cpp` | PCG 采样节点、瓦片感知 | runtime |
| `IGISDataProvider.h` | 数据源抽象接口 | pipeline |
| `TileManifest.h/.cpp` | 瓦片清单解析 | pipeline |
| `GeoJsonParser.h/.cpp` | GeoJSON 解析 | pipeline |
| `RasterLandCoverParser.h/.cpp` | WorldCover 栅格解析 | pipeline |
| `preprocess.py` | Python 预处理主入口 | pipeline |
| `srtm_to_terrain.py` | SRTM → 二进制 DEM 高程缓存 | pipeline |

## 代码约定

- 日志类别: `LogGIS`（所有模块统一）
- 关键流程用 `===== START =====` / `===== END =====` 标记
- 条件编译: `#if WITH_CESIUM ... #endif`
- 枚举前缀: `E`（EGISDataSourceType, ELandUseType, EProjectionType）
- 结构体前缀: `F`（FLandUsePolygon, FGISFeature, FTileManifest）
- UObject 前缀: `U`（UGISCoordinate, UTiledFileProvider）
- Actor 前缀: `A`（AGISWorldBuilder, ATiledWorldBuilder）
- 新文件遵循现有目录结构: Public/Data/, Public/Polygon/, Public/Runtime/, Public/PCG/
- Python 预处理输出 WGS84 GeoJSON，投影由 UE 侧处理

## Design Principles

- 接口优先：新数据源实现 IGISDataProvider，不改上层代码
- 零 GDAL 运行时：UE5 插件永远不依赖 GDAL，所有栅格处理在 Python 预处理完成
- 软依赖：WITH_CESIUM 条件编译，不装 Cesium 不影响任何现有功能
- 向后兼容：新增模式/字段不破坏现有数据流
- 精度边界：float32 在 70km 范围内精度 ~0.5cm，可接受

### Edit tool discipline
- 最小化替换范围，只选要改的行
- 大块替换时逐行确认 old_string 的每行都在 new_string 里
- 多次小编辑优于一次大编辑
- 修改 .h 时确认对应 .cpp 的 include 和签名同步

## 多 Agent 协作

```
agents/
├── algo/
│   ├── CLAUDE.md      ← 算法 agent 身份 + 专业知识
│   └── SHARED.md      ← 算法 agent 的知识共享 + TODO + Changelog
├── runtime/
│   ├── CLAUDE.md
│   └── SHARED.md
├── pipeline/
│   ├── CLAUDE.md
│   └── SHARED.md
└── uds/
    ├── CLAUDE.md
    └── SHARED.md
```

分支规则：
- **所有 agent 统一在 `claude/claudeMainBranch-0zjsx` 分支上工作**
- 除非有重大重构需要单独分支，否则不建新分支
- 重大重构需先征得主程序员同意

通信规则：
- Agent 之间通过 SHARED.md 异步通信
- API 变更必须同时通知下游 agent 的 SHARED.md
- 跨域修改需先在对方 SHARED.md 提 TODO
- peer review 发现的问题写到对方 SHARED.md TODO

## 当前状态

Phase 0-4 全部完成 ✅

待讨论/实现:
- 立交桥/涵洞 PCG 生成（需 bridge/tunnel 标签提取 + LayerIndex 分离）
- 道路 Spline Mesh 生成
- 水体 Mesh 生成
