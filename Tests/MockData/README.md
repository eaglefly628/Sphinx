# MockData - 离线测试数据

模拟上海陆家嘴区域的 GIS 数据，供 GISProcedural 插件离线调试使用。

## 目录结构

```
Region_Shanghai/
├── osm_data.geojson          ← 完整区域 GeoJSON (LocalFile 模式用)
├── DEM/
│   └── dem_tile.raw          ← 模拟 DEM 高程数据 (64x64, 16bit)
├── tiles/
│   ├── tile_0_0/
│   │   ├── osm.geojson       ← 瓦片 GeoJSON
│   │   └── landcover.json    ← ESA WorldCover 栅格
│   ├── tile_0_1/
│   ├── tile_1_0/
│   └── tile_1_1/
└── tile_manifest.json        ← 瓦片清单 (TiledFile 模式用)
```

## 使用方法

### Mode A: LocalFile
```
DataSourceType = LocalFile
GeoJsonPath = <ProjectDir>/Tests/MockData/Region_Shanghai/osm_data.geojson
DEMPath = <ProjectDir>/Tests/MockData/Region_Shanghai/DEM/
OriginLongitude = 121.495
OriginLatitude = 31.240
```

### Mode D: TiledFile
```
DataSourceType = TiledFile
TileManifestPath = <ProjectDir>/Tests/MockData/Region_Shanghai/tile_manifest.json
```

## 数据说明

- 坐标范围: 经度 121.49~121.50, 纬度 31.235~31.245 (陆家嘴约 1km²)
- 包含: 建筑 (6栋), 道路 (4条), 水体 (黄浦江), 绿地 (2块), 农田 (1块)
- DEM: 64x64 网格, 海拔 2~15m (模拟平坦河口地形)
- LandCover: 10x10 网格, ESA WorldCover 编码
