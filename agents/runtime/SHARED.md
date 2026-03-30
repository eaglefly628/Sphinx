# Runtime Agent (runtime) — Shared Notes

## [v1.0.0] Initial State

### 已完成的运行时模块

- **GISWorldBuilder**: 五模式调度完整，编辑器 GenerateInEditor/GenerateAndSaveDataAsset 按钮
- **TiledWorldBuilder**: 批量生成 + 单瓦片测试，进度日志
- **CesiumBridgeComponent**: ECEF 坐标桥接 + 离线 DEM 缓存 + PCG LOD 三级
- **GISCoordinate**: SimpleMercator/UTM/Cesium 三模式，Haversine 距离，UTM 双向转换
- **PCGGISNode**: 网格采样 + 点在多边形，输出 LandUseType/PolygonID/BuildingDensity/VegetationDensity
- **LandUseMapDataAsset**: 持久化 + 500m 网格空间索引 + FStreamableManager 异步加载

### 当前 API 接口

```cpp
// GISWorldBuilder 入口
UFUNCTION(CallInEditor) void GenerateInEditor();
UFUNCTION(CallInEditor) void GenerateAndSaveDataAsset();

// CesiumBridge 高程查询
float GetElevationAtLocation(double Lon, double Lat) const;
EPCGDetailLevel GetDetailLevelForDistance(float DistanceM) const;
```

### 待实现功能

- 道路 Spline Mesh 生成（需要 algo 侧的道路结构数据）
- 水体 Mesh 生成（需要 waterway polygon 数据）
- 与 uds 天气系统集成（天气影响 PCG 密度？雾/雨效果联动？）

## TODO (from lead review / peer review)

（暂无）

## Changelog

### [v1.0.0] — runtime
- 初始状态记录
