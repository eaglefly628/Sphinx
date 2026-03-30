# Pipeline Agent (pipeline) — Shared Notes

## [v1.0.0] Initial State

### 已完成的管线模块

**Python 预处理（10 脚本）**：
- preprocess.py: 主入口，编排全部 6 步
- pbf_to_geojson.py: OSM PBF → GeoJSON（osmium）
- tile_cutter.py: 统一 1km×1km 切块，sphinx_feature_id 去重
- projection.py: WGS84 ↔ UTM 双向转换
- dem_cropper.py: DEM 逐瓦片裁切 PNG16
- raster_classifier.py: ESA WorldCover → landcover.json（多数投票）
- manifest_writer.py: tile_manifest.json 生成
- validate.py: 坐标一致性、文件完整性验证
- srtm_to_terrain.py: SRTM → 二进制高程缓存（40B header + float grid）

**C++ 数据提供者**：
- TiledFileProvider: LRU 缓存（MaxCachedTiles=50），O(1) 网格定位
- TileManifest: JSON 解析、FindTilesInBounds
- GeoJsonParser: 全几何类型、7 种 OSM 类别推断
- RasterLandCoverParser: landcover.json → 多数投票查询
- LocalFileProvider / ArcGISRestProvider: 完整实现

### 测试覆盖

- test_pipeline.py T1-T7: UTM 精度、瓦片切割去重、manifest 验证、LandCover 投票、空间索引、端到端
- test_cesium_bridge.py T1-T7: DEM 缓存格式、双线性插值、Mercator 往返、PCG LOD、高程精度

### 待实现功能

- bridge/tunnel OSM 标签在 GeoJsonParser 中的提取（给 algo 用）
- srtm_to_terrain.py 支持 GeoTIFF 输入（当前仅 HGT）

## TODO (from lead review / peer review)

（暂无）

## Changelog

### [v1.0.0] — pipeline
- 初始状态记录
