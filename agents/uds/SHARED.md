# 小U (UDS Agent) — TODO & Communication Log

## Current Status: Initializing

## TODO List

### Phase 0: Setup (Current)
- [x] 创建 agent 定义文件 (CLAUDE.md, SHARED.md)
- [ ] 等待用户上传 UltraDynamicSky 插件到 Plugins/
- [ ] 分析 UDS 插件 API 和类结构
- [ ] 将 UDS 添加到 eaglewalk.uproject 的 Plugins 列表

### Phase 1: Basic Integration
- [ ] 确保 UDS 与 Cesium 天空系统兼容（禁用 Cesium 默认天空盒）
- [ ] 创建 EagleWalk 示例关卡，包含 Cesium 地球 + UDS 天空
- [ ] 验证 UDS 昼夜循环在 Cesium 场景中正常工作

### Phase 2: GIS-Weather Bridge
- [ ] 编写 C++ 桥接组件：读取 CesiumGeoreference 经纬度 → 设置 UDS 太阳位置
- [ ] 实现基于真实地理位置的日出日落时间计算
- [ ] 天气状态 API（供外部系统调用）

### Phase 3: Advanced Features
- [ ] 地形类型驱动天气（如水域附近更多雾气）
- [ ] 多区域天气过渡
- [ ] 性能优化和 LOD 配合

## Communication Log

### 2026-03-30 — 初始化
- 创建了全局 CLAUDE.md 和 agent 定义文件
- 项目已从 cesium-unreal-samples 克隆并重命名为 EagleWalk
- GISProcedural 插件已就位于 Plugins/
- **等待**: 用户上传 UltraDynamicSky 插件
