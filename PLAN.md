# Sphinx 全球 GIS 程序化生成 — 协作重构方案

## 目标

将 GISProcedural 插件从当前的单体小区域（~10km²）架构，扩展为支持全球级别（任意区域）的 Tile 流式加载架构，用于军事仿真运行时。

## 当前架构瓶颈

| 问题 | 现状 | 影响 |
|------|------|------|
| 精度 | `GISCoordinate` 使用简化 Mercator，FVector(float32) | 100km 距离误差 ±0.5cm~1m |
| 内存 | `LandUseMapDataAsset` 单一 TArray 存所有 Polygon | 上海级别 10 万+ Polygon 内存爆炸 |
| 查询 | `GetPolygonsInWorldBounds` O(n) 线性扫描 | 运行时卡顿 |
| 数据源 | 只支持 GeoJSON，不支持 PBF/栅格 LandCover | 无法处理全球数据 |
| 加载 | 同步一次性加载，无流式 | 无法与 World Partition 配合 |

## 核心设计决策

1. **Tile 系统**：1km × 1km 分块，每块一个 DataAsset
2. **预处理在 UE 外部**：Python/GDAL 管线（不把 GDAL 编进 UE）
3. **坐标策略**：UE 世界原点设在地图中心，最大偏移 ~70km = 7,000,000cm → float32 精度 ~0.5cm（可接受）；同时加 UTM 投影模式备用
4. **GeoJSON 不变**：Python 切块后仍输出 WGS84 GeoJSON，UE 侧做投影
5. **向后兼容**：现有 LocalFile/ArcGIS 小区域工作流完全保留

---

## 分阶段实施计划

### Phase 0：接口契约 + 共享类型定义（第 1 周，双方共同）

**目标**：定义数据格式和接口，双方各自开发前达成一致。

#### 产出物

**1. Tile 目录规范**
```
GISData/Shanghai/
  tile_manifest.json          ← UTiledFileProvider 读取入口
  tiles/
    tile_0_0/
      osm.geojson             ← 标准 GeoJSON, WGS84 坐标
      dem.png                 ← 16bit 灰度高度图（已支持）
      landcover.json          ← { width, height, cell_size_m, classes[] }
    tile_0_1/
    ...
```

**2. tile_manifest.json Schema**
```json
{
  "projection": "UTM51N",
  "epsg": 32651,
  "origin_lon": 121.47,
  "origin_lat": 31.23,
  "tile_size_m": 1000,
  "tiles": [
    {
      "x": 0, "y": 0,
      "geojson": "tiles/tile_0_0/osm.geojson",
      "dem": "tiles/tile_0_0/dem.png",
      "landcover": "tiles/tile_0_0/landcover.json",
      "bounds_geo": { "min_lon": 121.47, "min_lat": 31.23, "max_lon": 121.48, "max_lat": 31.24 },
      "polygon_count": 247
    }
  ]
}
```

**3. 接口扩展（IGISDataProvider.h）**
```cpp
// 新增可选方法（默认返回 false，向后兼容）
virtual bool QueryLandCover(
    const FGeoRect& Bounds,
    TArray<uint8>& OutClassGrid,
    int32& OutWidth, int32& OutHeight,
    float& OutCellSizeM)
{ return false; }
```

**4. LandUsePolygon 新增字段**
```cpp
UPROPERTY(BlueprintReadWrite) FIntPoint TileCoord = FIntPoint(0, 0);
UPROPERTY(BlueprintReadWrite) FBox WorldBounds;  // 缓存的 AABB
```

**5. GISCoordinate 投影模式枚举**
```cpp
UENUM(BlueprintType)
enum class EProjectionType : uint8 { SimpleMercator, UTM };
```

#### 验证检查点
- 双方各自能编译通过新的头文件
- 坐标往返测试：lon/lat → UTM → tile-local FVector → UTM → lon/lat，误差 < 1mm

---

### Phase 1：数据预处理 + 坐标升级（第 2-4 周，并行）

#### 小城负责：Python 预处理管线

**全部为新代码，在 `Tools/GISPreprocess/` 目录，零合并冲突**

| 文件 | 用途 |
|------|------|
| `preprocess.py` | 主入口脚本 |
| `pbf_to_geojson.py` | PBF → 按 tile 切分的 GeoJSON（osmium） |
| `tile_cutter.py` | 矢量/DEM/栅格统一 1km×1km 切块（shapely/rasterio） |
| `projection.py` | WGS84 ↔ UTM 转换（pyproj） |
| `raster_classifier.py` | ESA WorldCover .tif → per-tile landcover.json |
| `dem_cropper.py` | DEM per-tile 裁切输出 PNG |
| `manifest_writer.py` | 生成 tile_manifest.json |
| `validate.py` | 校验所有 tile：坐标范围、要素数量、文件完整性 |
| `requirements.txt` | Python 依赖 |
| `Dockerfile` | 可选，避免依赖地狱 |

**关键决策**：
- 输出 GeoJSON 保持 WGS84 坐标（不做 UTM 转换），投影由 UE 侧处理
- 跨 tile 边界的要素添加 `"clipped": true` 属性 + 全局 feature ID 用于去重
- DEM 输出 PNG 高度图（现有 DEMParser 已支持）
- LandCover 输出简单 JSON 网格，无需 GDAL

#### 鹰飞负责：GISCoordinate UTM 支持

**修改现有文件，不影响小城的新文件**

| 文件 | 改动 |
|------|------|
| `GISCoordinate.h` | 加 `EProjectionType` 枚举、UTM 配置字段、`SetUTMOrigin()` 方法 |
| `GISCoordinate.cpp` | 实现 UTM 正/逆投影（Karney 2011 级数展开，~50 行 C++，无外部依赖） |

现有 `SetOrigin(lon, lat)` 保持不变，默认走 SimpleMercator。

#### Phase 1 验证
- 小城：产出 5×5 tile 测试集（上海浦东 5km²），鹰飞用现有 `LocalFileProvider` 手动加载单个 tile 验证 GeoJSON 兼容性
- 鹰飞：UTM 坐标测试——东方明珠塔坐标 (121.4956E, 31.2397N) 通过 UTM 51N 转换后与 PROJ 参考值对比

---

### Phase 2：Tile 数据提供者 + 空间索引（第 5-7 周，并行）

#### 小城负责：新建 Tile 数据提供者（全部新文件）

| 新文件 | 用途 |
|------|------|
| `Public/Data/TiledFileProvider.h` | Tile 化数据提供者头文件 |
| `Private/Data/TiledFileProvider.cpp` | 实现：读 manifest、LRU 缓存 tile、QueryFeatures 按 bounds 找 tile |
| `Public/Data/TileManifest.h` | Manifest 解析结构体 |
| `Private/Data/TileManifest.cpp` | JSON 解析实现 |
| `Public/Data/RasterLandCoverParser.h` | ESA WorldCover JSON 网格解析 |
| `Private/Data/RasterLandCoverParser.cpp` | 实现：采样、多数类统计 |

**核心类设计**：
```cpp
UCLASS(BlueprintType)
class UTiledFileProvider : public UObject, public IGISDataProvider
{
    UPROPERTY() FString ManifestPath;

    // IGISDataProvider
    virtual bool QueryFeatures(const FGeoRect& Bounds, TArray<FGISFeature>& Out) override;
    virtual bool QueryElevation(...) override;
    virtual bool QueryLandCover(...) override;

private:
    TMap<FIntPoint, int32> TileGrid;        // (col,row) → O(1) 查找
    TMap<FString, TSharedPtr<FTileData>> Cache;  // LRU 缓存
    int32 MaxCachedTiles = 50;
};
```

关键：`QueryFeatures` 内部用 `TileGrid` O(1) 定位 tile，加载 GeoJSON 复用现有 `UGeoJsonParser`，缓存 tile 避免重复读盘。对 `PolygonDeriver` 完全透明——它只看到 `IGISDataProvider` 接口。

#### 鹰飞负责：WorldBuilder 扩展 + 空间索引

| 文件 | 改动 |
|------|------|
| `GISWorldBuilder.h` | 加 `EGISDataSourceType::TiledFile`，加 ManifestPath 属性 |
| `GISWorldBuilder.cpp` | `CreateDataProvider()` 新增 TiledFile case，按 tile 迭代生成 |
| `LandUseMapDataAsset.h` | 加 `TMap<FIntPoint, TArray<int32>> SpatialGrid`（500m 网格索引） |
| `LandUseMapDataAsset.cpp` | `BuildSpatialIndex()` + 重写 `GetPolygonsInWorldBounds()` 用索引查询 |
| `LandUseClassifier.h/.cpp` | `ClassifySingle` 加可选 LandCover 输入，ESA 类码映射规则 |

#### Phase 2 验证
- 加载 5×5 tile 数据集，`QueryFeatures` 跨 2 个 tile 查询返回正确要素
- 空间索引基准测试：10 万 polygon，`GetPolygonsInWorldBounds` < 1ms
- 完整管线：TiledFileProvider → PolygonDeriver → DataAsset → PCG，视觉验证

---

### Phase 3：World Partition 集成 + 大规模验证（第 8-10 周，并行）

#### 小城负责：World Partition 流式加载

| 新文件 | 用途 |
|------|------|
| `Public/Data/TiledLandUseMapDataAsset.h` | Tiled DataAsset 清单（软引用指向每个 tile 的 DataAsset）|
| `Private/Data/TiledLandUseMapDataAsset.cpp` | 按 tile 加载/卸载，异步加载支持 |
| `Public/Runtime/TiledWorldBuilder.h` | 编辑器批量生成：遍历所有 tile → 逐 tile 生成 DataAsset |
| `Private/Runtime/TiledWorldBuilder.cpp` | 实现 |

**核心设计**：
```cpp
UCLASS(BlueprintType)
class UTiledLandUseMapDataAsset : public UDataAsset
{
    // 每个 tile 的 DataAsset 作为软引用，按需加载
    UPROPERTY()
    TMap<FString, TSoftObjectPtr<ULandUseMapDataAsset>> TileAssets;

    // 全局元数据
    UPROPERTY() float TileSizeM = 1000.0f;
    UPROPERTY() double OriginLongitude, OriginLatitude;

    ULandUseMapDataAsset* LoadTile(const FString& TileID);
    void UnloadTile(const FString& TileID);
    TArray<FString> GetTileIDsInWorldBounds(const FBox& Bounds) const;
};
```

#### 鹰飞负责：PCG Tile 感知 + 性能优化

| 文件 | 改动 |
|------|------|
| `PCGGISNode.h` | 加 `TSoftObjectPtr<UTiledLandUseMapDataAsset> TiledDataAsset` |
| `PCGGISNode.cpp` | `ExecuteInternal` 加 tile 感知路径：检测 WP cell 范围 → 只加载对应 tile |
| `LandUseMapDataAsset.cpp` | 加异步 tile 加载（`FStreamableManager`） |

#### Phase 3 验证
- 10km × 10km（100 tiles）大规模测试，内存 < 2GB
- World Partition 飞行摄像机跨 tile 边界无卡顿
- PCG 生成延迟 < 100ms/partition cell
- 与卫星图对比上海标志性地标（东方明珠、外滩）

---

## 模块所有权一览

| 文件/模块 | 负责人 | 操作 | 阶段 |
|-----------|--------|------|------|
| `Tools/GISPreprocess/*` | **小城** | 新建 | P1 |
| `TiledFileProvider.h/.cpp` | **小城** | 新建 | P2 |
| `TileManifest.h/.cpp` | **小城** | 新建 | P2 |
| `RasterLandCoverParser.h/.cpp` | **小城** | 新建 | P2 |
| `TiledLandUseMapDataAsset.h/.cpp` | **小城** | 新建 | P3 |
| `TiledWorldBuilder.h/.cpp` | **小城** | 新建 | P3 |
| `GISCoordinate.h/.cpp` | **鹰飞** | 修改 | P1 |
| `IGISDataProvider.h` | **双方** | 修改 | P0 |
| `LandUsePolygon.h` | **鹰飞** | 修改 | P0 |
| `GISWorldBuilder.h/.cpp` | **鹰飞** | 修改 | P2 |
| `LandUseMapDataAsset.h/.cpp` | **鹰飞** | 修改 | P2+P3 |
| `LandUseClassifier.h/.cpp` | **鹰飞** | 修改 | P2 |
| `PCGGISNode.h/.cpp` | **鹰飞** | 修改 | P3 |
| 其他现有文件 | 无变动 | — | — |

**分工原则：小城只创建新文件，鹰飞只修改现有文件 → 零合并冲突**

---

## 风险与对策

| 风险 | 对策 |
|------|------|
| Tile 边界要素重复 | Python 分配全局 feature ID，UE 侧按 ID 去重 |
| Python 依赖地狱（GDAL/osmium） | 提供 Dockerfile + requirements.txt |
| ESA WorldCover 与 OSM 分类冲突 | OSM 明确标签优先，无标签时退回 WorldCover 栅格 |
| UE 内嵌 SQLite 复杂度 | 不用 SQLite，用 JSON manifest + TMap 内存索引替代 |
| float32 精度 | UE5 默认开启 LWC（FVector 已是 double）；即使关闭，70km 范围内精度 ~0.5cm |

---

## 协作检查点

| 编号 | 时间 | 检查内容 |
|------|------|---------|
| C0 | P0 完成 | 共享头文件双方编译通过 |
| C1 | P1 中期 | 小城发一个 tile 给鹰飞手动加载验证 |
| C2 | P1 完成 | 5×5 tile 测试集 + UTM 坐标对比通过 |
| C3 | P2 完成 | TiledFileProvider 加载 25 tile，相邻 tile 多边形无缝对齐 |
| C4 | P3 完成 | World Partition 流式加载 100 tile，PCG 跨 tile 无缝 |
| C5 | 最终 | 全上海（~6000km²）端到端通过 |
