# Algorithm Agent (algo) — Shared Notes

## [v1.0.0] Initial State

### 已完成的算法模块

- **PolygonDeriver**: 6 步管线完整实现，支持 DataProvider 模式和 Legacy 模式
- **TerrainAnalyzer**: 5 步流水线（高程网格→坡度→分类→CCL→边界），Horn 3x3 Sobel
- **LandUseClassifier**: 9 规则分类 + ESA WorldCover 融合，PCG 参数自动填充
- **RoadNetworkGraph**: O(n²) 交叉检测 + 边分割 + GetLeftmostTurn 面提取
- **DEMParser**: PNG/RAW/HGT 完整支持，GeoTIFF 简化版

### 当前 API 接口

```cpp
// PolygonDeriver 主入口
void GenerateFromProvider(IGISDataProvider* Provider, UGISCoordinate* Coord,
                          TArray<FLandUsePolygon>& OutPolygons);

// LandUseClassifier 单多边形分类
ELandUseType ClassifySingle(const FLandUsePolygon& Poly,
                            const TArray<FGISFeature>& NearbyFeatures,
                            const FLandCoverGrid* LandCover = nullptr);
```

### 待实现功能

- bridge/tunnel 标签提取 → EStructureLayer 枚举
- RoadNetworkGraph LayerIndex 分离（立交桥不交叉分割）
- 道路逐顶点高程（当前只有 AvgElevation）

## TODO (from lead review / peer review)

（暂无）

## Changelog

### [v1.0.0] — algo
- 初始状态记录
