# UDS Weather Agent (uds) — Shared Notes

## [v1.0.0] Initial State

### 当前状态

UDS 集成尚未开始。当前系统无天气/天空组件。

### 技术调研结论

**Ultra Dynamic Sky 集成方案**：
- 软依赖模式（WITH_UDS=0/1），类似 Cesium 集成
- WeatherBridgeComponent: 新建 UActorComponent，桥接 UDS API
- 天气→PCG 联动：通过 WeatherBridge 暴露当前天气状态，PCG 节点查询

**从太空穿越云层方案**：
1. 太空阶段：UE5 原生 Sky Atmosphere（支持 ground-to-space）
2. 穿云阶段：切换/淡入 UDS Volumetric Clouds（Render Over Opaque 模式）
3. 地面阶段：UDS 完全接管（天气、昼夜、云覆盖）
4. 关键参数：Bottom Altitude、Layer Height Scale、Cloud Render Mode

**与 Cesium 的协调方案**：
- UDS 管：天空、云、光照、天气效果
- Cesium 管：地形 mesh、影像纹理
- 冲突处理：禁用 Cesium 的 SunSky，使用 UDS 的 Directional Light

### 待实现功能

- WeatherBridgeComponent 组件（软依赖 UDS）
- Build.cs WITH_UDS 编译宏
- 天气状态 → PCG 密度/LOD 联动
- 太空→地面云层穿越 Blueprint 序列
- 军事仿真天气参数（能见度、风速、降水量）

## TODO (from lead review / peer review)

（暂无）

## Changelog

### [v1.0.0] — uds
- 初始状态记录，技术调研结论
