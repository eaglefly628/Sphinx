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
| LandUseClassifier | 100% | 9 条分类规则 + PCG 参数填充 |
| LocalFileProvider | 100% | 本地 GeoJSON + DEM 读取，带缓存 |
| ArcGISRestProvider | 100% | HTTP 查询、分页、多图层 |
| PCGGISNode | 100% | 网格采样 + 点在多边形测试，4 个 metadata 属性 |
| GISWorldBuilder | 100% | 三模式调度器，编辑器预览，异步生成 |
| LandUseMapDataAsset | 100% | 持久化多边形存储，空间查询 |
| GISCoordinate | 100% | 经纬度 ↔ UE5 世界坐标转换 |
| GISPolygonComponent | 100% | Actor 组件，调试可视化 |

详细算法文档见 [`Plugins/GISProcedural/README_CN.md`](Plugins/GISProcedural/README_CN.md)。

## 技术栈

- **引擎**：Unreal Engine 5
- **PCG**：UE5 程序化内容生成框架
- **数据格式**：GeoJSON、GeoTIFF、PNG 高度图（Mapbox Terrain RGB）、RAW (.r16/.r32)、SRTM HGT
- **在线服务**：ArcGIS REST Feature Services
- **语言**：C++（UE5 插件），无 UE5 以外的外部依赖

## 许可

内部项目 — MilSim Team。
