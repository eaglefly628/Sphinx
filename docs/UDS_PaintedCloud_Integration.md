# UDS Painted Cloud 集成指南

> **目标读者**：在 UE5 工程里集成 Ultra Dynamic Sky 9.x 的 Painted Cloud 系统，
> 需要把外部贴图（NASA 卫星、自研云分布、闪电 mask 等）喂进 UDS 体积云渲染的工程师。
>
> **本文不依赖** Sphinx 项目的 EagleCloud 插件 / Feeder / Cesium。提供**可移植到任意
> UE5 + UDS 9.x 工程**的最小集成方案 + 关键踩坑预警。

---

## 1. 背景：为什么不能直接写 RT

UDS 9.x 的 Painted Cloud 系统**不是 push 模式**（外部 actor 往 RT 里写）—— 它是
**pull 模式**：

```
UDS 内部每次 coverage update 触发时（snapped camera 变化、状态变化）：
  1. BeginDrawCanvasToRenderTarget(Cloud Coverage Render Target)  ← UDS 清空这张 RT
  2. ForEach actor in Cloud Paint Actors Manager.FilteredAndSortedActors:
       actor.DrawToCloudPaintTarget(Canvas, TargetMapping, TargetRes, ...)
                                    ↑↑↑ UDS 主动调用每个实现接口的 actor
  3. EndDrawCanvasToRenderTarget
  4. UDS volumetric cloud shader 采样 Cloud Coverage Render Target
```

**结论**：
- ❌ 直接 Canvas Draw 到 `Cloud Coverage Render Target`：会被 UDS 下次 update 时 clear 掉
- ❌ 直接 Canvas Draw 到 `CloudPaintTarget`：UDS shader 根本不读这张 RT
- ✅ 唯一正确方式：**实现 `UDS_CloudPaintActor_Interface`**，让 UDS 在 ForEach 时调用我们

---

## 2. RT 通道编码（必背）

实测 UDS 9.3A 的 `Cloud Coverage Render Target` 像素通道含义：

| 通道 | 语义 | 视觉效果 |
|---|---|---|
| **R** | Zero Coverage | 强制无云 / 减云（值越大云越少） |
| **G** | Mid Coverage | 部分云 / 中等密度 |
| **B** | Full Coverage | 强制满云 / 加云（值越大云越厚） |
| A | 未明确使用 | — |

**验证方法**：BP 子类的 `Draw to Cloud Paint Target` 用 Canvas DrawTexture 写纯色全画
布测试，观察天空效果（实测 R=1 减云，G=1 加部分云，B=1 加满云）。

**对应的 BP 变量**（在 `UDS_Cloud_Paint_Cell` BP 里）：
- `Full Coverage Present` (bool) → B 通道
- `Mid Coverage Present` (bool) → G 通道
- `Zero Coverage Present` (bool) → R 通道

---

## 3. 推荐架构：多源合成

```
数据源 1 (NASA 云图)    → 自有 RT_1 ←(读)─ BP_NASACloudPaintActor      ─→ UDS Canvas
数据源 2 (闪电 mask)    → 自有 RT_2 ←(读)─ BP_LightningPaintActor       ─→ UDS Canvas  
数据源 3 (局部强对流)   → 自有 RT_3 ←(读)─ BP_StormFrontPaintActor     ─→ UDS Canvas
                                                                          ↓ Additive 混合
                                                  UDS Cloud Coverage Render Target
                                                                          ↓
                                                  UDS volumetric cloud shader
```

**核心约束**（违反则架构破坏）：
1. ✅ 数据源 actor **只写自己的 RT**，绝不动 UDS 内部 RT
2. ✅ PaintActor 只通过接口在 UDS BeginDraw/EndDraw 框架内画 UDS Canvas
3. ✅ Canvas DrawTexture 全用 `BLEND_Additive`（或 channel-specific blend）让多个 actor 和谐叠加

**反例**（已踩过的坑）：在 UDS 框架外 Canvas Draw UDS 的 `Cloud Coverage Render Target`
= 在每次 UDS update 之间用 `BLEND_Opaque` 全擦，其他 PaintActor 的贡献全没了。

---

## 4. 核心代码：通用 PaintActor

### 4.1 C++ 类（建议命名 `AGenericCloudPaintActor`，本项目叫 `ANASACloudPaintActor`）

参考实现路径：
- `Plugins/GISProcedural/Source/GISProcedural/Public/Cloud/NASACloudPaintActor.h`
- `Plugins/GISProcedural/Source/GISProcedural/Private/Cloud/NASACloudPaintActor.cpp`

**关键 API**：

```cpp
UCLASS(Blueprintable, ClassGroup = "Cloud")
class YOURMODULE_API ACloudPaintActor : public AActor
{
    GENERATED_BODY()

public:
    /** 静态版：从 BP 子类的 "Draw to Cloud Paint Target" override 里直接调 */
    UFUNCTION(BlueprintCallable, Category = "Cloud Paint",
        meta = (DisplayName = "Draw Texture To Cloud Canvas (Static)"))
    static bool DrawTextureToCloudCanvas(
        UCanvas* Canvas,
        UTexture* SourceTexture,
        FVector TargetMapping,        // UDS 给的 world center+size
        int32 TargetRes,
        FVector2D SourceWorldCenter,  // 源贴图覆盖的世界中心 (cm)
        float SourceWorldSize,        // 源贴图覆盖的世界尺寸 (cm)
        bool bUseWorldMapping,        // false=铺满 canvas; true=按世界坐标对齐
        FLinearColor RenderColor,     // 通道蒙版: (0,0,1,1)=B; (1,0,0,1)=R; ...
        bool bCloudPaintingActive);   // UDS 全局开关 (从接口参数透传)
};
```

实现要点（见参考实现，约 100 行）：
- 校验 `Canvas / SourceTexture / TargetRes > 0`
- 计算 ScreenPosition / ScreenSize：
  - `bUseWorldMapping = false`：铺满 canvas
  - `bUseWorldMapping = true`：按 `TargetMapping = (centerX, centerY, fullSize)` 做世界对齐
- `Canvas->K2_DrawTexture(Texture, ScreenPos, ScreenSize, (0,0), (1,1), RenderColor, BLEND_Additive, 0, (0.5,0.5))`
- 可选：`GEngine->AddOnScreenDebugMessage` 屏幕打印 sampling 信息

### 4.2 模块依赖（Build.cs）

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine"  // UCanvas, UTexture, AActor 都在 Engine 模块
});
```

无额外依赖。

---

## 5. 编辑器集成步骤

### Step 1：复制 C++ 文件
把 `NASACloudPaintActor.h` 和 `.cpp` 复制到目标工程的一个 C++ 模块下，按需重命名类
（如 `ACloudPaintActor`）。修改 `YOURMODULE_API` 宏。

### Step 2：编译
- 项目根目录运行 `GenerateProjectFiles.bat`（Windows）/ `.command`（Mac）
- 编辑器内 Ctrl+Alt+F11 Live Coding，或 VS Build Solution

### Step 3：创建 BP 子类（绕开 BP picker bug）

**⚠️ 已知问题**：某些 UE 自定义版（AngelScript fork 等）的 Class Settings → Implemented
Interfaces → Add 下拉**过滤掉所有 BP interface**，搜不到 `UDS_CloudPaintActor_Interface`。

**绕开方案**：让 BP 子类**继承 UDS 自带的 `UDS_Cloud_Paint_Container`**，这个类已经实
现接口，子类自动继承。

操作：
1. Content Browser → 显示 Plugin Content（齿轮设置勾上）
2. 导航到 `Content/UltraDynamicSky/Blueprints/System/`
3. **右键 `UDS_Cloud_Paint_Container`** → **Create Child Blueprint Class**
4. 命名 `BP_MyCloudPaintActor` → 保存到 `Content/[your folder]/`

此时 BP 子类已自动实现：
- ✅ `UDS_CloudPaintActor_Interface` （让 UDS 调我们）
- ✅ `UDS_InterfaceActorArrayManagedActor` （让 Cloud Paint Actors Manager 自动发现）

### Step 4：Override 接口函数

1. 打开 `BP_MyCloudPaintActor`
2. 左侧 My Blueprint → **Functions** 分类 → 找继承的 `Draw to Cloud Paint Target`
3. 右键 → **Override** → 进入函数图
4. 在函数图里：
   - 添加 BP 变量 `SourceTexture`（类型 `Texture Object Reference`，**Instance Editable** 勾上）
   - 空白处右键 → 搜 `Draw Texture To Cloud Canvas` → 添加 static 节点
   - 连线：
     - Function Entry exec → static 节点 exec → Return Node
     - Function Entry 的 `Canvas` / `Target Mapping` / `Target Res` / `Cloud Painting Active` → static 节点同名 input
     - `SourceTexture` (Get) → static 节点 `Source Texture`
     - 其余 input（World Center / Size / UseWorldMapping）保留默认值或按需配置
     - **`Render Color`** 默认值改成 **R=0, G=0, B=1, A=1**（B 通道 = Full Coverage = 加云）
     - static 节点 `Return Value` → Return Node 的 `Success`
   - Function Entry 的 `Radial Storm Draw MID` / `Can Add Coverage` / `Can Subtract Coverage`：留空，本 actor 不用
5. Compile + Save

### Step 5：关卡部署

1. Content Browser 拖 `BP_MyCloudPaintActor` 到关卡（位置不影响 actor 视觉）
2. Outliner 选中该实例 → Details → **Source Texture** 字段赋值你的云图（任何 `Texture2D` 或 `RenderTarget2D`）
3. Outliner 选 `Ultra_Dynamic_Sky` actor → Details 改这些值：
   - **Cloud Coverage = 0**（关掉全局基准云，让我们的画作主角）
   - **Cloud Painting Active = ☑**
   - **Force Cloud Coverage Target Active = ☑**（强制每 tick 重画，让数据持续刷新）
4. Play → 天空应该出现源贴图驱动的云分布

---

## 6. 关键配置一览

| 设置项 | 值 | 位置 | 为何 |
|---|---|---|---|
| Cloud Coverage | **0** | Ultra_Dynamic_Sky Details | 全局基准云为 0 → 我们的 painted cloud 才是主角 |
| Cloud Painting Active | **☑** | Ultra_Dynamic_Sky Details | UDS 全局 painted cloud 开关 |
| Force Cloud Coverage Target Active | **☑** | Ultra_Dynamic_Sky Details | 强制每 tick 重画 RT，否则只在 snapped camera 变化时更新 |
| Sky Mode | **Volumetric Clouds** | Ultra_Dynamic_Sky Details | painted cloud 仅在 Volumetric 模式有效 |
| BP 函数节点 RenderColor | **(0, 0, 1, 1)** | BP 函数图 | B 通道 = 加云 |
| C++ BlendMode | **BLEND_Additive** | NASACloudPaintActor.cpp | 让多 actor 贡献可叠加，不互相擦 |
| 源贴图 Sampler | **Wrap / Clamp** | 源 Texture Asset Settings | 经纬度采样：X=Wrap (经度循环)，Y=Clamp (纬度不循环) |

---

## 7. 常见问题排查

| 现象 | 原因 | 解决 |
|---|---|---|
| 天空完全没变化 | UDS shader 没 sample 我们写的 RT | 检查 Cloud Painting Active = true，Sky Mode = Volumetric |
| 天空变全白 / 全没云 | 我们用 Opaque + 单通道值过强（如 R=255 over-bright） | RenderColor 数值范围 0-1，不要超 |
| 云出现但模式不对（高频噪声） | 源贴图是全球图，被压成 200km canvas | 改用局部窗口贴图，或 bUseWorldMapping=true + 调 SourceWorldSize |
| 云跟着相机走 | bUseWorldMapping=false 时 canvas 永远铺满源贴图，移动 = 相对滑动 | bUseWorldMapping=true + 设 SourceWorldCenter/Size |
| Print 只刷一次 | UDS 按需 update（snapped 相机变才触发） | 勾 Force Cloud Coverage Target Active 强制刷 |
| painter 工具画的 cell 在 PIE 里被擦 | 我们 Opaque 全画布覆盖 | 改 BlendMode = Additive |
| Add Interface picker 搜不到 UDS interface | UE 自定义版 BP picker bug | 走 Step 3：右键 UDS_Cloud_Paint_Container → Create Child Blueprint |

---

## 8. 调试设施

`NASACloudPaintActor.cpp` 和 `SatelliteCloudFeeder.cpp` 都内置了
`GEngine->AddOnScreenDebugMessage`：

```
[NASA] Map=(X,Y,Size) Res=N UseWorldMap=bool  ScreenPos=(X,Y) ScreenSize=(X,Y)  Color=(R,G,B,A)
```

- 黄色 = NASACloudPaintActor 实时采样
- 青色 = Feeder 实时经纬度（项目特定，可移除）
- Key=101/102 让消息覆盖更新不堆叠

迁移时如不需要 Feeder 部分，删 `SatelliteCloudFeeder.cpp` 内对应代码块即可。

---

## 9. 多 actor 合成扩展（未来）

要加新数据源（如闪电）：

1. 创建数据源 actor（生成自己的 RT，比如 `RT_Lightning_Mask_256`）
2. 右键 `UDS_Cloud_Paint_Container` → Create Child Blueprint → `BP_LightningPaintActor`
3. Override `Draw to Cloud Paint Target` → 调 `DrawTextureToCloudCanvas`，`SourceTexture` 接闪电 RT，`RenderColor` 选合适通道（如 G = Mid Coverage 模拟闪电照亮的局部云团）
4. spawn 到关卡

UDS Cloud Paint Actors Manager 自动发现 + 排序调用，多 actor Additive 合成。

---

## 10. 文件清单（迁移最小集）

```
[YourPlugin]/Source/[YourModule]/Public/Cloud/
    NASACloudPaintActor.h          ← 必复制（重命名按需）

[YourPlugin]/Source/[YourModule]/Private/Cloud/
    NASACloudPaintActor.cpp        ← 必复制

[YourPlugin]/Source/[YourModule]/
    [YourModule].Build.cs          ← 检查 Engine 在 PublicDependencyModuleNames（一般默认就有）

Content/[YourFolder]/
    BP_MyCloudPaintActor.uasset    ← 编辑器创建（继承 UDS_Cloud_Paint_Container）
```

**前置依赖**：
- UE5.x（任意子版本，已在 5.5 / 5.6 测试）
- Ultra Dynamic Sky 9.x（Marketplace 或自购）

**无需依赖**：
- Sphinx 项目的 EagleCloud 插件（除非你也需要 NASA fetch 管线）
- CesiumForUnreal
- GIS 处理模块

---

## 11. 参考 commit（Sphinx 项目实现）

| Commit | 内容 |
|---|---|
| `d04b3b6` | 初版 ANASACloudPaintActor + 反向工程笔记 |
| `1807bb4` | 加 static UFUNCTION 绕开 BP picker bug |
| `e10e002` | BlendMode 改 Additive + RT 通道编码文档 |
| `600ea90` | P0 里程碑：天空真实云分布可见 |
| `73d9f7c` | 屏幕 debug 信息（Feeder + NASA actor） |

`git show <SHA>` 看具体代码。

---

## 12. 已知待办（Sphinx 项目）

- [ ] **架构重构**：Feeder 拥有自有 RT，不再写 UDS 内部 RT（拆解"最后写入者赢"）
- [ ] **Cesium georef 集成**：相机经纬度 → 源贴图 UV 精确对齐
- [ ] **多源合成验证**：实际加一个 LightningPaintActor 验证 Additive 合成正确性
- [ ] **`Apply Effect to Cloud Coverage Value` 接口**：UDS 的另一个接口，按 3D 采样点逐点调（暂未用）

---

**维护者**：uds agent (小U)
**最后更新**：v1.5.0-WIP (2026-05-16)
**反向工程依据**：UDS 9.3A 的 `Ultra_Dynamic_Sky.Update Painted Cloud Coverage Target` 和 `UDS_Cloud_Paint_Container.Draw to Cloud Paint Target` BP 文本导出（Ctrl+A 复制为文本后解码）
