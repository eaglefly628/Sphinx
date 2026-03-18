# GISProcedural 插件

基于真实 GIS 数据（GeoJSON、DEM、ArcGIS REST）的 Unreal Engine 5 程序化世界生成插件。

## 架构总览

```
数据源                    离线处理                    持久化              在线消费
─────────                ─────────                  ─────────           ─────────

GeoJSON 文件 ──┐
               ├─→ IGISDataProvider ──→ PolygonDeriver ──→ DataAsset ──→ PCG 节点 ──→ 采样点
ArcGIS REST ──┘        │                    │            (.uasset)         │
                       │                    │                              │
(可选) DEM 文件 ───→ QueryElevation    TerrainAnalyzer               属性过滤
                       │                    │                              │
                       │              ┌─ 有高程：矢量切割               ├─ 树木 HISM
                       │              └─ 无高程：矢量面域直接           ├─ 建筑 HISM
                       │                    │                          └─ 水面 Mesh
                       │              LandUseClassifier
                       │                    │
                       │              AssignPCGParams
                       │                    │
                       └────────────→ TArray<FLandUsePolygon>
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
| **GISCoordinate** | 100% | 经纬度 ↔ UE5 世界坐标双向转换 |
| **DEMParser** | 85% | PNG/RAW/HGT 完整；GeoTIFF 简化版（完整支持需集成 GDAL） |
| **TerrainAnalyzer** | 100% | 5 步流水线：高程网格 → 坡度坡向 → 分类 → 连通分量标记 → 边界提取 |
| **PolygonDeriver** | 100% | 两种模式的多边形生成，含迭代 Sutherland-Hodgman 多边形-线段切割 |
| **RoadNetworkGraph** | 100% | 平面图构建，O(n²) 交叉检测 + 边分割 |
| **LandUseClassifier** | 100% | 9 条分类规则 + PCG 参数填充 |
| **PCGGISNode** | 100% | 网格采样 + 点在多边形测试，输出 4 个 metadata 属性 |
| **GISWorldBuilder** | 100% | 三模式调度器（LocalFile / ArcGIS / DataAsset），编辑器预览，异步生成 |
| **LandUseMapDataAsset** | 100% | 持久化多边形存储，支持空间查询 |
| **GISPolygonComponent** | 100% | Actor 组件，射线法点在多边形测试，调试可视化 |

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

## 文件结构

```
Plugins/GISProcedural/
├── GISProcedural.uplugin
├── README.md                    ← 英文文档
├── README_CN.md                 ← 中文文档
├── Source/GISProcedural/
│   ├── GISProcedural.Build.cs
│   ├── Public/
│   │   ├── GISProceduralModule.h
│   │   ├── Data/
│   │   │   ├── IGISDataProvider.h        数据源接口
│   │   │   ├── GeoRect.h                 地理范围结构体
│   │   │   ├── GISFeature.h              要素数据结构
│   │   │   ├── GISCoordinate.h           坐标转换
│   │   │   ├── GeoJsonParser.h           GeoJSON 解析
│   │   │   ├── LocalFileProvider.h       本地文件数据源
│   │   │   ├── ArcGISRestProvider.h      ArcGIS REST 数据源
│   │   │   └── LandUseMapDataAsset.h     离线 DataAsset
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
│   │       └── GISWorldBuilder.h         世界构建器
│   └── Private/
│       └── (对应 .cpp 实现文件)
```

## 已知限制

- **DEMParser GeoTIFF**：当前为简化 TIFF 读取器；完整 GeoTIFF 支持需集成 GDAL/libgeotiff。临时方案：使用 `ManualTileInfo` 属性手动指定瓦片信息，或使用 PNG/RAW 格式。
- **LandUseClassifier 水体检测**：可选增强项，需要水体矢量数据；当前分类在无水体矢量时仍可通过 DEM 候选水体检测工作。
