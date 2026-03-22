# CesiumTiled 模式 — 从空项目开始测试指南

## 前置条件

| 工具 | 版本 | 用途 |
|------|------|------|
| Unreal Engine | 5.3+ | 引擎 |
| Cesium for Unreal | 2.x | 地球地形渲染（可选，不装走 Mercator 回退） |
| Python | 3.10+ | 数据预处理 |
| numpy | 任意 | DEM 处理 |

---

## 第一步：准备 GIS 数据（Python 端，~10 分钟）

### 1.1 获取测试数据

```bash
# 下载上海区域 SRTM DEM（1-arc-second）
# 从 https://earthexplorer.usgs.gov/ 下载 N31E121.hgt
# 或用已有测试数据
```

### 1.2 下载 OSM 数据

```bash
# 从 overpass-turbo.eu 导出上海浦东 2km×2km 的 GeoJSON
# 或使用仓库自带测试数据：Tests/MockData/Region_Shanghai/
```

### 1.3 运行预处理管线

```bash
cd Tools/GISPreprocess/

# 安装依赖
pip install -r requirements.txt
pip install numpy

# (a) 切分 GeoJSON 为 tiles + 生成 tile_manifest.json
python preprocess.py \
  --input ../../Tests/MockData/Region_Shanghai/osm_data.geojson \
  --output ../../Tests/MockData/Region_Shanghai/ \
  --tile-size 1000 \
  --origin-lon 121.47 --origin-lat 31.23

# (b) 生成 DEM 高程缓存（如果有 SRTM .hgt 文件）
python srtm_to_terrain.py \
  /path/to/N31E121.hgt \
  -o ../../Tests/MockData/Region_Shanghai/ \
  --tile-size 1000 \
  --grid-size 100

# 如果没有真实 DEM，跳过此步骤（系统会降级到无地形模式）
```

### 1.4 验证产出

```
Tests/MockData/Region_Shanghai/
├── tile_manifest.json              ← UE 读取入口
├── tiles/
│   ├── tile_0_0/osm.geojson
│   ├── tile_0_1/osm.geojson
│   └── ...
└── dem_cache/                      ← 离线高程缓存（可选）
    ├── elevation_0_0.bin
    ├── elevation_0_1.bin
    └── dem_cache_index.json
```

---

## 第二步：创建 UE5 项目（~5 分钟）

### 2.1 新建空项目

1. 打开 Unreal Engine → New Project → **Blank** (C++)
2. 项目名：`SphinxTest`
3. 目标平台：Desktop

### 2.2 安装插件

```bash
# 方式 A：符号链接（推荐开发时用）
ln -s /path/to/Sphinx/Plugins/GISProcedural SphinxTest/Plugins/GISProcedural

# 方式 B：复制
cp -r /path/to/Sphinx/Plugins/GISProcedural SphinxTest/Plugins/
```

### 2.3 安装 Cesium for Unreal（可选）

- Epic Games Launcher → Marketplace → 搜索 "Cesium for Unreal" → 安装
- 或从 GitHub releases 手动安装到 `Plugins/CesiumForUnreal/`
- **不装也可以**：系统自动走 Mercator 回退模式，WITH_CESIUM=0

### 2.4 复制测试数据

```bash
mkdir -p SphinxTest/Content/GISData/Region_01/

# 复制 tile_manifest + tiles
cp -r Tests/MockData/Region_Shanghai/tiles SphinxTest/Content/GISData/Region_01/
cp Tests/MockData/Region_Shanghai/tile_manifest.json SphinxTest/Content/GISData/Region_01/

# 复制 DEM 缓存（如果有）
cp -r Tests/MockData/Region_Shanghai/dem_cache SphinxTest/Content/GISData/Region_01/
```

### 2.5 重新生成项目文件 & 编译

```bash
cd SphinxTest/
# Windows
GenerateProjectFiles.bat && MSBuild SphinxTest.sln /p:Configuration=Development

# Mac/Linux
./GenerateProjectFiles.sh && make
```

---

## 第三步：配置场景（~5 分钟）

### 3.1 放置 GISWorldBuilder

1. 打开编辑器
2. Place Actors → 搜索 `GISWorldBuilder` → 拖入场景
3. 在 Details 面板配置：

```
GIS|DataSource:
  Data Source Type = "Cesium + Tiled"     ← 选 CesiumTiled

GIS|DataSource|TiledFile:
  Tile Manifest Path = "GISData/Region_01/tile_manifest.json"

GIS|DataSource|Cesium:
  Cesium Georeference Actor = (选择场景中的 CesiumGeoreference)
  DEM Cache Directory = "GISData/Region_01/dem_cache/"

GIS|Input:
  Origin Longitude = 121.47
  Origin Latitude = 31.23

GIS|Debug:
  Draw Debug Polygons = true ✓
```

### 3.2 放置 Cesium 地形（如果装了 Cesium 插件）

1. Place Actors → `CesiumGeoreference` → 拖入场景
2. 设置 Origin:
   - Longitude = 121.47
   - Latitude = 31.23
   - Height = 0
3. Place Actors → `Cesium3DTileset` → 拖入场景
   - 选择 "Cesium World Terrain" 或自托管 URL
4. Place Actors → `CesiumSunSky` → 拖入场景（光照）

### 3.3 没装 Cesium 的替代方案

不装 Cesium 也能工作，只是没有 3D 地球地形渲染：

1. GISWorldBuilder 的 `CesiumGeoreferenceActor` 留空
2. 系统自动回退到 SimpleMercator 坐标模式
3. Debug 线框仍会正常显示

---

## 第四步：运行 & 验证（~2 分钟）

### 4.1 编辑器内生成

1. 选中 GISWorldBuilder Actor
2. 点击 Details 面板中的 **"Generate In Editor"** 按钮
3. 查看 Output Log（过滤 `LogGIS`）

### 4.2 期望看到的日志

```
LogGIS: GISWorldBuilder: ===== GenerateAll START (mode=4) =====
LogGIS: GISWorldBuilder: CesiumTiled → initializing TiledFileProvider
LogGIS: TiledFileProvider: Loading manifest → .../tile_manifest.json
LogGIS: CesiumBridge: Loaded DEM cache tile (0,0) → 100x100 grid, 10.0m cell
LogGIS: GISWorldBuilder: CesiumTiled → ready (bridge=Cesium, DEM tiles=4)
LogGIS: PolygonDeriver: Elevation from CesiumBridge DEM cache (33x33)
LogGIS: PolygonDeriver: ===== GenerateFromProvider END → 47 polygons (0.234s) =====
LogGIS: GISWorldBuilder: Spawned 47 polygon actors
LogGIS: ========== GIS World Builder Statistics ==========
```

### 4.3 期望的视觉结果

- **有 Cesium**：3D 地球地形上叠加彩色多边形线框（黄=住宅、红=商业、绿=森林）
- **无 Cesium**：平面上的彩色多边形线框（Z=0 平面）

### 4.4 验证清单

| 检查项 | 预期 |
|--------|------|
| LogGIS 无 Error 级别日志 | 是 |
| 生成 polygon 数 > 0 | 是 |
| Debug 线框可见 | 是 |
| 场景中有 `GIS_Poly_*` 子 Actor | 是 |
| 内存占用 < 500MB | 是 |
| 生成耗时 < 2 秒（小区域） | 是 |

---

## 第五步：PCG 集成（可选，~10 分钟）

1. 创建 PCG Graph
2. 添加 `PCG GIS Node` → 关联 `GISWorldBuilder` 的 `LandUseDataAsset`
3. 添加 Mesh Spawner → 选择建筑/树木 mesh
4. 运行 PCG → 在多边形内生成程序化内容

---

## 故障排除

| 问题 | 解决方案 |
|------|---------|
| `LogGIS: Error: ManifestPath is empty` | 检查 TileManifestPath 路径是否正确 |
| `LogGIS: Error: Failed to load manifest` | 确认 tile_manifest.json 在 Content/ 下 |
| `CesiumTiled → TiledFileProvider init failed` | 检查 manifest JSON 格式 |
| 没有 Debug 线框 | 确认 `bDrawDebugPolygons = true` |
| 编译报 `CesiumGeoreference.h not found` | 正常，不装 Cesium 时 WITH_CESIUM=0 跳过 |
| polygon 数 = 0 | 检查 GeoJSON 文件是否有面域要素 |

---

## 数据源模式对比

| 模式 | 地形 | 矢量 | 坐标 | 适用场景 |
|------|------|------|------|---------|
| LocalFile | 本地 DEM | 本地 GeoJSON | Mercator | 小区域 <10km² |
| TiledFile | 本地 DEM tiles | 本地 tile GeoJSON | Mercator | 大区域 ~100km² |
| **CesiumTiled** | **离线 DEM 缓存** | **本地 tile GeoJSON** | **Cesium ECEF** | **全球级别** |
| ArcGISRest | 无 | HTTP REST | Mercator | 在线数据源 |
| DataAsset | N/A | 预生成 | 已转换 | 运行时加载 |
