# Sphinx

> **[English Version →](README.md)**

基于真实 GIS 数据驱动的 Unreal Engine 5 程序化军事仿真世界生成项目。

## 项目概述

Sphinx 将真实地理数据——OpenStreetMap 导出、DEM 高程瓦片、ArcGIS 要素服务——转化为土地分类多边形，驱动 UE5 的 PCG（程序化内容生成）框架自动填充地形上的建筑、植被、道路和水体。

### 核心能力

- **多源 GIS 数据接入** — 本地 GeoJSON/DEM 文件或在线 ArcGIS REST 查询
- **地形分析** — DEM → 坡度坡向 → 地形分类 → 连通区域提取
- **多边形推导** — 地形分区被道路/河流/海岸线矢量切割为土地利用多边形
- **自动分类** — 9 条规则系统：住宅、商业、工业、森林、农田、水体、道路、开放空间、军事
- **PCG 集成** — 自定义采样节点输出带密度/类型 metadata 的点集，驱动下游 Spawner
- **离线→在线流水线** — 编辑器生成 → DataAsset 持久化 → 运行时 PCG 读取

## 架构总览

```
模式 A/B/C：单区域管线
─────────────────────
GeoJSON (OSM) ──┐
                ├─→ IGISDataProvider → PolygonDeriver → ULandUseMapDataAsset → PCG 采样器
ArcGIS REST  ──┘        │                    │            (.uasset)              │
                        │                    │                                   │
DEM 瓦片 ──────→ QueryElevation        TerrainAnalyzer                    属性过滤器
(GeoTIFF/PNG/RAW)       │                    │                                   │
                        │              ┌─ 有高程 → 矢量切割                     ├→ 建筑 HISM
                        │              └─ 无高程 → 矢量面域                     ├→ 树木 HISM
                        │                    │                                   ├→ 道路 Spline
                        │              LandUseClassifier                         └→ 水面 Mesh
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
```

## 快速开始

### 本地文件模式

1. 从 [overpass-turbo](https://overpass-turbo.eu/) 导出目标区域的 GeoJSON
2. （可选）从 [OpenTopography](https://opentopography.org/) 或 SRTM 下载 DEM 瓦片
3. 将文件放到 `Content/GISData/YourRegion/` 下
4. 将 `AGISWorldBuilder` 拖入关卡
5. 设置 `DataSourceType = LocalFile`，配置文件路径和原点经纬度
6. **Generate In Editor** → 预览 → **Generate And Save Data Asset** 保存
7. 在 PCG Graph 中通过 GIS Land Use Sampler 节点引用生成的 DataAsset

### ArcGIS REST 在线模式

1. 设置 `DataSourceType = ArcGISRest`
2. 输入 Feature Service URL 和 API Key
3. **Generate And Save Data Asset** → 后续 PCG 流程相同

### 瓦片文件模式（全球规模）

1. 准备原始数据：OSM PBF、DEM GeoTIFF、ESA WorldCover GeoTIFF
2. 运行 Python 预处理管线：
   ```bash
   python Tools/GISPreprocess/preprocess.py --input ./RawData --output Content/GISData/Region_01 --tile-size 1024
   ```
3. 设置 `DataSourceType = TiledFile`，将 `TileManifestPath` 指向生成的 `tile_manifest.json`
4. 在编辑器中使用 `ATiledWorldBuilder` 批量生成逐瓦片 DataAsset（含 LandCover 融合）
5. 在 PCG 图表中引用 `UTiledLandUseMapDataAsset` 目录 — 瓦片自动流式加载

## 插件：GISProcedural

| 模块 | 完成度 | 用途 |
|------|--------|------|
| GeoJsonParser | 100% | GeoJSON 解析，OSM 类别推断 |
| DEMParser | 85% | GeoTIFF（简化版）、PNG 高度图、RAW、SRTM HGT |
| TerrainAnalyzer | 100% | 高程网格 → 坡度 → 分类 → 连通分量标记 → 边界多边形 |
| PolygonDeriver | 100% | 地形分区 + 矢量切割（Sutherland-Hodgman 变体） |
| RoadNetworkGraph | 100% | 平面图，交叉检测 + 边分割 |
| LandUseClassifier | 100% | 9 条分类规则 + PCG 参数填充 + LandCover 栅格融合 |
| LocalFileProvider | 100% | 本地 GeoJSON + DEM 读取，带缓存 |
| ArcGISRestProvider | 100% | HTTP 查询、分页、多图层 |
| PCGGISNode | 100% | 网格采样 + 点在多边形测试，支持瓦片流式配置 |
| GISWorldBuilder | 100% | 四模式调度器（LocalFile/ArcGIS/DataAsset/TiledFile） |
| LandUseMapDataAsset | 100% | 持久化多边形存储，空间查询 |
| GISCoordinate | 100% | 经纬度 ↔ UE5 世界坐标 + WGS84 ↔ UTM 投影 |
| GISPolygonComponent | 100% | Actor 组件，调试可视化 |
| **TileManifest** | 100% | 瓦片清单 JSON 解析，地理范围查询 |
| **LandCoverGrid** | 100% | ESA WorldCover 栅格类型，土地覆盖融合 |
| **TiledFileProvider** | 100% | 瓦片化数据源，LRU 缓存，跨瓦片去重 |
| **RasterLandCoverParser** | 100% | landcover.json 解析，多数投票区域查询 |
| **TiledLandUseMapDataAsset** | 100% | 逐瓦片 DataAsset 目录，FStreamableManager 异步流式加载 |
| **TiledWorldBuilder** | 100% | 编辑器批量工具：清单 → 逐瓦片 DataAsset 生成 |

详细算法文档见 [`Plugins/GISProcedural/README_CN.md`](Plugins/GISProcedural/README_CN.md)。

## 扩展路线图（全球军事仿真）

全部 4 阶段已完成。插件已从 ~10 km² 演示扩展到支持全球覆盖：

| 阶段 | 内容 | 状态 |
|------|------|------|
| **P0** 接口约定 | 共享类型、UTM 投影、LandCover 融合 API | 完成 |
| **P1** 外部预处理 | Python/GDAL 管线：PBF→GeoJSON、DEM 切片、WorldCover | 完成 |
| **P2** TiledFileProvider + 空间索引 | 瓦片清单解析、网格空间索引、LRU 缓存 | 完成 |
| **P3** 瓦片 DataAsset + 流式加载 | 逐瓦片 DataAsset 生成、异步流式加载目录 | 完成 |

原始协作计划见 [`PLAN.md`](PLAN.md)。

## 开发流程

```
1. 准备 GIS 数据
   overpass-turbo → GeoJSON 导出
   OpenTopography / SRTM → DEM 瓦片
   ESA WorldCover → LandCover GeoTIFF（可选，10m 分辨率）

2. 运行预处理管线（瓦片模式）
   python Tools/GISPreprocess/preprocess.py --input ./RawData --output Content/GISData/Region_01 --tile-size 1024

3. 编辑器：生成 & 预览
   拖入 AGISWorldBuilder → 设置 DataSourceType → GenerateInEditor → 目视检查
   （或使用 ATiledWorldBuilder 进行瓦片化批量生成）

4. 编辑器：烘焙为 DataAsset
   GenerateAndSaveDataAsset → ULandUseMapDataAsset 保存到 /Game/GISData/
   （瓦片模式：TiledWorldBuilder 自动生成逐瓦片 DataAsset）

5. PCG 图表
   GIS Land Use Sampler 节点 → 属性过滤器 → 建筑/树木/道路 Spawner
   （启用 bEnableTiling + 设置 LoadRadius 实现瓦片感知采样）

6. 运行时流式加载
   TiledLandUseMapDataAsset 按需异步加载瓦片 DataAsset
```

## 技术栈

- **引擎**：Unreal Engine 5
- **PCG**：UE5 程序化内容生成框架
- **数据格式**：GeoJSON、GeoTIFF、PNG 高度图（Mapbox Terrain RGB）、RAW (.r16/.r32)、SRTM HGT
- **在线服务**：ArcGIS REST Feature Services
- **预处理**：Python 3.10+、GDAL/OGR、osmium-tool（Phase 1+）
- **投影**：WGS84 ↔ UTM（内置）、简化 Mercator 用于局部区域
- **语言**：C++（UE5 插件）、Python（外部预处理）

## 许可

内部项目 — MilSim Team。
