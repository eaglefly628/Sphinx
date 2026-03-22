# GISProcedural 插件

基于真实 GIS 数据（GeoJSON、DEM、ArcGIS REST、Cesium 3D Tiles）的 Unreal Engine 5 程序化世界生成插件。

## 架构总览

```
数据源                    离线处理                    持久化              在线消费
─────────                ─────────                  ─────────           ─────────

模式 A/B/C：单区域管线
───────────────────────
GeoJSON 文件 ──┐
               ├─→ IGISDataProvider ──→ PolygonDeriver ──→ DataAsset ──→ PCG 节点 ──→ 采样点
ArcGIS REST ──┘        │                    │            (.uasset)         │
                       │                    │                              │
(可选) DEM 文件 ───→ QueryElevation    TerrainAnalyzer               属性过滤
                       │                    │                              │
                       │              ┌─ 有高程：矢量切割               ├─ 建筑 HISM
                       │              └─ 无高程：矢量面域直接           ├─ 树木 HISM
                       │                    │                          ├─ 道路 Spline
                       │              LandUseClassifier                └─ 水面 Mesh
                       │                    │
                       │              AssignPCGParams
                       │                    │
                       └────────────→ TArray<FLandUsePolygon>

模式 D：瓦片化管线（全球规模）
────────────────────────────
Python 预处理（PBF/DEM/WorldCover）
        │
        ├→ tile_manifest.json
        ├→ tiles/tile_X_Y.geojson
        ├→ tiles/tile_X_Y_dem.png
        └→ tiles/tile_X_Y_landcover.json
                │
    TiledFileProvider（LRU 缓存，跨瓦片去重）
                │
    TiledWorldBuilder（编辑器批量工具）
                │
    逐瓦片 ULandUseMapDataAsset（空间索引）
                │
    TiledLandUseMapDataAsset（异步流式加载目录）
                │
        PCG 采样器（瓦片感知，LoadRadius）

模式 E：CesiumTiled（全球规模 + 3D 地球）
─────────────────────────────────────────
Python 预处理（SRTM → 高程缓存）
        │
        ├→ dem_cache/elevation_X_Y.bin   ← O(1) 双线性插值查表
        ├→ tile_manifest.json            ← 矢量数据索引
        └→ tiles/tile_X_Y.geojson        ← 本地 GeoJSON
                │
    CesiumBridgeComponent
        ├→ LLH ↔ UE5 坐标桥接（Cesium ECEF 或 Mercator 回退）
        ├→ 离线 DEM 高程缓存（替代运行时 Line Trace）
        └→ PCG LOD 距离控制（全细节 10m / 中等 30m / 低 100m / 剔除）
                │
    TiledFileProvider（矢量数据）+ CesiumBridge（高程数据）
                │
    PolygonDeriver（高程优先级：DEM 缓存 > DataProvider）
                │
    Cesium 3D Tiles 地形渲染 + PCG 程序化内容
```

## 工作流程

### 离线阶段（编辑器）

**模式 A：本地文件**

1. 将 GeoJSON 放到 `Content/GISData/Region_01/osm_data.geojson`
2. （可选）将 DEM 瓦片放到 `Content/GISData/Region_01/DEM/`
3. 将 `AGISWorldBuilder` 拖入关卡
4. 在 Details 面板中配置：
   - `DataSourceType` = LocalFile
   - `GeoJsonPath`、`DEMPath`（可选）
   - `OriginLongitude` / `OriginLatitude`（投影原点经纬度）
5. 点击 **Generate In Editor** 预览
6. 点击 **Generate And Save Data Asset** 保存为 `.uasset`

**模式 B：ArcGIS REST 在线服务**

1. 在 Details 面板中配置：
   - `DataSourceType` = ArcGISRest
   - `FeatureServiceUrl`、`ArcGISApiKey`
   - `AdditionalLayerUrls`（可选，多图层）
   - `QueryBounds`（可选，查询范围）
2. 点击 **Generate And Save Data Asset**

**模式 C：已有 DataAsset**

1. `DataSourceType` = DataAsset
2. 引用已有的 `ULandUseMapDataAsset`
3. 点击 **Generate In Editor** 直接读取显示

**模式 D：瓦片化文件（大规模）**

1. 运行 Python 预处理管线生成瓦片数据：
   ```bash
   python Tools/GISPreprocess/preprocess.py --input ./RawData \
       --output Content/GISData/Region_01 --tile-size 1024 \
       --origin-lon 121.47 --origin-lat 31.23
   ```
2. 方式 A：使用 `AGISWorldBuilder`
   - `DataSourceType` = TiledFile
   - `TileManifestPath` = `GISData/Region_01/tile_manifest.json`
   - 点击 **Generate And Save Data Asset**
3. 方式 B：使用 `ATiledWorldBuilder`（批量模式，推荐大面积使用）
   - 将 `ATiledWorldBuilder` 拖入关卡
   - 设置 `ManifestPath`、`ClassifyRules`
   - 点击 **Generate All Tiles**
   - 产出逐瓦片 DataAsset + `UTiledLandUseMapDataAsset` 目录

**模式 E：CesiumTiled（全球规模 + 3D 地球）**

1. （可选）安装 [Cesium for Unreal](https://cesium.com/platform/cesium-for-unreal/) — 不装也能工作（自动回退 Mercator 模式）
2. 生成离线 DEM 高程缓存：
   ```bash
   python Tools/GISPreprocess/srtm_to_terrain.py /path/to/N31E121.hgt \
       -o Content/GISData/Region_01 --tile-size 1000 --grid-size 100
   ```
3. 配置 `AGISWorldBuilder`：
   - `DataSourceType` = CesiumTiled
   - `TileManifestPath` = `GISData/Region_01/tile_manifest.json`
   - `DEMCacheDirectory` = `GISData/Region_01/dem_cache/`
   - （如装了 Cesium）`CesiumGeoreferenceActor` = 场景中的 CesiumGeoreference
4. 点击 **Generate In Editor** → 地形感知的多边形 + Cesium 3D 地球渲染

无需 Cesium ion 账号 — 所有数据完全自托管。

### 在线阶段（运行时 / PCG）

1. 在 PCG Graph 中放置 **GIS Land Use Sampler** 节点
   - `LandUseDataAsset` = 引用离线生成的 `.uasset`
   - `SamplingInterval` = 10m（可配置）
   - `FilterTypes` = [Forest]（可选，只采某类型）
   - `bJitterPoints` = true（随机抖动）
2. PCG 节点输出带 metadata 的采样点：
   - `LandUseType`、`PolygonID`、`BuildingDensity`、`VegetationDensity`
3. 下游 PCG 节点通过 Attribute Filter 按地块类型驱动不同 Spawner

## 模块清单

| 模块 | 完成度 | 说明 |
|------|--------|------|
| **IGISDataProvider** | 100% | 数据源抽象接口 |
| **LocalFileProvider** | 100% | 本地 GeoJSON + DEM 读取，带缓存 |
| **ArcGISRestProvider** | 100% | HTTP 查询、分页、多图层、API Key 认证 |
| **GeoJsonParser** | 100% | 完整 GeoJSON 解析，全几何类型，OSM 类别推断 |
| **GISCoordinate** | 100% | 经纬度 ↔ UE5 世界坐标 + WGS84 ↔ UTM + Cesium ECEF 模式 |
| **DEMParser** | 85% | PNG/RAW/HGT 完整；GeoTIFF 简化版（完整支持需集成 GDAL） |
| **TerrainAnalyzer** | 100% | 5 步流水线：高程网格 → 坡度坡向 → 分类 → 连通分量标记 → 边界提取 |
| **PolygonDeriver** | 100% | 两种模式的多边形生成，含迭代 Sutherland-Hodgman 多边形-线段切割 |
| **RoadNetworkGraph** | 100% | 平面图构建，O(n²) 交叉检测 + 边分割 |
| **LandUseClassifier** | 100% | 9 条分类规则 + PCG 参数填充 + ESA WorldCover 栅格融合 |
| **PCGGISNode** | 100% | 网格采样 + 点在多边形测试，瓦片感知执行，输出 4 个 metadata 属性 |
| **GISWorldBuilder** | 100% | 五模式调度器（LocalFile / ArcGIS / DataAsset / TiledFile / CesiumTiled），编辑器预览，异步生成 |
| **LandUseMapDataAsset** | 100% | 持久化多边形存储 + 网格空间索引 + 异步瓦片加载 |
| **GISPolygonComponent** | 100% | Actor 组件，射线法点在多边形测试，调试可视化 |
| **CesiumBridgeComponent** | 100% | LLH↔UE5 坐标桥接、离线 DEM 高程缓存、PCG LOD 距离控制 |
| **TiledFileProvider** | 100% | 瓦片化数据源，LRU 缓存，跨瓦片去重 |
| **TileManifest** | 100% | 瓦片清单 JSON 解析，地理范围查询 |
| **RasterLandCoverParser** | 100% | ESA WorldCover JSON 栅格解析，多数投票区域查询 |
| **TiledLandUseMapDataAsset** | 100% | 逐瓦片 DataAsset 目录，FStreamableManager 异步流式加载 |
| **TiledWorldBuilder** | 100% | 编辑器批量工具：清单 → 逐瓦片 PolygonDeriver → DataAsset 目录 |
| **LandCoverGrid** | 100% | ESA WorldCover 共享类型（EWorldCoverClass, FLandCoverGrid） |

## 核心算法

### CutPolygonWithLines（多边形切割）

基于 Sutherland-Hodgman 变体的迭代切割：
- 对每条切割线（道路/河流/海岸线 polyline）的每个线段：
  - 检测线段与多边形边界的交点对
  - 沿延长线将多边形切成左右两半
  - 用两半替换工作集中的原多边形
- 过滤退化碎片（面积低于阈值）

### ComputeIntersectionsAndSplit（道路交叉分割）

O(n²) 边对扫描，构建真正的平面图：
- 检测所有线段-线段交叉（排除共享端点的相邻边）
- 在交叉点插入新节点（带去重容差）
- 将原始边分割为子边链
- 是后续 Planar Face Extraction（`GetLeftmostTurn()`）的前提

### TerrainAnalyzer 流水线

1. **BuildElevationGrid** — 从 DEM 瓦片构建规则高程网格
2. **ComputeSlopeAspect** — Horn 3×3 Sobel 法计算坡度和坡向
3. **ClassifyTerrainGrid** — 高程 + 坡度 → 6 个地形类别
4. **LabelConnectedZones** — 两遍 CCL + Union-Find + 小区域合并
5. **ExtractZoneBoundaries** — 轮廓追踪 → 多边形顶点序列

### 离线 DEM 高程缓存（CesiumBridgeComponent）

二进制缓存格式（每 tile）：`[int32 GridSize][float CellSizeM][4×double 地理范围][float×GridSize²]`
- O(1) 双线性插值查表（~5ns/次查询）
- 高程优先级：DEM 缓存 → DataProvider::QueryElevation → 回退 0
- thread_local 上次命中缓存，内循环中摊销 O(1) 查找

### PCG LOD 距离控制（CesiumBridgeComponent）

三级采样密度联动相机距离：
- 全细节（≤1km）：10m 采样间隔
- 中等（≤3km）：30m 采样间隔
- 低细节（≤7km）：100m 采样间隔
- 剔除（>7km）：不采样

## 文件结构

```
Plugins/GISProcedural/
├── GISProcedural.uplugin
├── Source/GISProcedural/
│   ├── GISProcedural.Build.cs
│   ├── Public/
│   │   ├── GISProceduralModule.h
│   │   ├── Data/
│   │   │   ├── IGISDataProvider.h        数据源接口
│   │   │   ├── GeoRect.h                 地理范围结构体
│   │   │   ├── GISFeature.h              要素数据结构
│   │   │   ├── GISCoordinate.h           坐标转换（SimpleMercator/UTM/Cesium）
│   │   │   ├── GeoJsonParser.h           GeoJSON 解析
│   │   │   ├── LocalFileProvider.h       本地文件数据源
│   │   │   ├── ArcGISRestProvider.h      ArcGIS REST 数据源
│   │   │   ├── LandUseMapDataAsset.h     离线 DataAsset
│   │   │   ├── TileManifest.h            瓦片清单解析
│   │   │   ├── TiledFileProvider.h       瓦片化数据源
│   │   │   ├── TiledLandUseMapDataAsset.h  逐瓦片 DataAsset 目录
│   │   │   ├── RasterLandCoverParser.h   地表覆盖栅格解析
│   │   │   └── LandCoverGrid.h           WorldCover 共享类型
│   │   ├── DEM/
│   │   │   ├── DEMTypes.h                DEM 类型定义
│   │   │   ├── DEMParser.h               DEM 解析器
│   │   │   └── TerrainAnalyzer.h         地形分析器
│   │   ├── Polygon/
│   │   │   ├── LandUsePolygon.h          输出多边形结构体
│   │   │   ├── PolygonDeriver.h          多边形推导核心
│   │   │   ├── RoadNetworkGraph.h        道路网络图
│   │   │   └── LandUseClassifier.h       土地分类器
│   │   ├── PCG/
│   │   │   ├── PCGGISNode.h              PCG 采样节点
│   │   │   └── PCGLandUseData.h          PCG 数据类型
│   │   ├── Components/
│   │   │   └── GISPolygonComponent.h     Actor 组件
│   │   └── Runtime/
│   │       ├── GISWorldBuilder.h         五模式世界构建器
│   │       ├── TiledWorldBuilder.h       瓦片批量生成器
│   │       └── CesiumBridgeComponent.h   Cesium 桥接组件
│   └── Private/
│       ├── GISProceduralModule.cpp
│       ├── Data/
│       │   ├── GISCoordinate.cpp
│       │   ├── GeoJsonParser.cpp
│       │   ├── LocalFileProvider.cpp
│       │   ├── ArcGISRestProvider.cpp
│       │   ├── LandUseMapDataAsset.cpp
│       │   ├── TileManifest.cpp
│       │   ├── TiledFileProvider.cpp
│       │   ├── TiledLandUseMapDataAsset.cpp
│       │   └── RasterLandCoverParser.cpp
│       ├── DEM/
│       │   ├── DEMParser.cpp
│       │   └── TerrainAnalyzer.cpp
│       ├── Polygon/
│       │   ├── PolygonDeriver.cpp
│       │   ├── RoadNetworkGraph.cpp
│       │   └── LandUseClassifier.cpp
│       ├── PCG/
│       │   ├── PCGGISNode.cpp
│       │   └── PCGLandUseData.cpp
│       ├── Components/
│       │   └── GISPolygonComponent.cpp
│       └── Runtime/
│           ├── GISWorldBuilder.cpp
│           ├── TiledWorldBuilder.cpp
│           └── CesiumBridgeComponent.cpp
```

## Python 预处理管线

位于 `Tools/GISPreprocess/`。将原始 GIS 数据转换为瓦片化格式。

```bash
python preprocess.py --input ./RawData --output Content/GISData/Region_01 \
    --tile-size 1024 --origin-lon 121.47 --origin-lat 31.23
```

| 脚本 | 用途 |
|------|------|
| `preprocess.py` | 主入口，编排所有步骤 |
| `pbf_to_geojson.py` | PBF → GeoJSON（osmium/ogr2ogr） |
| `tile_cutter.py` | 矢量瓦片切割，跨边界要素追踪 |
| `projection.py` | WGS84 ↔ UTM 转换（与 UE5 GISCoordinate 匹配） |
| `raster_classifier.py` | ESA WorldCover GeoTIFF → 逐瓦片 landcover.json |
| `dem_cropper.py` | DEM GeoTIFF → 逐瓦片 16bit PNG |
| `manifest_writer.py` | 生成 tile_manifest.json |
| `validate.py` | 输出完整性验证 |
| `srtm_to_terrain.py` | SRTM/GeoTIFF → 二进制高程缓存（CesiumTiled 模式用） |

**依赖**：Python 3.10+。可选：GDAL（DEM/栅格）、osmium-tool（PBF）、numpy（DEM 缓存生成）。

## 已知限制

- **DEMParser GeoTIFF**：当前为简化 TIFF 读取器；完整 GeoTIFF 支持需集成 GDAL/libgeotiff。临时方案：使用 `ManualTileInfo` 属性手动指定瓦片信息，或使用 PNG/RAW 格式。
- **LandUseClassifier 水体检测**：可选增强项，需要水体矢量数据；当前分类在无水体矢量时仍可通过 DEM 候选水体检测工作。
- **Multi-tile DEM 合并**：TiledFileProvider 当前仅返回单瓦片 DEM；跨瓦片 DEM 查询需后续 DEMParser 增强。
- **Multi-tile LandCover 合并**：跨瓦片 LandCover 查询暂未支持；单瓦片查询正常工作。
- **Cesium 软依赖**：未安装 CesiumForUnreal 时 WITH_CESIUM=0。CesiumBridgeComponent 回退到 Mercator 坐标模式；所有坐标和高程 API 仍正常工作。
