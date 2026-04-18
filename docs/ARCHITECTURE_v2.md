# 语义化数字地球 — 架构演进 v1.0 → v2.0

本文档定义从 Sphinx v1.0（GIS 程序化生成）到 v2.0（语义化数字地球）的架构演进路径。
所有 agent 在开始 v2.0 相关开发前必须阅读此文档。

---

## 1. 愿景对比

| 维度 | v1.0 Sphinx | v2.0 语义化数字地球 |
|------|------------|-------------------|
| 定位 | 军事仿真用 GIS 程序化世界生成 | 全球语义化数字孪生系统 |
| 数据来源 | GeoJSON + DEM（本地/ArcGIS） | GeoParquet + 3D Tiles + DEM |
| 几何来源 | DEM → PolygonDeriver 推导多边形 | OSM/研究机构公开数据 → Py3DTiles 切片 |
| 渲染方式 | UE Landscape + PCG 散布 | Cesium 3D Tiles + PCG 装饰（HISM） |
| 语义查询 | 无 | 射线 → Feature ID → DuckDB → JSON UI |
| 尺度 | 小区域 ~10km² / 城市级 ~100km² | 全球级，太空到街道无缝 |
| 交互 | 静态场景 | 单体化可点击、载具漫游、动态轨迹 |

---

## 2. 技术栈对比

### 2.1 数据存储

| | v1.0 | v2.0 |
|--|------|------|
| 属性存储 | GeoJSON properties / UDataAsset | **GeoParquet**（列式，云原生） |
| 几何存储 | GeoJSON geometry / DEM PNG | **3D Tiles**（.glb + EXT_mesh_features） |
| 查询引擎 | TMap 内存索引 O(1) | **DuckDB** + Spatial Extension |
| 元数据链接 | 嵌入 FLandUsePolygon | **Feature ID 外键** → GeoParquet 行映射 |

### 2.2 预处理管线

| | v1.0 | v2.0 |
|--|------|------|
| 输入 | OSM PBF / GeoJSON / WorldCover TIF | GeoParquet（公开研究数据集）+ OSM |
| 工具 | shapely / rasterio / pyproj | **GeoPandas + Py3DTiles** + DuckDB |
| 输出 | WGS84 GeoJSON + DEM PNG + manifest | **3D Tiles**（几何+Feature ID）+ GeoParquet（属性） |
| 索引 | tile_manifest.json | 3D Tiles tileset.json（隐式空间索引） |

### 2.3 前端渲染

| | v1.0 | v2.0 |
|--|------|------|
| 地形 | UE Landscape（DEM 驱动） | **Cesium World Terrain** + 本地 DEM |
| 建筑 | PCG 散布 StaticMesh | **3D Tiles 白模** + World Aligned Texture 程序化材质 |
| 植被 | PCG → StaticMesh 实例 | **Niagara + HISM** GPU 实例化 |
| 道路 | 未实现 | 程序化 Shader / Spline Mesh |
| 水体 | 未实现 | 程序化动态 Shader |
| 坐标系 | SimpleMercator / UTM | **ACesiumGeoreference** WGS84 椭球体 |

### 2.4 交互系统

| | v1.0 | v2.0 |
|--|------|------|
| 拾取 | 无 | **射线 → GetPrimitiveFeatures → Feature ID** |
| 查询 | 无 | Feature ID → Web API → DuckDB → JSON |
| UI | 无 | UMG 面板动态弹出结构化信息 |
| 漫游 | 无 | UCesiumFlyToComponent + GlobeAwareDefaultPawn |
| 载具 | 无 | 椭球体重力自适应、贴地行驶 |

---

## 3. 可复用模块分析

### 3.1 直接复用（无需改动或少量适配）

| 模块 | 文件 | 复用方式 |
|------|------|---------|
| Cesium 桥接 | `CesiumBridgeComponent.h/.cpp` | 坐标转换、DEM 缓存逻辑直接复用 |
| 坐标转换 | `GISCoordinate.h/.cpp` | UTM/Mercator/Cesium 三模式保留 |
| DEM 解析 | `DEMParser.h/.cpp` | 本地 DEM 解析不变 |
| 地形分析 | `TerrainAnalyzer.h/.cpp` | 坡度/分类算法可用于地形感知 PCG |
| Python 框架 | `Tools/GISPreprocess/` | projection.py、validate.py 可复用 |
| 测试框架 | `Tests/` | 测试结构和 MockData 模式可复用 |

### 3.2 需要扩展（保留核心，增加新路径）

| 模块 | 文件 | 扩展内容 |
|------|------|---------|
| PCG 节点 | `PCGGISNode.h/.cpp` | 新增从 3D Tiles 元数据采样路径（替代 DataAsset 查询） |
| 世界构建器 | `GISWorldBuilder.h/.cpp` | 新增 Cesium 3D Tiles 主渲染模式 |
| 数据提供者接口 | `IGISDataProvider.h` | 可能新增 GeoParquet 查询方法 |
| 分类器 | `LandUseClassifier.h/.cpp` | 适配 GeoParquet 分类字段 |

### 3.3 v2.0 全新模块

| 模块 | 说明 | Owner |
|------|------|-------|
| GeoParquet 管线 | GeoPandas 提取几何 → Py3DTiles 切片 → Feature ID 注入 | pipeline |
| DuckDB 集成 | UE 内嵌或 Web API 查询 GeoParquet | 新模块 |
| Feature ID 拾取 | 射线 → GetPrimitiveFeatures → ID 提取 | runtime |
| 信息面板 UI | UMG 动态面板渲染 DuckDB 返回的 JSON | runtime |
| 子关卡裁剪 | LevelInstance + CesiumPolygonRasterOverlay | runtime |
| 程序化材质 | World Aligned Texture 建筑/道路/水体 Shader | runtime |
| Niagara 植被 | GPU 实例化海量树木草皮 | runtime |
| 载具系统 | GlobeAwareDefaultPawn + 椭球体重力 | runtime |
| Web API | Node.js/Python 中间层，DuckDB 查询服务 | pipeline |

---

## 4. 数据流对比

### v1.0 数据流
```
OSM PBF/GeoJSON
  → Python 预处理（tile_cutter → manifest_writer）
    → UE TiledFileProvider → GeoJsonParser
      → PolygonDeriver → LandUseClassifier
        → LandUseMapDataAsset
          → PCGGISNode → StaticMesh 实例
```

### v2.0 数据流
```
GeoParquet（公开数据集 / OSM 提取）
  → Python 管线:
      ├── GeoPandas 提取几何 + 高度
      │     → Py3DTiles 切片（.glb + Feature ID）
      │         → 3D Tiles tileset.json
      └── 属性保留在 GeoParquet（Feature ID 为外键）
  → UE 前端:
      ├── ACesium3DTileset 加载 3D Tiles → 渲染建筑白模
      ├── 程序化材质 → World Aligned Texture → 建筑表面
      ├── Niagara + HISM → 绿地范围动态生成植被
      ├── 程序化 Shader → 水体/道路
      └── 射线拾取 → Feature ID → DuckDB → UMG 面板
```

---

## 5. 关键技术决策

### 5.1 视算分离
3D Tiles 只携带几何 + Feature ID（整型外键），不嵌入文本属性。
属性查询走 DuckDB，前端按需拉取，不预加载。
**好处**: 3D Tiles 轻量、渲染快；属性更新不需要重新切片。

### 5.2 Cesium 为主渲染引擎
v1.0 的 UE Landscape + PCG 路径保留但降为辅助。
v2.0 主路径：Cesium 3D Tiles（地形+建筑）+ PCG 装饰（植被/细节）。

### 5.3 GeoParquet 为核心数据资产
GeoJSON 在 v1.0 中作为中间格式保留。
v2.0 的核心数据仓库是 GeoParquet：
- 列式存储，压缩比高
- DuckDB 原生支持空间查询
- 研究机构已公布大量公开数据集
- 未来商业化的核心资产

### 5.4 高精度替换策略
LevelInstance 子关卡 + CesiumCartographicPolygon 裁剪底层粗糙 Tiles。
手工/DCC 资产无缝嵌入程序化场景。

---

## 6. 待验证技术风险

来自与小城的技术讨论，需要在 Phase 5（1km² 原型）中实操验证：

| # | 风险 | 评估 | 验证方案 |
|---|------|------|---------|
| R1 | Cesium Tiles 本地缓存 | 技术可行，已有 DEM 缓存逻辑可参考 | Phase 5: 付费拉取 Cesium ion tiles，测试本地缓存 |
| R2 | PCG 动态生成速度 | HISM 是正解，但需配合预切 chunk + 后台流式 | Phase 5: 1km² 草地 HISM 基准测试，测量帧时间 |
| R3 | 逐 Tile 贴图内存 | 预估可控（256m tile + 1K 贴图 → ~100 tiles = ~100MB） | Phase 5: 实测不同 tile 分级下的 GPU 内存占用 |
| R4 | 道路贴地 | 两个方向：Spline Mesh 法线投影（精确慢）/ Decal（快糙） | Phase 5: 两种方案各做 1km 对比 |
| R5 | 水体接缝 | 需实操看效果 | Phase 5: 测试程序化水体 Shader 与 Cesium 地形接缝 |
| R6 | 云层高切换细节 | 与 UDS 集成相关 | Phase 6+: UDS 天气系统适配全球尺度 |
| R7 | 弹坑改变地形 | Decal（视觉欺骗，不改碰撞）vs Runtime Virtual Heightfield（真改地形，性能贵） | 另开专题讨论 |

---

## 7. 模块所有权（v2.0 新增部分）

| 模块 | Owner | 阶段 |
|------|-------|------|
| GeoParquet 预处理管线 | pipeline | Phase 5 |
| Py3DTiles 切片 + Feature ID | pipeline | Phase 5 |
| DuckDB Web API | pipeline | Phase 5 |
| Cesium 3D Tiles 加载集成 | runtime | Phase 5 |
| Feature ID 拾取 + UMG 面板 | runtime | Phase 5 |
| 程序化材质（建筑/道路/水体） | runtime | Phase 5 |
| Niagara + HISM 植被系统 | runtime | Phase 5-6 |
| 子关卡裁剪 | runtime | Phase 8 |
| 载具系统 | runtime | Phase 7 |
| 地形分析（PCG 感知） | algo | Phase 5-6 |
| AI 视觉识别 + 数据闭环 | 新领域 | Phase 9 |

---

## 8. 参考资源

- Cesium 3D Tiles: EXT_mesh_features 扩展 → Feature ID 注入
- Py3DTiles: Python 切片工具
- GeoParquet: 列式地理数据格式
- DuckDB Spatial Extension: 空间查询
- GeoPandas: Python 地理数据处理
- UCesiumPolygonRasterOverlay: 区域裁剪
- UCesiumFlyToComponent: 平滑飞行
- GlobeAwareDefaultPawn: 椭球体重力自适应
