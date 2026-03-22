# GISProcedural 插件使用手册

**版本**: v1.0.19
**引擎**: Unreal Engine 5
**更新日期**: 2026-03-22
**版本规则**: 主版本.次版本.提交序号（每次 git commit 自动递增）

---

## 目录

1. [环境准备](#1-环境准备)
2. [插件安装](#2-插件安装)
3. [模式 A：本地文件快速上手](#3-模式-a本地文件快速上手)
4. [模式 B：ArcGIS REST 在线数据](#4-模式-barcgis-rest-在线数据)
5. [模式 C：已有 DataAsset 直接加载](#5-模式-c已有-dataasset-直接加载)
6. [模式 D：瓦片化全球规模管线](#6-模式-d瓦片化全球规模管线)
7. [模式 E：CesiumTiled 全球规模 + 3D 地球](#7-模式-ecesiumtiled-全球规模--3d-地球)
8. [PCG 图表集成](#8-pcg-图表集成)
9. [调试与日志](#9-调试与日志)
10. [常见问题](#10-常见问题)
11. [附录：模块清单](#附录模块清单)

---

## 1. 环境准备

### 1.1 必须项

| 项目 | 要求 |
|------|------|
| Unreal Engine | 5.1 或更高版本 |
| 编译器 | Visual Studio 2022 (Windows) 或 Xcode 14+ (Mac) |
| C++ 标准 | C++17（UE5 默认） |

### 1.2 可选项（瓦片模式 / CesiumTiled 模式需要）

| 项目 | 用途 |
|------|------|
| Python 3.10+ | 运行预处理管线 |
| GDAL/OGR (`pip install gdal`) | DEM 裁切、WorldCover 栅格解析 |
| osmium-tool (`pip install osmium`) | PBF → GeoJSON 转换 |
| numpy (`pip install numpy`) | DEM 高程缓存生成（srtm_to_terrain.py） |
| Cesium for Unreal 2.x | 可选，3D 地球渲染（不装走 Mercator 回退） |

安装 Python 依赖：

```bash
pip install -r Tools/GISPreprocess/requirements.txt
```

---

## 2. 插件安装

### 2.1 拷贝插件

将 `Plugins/GISProcedural/` 整个文件夹拷贝到你的 UE5 项目的 `Plugins/` 目录下：

```
YourProject/
├── Content/
├── Source/
├── Plugins/
│   └── GISProcedural/        ← 拷贝到这里
│       ├── GISProcedural.uplugin
│       ├── Source/
│       └── Content/
└── YourProject.uproject
```

### 2.2 启用插件

1. 打开 UE5 编辑器
2. **Edit → Plugins**
3. 搜索 `GISProcedural`
4. 勾选 **Enabled**
5. 重启编辑器

### 2.3 验证安装

打开 **Output Log** (Window → Developer Tools → Output Log)，搜索：

```
LogGIS: GISProcedural: Module started
```

看到此日志说明插件已正确加载。

---

## 3. 模式 A：本地文件快速上手

**适用场景**：小区域（~10km² 以内），本地已有 GeoJSON/DEM 文件。

### 3.1 准备数据

#### 步骤 1：导出 GeoJSON

1. 打开 [overpass-turbo](https://overpass-turbo.eu/)
2. 定位到目标区域
3. 输入 Overpass QL 查询，例如：
   ```
   [out:json];
   (
     way["building"]({{bbox}});
     way["highway"]({{bbox}});
     way["landuse"]({{bbox}});
     way["natural"]({{bbox}});
     way["waterway"]({{bbox}});
   );
   out body;
   >;
   out skel qt;
   ```
4. 点击 **Export → GeoJSON → download**
5. 保存为 `osm_data.geojson`

#### 步骤 2：（可选）准备 DEM

从 [OpenTopography](https://opentopography.org/) 或 SRTM 下载高程数据：
- 支持格式：GeoTIFF、PNG 高度图、RAW (.r16/.r32)、SRTM HGT
- 如无 DEM，插件会使用 "仅矢量" 模式生成面域

#### 步骤 3：放置文件

```
Content/GISData/MyRegion/
├── osm_data.geojson
└── DEM/                    ← 可选
    └── dem_tile.png
```

### 3.2 编辑器操作

#### 步骤 4：放置 WorldBuilder

1. 在 Content Browser 中找到 **Place Actors → All Classes → GISWorldBuilder**
2. 将 `AGISWorldBuilder` 拖入关卡视口

#### 步骤 5：配置属性

在 **Details** 面板中设置：

| 属性 | 值 | 说明 |
|------|-----|------|
| `DataSourceType` | **LocalFile** | 数据源模式 |
| `GeoJsonPath` | `GISData/MyRegion/osm_data.geojson` | 相对于 Content 目录 |
| `DEMPath` | `GISData/MyRegion/DEM/` | 可选，DEM 文件或目录 |
| `DEMFormat` | `PNG_Heightmap` / `GeoTIFF` / `RAW_16` | 高程格式 |
| `OriginLongitude` | `121.4737` | 地图中心经度 (WGS84) |
| `OriginLatitude` | `31.2304` | 地图中心纬度 (WGS84) |
| `MinPolygonArea` | `100.0` | 最小多边形面积 (m²)，过滤碎片 |
| `bDrawDebugPolygons` | `true` | 预览时绘制调试线 |

#### 步骤 6：预览生成

1. 选中 `AGISWorldBuilder` Actor
2. 在 Details 面板顶部点击 **Generate In Editor** 按钮
3. 观察视口中出现的彩色调试线（不同颜色代表不同土地类型）
4. 查看 Output Log 中的统计信息：
   ```
   LogGIS: ========== GIS World Builder Statistics ==========
   LogGIS: Total Polygons: 156 | Total Area: 2340567.3 m2
   LogGIS:   Residential :  45 polygons |  234567.1 m2 |  10.0%
   LogGIS:   Forest      :  32 polygons |  890123.4 m2 |  38.0%
   ...
   ```

#### 步骤 7：保存 DataAsset

1. 确认预览效果满意
2. 点击 **Generate And Save Data Asset**
3. 生成的 `.uasset` 保存到 `/Game/GISData/LandUse_<ActorLabel>.uasset`
4. 日志确认：
   ```
   LogGIS: GISWorldBuilder: Saved DataAsset to .../LandUse_GISWorldBuilder.uasset (156 polygons)
   ```

---

## 4. 模式 B：ArcGIS REST 在线数据

**适用场景**：需要从 ArcGIS Feature Service 实时拉取矢量数据。

### 4.1 操作步骤

1. 拖入 `AGISWorldBuilder` 到关卡
2. 配置属性：

| 属性 | 值 | 说明 |
|------|-----|------|
| `DataSourceType` | **ArcGISRest** | |
| `FeatureServiceUrl` | `https://services.arcgis.com/.../FeatureServer/0` | Feature Service URL |
| `ArcGISApiKey` | `你的API Key` | ArcGIS 开发者 API Key |
| `AdditionalLayerUrls` | 可选，添加更多图层 URL | 多图层支持 |
| `QueryBounds` | 可选，限制查询范围 | 减少数据量 |

3. 点击 **Generate And Save Data Asset**

---

## 5. 模式 C：已有 DataAsset 直接加载

**适用场景**：已经通过模式 A/B/D 生成了 DataAsset，想直接加载使用。

1. 拖入 `AGISWorldBuilder` 到关卡
2. 设置 `DataSourceType` = **DataAsset**
3. 在 `LandUseDataAsset` 属性中引用已有的 `.uasset` 文件
4. 点击 **Generate In Editor** → 直接加载已有多边形数据

---

## 6. 模式 D：瓦片化全球规模管线

**适用场景**：大面积区域（城市/国家/全球），需要瓦片化分块处理和运行时流式加载。

### 6.1 Python 预处理

#### 步骤 1：准备原始数据

```
RawData/
├── region.osm.pbf          ← OpenStreetMap PBF 下载
├── dem.tif                 ← DEM GeoTIFF (SRTM/ASTER)
└── worldcover.tif          ← ESA WorldCover GeoTIFF (可选，10m分辨率)
```

**数据下载来源**：
- OSM PBF: [Geofabrik](https://download.geofabrik.de/) 或 [BBBike](https://extract.bbbike.org/)
- DEM: [OpenTopography](https://opentopography.org/) 或 [USGS EarthExplorer](https://earthexplorer.usgs.gov/)
- WorldCover: [ESA WorldCover](https://worldcover2021.esa.int/)

#### 步骤 2：运行预处理管线

```bash
cd Tools/GISPreprocess/

python preprocess.py \
    --input ../../RawData \
    --output ../../Content/GISData/Region_01 \
    --tile-size 1024 \
    --origin-lon 121.47 \
    --origin-lat 31.23
```

**参数说明**：

| 参数 | 说明 | 示例 |
|------|------|------|
| `--input` | 原始数据目录 | `./RawData` |
| `--output` | 输出目录（Content 下） | `Content/GISData/Region_01` |
| `--tile-size` | 瓦片大小（米） | `1024`（即 1km×1km） |
| `--origin-lon` | 地图中心经度 | `121.47` |
| `--origin-lat` | 地图中心纬度 | `31.23` |

#### 步骤 3：验证输出

预处理完成后目录结构：

```
Content/GISData/Region_01/
├── tile_manifest.json              ← 瓦片清单
├── tiles/
│   ├── tile_0_0.geojson           ← 各瓦片矢量数据
│   ├── tile_0_0_dem.png           ← 各瓦片 DEM
│   ├── tile_0_0_landcover.json    ← 各瓦片地表覆盖
│   ├── tile_0_1.geojson
│   ├── tile_1_0.geojson
│   └── ...
└── validation_report.txt           ← 验证报告
```

管线执行 6 个步骤：
1. PBF → GeoJSON 转换
2. UTM 投影 + 瓦片切割
3. DEM 裁切（逐瓦片 PNG16）
4. WorldCover 栅格分类（逐瓦片 JSON）
5. 生成 tile_manifest.json
6. 完整性验证

### 6.2 UE5 批量生成

#### 步骤 4：使用 TiledWorldBuilder

1. 在关卡中放置 **ATiledWorldBuilder** Actor
2. 配置属性：

| 属性 | 值 | 说明 |
|------|-----|------|
| `ManifestPath` | `GISData/Region_01/tile_manifest.json` | 相对于 Content |
| `OutputPackagePath` | `/Game/GISData/Region_01/` | DataAsset 输出路径 |
| `ClassifyRules` | 引用分类规则资产 | 可选，自定义分类 |
| `MinPolygonArea` | `50.0` | 最小面积阈值 |
| `DEMFormat` | `PNG_Heightmap` | DEM 格式 |

3. 点击 **Generate All Tiles**
4. 观察日志输出进度：
   ```
   LogGIS: TiledWorldBuilder: [1/24] Processing tile_0_0 (156 features)...
   LogGIS: TiledWorldBuilder: [2/24] Processing tile_0_1 (89 features)...
   ...
   LogGIS: ========================================
   LogGIS: TiledWorldBuilder: Generation complete
   LogGIS:   Tiles: 20 success / 4 skipped / 0 failed (of 24)
   LogGIS:   Total polygons: 3456
   LogGIS:   Elapsed: 12.35s (1.9 tiles/sec)
   LogGIS: ========================================
   ```

#### 步骤 5：（可选）单瓦片测试

首次使用时建议先测试单个瓦片：
1. 点击 **Generate Single Test Tile**（自动选择要素最多的瓦片）
2. 确认生成效果正常后再执行全量生成

### 6.3 输出产物

批量生成后产生：
- `Tiles/tile_X_Y.uasset` — 每个瓦片的 `ULandUseMapDataAsset`（含空间索引）
- `TiledLandUse_<name>.uasset` — `UTiledLandUseMapDataAsset` 总目录（含所有瓦片软引用）

---

## 7. 模式 E：CesiumTiled 全球规模 + 3D 地球

**适用场景**：全球级别渲染，需要 Cesium 3D Tiles 地形 + 本地 GeoJSON PCG 内容叠加。

### 7.1 前置条件

- 完成模式 D 的 Python 预处理（生成 tile_manifest.json + tiles/）
- （可选）安装 [Cesium for Unreal](https://cesium.com/platform/cesium-for-unreal/) — 不装也能工作（自动回退 Mercator 模式，WITH_CESIUM=0）

### 7.2 生成离线 DEM 高程缓存

CesiumTiled 模式使用离线 DEM 缓存替代运行时 Line Trace，实现 O(1) 高程查询。

```bash
cd Tools/GISPreprocess/

python srtm_to_terrain.py \
    /path/to/N31E121.hgt \
    -o ../../Content/GISData/Region_01 \
    --tile-size 1000 \
    --grid-size 100
```

**参数说明**：

| 参数 | 说明 | 示例 |
|------|------|------|
| 第一个参数 | SRTM HGT 或 GeoTIFF 文件 | `N31E121.hgt` |
| `-o` | 输出目录 | `Content/GISData/Region_01` |
| `--tile-size` | 瓦片大小（米），与 preprocess.py 一致 | `1000` |
| `--grid-size` | 每瓦片高程网格分辨率 | `100`（即 100×100 点，10m 间距） |

输出：

```
Content/GISData/Region_01/
├── tile_manifest.json          ← 已有（模式 D 产出）
├── tiles/                      ← 已有
└── dem_cache/                  ← 新增
    ├── dem_cache_index.json    ← 缓存索引
    ├── elevation_0_0.bin       ← 二进制高程网格（40B 头 + float 数组）
    ├── elevation_0_1.bin
    └── ...
```

如无真实 DEM 数据，可跳过此步骤（系统降级为无地形模式）。

### 7.3 UE5 编辑器配置

1. 拖入 `AGISWorldBuilder` 到关卡
2. 配置属性：

| 属性 | 值 | 说明 |
|------|-----|------|
| `DataSourceType` | **CesiumTiled** | |
| `TileManifestPath` | `GISData/Region_01/tile_manifest.json` | 相对于 Content |
| `DEMCacheDirectory` | `GISData/Region_01/dem_cache/` | 高程缓存目录 |
| `OriginLongitude` | `121.47` | 地图中心经度 |
| `OriginLatitude` | `31.23` | 地图中心纬度 |

3. （如装了 Cesium 插件）额外配置：
   - 在场景中放置 `CesiumGeoreference`（设置 Origin 经纬度）
   - 在场景中放置 `Cesium3DTileset`（选择 Cesium World Terrain 或自托管 URL）
   - `GISWorldBuilder` 的 `CesiumGeoreferenceActor` 引用场景中的 CesiumGeoreference

4. 点击 **Generate In Editor**

### 7.4 期望日志

```
LogGIS: GISWorldBuilder: ===== GenerateAll START (mode=4) =====
LogGIS: GISWorldBuilder: CesiumTiled → initializing TiledFileProvider
LogGIS: TiledFileProvider: Loading manifest → .../tile_manifest.json
LogGIS: CesiumBridge: Loaded DEM cache tile (0,0) → 100x100 grid, 10.0m cell
LogGIS: GISWorldBuilder: CesiumTiled → ready (bridge=Cesium, DEM tiles=4)
LogGIS: PolygonDeriver: Elevation from CesiumBridge DEM cache (33x33)
LogGIS: PolygonDeriver: ===== GenerateFromProvider END → 47 polygons (0.234s) =====
```

### 7.5 没装 Cesium 的替代方案

不装 Cesium for Unreal 也完全可以使用 CesiumTiled 模式：
- 系统自动回退到 SimpleMercator 坐标模式
- DEM 高程缓存仍正常工作（高程查询不依赖 Cesium）
- 只是没有 3D 地球地形渲染，调试线框在 Z=0 平面显示

### 7.6 PCG LOD 距离控制

CesiumBridgeComponent 内置三级 LOD：

| 距离 | 级别 | 采样间隔 | 说明 |
|------|------|----------|------|
| ≤1km | Full | 10m | 全细节 |
| ≤3km | Medium | 30m | 中等密度 |
| ≤7km | Low | 100m | 稀疏 |
| >7km | Culled | — | 不采样 |

与 Cesium 自身的 3D Tiles LOD 联动，避免渲染负担叠加。

---

## 8. PCG 图表集成

### 7.1 添加采样节点

1. 打开 PCG Graph Editor
2. 添加节点：**GIS Land Use Sampler**（类别：Sampler）
3. 配置节点属性：

| 属性 | 说明 | 默认值 |
|------|------|--------|
| `LandUseDataAsset` | 引用生成的 `.uasset` | 必填 |
| `SamplingInterval` | 采样间距（米） | `10.0` |
| `bJitterPoints` | 是否随机偏移采样点 | `true` |
| `JitterAmount` | 偏移量（米） | `2.0` |
| `FilterTypes` | 只采样指定类型（空=全部） | 空 |
| `bEnableTiling` | 启用瓦片感知（大地图必须开启） | `false` |
| `TileSizeM` | 瓦片大小（米），与预处理一致 | `1024.0` |
| `LoadRadius` | 加载半径（瓦片大小倍数） | `2.0` |

### 7.2 输出属性

采样节点为每个 PCG 点输出以下 Metadata 属性：

| 属性名 | 类型 | 说明 |
|--------|------|------|
| `LandUseType` | int32 | 土地类型枚举值 |
| `PolygonID` | int32 | 所属多边形 ID |
| `BuildingDensity` | float | 建筑密度 (0~1) |
| `VegetationDensity` | float | 植被密度 (0~1) |

### 7.3 下游过滤与生成

在 GIS Sampler 之后接 **Attribute Filter** 节点：

```
GIS Land Use Sampler
        │
   Attribute Filter (LandUseType == 0 → Residential)  → Building HISM Spawner
   Attribute Filter (LandUseType == 3 → Forest)        → Tree HISM Spawner
   Attribute Filter (LandUseType == 5 → Water)         → Water Mesh Generator
   Attribute Filter (LandUseType == 6 → Road)          → Road Spline Builder
```

**土地类型枚举值对照**：

| 值 | 类型 | 颜色（调试） |
|----|------|-------------|
| 0 | Residential（住宅） | 黄色 |
| 1 | Commercial（商业） | 红色 |
| 2 | Industrial（工业） | 橙色 |
| 3 | Forest（森林） | 绿色 |
| 4 | Farmland（农田） | 浅绿 |
| 5 | Water（水体） | 蓝色 |
| 6 | Road（道路） | 灰色 |
| 7 | OpenSpace（开放空间） | 浅灰 |
| 8 | Military（军事） | 暗红 |

---

## 9. 调试与日志

### 8.1 日志过滤

所有插件日志统一使用 `LogGIS` 类别。在 UE5 Output Log 中：

1. 打开 **Window → Developer Tools → Output Log**
2. 在过滤框输入 `LogGIS`
3. 即可只看 GIS 管线相关日志

### 8.2 日志级别

| 级别 | 说明 | 开启方式 |
|------|------|----------|
| `Log` | 关键步骤（默认可见） | 默认开启 |
| `Warning` | 异常但可恢复 | 默认开启 |
| `Error` | 错误（需修复） | 默认开启 |
| `Verbose` | 详细调试信息 | 需手动开启 |

开启 Verbose 级别（在控制台输入）：

```
Log LogGIS Verbose
```

恢复默认：

```
Log LogGIS Log
```

### 8.3 关键日志标记

调试时关注以下 START/END 标记对：

```
GISWorldBuilder: ===== GenerateAll START =====
GISWorldBuilder: ===== GenerateAll END (156 polygons, 1.234s) =====

PolygonDeriver: ===== GenerateFromProvider START =====
PolygonDeriver: ===== GenerateFromProvider END → 156 polygons (0.567s) =====

TiledFileProvider: ===== Initialize START =====
TiledFileProvider: ===== Initialize END =====

PCGGISLandUseSampler: ===== Execute START =====
PCGGISLandUseSampler: ===== Execute END =====
```

### 8.4 可视化调试

在 `AGISWorldBuilder` 的 Details 面板中：
- `bDrawDebugPolygons` = true → 显示多边形边界线
- `bSpawnPerPolygonActors` = true → 每个多边形生成独立 Actor（便于检查）
- `DebugDrawDuration` = -1.0 → 永久显示（正数为秒数）

---

## 10. 常见问题

### Q1：点击 Generate 后没有多边形输出？

**检查**：
1. Output Log 中搜索 `LogGIS` 看是否有 Error
2. 确认 `OriginLongitude/OriginLatitude` 设置正确（必须在 GeoJSON 数据范围内）
3. 确认 `GeoJsonPath` 路径正确（相对于 `Content/` 目录）
4. 降低 `MinPolygonArea` 值（默认 100m²，小区域可设为 10）

### Q2：生成了多边形但看不到调试线？

**解决**：
1. 确认 `bDrawDebugPolygons` = true
2. 调整视角，多边形可能在地面以下或很远处
3. 检查 `OriginLongitude/OriginLatitude` 是否偏差过大（应设为数据区域中心）

### Q3：瓦片模式下 Python 管线报错？

**常见原因**：
- `ModuleNotFoundError: osmium` → 安装 osmium: `pip install osmium`
- `ModuleNotFoundError: osgeo` → 安装 GDAL: `pip install gdal`（需系统先安装 GDAL 库）
- 无 PBF 文件 → 管线自动跳过 PBF 步骤，需确保有 GeoJSON 替代输入

### Q4：PCG 采样点太少/太多？

**调整 `SamplingInterval`**：
- 密集区域（城市）：5~10m
- 稀疏区域（森林/农田）：20~50m
- 全局默认：10m

### Q5：大地图帧率低？

**开启瓦片模式**：
1. 在 PCG Sampler 节点中设置 `bEnableTiling` = true
2. 设置 `TileSizeM` = 1024（与预处理一致）
3. 设置 `LoadRadius` = 2.0（只加载周围 2 个瓦片范围内的多边形）

### Q6：坐标偏移过大？

**原因**：浮点精度。UE5 使用 float32，超过 ~70km 距离精度下降。

**解决**：
- 确保 `OriginLongitude/OriginLatitude` 设为地图**正中心**
- 使用瓦片模式，每个瓦片独立定位

### Q7：CesiumTiled 模式编译报错 `CesiumGeoreference.h not found`？

**正常现象**。不装 Cesium for Unreal 时 `WITH_CESIUM=0`，所有 Cesium 头文件引用被条件编译跳过。如果仍报错：
1. 确认 `GISProcedural.Build.cs` 中的 Cesium 检测逻辑正确
2. 清理中间文件：删除 `Intermediate/` 和 `Binaries/` 后重新生成项目文件

### Q8：CesiumTiled 模式下多边形高程为 0？

**原因**：未生成 DEM 高程缓存，或缓存路径配置错误。

**解决**：
1. 运行 `srtm_to_terrain.py` 生成 `dem_cache/` 目录
2. 确认 `DEMCacheDirectory` 路径正确（相对于 Content/）
3. 检查日志中是否有 `CesiumBridge: Loaded DEM cache tile` 信息

---

## 附录：模块清单

| 模块 | 完成度 | 功能 |
|------|--------|------|
| GeoJsonParser | 100% | GeoJSON 解析，OSM 类别推断 |
| DEMParser | 85% | GeoTIFF（简化版）、PNG 高度图、RAW、SRTM HGT |
| TerrainAnalyzer | 100% | 高程网格 → 坡度 → 分类 → 连通分量标记 → 边界多边形 |
| PolygonDeriver | 100% | 地形分区 + 矢量切割（Sutherland-Hodgman 变体） |
| RoadNetworkGraph | 100% | 平面图，交叉检测 + 边分割 |
| LandUseClassifier | 100% | 9 条分类规则 + PCG 参数填充 + LandCover 栅格融合 |
| LocalFileProvider | 100% | 本地 GeoJSON + DEM 读取，带缓存 |
| ArcGISRestProvider | 100% | HTTP 查询、分页、多图层 |
| PCGGISNode | 100% | 网格采样 + 点在多边形测试，瓦片感知 |
| GISWorldBuilder | 100% | 五模式调度器（LocalFile/ArcGIS/DataAsset/TiledFile/CesiumTiled） |
| LandUseMapDataAsset | 100% | 持久化多边形存储 + 网格空间索引 + 异步瓦片加载 |
| GISCoordinate | 100% | 经纬度 ↔ UE5 世界坐标 + WGS84 ↔ UTM + Cesium ECEF 模式 |
| GISPolygonComponent | 100% | Actor 组件，调试可视化 |
| TileManifest | 100% | 瓦片清单 JSON 解析，地理范围查询 |
| LandCoverGrid | 100% | ESA WorldCover 栅格类型 |
| TiledFileProvider | 100% | 瓦片化数据源，LRU 缓存，跨瓦片去重 |
| RasterLandCoverParser | 100% | landcover.json 解析，多数投票区域查询 |
| TiledLandUseMapDataAsset | 100% | 逐瓦片 DataAsset 目录，异步流式加载 |
| TiledWorldBuilder | 100% | 编辑器批量工具：清单 → 逐瓦片 DataAsset |
| CesiumBridgeComponent | 100% | LLH↔UE5 坐标桥接、离线 DEM 高程缓存、PCG LOD 距离控制 |

---

*GISProcedural Plugin v1.0 — MilSim Team*
