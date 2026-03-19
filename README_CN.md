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
数据源                    编辑器（离线）                持久化                运行时（在线）
─────────                ─────────────               ─────────             ────────────

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
| **TileManifest** | **新增** | 瓦片清单共享类型，流式管线基础 |
| **LandCoverGrid** | **新增** | ESA WorldCover 栅格类型，土地覆盖融合 |

详细算法文档见 [`Plugins/GISProcedural/README_CN.md`](Plugins/GISProcedural/README_CN.md)。

## 扩展路线图（全球军事仿真）

插件按 4 阶段计划从 ~10 km² 演示扩展到全球覆盖：

| 阶段 | 内容 | 负责人 |
|------|------|--------|
| **P0** 接口约定 | 共享类型、UTM 投影、LandCover 融合 API | 鹰飞（修改现有文件） |
| **P1** 外部预处理 | Python/GDAL 管线：PBF→GeoJSON、DEM 切片、WorldCover | 小城（新增文件） |
| **P2** TiledFileProvider + 空间索引 | 瓦片清单解析、R-tree 空间索引、LRU 缓存 | 小城（新增文件） |
| **P3** World Partition 集成 | WP 流式数据源、关卡实例化瓦片 Actor | 鹰飞（修改现有文件） |

完整协作计划见 [`PLAN.md`](PLAN.md)。

## 开发流程

```
1. 准备 GIS 数据
   overpass-turbo → GeoJSON 导出
   OpenTopography / SRTM → DEM 瓦片
   ESA WorldCover → LandCover GeoTIFF（可选，10m 分辨率）

2. (P1+) 运行预处理管线
   python Tools/preprocess.py --input ./RawData --output Content/GISData/Region_01 --tile-size 1024

3. 编辑器：生成 & 预览
   拖入 AGISWorldBuilder → 设置 DataSourceType → GenerateInEditor → 目视检查

4. 编辑器：烘焙为 DataAsset
   GenerateAndSaveDataAsset → ULandUseMapDataAsset 保存到 /Game/GISData/

5. PCG 图表
   GIS Land Use Sampler 节点 → 属性过滤器 → 建筑/树木/道路 Spawner

6. (P3+) 运行时流式加载
   World Partition 随玩家移动自动加载瓦片 DataAsset
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
