# Sphinx GISProcedural — v2.0 架构设计文档

> 代号：鹰行者（EagleSim） · 第一阶段技术基座
> 基于 Phase 0-4 已交付代码（4 月 23 日主线），面向 6 个月交付窗口

---

## 1. v1.0 → v2.0 全维度对比

| 维度 | v1.0（已交付） | v2.0（本期目标） |
|------|--------------|----------------|
| **全球渲染器** | ArcGIS Maps SDK（在线 + .tpkx 离线） | Cesium for Unreal（开源，WGS84 椭球，完全离线） |
| **数据格式** | GeoJSON 切片 + PNG DEM + landcover.json | GeoParquet 列式切片 + Cloud-Optimized GeoTIFF DEM |
| **空间查询** | O(n) 线性扫描（`GetPolygonsInWorldBounds`） | DuckDB + spatial 扩展，BBOX 预过滤 + 索引 |
| **交互拾取** | Raycast → 遍历 TArray | Feature ID 语义缓冲区，GPU 侧 O(1) 查询 |
| **植被渲染** | PCG 静态网格体点采样 | Niagara GPU 粒子驱动植被（LOD 动态） |
| **地形融合** | PNG 高程 + UE Landscape | Cesium World Terrain + 本地 DEM 叠加覆写 |
| **材质系统** | 固定材质实例，人工赋值 | 程序化材质（运行时按 LandUseType 参数驱动） |
| **数据流** | 同步加载，单 DataAsset | 异步流式，TiledLandUseMapDataAsset + LRU 缓存 |
| **坐标精度** | 简化 Mercator（float32，±0.5 cm/100 km） | UTM 双精度（Karney 2011），WGS84/CGCS2000 双模 |
| **多用户预留** | 无 | GameMode/GameState Authority 架构占位，TCP Socket 接口空实现 |

---

## 2. 可复用模块清单（直接从 v1.0 继承）

以下模块已在 Phase 0-4 中完整实现并通过 71 项测试，v2.0 **零改动复用**：

| 模块 | 位置 | 复用点 |
|------|------|--------|
| `GISCoordinate` | `Public/Data/GISCoordinate.h` | `GeoToUTM()` / `UTMToGeo()` / `GeoToWorld()` — v2.0 坐标引擎基础 |
| `DEMParser` | `Public/DEM/DEMParser.h` | PNG/RAW/HGT/GeoTIFF 四格式解析；v2.0 本地 DEM 叠加直接调用 |
| `TerrainAnalyzer` | `Public/DEM/TerrainAnalyzer.h` | Horn 3×3 Sobel 坡度/坡向；CCL 连通域合并 — 供程序化材质参数读取 |
| `IGISDataProvider` | `Public/Data/IGISDataProvider.h` | 抽象接口不变；新增 `GeoParquetProvider` 实现此接口 |
| `TileManifest` | `Public/Data/TileManifest.h` | manifest.json 解析 + Geo-BBOX 查询；v2.0 扩展 parquet 字段 |
| `TiledFileProvider` | `Public/Data/TiledFileProvider.h` | LRU 缓存 + `sphinx_feature_id` 跨 Tile 去重 — Feature ID 查询的 ID 来源 |
| `TiledLandUseMapDataAsset` | `Public/Data/TiledLandUseMapDataAsset.h` | `FStreamableManager` 异步流式加载 — v2.0 继续作为运行时多 Tile 容器 |
| `LandUseClassifier` | `Public/Polygon/LandUseClassifier.h` | 9 规则分类 + ESA WorldCover 融合 — 驱动程序化材质 LandUseType 参数 |
| `PolygonDeriver` | `Public/Polygon/PolygonDeriver.h` | Sutherland-Hodgman 切割 — v2.0 GeoParquet 边界对齐时调用 |
| `RasterLandCoverParser` | `Public/Data/RasterLandCoverParser.h` | ESA WorldCover 11 类栅格解析 — Niagara 植被密度图输入 |
| `PCGGISLandUseSampler` | `Public/PCG/PCGGISNode.h` | 点生成保留作建筑 Spawn；植被改由 Niagara 接管 |
| `GeoJsonParser` | `Public/Data/GeoJsonParser.h` | 兼容层：非 Parquet 区域（机场等自定义数据）仍走 GeoJSON |
| Python 预处理管线 | `Tools/GISPreprocess/` | `pbf_to_geojson.py` / `tile_cutter.py` / `dem_cropper.py` 扩展为 Parquet 输出 |

> **CesiumBridge**（新建，见第 3 节）充当 Cesium Tile 与上述模块的适配层，不修改任何已有接口。

---

## 3. 全新模块清单（v2.0 新增）

| 模块 | 类型 | 职责 |
|------|------|------|
| **CesiumBridge** | C++ Actor Component | 管理 Cesium3DTileset 生命周期；将 Cesium WGS84 坐标转换为 UE5 世界坐标，向现有 `GISCoordinate` 发布基准变换 |
| **GeoParquetProvider** | C++ / Python | 实现 `IGISDataProvider`；运行时通过 Arrow C Data Interface 读取 `.parquet` 切片列数据 |
| **DuckDBSpatialQuery** | C++ Wrapper | 封装 DuckDB 进程内 SQL 引擎；执行 `ST_Intersects` BBOX 过滤 + `feature_id` 聚合，替换 O(n) 线性扫描 |
| **FeatureIDPickingBuffer** | C++ + HLSL | 自定义 SceneCapture 通道，将 `sphinx_feature_id`（uint32）写入 R32 离屏 RT；鼠标点击从 CPU 侧读取像素完成 O(1) 语义拾取 |
| **ProceduralMaterialSystem** | C++ + Material Graph | 运行时 MID（Material Instance Dynamic）按 `LandUseType` 注入宏观混合权重（草地/岩石/沙地/湿地/水面）；坡度/坡向由 `TerrainAnalyzer` 输出驱动细节混合 |
| **NiagaraVegetationDriver** | C++ + Niagara | 读取 `PCGLandUseData::VegetationDensity` 与 ESA WorldCover 密度图，生成 GPU Sprite/Mesh 粒子植被；支持风场扰动与 LOD 剔除 |
| **EquipmentPlugin Runtime** | C++ | `AEquipmentBase` 基类 + `EquipmentManager` 扫描/注册/Spawn；Manifest.json 格式含 `sensors`/`weapons` 预留字段 |
| **WeatherStateManager** | C++ + Blueprint | 12 参数天气状态机（云量/降水/能见度/风速/温湿度等）；驱动 Ultra Dynamic Sky 参数 + 程序化材质湿润混合 |
| **CoordInputPanel** | UMG Widget | 经纬度/CGCS2000 输入框 → 调用 `GISCoordinate` 转换 → 摄像机飞行定位；装备 Spawn 地理坐标入口 |

---

## 4. v1.0 vs v2.0 完整数据流图

```
┌──────────────────────────── 离线预处理（Tools/GISPreprocess/）────────────────────────────┐
│                                                                                          │
│  OSM PBF  ──► pbf_to_geojson.py ──► tile_cutter.py ──► parquet_writer.py ──► tile.parquet│
│  DEM TIF  ──► dem_cropper.py ──────────────────────────────────────────────► dem.png     │
│  ESA WorldCover ──► raster_classifier.py ──────────────────────────────────► landcover.json│
│                                          └──► manifest_writer.py ──────────► manifest.json│
└──────────────────────────────────────────────────────────────────────────────────────────┘
                                              │
                    ┌─────────────────────────▼──────────────────────────────┐
                    │              运行时数据层（UE5 Plugin）                  │
                    │                                                        │
                    │  TileManifest ──► GeoParquetProvider                  │
                    │                       │                                │
                    │               DuckDBSpatialQuery                       │
                    │          (ST_Intersects + feature_id index)            │
                    │                       │                                │
                    │           TiledLandUseMapDataAsset                     │
                    │           (LRU Cache + FStreamableManager)             │
                    └───────────────────────┬────────────────────────────────┘
                                            │
          ┌─────────────────────────────────▼──────────────────────────────────────┐
          │                        处理层（Plugin Core）                             │
          │                                                                        │
          │  GeoJsonParser (兼容层)    PolygonDeriver     LandUseClassifier        │
          │  DEMParser                TerrainAnalyzer     RasterLandCoverParser    │
          │  GISCoordinate (UTM/WGS84/CGCS2000)                                   │
          └───────────┬────────────────────────────┬────────────────────────────── ┘
                      │                            │
          ┌───────────▼──────────┐    ┌────────────▼──────────────────────────────┐
          │   渲染与交互层         │    │              内容生成层                     │
          │                      │    │                                            │
          │  CesiumBridge        │    │  PCGGISLandUseSampler → 建筑 Spawn         │
          │  Cesium3DTileset     │    │  NiagaraVegetationDriver → 植被 GPU 粒子   │
          │  FeatureIDPickingBuf │    │  ProceduralMaterialSystem → 地表材质混合   │
          │  WeatherStateManager │    │  EquipmentManager → 装备 Spawn/控制        │
          │  UE5 Lumen/Nanite    │    │                                            │
          └──────────────────────┘    └────────────────────────────────────────────┘
                      │
          ┌───────────▼──────────┐
          │      应用层 / HUD    │
          │                      │
          │  CoordInputPanel     │
          │  装备列表 UI          │
          │  天气控制面板         │
          │  鹰眼图 / 指北针      │
          └──────────────────────┘
```

---

## 5. 五项关键技术决策

### TD-1：渲染器从 ArcGIS Maps SDK 切换到 Cesium for Unreal

**背景**：ArcGIS SDK 依赖 Esri 云授权；离线 `.tpkx` 工具链封闭；大场景内存峰值难以控制。

**决策**：采用 Cesium for Unreal（开源 Apache-2.0）作为全球渲染基座。

| 对比项 | ArcGIS Maps SDK | Cesium for Unreal |
|--------|-----------------|-------------------|
| 授权 | 商业，需 API Key | 开源，免授权费 |
| 离线支持 | .tpkx 封装，工具链封闭 | 标准 3D Tiles，自建离线服务 |
| 精度 | float64 内部 | float64 WGS84 椭球原生 |
| 与 UE5 集成 | 黑盒组件 | 开放源码，可 Fork 定制 |

**影响**：需新建 `CesiumBridge` 适配层，现有 `GISCoordinate` 坐标变换接口不变。

---

### TD-2：数据格式从 GeoJSON 切片升级为 GeoParquet

**背景**：单个 1 km² 切片 GeoJSON 上海市区约 2.4 MB；10 × 10 km 区域 240 MB 纯文本，解析慢。

**决策**：Python 预处理管线输出 GeoParquet（Apache Parquet + GeoArrow 几何列）。

- 压缩率：GeoJSON → GeoParquet 约 **5-8×** 体积压缩（Snappy 编码）
- 解析速度：列式读取比 JSON 解析快 **10-20×**
- 索引：Parquet Row Group 统计量天然支持 BBOX 跳过，配合 DuckDB 实现零扫描无关 Tile

**影响**：`GeoJsonParser` 作为兼容层保留（机场等自定义非标数据），新建 `GeoParquetProvider`。

---

### TD-3：查询引擎引入 DuckDB 替换线性扫描

**背景**：`LandUseMapDataAsset::GetPolygonsInWorldBounds` 是 O(n) 遍历，上海 10 万+ Polygon 运行时卡顿（PLAN.md 已记录）。

**决策**：嵌入 DuckDB（进程内，无需独立服务器），通过 `spatial` 扩展执行：

```sql
SELECT feature_id, geometry, land_use_type, elevation, slope
FROM read_parquet('tile_*.parquet', hive_partitioning=true)
WHERE ST_Intersects(geometry, ST_MakeEnvelope(x0, y0, x1, y1))
```

- 查询延迟目标：BBOX 10 km² 范围 < **15 ms**（Parquet Row Group 过滤 + 向量化执行）
- Win64 二进制约 12 MB，无外部依赖

---

### TD-4：Feature ID 语义拾取缓冲区

**背景**：v1.0 鼠标点击走 UE5 LineTrace，命中 UStaticMeshComponent 后还需回溯 TArray 匹配 PolygonID，O(n) 且无法区分同一 Mesh 内不同 Feature。

**决策**：在 `SceneCapture2D` 自定义通道中写入 `sphinx_feature_id`（uint32，来自 `TiledFileProvider` 已有字段）到 R32_UINT 离屏 RT；点击时 CPU 读取单像素，直接查 DuckDB 返回属性。

```
鼠标点击 → ReadRenderTargetPixel(x,y) → feature_id (uint32)
         → DuckDB: SELECT * FROM features WHERE sphinx_feature_id = ?
         → 弹出属性面板（无需遍历）
```

**约束**：uint32 空间支持 40 亿 Feature，足够覆盖全球 OSM 数据量。

---

### TD-5：植被从 PCG 静态网格体迁移到 Niagara GPU 驱动

**背景**：PCG 植被在 10 km² 区域生成 50 万棵树时 CPU 调度瓶颈明显；静态网格体 DrawCall 过多。

**决策**：`NiagaraVegetationDriver` 读取 `VegetationDensity`（`LandUsePolygon` 已有字段）和 ESA WorldCover 密度图作为输入，在 GPU 侧生成 Niagara Mesh Renderer 植被实例：

- GPU Instancing，DrawCall 数量与密度无关
- Niagara 风场扰动共享 `WeatherStateManager.windSpeed/windDirection` 参数
- 保留 `PCGGISLandUseSampler` 负责建筑/道路 Spawn（不改动）

---

## 6. 七项待验证风险（R1–R7）

| ID | 风险描述 | 来源 | 概率 | 影响 | 应对策略 | 验证截止 |
|----|----------|------|------|------|----------|----------|
| **R1** | Cesium Tile 流式加载与 UE5 World Partition 的 Streaming Source 冲突，导致双重 LOD 决策互斥 | 小城/ljb M1 讨论 | 高 | 高 | M1 第 2 周搭建最小复现场景；若冲突则 Cesium 仅作背景球，World Partition 接管训练区域 LOD | Week 4 |
| **R2** | GeoParquet → UE5 运行时读取管线（Arrow C Data Interface on Win64）尚未在 UE5.5 验证 | 小城技术预研 | 中 | 高 | M1 第 1 周单独 PoC：UE5 C++ 调用 libparquet/libarrow 静态链接，验证二进制兼容性 | Week 2 |
| **R3** | DuckDB Win64 动态库与 UE5 Module 加载顺序冲突（`__declspec(dllimport)` 符号冲突） | TJS 评估 | 中 | 中 | 使用 DuckDB 纯头文件单文件模式（`duckdb.hpp` amalgamation）规避动态链接；备选：进程外 DuckDB 服务 | Week 3 |
| **R4** | Feature ID 缓冲区在 R32_UINT SceneCapture 模式下 MSAA 导致 ID 插值失真（浮点混合破坏语义 ID） | 小城渲染讨论 | 中 | 中 | 关闭 SceneCapture 的 MSAA；使用 TAA 超采样替代；或切换为 Stencil Buffer 拾取 | Week 5 |
| **R5** | Niagara GPU 植被在 RTX 4080 上 10 km² / 50 万实例时显存超限（预估 3-4 GB） | 双飞性能评估 | 中 | 高 | 分层实例上限（可见视锥内 20 万 + 后台 cullled）；引入 Niagara LOD 半径参数；M4 专项压测 | Week 14 |
| **R6** | Cesium World Terrain 高程与本地 DEM（`DEMParser` 输出）叠加时出现接缝跳变（Z 轴不连续） | ljb DEM 讨论 | 高 | 中 | 训练区域边界设 500 m 过渡带，用双线性插值混合 Cesium 高程与本地 DEM；高精度区域完全屏蔽 Cesium 地形 | Week 6 |
| **R7** | 多用户（EagleSim R7）在 Cesium Globe 坐标下的 UE5 网络同步：Pawn `ReplicatedMovement` 使用 float32 FVector，全球坐标精度丢失 | 小城网络讨论 | 低（V1.0 不实现）| 高（V2.0 阻塞） | V1.0 架构预留：所有装备移动用 `FDoubleVector` + 相对原点编码传输；`GameState` 持有全局原点经纬度；V2.0 填充实现 | V2.0 M1 |

---

## 附录 A：模块依赖关系

```
CesiumBridge
    └── GISCoordinate (复用)
            └── UTM / WGS84 / CGCS2000 转换

GeoParquetProvider
    ├── IGISDataProvider (接口复用)
    ├── DuckDBSpatialQuery (新建)
    └── TileManifest (复用，扩展 parquet 字段)

FeatureIDPickingBuffer
    └── TiledFileProvider.sphinx_feature_id (复用)

ProceduralMaterialSystem
    ├── LandUseClassifier (复用)
    └── TerrainAnalyzer (复用，坡度/坡向输出)

NiagaraVegetationDriver
    ├── RasterLandCoverParser (复用，密度图)
    └── WeatherStateManager (新建，风场参数)

EquipmentPlugin Runtime
    └── GISCoordinate (复用，地理坐标 Spawn)
```

---

## 附录 B：M1 验证任务优先级

| 优先级 | 任务 | 对应风险 | 负责 |
|--------|------|----------|------|
| P0 | Cesium + World Partition 最小场景兼容性 | R1 | ljb + 小城 |
| P0 | libparquet/libarrow Win64 UE5 链接 PoC | R2 | TJS |
| P1 | DuckDB amalgamation 单文件编译验证 | R3 | TJS |
| P1 | Feature ID SceneCapture MSAA 失真复现 | R4 | 小城 |
| P2 | DEMParser 输出与 Cesium 地形接缝测试（上海样本） | R6 | 双飞 |
