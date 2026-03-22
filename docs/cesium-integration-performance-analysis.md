# Cesium 集成性能分析报告

> 日期: 2026-03-22
> 状态: 计划审查阶段

## 当前系统关键参数

| 参数 | 值 |
|------|-----|
| 每 tile 面积 | 1km × 1km |
| 每 tile 多边形数 | 50~200（住宅区） |
| 每多边形平均顶点数 | 4~12 |
| PCG 采样间隔 | 10m（≈100 点/万m²） |
| PCG 加载半径 | 最大 7×7 = 49 tiles |
| LRU 缓存 | 50 tiles |
| Actor 生成 | 1 Actor per polygon |

---

## 逐环节非线性爆炸分析

### 1. 地形高程获取 — ~~Line Trace~~ → 离线 DEM 缓存（修正）

**原方案（运行时 Line Trace）的问题**：

```
每 tile:    ~150 polygons × 1 trace (仅中心点) = 150 traces
49 tiles:   150 × 49 = 7,350 traces

逐顶点采样：
每 tile:    150 polygons × 8 vertices = 1,200 traces
49 tiles:   1,200 × 49 = 58,800 traces
```

非线性爆炸点：Cesium 近处 LOD 越高 → mesh 三角面越多 → 每次 Line Trace 的碰撞检测越慢。
近处 1 tile 的 Cesium mesh 可能有 50万~200万三角面。

**修正方案：离线 DEM 高程缓存**

不应运行时做 Line Trace。正确方案是：

1. **离线阶段**：读取 DEM 数据（GeoTIFF/HGT），按 tile 网格生成高程缓存
2. **缓存格式**：每 tile 一张 `100×100 float` 高程网格（10m 间距），仅 40KB/tile
3. **运行时查询**：双线性插值查表，O(1) 复杂度，~5ns/次
4. **总缓存量**：49 tiles × 40KB = ~2MB（可忽略）

好处：
- 零运行时 Line Trace 开销
- 不依赖 Cesium mesh 加载状态
- 确定性结果（不受 LOD 影响）
- 可预计算坡度、坡向等地形特征

仅在以下情况需要运行时 Line Trace：
- 用户实时编辑地形
- Cesium 3D Tiles 是唯一地形数据源且无法导出 DEM

### 2. Actor 数量 — 线性但基数大

```
当前：1 Actor per polygon
49 tiles × 150 polygons = 7,350 Actors（仅 polygon 容器）
UE5 每个 AActor ~2KB 开销 → ~15MB（可控）
```

控制方案：远距 tile（>3km）不 spawn Actor，仅保留元数据。

### 3. PCG Mesh Instances — 主要渲染负担

```
49 tiles × (1km² / 10m²) = ~490,000 个 PCG 点
每点可能 spawn 1 个 mesh instance

建筑: ~500 三角面/栋 × 2000 栋 = 100万三角面
植被: ~200 三角面/棵 × 5000 棵 = 100万三角面
```

非线性爆炸点：采样间隔从 10m 降到 5m 时，点数 ×4（二次增长）。

控制方案：
- 近处(< 1km) 间隔 10m，中距(1-3km) 间隔 30m，远处(3km+) 间隔 100m
- 必须使用 HISM，否则 draw call 爆炸

### 4. Cesium + PCG 渲染叠加

```
Cesium 自身渲染:
  - 3D Tiles: 近处 ~200万三角面（自动 LOD）
  - 影像纹理: ~50 tiles × 4MB ≈ 200MB VRAM

PCG 渲染:
  - 建筑+植被: ~200万三角面
  - Draw Calls: 用 HISM → ~50-100，不用 → 数千

合计: ~400万三角面 + 200MB VRAM（纹理）
```

### 5. 坐标转换开销

```
原来 GeoToWorld(): ~5ns/次
Cesium ECEF 变换: ~200-500ns/次

批量 10,000 顶点: 原来 0.05ms → Cesium 2-5ms
```

影响可忽略，绝对值在毫秒级。

### 6. 内存估算

```
Cesium tile 缓存:    500MB - 1GB（3D Tiles mesh + 影像纹理）
GIS tile 缓存:       50 tiles × ~2MB = 100MB
DEM 高程缓存:        49 tiles × 40KB = ~2MB
PCG 点数据:          490K × 48 bytes = ~24MB
Polygon 数据:        7,350 × ~1KB = ~7MB

总计: ~650MB - 1.1GB（主要是 Cesium）
```

---

## 综合评估

| 环节 | 复杂度 | 是否会爆 | 距离可控？ |
|------|--------|---------|-----------|
| 地形高程查询（DEM缓存） | O(1) 查表 | 不会爆 | — |
| Actor 数量 | O(tiles × polys) | 线性可控 | 是 |
| PCG mesh instances | O(tiles × area / interval²) | **会爆** | 是，动态间隔 |
| Draw calls | O(mesh_types × tiles) | 用 HISM 可控 | 是 |
| Cesium 渲染 | Cesium 自管 LOD | 不会爆 | 自动 |
| 内存 | O(cached_tiles) | 线性可控 | 是，LRU |

---

## 硬件需求预估

### 最小可用配置（7×7 km，PCG 近处 3×3 tiles）

| 硬件 | 最低 | 推荐 |
|------|------|------|
| CPU | 6 核 / 12 线程（i5-12400） | 8 核 / 16 线程（i7-13700） |
| RAM | 16 GB | 32 GB |
| GPU | RTX 3060 / RX 6700 XT (8GB) | RTX 3070 / RX 6800 (10GB+) |
| 存储 | SSD | NVMe SSD |
| 目标帧率 | 30 fps | 60 fps |

### 大范围配置（15×15 km，PCG 近处 5×5 tiles）

| 硬件 | 推荐 |
|------|------|
| CPU | 8+ 核（i7-13700K / R7 7800X3D） |
| RAM | 32-64 GB |
| GPU | RTX 4070 Ti / RX 7800 XT (12GB+) |
| 目标帧率 | 30-60 fps |

---

## 计划修正建议

1. **去掉运行时 Line Trace**：改为离线 DEM 高程缓存 + 双线性插值查表
2. **PCG 距离衰减**：近/中/远三级采样密度，与 Cesium LOD 联动
3. **强制 HISM**：PCG 输出必须走 Hierarchical Instanced Static Mesh
4. **高程缓存层**：`CesiumBridge` 中增加 `FDEMCache` 模块，管理离线高程数据
