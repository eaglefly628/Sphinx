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

## TODO (from lead review)

- [ ] **P1: 完善 WeatherBridge.as** (from lead) — 当前 Script/WeatherBridge.as 只有 auto-find 功能。需补充：1) Cesium 经纬度→UDS Latitude/Longitude/TimeZone 同步 2) 时间同步（Cesium 时间→UDS Time of Day）3) 基础天气控制函数（SetWeather/SetTimeOfDay）。**全部用 AngelScript，不写 C++**，写前必读 `docs/AngelScript_Guide.md`
- [ ] **P2: 天气→PCG 联动接口** (from lead) — 暴露当前天气状态（雨/雪/雾/能见度）供 runtime 的 PCG 节点查询，用 UPROPERTY 暴露给蓝图/AS
- [ ] **P3: 太空→地面云层穿越** (from lead) — 按调研方案实现 Sky Mode 切换序列。优先级最低，先完成 P1/P2

## Changelog

### [v1.5.0-WIP] — uds — UDS Painted Cloud 接口直连（NASACloudPaintActor）

#### 背景

v1.4.0 BP Bridge 让 EagleCloud 不再用字符串反射，但**天上仍然没有 NASA 云**。深入诊断发现根因：UDS 9.3A 的 painted cloud 系统**不是 push 模式**（外部往一个 RT 写）而是**pull 模式**（UDS 每帧通过 `Cloud Paint Actors Manager` 主动调每个实现 `UDS_CloudPaintActor_Interface` 的 actor 的 `Draw to Cloud Paint Target` 函数）。我们之前 Canvas-Draw 的 `CloudPaintTarget` 不是 UDS 渲染采样源，真正的源是 `Cloud Coverage Render Target`，且每帧由 UDS 自己 BeginDraw → ForEach actor → EndDraw 重建。

通过反向工程 `UDS_Ultra_Dynamic_Sky.UpdatePaintedCloudCoverageTarget` 函数 + `UDS_Cloud_Paint_Container.Draw to Cloud Paint Target` 函数（Ctrl+A→Ctrl+C 文本导出 + 节点级解码），完整管线确认：

```
UDS tick 内部:
  Mapping = self.Cloud Coverage Target Mapping()    // Vector(centerX_world, centerY_world, fullSize_world) cm
  BeginDrawCanvasToRenderTarget(Cloud Coverage Render Target) → Canvas
  ForEach Cloud Paint Actors Manager.FilteredAndSortedActors:
      actor.DrawToCloudPaintTarget(Canvas, RadialStormMID, Mapping, TargetRes, CanAdd, CanSub, Active)
  EndDrawCanvasToRenderTarget
```

#### 架构变更

新增 C++ 类 **`ANASACloudPaintActor`** (`Plugins/GISProcedural/Source/GISProcedural/Public/Cloud/`):
- 继承 `AActor`，无 tick（UDS 主动调）
- 核心 UFUNCTION: `DrawNASAToCanvas(Canvas, TargetMapping, TargetRes, CanAdd, CanSub, Active) → bool`
  - 完全对齐接口签名
  - 默认 full-canvas 铺满模式（最快、最简单，相机移动时贴图跟相机走）
  - `bUseWorldMapping = true` 走世界对齐模式（按 `TargetMapping = (centerX, centerY, fullSize)` 公式做 UV 偏移/缩放）
- UPROPERTY: `NASACoverageTexture` / `NASAWorldCenter` / `NASAWorldSize` / `RenderColor` / `bEnabled` / `PaintPriority`
- BlendMode = Opaque（与 cell BP 行为对齐）

#### BP 子类（用户在编辑器创建）

`BP_NASACloudPaintActor`（无 .uasset 提交，CLI 不能创建）：
- Parent Class = `ANASACloudPaintActor`
- Class Settings → Implemented Interfaces → `UDS_CloudPaintActor_Interface`
- Override `Draw to Cloud Paint Target` → 函数体一个节点：调 `self.DrawNASAToCanvas(...)` 传入所有同名参数

#### 测试 / 风险

- **风险 1**：`Cloud Paint Actors Manager` (UDS_InterfaceActorArrayManager) 是否能自动发现我们的 actor 未验证。如果不发现，需要在 spawn 后主动调 `Activate` 事件触发 manager 的 Filter And Sort Array 重扫描。
- **风险 2**：`TargetMapping` 解码假设是 `(centerX, centerY, fullSize)`，从 cell BP 反解的（`Target Location = X + Z/2, Y + Z/2`）。默认 full-canvas 模式不依赖此假设，安全。
- **测试路径**：编辑器编译 → 创建 BP_NASACloudPaintActor → spawn → 设 NASACoverageTexture → Play → 查 LogGIS_NASACloud + 看天空。

#### 调试发现（实测）

**P0 验证结果**（用户实际测试）：
- ✅ Manager 自动发现：BP_NASACloudPaintActor 继承自 `UDS_Cloud_Paint_Container`（自带 `UDS_InterfaceActorArrayManagedActor` 接口），自动被 Cloud Paint Actors Manager 加进 FilteredAndSortedActors
- ✅ UDS 调用接口：Print String 在 PIE 启动时 fire 一次，Success=true，证明整条 actor → interface → DrawToCloudPaintTarget 链路通的
- ✅ Canvas Draw 落到正确的 RT：painter 之前画的 cell **被我们擦了**（Opaque + 全画布），证明我们写的就是 UDS 渲染采样的那张 `Cloud Coverage Render Target`

**RT 通道编码**（UDS 9.3A 实测）：
- **R 通道 = Zero Coverage** （强制无云 / 减云）
- **G 通道 = Mid Coverage** （部分云）
- **B 通道 = Full Coverage** （强制满云 / 加云）
- 与 cell BP 的 `Full/Mid/Zero Coverage Present` 三个 bool 变量对应

**测试方法**（验证通道）：RenderColor 改纯红 / 纯绿 / 纯蓝单独跑：
- (1,0,0,1) → 云**变少**
- (0,1,0,1) → 云**变多**
- (0,0,1,1) → 云**全是**

**BlendMode 选择**：
- 旧用 `BLEND_Opaque` → 全画布覆盖，painter cell 被擦
- 新用 `BLEND_Additive` → Draw 累加到 RT，painter cell + NASA actor 共存

**默认 Cloud Coverage 必须设 0**：
- UDS `Cloud Coverage` 默认值（5 等）是**全局基准云量**，加在 painted RT 之上
- 如果不为 0：天空一直被默认云填满 → NASA 的 `+B` 看不出差别
- 设 0：默认无云 → NASA 数据直接渲染 → 出现实际地球云分布
- 用户在 PIE 里实测：关掉默认 5 后，**天空看起来像一个地球**（NASA 真实云系可见）✅

#### 代码层 TODO（下一步）

- [x] **P0 ✅**：用 RenderColor=(0,0,1,1) + Additive 跑，天空出现 NASA 模式（用户截图确认 — 高频斑点感是因为全球图被压缩到 200km canvas，但分布是 NASA 实际数据）
- [ ] **P1**：BP_EagleCloudBridge 改造 — Canvas Draw 到自有 `NASA_Coverage_RT_256`，spawn `BP_NASACloudPaintActor` 并把 RT 引用传过去（替代当前 GetUDSCloudRT 死路）
- [ ] **P1**：实现 NASA 双通道编码 — 亮处加 B，暗处加 R（同时强制清空），需要 2 个 Draw pass 或 1 个材质
- [ ] **P2**：`bUseWorldMapping = true` 模式实测，验证 TargetMapping 解码假设（相机移动时贴图应该"贴在世界上"）
- [ ] **P3**：可选 — 实现第二接口 `Apply Effect to Cloud Coverage Value`（按 3D 采样点逐点调整 coverage）

### [v1.4.0] — uds — 重构：BP Bridge 桥接模式（消除字符串反射）

#### 背景

v1.0.0 ~ v1.3.0 的 EagleCloud 通过 `FProperty + FindPropertyByName` 反射访问 UDS 9.x 的 BP 变量（"Cloud Painting Active"、"Cloud Coverage Render Target" 等）。在 v1.3.0 后用户调试 P0 时发现：
- Feeder 的反射调用全部"安静成功"（`GetObjectProp` 静默 return null），但天上没出 NASA 云图
- Details 面板搜不到 `Cloud Coverage Render Target` 字段，强烈怀疑 UDS 9.3A 的 BP 变量内部 FName 与我们假设不一致
- 即使加了 `bVerboseLogging` + `DumpUDSState` 一键诊断，仍是事后定位，不是事前防御

经主程审议（用户提出）：**字符串反射 = 定时炸弹**。三方案对比后选**方案一：BP Bridge**。

#### 架构变更

新增 C++ 类 **`AEagleCloudUDSBridge`** (`Plugins/EagleCloud/Source/EagleCloud/Public/EagleCloudUDSBridge.h`):
- 派生自 `AActor`
- 5 个 `BlueprintImplementableEvent`：
  - `bool InitializeUDS()`
  - `UTextureRenderTarget2D* GetUDSCloudRT()`
  - `void SetCoverageWindow(FVector2D WorldXY)`
  - `void SetPaintingState(bool bActive, bool bForce, float Opacity, float AffectsGlobal)`
  - `void SetSkyMode(int32 ModeIndex)`
- C++ cpp 不实现这些 Event（只构造函数），由 BP 子类 `BP_EagleCloudBridge` 实现

`ASatelliteCloudFeeder` (改造):
- 删 `FindUDSActor` / `GetUDSCloudRT` / `EnableUDSPainting` / `SetUDSTargetLocation` / `DumpUDSState` / namespace 内 5 个反射 helper
- 删 `CachedUDS`
- 新增 `TObjectPtr<AEagleCloudUDSBridge> Bridge` UPROPERTY
- `BeginPlay` 加 `ensureMsgf(Bridge, ...)` 守卫，null 时禁用 Tick
- `ApplyToUDS` 改为：调 `Bridge->SetPaintingState(...)` → `Bridge->GetUDSCloudRT()` → Canvas Draw → `Bridge->SetCoverageWindow(WorldXY)`
- `SyncPropertiesToUDS` 改为：调 `Bridge->SetPaintingState(...)`
- `RefreshIntervalSeconds` 默认 0 → **0.2**（0 = BeginPlay 一次跑然后再不重试，是高发坑）
- `bVerboseLogging` 默认 true → **false**（Bridge 失败现在 BP 编译期暴露，不需要默认开 verbose）

`AAtmosphereCloudManager` (改造):
- 删 `FindUDSActor` / `CachedUDS` / `UDS_SkyModeProperty`（不再需要属性名字符串）
- 保留 `UDS_SkyMode_Volumetric` / `UDS_SkyMode_Space` 作为传 Bridge 的整数索引
- 新增 `TObjectPtr<AEagleCloudUDSBridge> Bridge` UPROPERTY，`BeginPlay` 时若 null 自动从 `Feeder->Bridge` 借（用户少拖一次）
- `ApplySkyMode` 改为：算出 DesiredMode → `Bridge->SetSkyMode(DesiredMode)`
- 删 `Kismet/GameplayStatics.h` / `UObject/UnrealType.h` 等反射相关 include

代码量：C++ 净减约 **170 行**（v1.3.0 的反射诊断代码 + 旧 helper 全部移除），可读性 ↑↑。

#### 用户必做（编辑器侧，约 5 分钟）

> 拉代码 + 重编后必须做这一步，否则 `ensureMsgf(Bridge, ...)` 会在 PIE 启动时弹断言对话框。

**Step A — 创建 BP 子类**
1. UE Editor → Content Browser → 进入 `Plugins/EagleCloud/` 任意位置
2. 右键空白 → **Blueprint Class** → 弹窗选 **All Classes** → 搜 `EagleCloudUDSBridge` → 选中 → Select
3. 命名 `BP_EagleCloudBridge` → 创建

**Step B — 实现 5 个 Event**

打开 `BP_EagleCloudBridge` → 切到 **Event Graph** → 右键 → 搜 "Initialize UDS"（事件名）→ 选 `Event Initialize UDS` → 添加。同样添加另外 4 个 `Event Get UDSCloud RT` / `Event Set Coverage Window` / `Event Set Painting State` / `Event Set Sky Mode`。

蓝图变量 → 加一个 `CachedUDS` (Type: `Ultra_Dynamic_Sky` 或 `Actor`)。

每个 Event 实现：

| Event | 实现 |
|---|---|
| `Initialize UDS` (Output: `Return Value` Bool) | `Get All Actors of Class (Ultra_Dynamic_Sky)` → `Get [0]` → `Set CachedUDS` → 同时 `Set CachedUDS.Cloud Painting Active = true`、`Set CachedUDS.Force Cloud Coverage Target Active = true` → `Is Valid (CachedUDS)` 接 `Return Value` |
| `Get UDS Cloud RT` (Output: `Return Value` `Texture Render Target 2D`) | `CachedUDS` → `Get Cloud Coverage Render Target`（拖出引脚找）→ 接 `Return Value` |
| `Set Coverage Window` (Input: `WorldXY` FVector2D) | `CachedUDS` → `Set Cloud Coverage Target Location` 输入引脚连 `WorldXY` |
| `Set Painting State` (Inputs: bActive, bForce, Opacity, AffectsGlobal) | `CachedUDS` → 4 个 `Set Variable` 节点串起来：`Cloud Painting Active`/`Force Cloud Coverage Target Active`/`Painted Cloud Coverage Opacity`/`Painted Coverage Affects Global Values` |
| `Set Sky Mode` (Input: `ModeIndex` int32) | `CachedUDS` → `Set Sky Mode` → 输入引脚需要 enum，BP 里**右键 ModeIndex 引脚 → "Cast to <UDS_SkyMode 枚举>"**（或用 `Equal` + `Select Enum` 做 dispatch）|

> **如果 UDS 9.3A 中变量名不是上面这些**：BP 编译时引脚就连不上，红字直接告诉你字段叫什么。改成对的就行。这是 BP Bridge 比反射强的核心点。

> Compile + Save 整个 BP。

**Step C — 关卡布置**
1. 关卡里**拖一个 `BP_EagleCloudBridge` Actor** 进 Outliner（位置随便）
2. 选 `ASatelliteCloudFeeder` Actor → Details → **EagleCloud | Bridge** → **Bridge** 字段拖 `BP_EagleCloudBridge`
3. 选 `AAtmosphereCloudManager` Actor → Details → **EagleCloud | Refs** → **Bridge** 字段也拖（或留 None，BeginPlay 会从 Feeder 借）

**Step D — 验证**
- Play → Output Log 过滤 `LogEagleCloud`
- 应该看到：
  ```
  ===== SatelliteCloudFeeder START =====
  Mode: GLOBAL (sample by lat/lon)
  Bridge.InitializeUDS() -> OK
  ```
- 没有 `ensureMsgf` 弹窗
- 天上 UDS 体积云开始呈现 NASA 全球云图图案

#### 兜底（方案二）

C++ 内任何残留反射点（未来如果需要）必须用 `ensureMsgf` 让失败响亮。BP Bridge 接口本身已不再有反射，本次保留无残留点。

#### 还在 P0~P2 编辑器侧（用户继续中）

参见 v1.2.0 清单。本次重构**只改了"如何与 UDS 通信"**，不改导入纹理 / MPC / M_AtmosphereShell / 球壳 Mesh 这条管线。`MPC_AtmosphereCloud` 4 标量、`M_AtmosphereShell` 都不变。

---

### [v1.3.0] — uds — AtmosphereCloudManager 高空真正切 Sky Mode = Space (P3 代码层完成)

#### 本次提交内容（代码层，已 push 到 `claude/claudeMainBranch-0zjsx`）

**`AAtmosphereCloudManager` 新增 Sky Mode 切换** (P3 来自 v1.2.0 留尾):

之前实现：高空只 fade `AffectsGlobalValues → 0`，UDS volumetric cloud pass 仍在跑，浪费 GPU。

现在实现：相机海拔越过 `HighAltitudeKm ± SkyModeSwitchHysteresisKm` 时，反射写 UDS Actor 的 `Sky Mode` 枚举属性，从 `Volumetric Clouds` 真正切到 `Space`，关闭 volumetric pass。

新增 UPROPERTY (Category `EagleCloud|SkyMode`):
- `bManageUDSSkyMode` (bool, default true): 自动管理总开关
- `UDS_SkyModeProperty` (FName, default `"Sky Mode"`): 反射查找的属性名
- `UDS_SkyMode_Volumetric` (uint8, default 0): UDS 枚举的 Volumetric 值
- `UDS_SkyMode_Space` (uint8, default 5): UDS 枚举的 Space 值
- `SkyModeSwitchHysteresisKm` (double, default 25.0): 边界滞回带

实现细节:
- 反射兼容 `FByteProperty`（旧式 BP enum）和 `FEnumProperty`（UENUM class enum）两种
- `LastAppliedSkyMode` 状态机记忆，避免每帧 set
- 无 UDS 时静默退出（`CachedUDS` lazy 查找，下一 tick 重试）
- 找不到属性时 `bManageUDSSkyMode=false` 自闭包，避免日志洪水

#### 验证清单（编辑器侧）

> 用户上一 session 已生成 NASA PNG（`Tools/Weather/output/CloudGlobal_GEO_*.png` + `*_CloudMask.png`）。
> 当前正在做 P0 导入（见下一 session 必做清单）。
> 拉取本 commit 后重新编译插件即可获得 Sky Mode 切换能力。

需要在编辑器里验证（**P3 集成测试**）:
- [ ] UDS Actor 反射属性名确实是 `"Sky Mode"`（带空格）。如果 UDS BP 变量内部 FName 不一样，编辑器面板里改 `AAtmosphereCloudManager.UDS_SkyModeProperty`。
- [ ] UDS Sky Mode 枚举顺序是否仍为 Volumetric=0 / Space=5。如 UDS 升级换序，编辑器面板里改 `UDS_SkyMode_Volumetric` / `UDS_SkyMode_Space` 数值。
- [ ] Play 时垂直爬升越过 `HighAltitudeKm + 25km`（默认 525km），Output Log 应出现 `UDS Sky Mode -> 5 ... (Space)`。GPU profiler 中 Volumetric Cloud pass 应归零。
- [ ] 反向降落越过 `HighAltitudeKm - 25km`（475km），日志切回 Volumetric。
- [ ] 在 [475, 525] 抖动飞行，日志**不应**疯狂切换（hysteresis 生效）。

#### 还在 P0~P2 编辑器侧（用户继续中）

参见 v1.2.0 清单。本次 push 不影响该流程，编译完即可继续。

---

### [v1.2.0] — uds — 全球云图数据管线完成，下一步 UE5 编辑器接入

#### 数据管线状态：完成 ✅

**`Tools/Weather/fetch_gibs_cloud.py`** 最终方案：
- 数据源：NOAA nowCOAST WMS `global_longwave_imagery_mosaic`
- 卫星：GOES-18/19 + Himawari-9 + Meteosat-9/10，全球缝合，无拼接缝隙
- 波段：长波红外 12µm，冷云顶=亮，日夜全覆盖，无夜侧黑区
- 覆盖：60°S~60°N，极区 (`clear_polar_bands`) 置零
- 输出：`CloudGlobal_GEO_<timestamp>.png` + `*_CloudMask.png`
- 单次 HTTP 请求即可，stdlib only，无需认证

**C++ 插件 EagleCloud 状态**：
- `ASatelliteCloudFeeder`：Global mode 完整，UV 等经纬度→UDS RT，camera 跟随
- `AAtmosphereCloudManager`：高度 LOD smoothstep，MPC 写入 MacroAlpha/UDSDensity/AltitudeKm/NoDataThreshold
- `SatelliteCloudFeeder.NoDataThreshold = 0.05`：无数据区平滑过渡到程序化噪声（Shader 端待实现）

#### 架构确认（下一 Session 实现目标）

完整架构（经用户确认）：

```
NASA PNG（等经纬度灰度）
  ├─► 宏观层 Macro Shell（大气球壳 Mesh）
  │     Material: Translucent Unlit，UV=球极→等经纬度
  │     Alpha = MPC.MacroAlpha（高度驱动，太空时=1，地面时=0）
  │     Fresnel 边缘柔化 + 伪3D受光（NASA图当Heightmap算Normal）
  │     风速 UV Panner = MPC.CloudScrollSpeed（与UDS同步）
  │
  └─► 微观层 UDS Volumetric Clouds
        SatelliteCloudFeeder 将全球纹理局部 UV 采样注入 UDS RT
        UV 映射：U = U_origin + WorldX/PlanetCircumference
                V = V_origin + WorldY/PlanetCircumference
        Density = MPC.UDSDensity（高度驱动，地面时=1，太空时=0）

控制中枢 AAtmosphereCloudManager (Tick):
  Altitude = (CamZ - GroundZ) * 0.00001 km
  T = smoothstep((Alt - LowKm) / (HighKm - LowKm))
  MacroAlpha = T
  UDSDensity = 1 - T
  → MPC 驱动两套系统
```

#### 下一 Session 必做清单（编辑器操作）

**P0：导入云图 PNG（必须先做）**
- Import `CloudGlobal_GEO_*.png` as UTexture2D
- 设置：sRGB=OFF，Compression=Masks(no sRGB)，MipMaps=ON
- Tiling：X=Wrap（经度循环），Y=Clamp（纬度不循环）
- 将此 Texture 赋给 `SatelliteCloudFeeder.GlobalCloudTexture`

**P0：创建 MPC_AtmosphereCloud**
- 4 个标量参数：`MacroAlpha`、`UDSDensity`、`AltitudeKm`、`NoDataThreshold`
- 赋给关卡中的 `AAtmosphereCloudManager.MPC`

**P1：创建 M_AtmosphereShell 材质**
- Blend Mode: Translucent，Lighting Model: Unlit
- UV = atan2(N.y, N.x)/2π + 0.5（经度），acos(N.z)/π（纬度）→ 等经纬度采样
- Cloud Density = Texture Sample（CloudMask）×  MPC.MacroAlpha
- Opacity = Cloud Density（带 Fresnel 边缘柔化）
- 无数据兜底：`lerp(NASADensity, ProceduralNoise×0.5, 1-smoothstep(0, NoDataThreshold, NASADensity))`
- 伪3D受光：NASA灰度图当Heightmap → ddx/ddy 算法线 → dot(Normal, SunDir) 简单漫反射

**P1：关卡布置**
- 放一个大球体 Mesh（半径=大气层高度，约地球半径×1.05）
- 赋 M_AtmosphereShell 材质
- 引用给 `AAtmosphereCloudManager.MacroShellActor`

**P2：踩坑提示**
- Z-Fighting：M_AtmosphereShell 开 Disable Depth Test 或调 Translucency Sort Priority（壳体 < 体积云）
- UDS Texture Address Mode：确保 UDS Coverage RT 采样不 Tile（设为 Clamp 或用 UV frac 保护）
- 对齐验证：先关 UV Scroll，垂直降落检查 2D 壳边界与体积云边界是否重合

**P3：代码层 TODO（下 Session 继续）**
- `AtmosphereCloudManager`：HighAltitudeKm 以上切 UDS Sky Mode = Space（真正关掉 Volumetric 节省 GPU）
- `SatelliteCloudFeeder::GetSampleCenterLatLon()`：升级为 Cesium georef（当前 flat-earth 近似）
- WeatherBridge.as P1：Cesium 经纬度 → UDS Latitude/Longitude/TimeZone 同步

### [v1.1.0] — uds — EagleCloud 全球卫星云图 + 调试菜单修复
**已完成**:
- `Plugins/EagleCloud/` 新插件 (C++, 非 AngelScript — 因性能需求)
  - `ASatelliteCloudFeeder`: Phase A (local 200km RT 绘制) + Phase B (全球等经纬度纹理 UV 采样 + camera 跟随)
  - `AAtmosphereCloudManager`: 高度 LOD 混合 (smoothstep), Feeder.AffectsGlobalValues + MPC 驱动
  - `Tools/Weather/fetch_gibs_cloud.py`: NASA GIBS WMS 全球云图下载
  - `Tools/Weather/gen_cloud_test.py`: Phase A 测试 PNG 生成器
  - `Plugins/EagleCloud/README.md`: Phase A/B/C 完整安装说明
- `WeatherDebugMenu` 全部修复:
  - Cloud/Fog → UDW Actor (不是 UDS)，值 ×10 (UDW 0–10 刻度)
  - Thunder FName 修正: `Thunder/Lightning` (无空格)
  - Manual Override 标志补充
  - ComboBox ForegroundColor = White (修复黑字不可读)
  - 7个值标签实时更新
  - DumpActorProperties debug 代码已删除

**待编辑器完成 (无法从 CLI 创建 .uasset)**:
- `MPC_AtmosphereCloud` — 3 个标量参数 (MacroAlpha/UDSDensity/AltitudeKm)
- `M_AtmosphereShell` — 球形壳半透明材质，读 GlobalCloudTexture
- 关卡中的大气球体 Mesh Actor

**代码层 TODO**:
- [ ] `AtmosphereCloudManager` 在 HighAltitudeKm 以上切换 UDS `Sky Mode` 为 Space (目前只 fade AffectsGlobalValues，未真正关闭 Volumetric)
- [ ] `SatelliteCloudFeeder::GetSampleCenterLatLon()` 升级为 Cesium georef (当前 flat-earth 近似，200km 窗口内误差 <1%)
- [ ] 经度换行 (±180°) 时双 Draw Call 精确裁切 (当前靠 Wrap 纹理模式，视觉上 OK)
- [ ] WeatherBridge.as P1: Cesium 经纬度→UDS Latitude/Longitude/TimeZone 同步 (原 SHARED 遗留)

### [v1.0.0] — uds
- 初始状态记录，UDS 插件 API 完整分析
- 确定集成方案：软依赖 + 反射/AngelScript 桥接
