# UDS Weather Agent (uds) — Shared Notes

## [v1.0.0] Initial State

### 当前状态

UDS 插件已入库（Git LFS），API 分析完成。集成代码待创建。

### 技术调研结论

**UDS 插件分析**：
- **纯 Blueprint 插件**，无 C++ 源码，784 个 .uasset 文件
- 两个核心 Actor：`Ultra_Dynamic_Sky`（天空）和 `Ultra_Dynamic_Weather`（天气）
- 通过 UE 反射系统（FindObject/CallFunction）或 AngelScript 从 C++ 桥接

**Ultra Dynamic Sky 核心 API**：
- `Time of Day` (float 0-2400): 昼夜循环主控
- `Cloud Coverage` / `Fog`: 云量和雾
- `Sky Mode`: Volumetric Clouds / Static / 2D Dynamic / No Clouds / Aurora / Space
- `Color Mode`: Sky Atmosphere / Simplified Color
- `Simulate Real Sun/Moon/Stars`: 基于经纬度+日期的真实天文位置
- `Latitude` / `Longitude` / `Time Zone` / `North Yaw`: 地理模拟参数
- `Bottom Altitude`: 云底高度（穿越云层关键参数）
- 事件分发器：Sunset, Sunrise, Midnight, Hourly, Every Minute, Custom Time

**Ultra Dynamic Weather 核心 API**：
- Weather State: Cloud Coverage, Fog, Wind Intensity, Rain, Snow, Dust, Thunder/Lightning
- Material State: Snow Coverage, Wetness, Dust Coverage
- `Change Weather(Preset, TransitionLength)`: 天气切换函数
- `Random Weather Variation`: 随机天气系统，按季节概率
- `Weather Override Volume`: 区域性天气覆盖
- `Radial Storm`: 径向风暴区域
- 季节系统: Season float 0-4 (Spring→Summer→Autumn→Winter→Spring)
- 温度系统: 基于季节和气候预设

**Simulation（真实太阳位置）要点**：
- 启用 `Simulate Real Sun` 后，经纬度+日期驱动太阳位置
- 精度：太阳位置误差 <1°，月亮基于2017轨道校准
- `North Yaw` 控制世界空间北向
- 与 CesiumGeoreference 的经纬度天然对齐

**集成方案（确定）**：
- 软依赖模式（WITH_UDS=0/1），类似 Cesium 集成
- WeatherBridgeComponent: UActorComponent，通过 UE 反射/AngelScript 桥接 UDS
- 从 CesiumGeoreference 读取经纬度 → 设置 UDS Latitude/Longitude/TimeZone
- 天气→PCG 联动：暴露天气状态供 PCG 节点查询

**从太空穿越云层方案**：
1. 太空阶段：UDS Sky Mode = Space（只有日月星）
2. 穿云阶段：切到 Volumetric Clouds，Bottom Altitude 设 2-5km
3. 地面阶段：UDS 完全接管（Cloud Render Over Opaque 模式）
4. 关键：UDS 不支持球形行星，需在高空→地面切换时处理

**与 Cesium 的协调方案**：
- UDS 管：天空、云、光照（Directional Light、Sky Light、Height Fog）
- Cesium 管：地形 mesh、影像纹理
- 冲突处理：移除场景中的 Cesium SunSky，使用 UDS 的光照组件
- 设置 UDS 时需先移除场景中所有：Directional Lights, Sky Light, Exponential Height Fog, Sky Atmosphere, Volumetric Cloud

### 待实现功能

- [ ] WeatherBridgeComponent 组件（软依赖 UDS）
- [ ] Build.cs WITH_UDS 编译宏
- [ ] Cesium 经纬度 → UDS Simulation 参数同步
- [ ] 天气状态 → PCG 密度/LOD 联动
- [ ] 太空→地面云层穿越序列
- [ ] 军事仿真天气参数（能见度、风速、降水量）

## TODO (from lead review / peer review)

（暂无）

## Changelog

### [v1.0.0] — uds
- 初始状态记录，UDS 插件 API 完整分析
- 确定集成方案：软依赖 + 反射/AngelScript 桥接
