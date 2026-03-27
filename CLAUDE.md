# Sphinx — GIS Procedural Generation for UE5

军事仿真用全球 GIS 程序化世界生成系统。UE5 C++ 插件 + Python 预处理管线。

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

| 文件 | 职责 |
|------|------|
| `GISWorldBuilder.h/.cpp` | 五模式入口，编辑器生成按钮 |
| `PolygonDeriver.h/.cpp` | 核心算法：DEM→地形→矢量切割→分类→PCG参数 |
| `GISCoordinate.h/.cpp` | 坐标转换（SimpleMercator/UTM/Cesium 三种模式） |
| `CesiumBridgeComponent.h/.cpp` | Cesium 桥接、离线 DEM 缓存、PCG LOD |
| `TiledFileProvider.h/.cpp` | 瓦片数据源、LRU 缓存、manifest 读取 |
| `LandUseClassifier.h/.cpp` | 9 规则分类 + ESA WorldCover 融合 |
| `IGISDataProvider.h` | 数据源抽象接口（QueryFeatures/Elevation/LandCover） |
| `preprocess.py` | Python 预处理主入口 |
| `srtm_to_terrain.py` | SRTM → 二进制 DEM 高程缓存 |

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

## 协作

- **鹰飞**: 修改现有 C++ 文件（WorldBuilder, Coordinate, Classifier, PCG）
- **小城**: 新建文件（TiledFileProvider, TileManifest, Python 工具）
- 分工原则: 小城只创建新文件，鹰飞只修改现有文件 → 零合并冲突
- 主分支: master
- 开发分支命名: claude/<feature>-<id>

## 当前状态

Phase 0-4 全部完成 ✅
- ✅ 五种数据源模式
- ✅ 瓦片化流式加载 + World Partition
- ✅ 空间索引 O(k) 查询
- ✅ UTM + SimpleMercator + Cesium 坐标模式
- ✅ Cesium 集成（软依赖, 离线 DEM 缓存）
- ✅ PCG 集成（自定义采样节点, 瓦片感知）
- ✅ Python 预处理管线（10 脚本）
- ✅ 71+ 自动化测试

待讨论/实现:
- 立交桥/涵洞 PCG 生成（需 bridge/tunnel 标签提取 + LayerIndex 分离）
- 道路 Spline Mesh 生成
- 水体 Mesh 生成
