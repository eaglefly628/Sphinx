# UE AngelScript 语法指南

写 `.as` 前必读。UE AngelScript ≠ 原版 AngelScript，也 ≠ C++。

> 官方文档: https://angelscript.hazelight.se/
> API 参考: https://angelscript.hazelight.se/api

---

# Part 1: 官方语法参考（系统性知识）

以下内容提取自 Hazelight 官方文档，覆盖 UE AngelScript 核心语法。
**写代码前先读这部分，不要靠猜。**

---

## 1. 与 C++ 的关键差异

### 1.1 没有指针，只有对象引用
UObject 类型变量自动是引用，用 `.` 而非 `->`：
```angelscript
// ✅ AngelScript
void TeleportActor(AActor ActorRef, AActor TargetActor)
{
    FTransform T = TargetActor.GetActorTransform();
    ActorRef.SetActorTransform(T);
}

// ❌ 不存在
AActor* Ptr = nullptr;  // 没有指针语法
Ptr->DoSomething();     // 没有箭头操作符
```

### 1.2 不需要 UPROPERTY 防 GC
C++ 中必须 UPROPERTY 才能防 GC，AngelScript 中**所有对象引用自动注册到 GC**。
UPROPERTY 的作用仅仅是暴露给编辑器/蓝图。

### 1.3 float 是 64 位 double
```angelscript
float Val = 1.0;          // 64-bit double！
float32 ValSingle = 1.f;  // 显式 32-bit
float64 ValDouble = 1.0;  // 显式 64-bit
```
**注意**: 如果需要与 C++ 32位浮点交互，显式用 `float32`。

### 1.4 默认可见性不同
- `UPROPERTY()` 默认 = `EditAnywhere + BlueprintReadWrite`（C++ 中无默认）
- `UFUNCTION()` 默认 = `BlueprintCallable`（C++ 中必须显式声明）

### 1.5 函数参数默认只读
struct 参数内部实现为 `const &`，不能修改：
```angelscript
// 参数是只读的（内部 const ref）
void ReadOnly(FMyStruct S)
{
    // S.Value = 5.0;  // ❌ 编译错误
}

// 要修改必须显式 &
void Modify(FMyStruct& S)
{
    S.Value = 5.0;  // ✅
}

// 输出参数用 &out
void Output(FMyStruct&out Result, bool&out bSuccess)
{
    Result.Value = 1.0;
    bSuccess = true;
}
```

### 1.6 没有构造函数，用 default 语句 + 内联赋值
```angelscript
class AMyActor : AActor
{
    // 内联默认值
    UPROPERTY()
    float Speed = 300.0;

    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent SceneRoot;

    UPROPERTY(DefaultComponent, Attach = SceneRoot)
    UCapsuleComponent Capsule;

    // default 语句设置组件属性
    default Capsule.CapsuleHalfHeight = 88.0;
    default Capsule.CapsuleRadius = 40.0;
    default Capsule.bGenerateOverlapEvents = true;
}
```

---

## 2. 类与继承

### 2.1 Actor 类
```angelscript
class AMyActor : AActor
{
    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Print("Hello from AngelScript!");
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
    }
}
```

### 2.2 Component 类
```angelscript
class UMyComponent : UActorComponent
{
    UPROPERTY()
    float MyValue = 0.0;

    // 组件也能 override BeginPlay / Tick
    UFUNCTION(BlueprintOverride)
    void BeginPlay() { }
}
```

### 2.3 Struct（值类型）
```angelscript
struct FMyData
{
    UPROPERTY()
    float Number = 4.0;

    UPROPERTY()
    FString Name = "Default";

    // struct 不能有 UFUNCTION，但可以有普通方法
    float GetDoubled() const
    {
        return Number * 2.0;
    }
};
```

---

## 3. UPROPERTY 说明符

```angelscript
// 默认行为（EditAnywhere + BlueprintReadWrite）
UPROPERTY()
float BasicValue = 1.0;

// 常用说明符
UPROPERTY(EditDefaultsOnly)           // 只在蓝图默认值中编辑，实例不可改
UPROPERTY(EditInstanceOnly)           // 只在关卡实例上编辑
UPROPERTY(VisibleAnywhere)            // 可看不可改
UPROPERTY(NotEditable)                // 完全不可编辑
UPROPERTY(BlueprintReadOnly)          // 蓝图只读
UPROPERTY(BlueprintHidden)            // 蓝图不可见
UPROPERTY(Category = "My Category")   // 分类分组
UPROPERTY(Transient)                  // 不序列化

// 组件专用
UPROPERTY(DefaultComponent)                          // 自动创建
UPROPERTY(DefaultComponent, RootComponent)            // 设为根组件
UPROPERTY(DefaultComponent, Attach = SceneRoot)       // 挂到指定组件
UPROPERTY(DefaultComponent, Attach = Mesh, AttachSocket = RightHand)  // 挂到骨骼插槽
UPROPERTY(OverrideComponent = SceneRoot)              // 子类替换父类组件
```

---

## 4. UFUNCTION 说明符

```angelscript
// 默认 BlueprintCallable
UFUNCTION()
void MyFunc() { }

// 重写引擎事件（BeginPlay, Tick, ConstructionScript 等）
UFUNCTION(BlueprintOverride)
void BeginPlay() { }

// 允许蓝图子类重写
UFUNCTION(BlueprintEvent)
void OnCustomEvent() { }

// 蓝图纯函数（无副作用）
UFUNCTION(BlueprintPure)
float GetSpeed() const { return Speed; }

// 不暴露给蓝图
UFUNCTION(NotBlueprintCallable)
void InternalFunc() { }

// 访问控制
UFUNCTION()
private void PrivateFunc() { }

UFUNCTION()
protected void ProtectedFunc() { }
```

---

## 5. 组件系统

### 5.1 声明与挂载
```angelscript
class AMyCharacter : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent SceneRoot;

    UPROPERTY(DefaultComponent, Attach = SceneRoot)
    USkeletalMeshComponent CharacterMesh;

    // 挂到骨骼插槽
    UPROPERTY(DefaultComponent, Attach = CharacterMesh, AttachSocket = RightHand)
    UStaticMeshComponent WeaponMesh;

    // 子组件可以挂到子组件
    UPROPERTY(DefaultComponent, Attach = WeaponMesh)
    UStaticMeshComponent ScopeMesh;

    // default 设置组件属性
    default CharacterMesh.RelativeLocation = FVector(0.0, 0.0, 50.0);
    default ScopeMesh.bHiddenInGame = true;
}
```

### 5.2 获取组件
```angelscript
// 获取已有组件（返回 nullptr 如果不存在）
USkeletalMeshComponent SkelComp = USkeletalMeshComponent::Get(SomeActor);
USkeletalMeshComponent Named = USkeletalMeshComponent::Get(SomeActor, n"WeaponMesh");

// 获取或创建
UMyComponent Comp = UMyComponent::GetOrCreate(SomeActor);

// 动态创建新组件
UStaticMeshComponent NewComp = UStaticMeshComponent::Create(SomeActor);
NewComp.AttachToComponent(SomeActor.RootComponent);
```

### 5.3 获取所有组件
```angelscript
TArray<UStaticMeshComponent> MeshComps;
SomeActor.GetComponentsByClass(MeshComps);
for (UStaticMeshComponent Comp : MeshComps)
{
    Print(f"Mesh: {Comp.Name}");
}
```

### 5.4 子类覆盖父类组件
```angelscript
class ABaseActor : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent SceneRoot;
}

class AChildActor : ABaseActor
{
    // 替换父类的 SceneRoot 为 StaticMesh
    UPROPERTY(OverrideComponent = SceneRoot)
    UStaticMeshComponent RootMesh;
}
```

---

## 6. Spawn 与 ConstructionScript

### 6.1 Spawn Actor
```angelscript
// 直接 Spawn
AMyActor Spawned = SpawnActor(AMyActor, SpawnLocation, SpawnRotation);

// Spawn 蓝图化的 Actor（用 TSubclassOf）
UPROPERTY()
TSubclassOf<AMyActor> ActorClass;

AMyActor Spawned = SpawnActor(ActorClass, Location, Rotation);
```

### 6.2 ConstructionScript
```angelscript
UFUNCTION(BlueprintOverride)
void ConstructionScript()
{
    for (int i = 0; i < MeshCount; ++i)
    {
        UStaticMeshComponent MeshComp = UStaticMeshComponent::Create(this);
        MeshComp.SetStaticMesh(MeshAsset);
    }
}
```

---

## 7. 常用函数库（命名空间）

C++ 蓝图函数库自动映射为命名空间：

| C++ 类 | AngelScript 命名空间 |
|---------|---------------------|
| `UKismetMathLibrary` | `Math::` |
| `UGameplayStatics` | `Gameplay::` |
| `UKismetSystemLibrary` | `System::` |
| `UNiagaraFunctionLibrary` | `Niagara::` |
| `UWidgetBlueprintLibrary` | `Widget::` |

**命名规则**: 自动去掉 `U`, `Kismet`, `Blueprint`, `Statics`, `Library`, `FunctionLibrary` 前后缀。

```angelscript
// ✅ 用命名空间
float R = Math::RandRange(0.0, 1.0);
System::SetTimer(this, n"OnTimer", 2.0, false);

// ❌ C++ 风格（不存在）
float R = FMath::RandRange(0.0, 1.0);
```

---

## 8. FName 字面量

`n""` 前缀创建编译期 FName，避免运行时查表：
```angelscript
FName MyName = n"WeaponSocket";

// 委托绑定
Delegate.BindUFunction(this, n"OnCallback");

// Map 键
TMap<FName, int> Values;
Values.Add(n"Health", 100);
```

---

## 9. 格式化字符串（f-string）

`f""` 前缀 + `{}` 插值：
```angelscript
Print(f"Actor {GetName()} at {ActorLocation}");

// 表达式 + 值
Print(f"{DeltaSeconds =}");       // 输出: DeltaSeconds = 0.016

// 浮点精度
Print(f"{Value :.3}");            // 3位小数

// 整数补零
Print(f"{Count :010d}");          // 补零到10位

// 十六进制 / 二进制
Print(f"{Flags :#x}");            // 0x1A
Print(f"{Bits :b}");              // 11010

// 对齐
Print(f"{Name :>40}");            // 右对齐40字符
Print(f"{Name :_<40}");           // 左对齐，下划线填充

// 枚举
Print(f"{MyEnum :n}");            // 只输出名字，不带类型前缀

// 转义花括号
Print(f"Literal brace: {{");      // 输出: Literal brace: {
```

---

## 10. 委托与事件

### 10.1 委托（单播）
```angelscript
// 声明（全局作用域）
delegate void FOnDamage(AActor Target, float Amount);

// 使用
UPROPERTY()
FOnDamage OnDamageDelegate;

// 绑定（两种方式）
OnDamageDelegate.BindUFunction(this, n"HandleDamage");
// 或
OnDamageDelegate = FOnDamage(this, n"HandleDamage");

// 执行
OnDamageDelegate.ExecuteIfBound(Target, 50.0);

// 被绑定的函数必须是 UFUNCTION
UFUNCTION()
void HandleDamage(AActor Target, float Amount) { }
```

### 10.2 事件（多播）
```angelscript
// 声明（全局作用域）
event void FOnCountChanged(int NewCount);

// 使用
UPROPERTY()
FOnCountChanged OnCountChanged;

// 添加监听（可多个）
OnCountChanged.AddUFunction(this, n"OnCountUpdate");
OnCountChanged.AddUFunction(Other, n"OtherHandler");

// 广播
OnCountChanged.Broadcast(42);

// 被绑定的函数必须是 UFUNCTION
UFUNCTION()
void OnCountUpdate(int NewCount) { }
```

---

## 11. Mixin 方法（扩展方法）

给现有类型添加方法，无需修改原始定义：
```angelscript
// 扩展 AActor
mixin void TeleportTo(AActor Self, FVector Location)
{
    Self.ActorLocation = Location;
}

// 使用
SomeActor.TeleportTo(FVector(0, 0, 100));

// 扩展 struct（必须用 & 才能修改）
mixin void Reset(FVector& V)
{
    V = FVector(0, 0, 0);
}

FVector Pos = FVector(1, 2, 3);
Pos.Reset();  // Pos 变为 (0,0,0)
```

---

## 12. Property 访问器

用 `property` 关键字让函数表现为属性：
```angelscript
FVector GetRotatedOffset() const property
{
    return ActorRotation.RotateVector(FVector(0, 1, 1));
}

// C++ 绑定的属性自动可用：
// Actor.ActorLocation 等价于 GetActorLocation()
```

---

## 13. Gameplay Tags

```angelscript
// 标签自动绑定到 GameplayTags 命名空间
// 点号变下划线
FGameplayTag Tag = GameplayTags::UI_Action_Escape;  // = "UI.Action.Escape"
```

---

## 14. 获取所有 Actor

```angelscript
// ✅ 正确用法 — 模板化，自动推断类型
TArray<ANiagaraActor> Actors;
GetAllActorsOfClass(Actors);

// ✅ 也可以显式传 class（非模板用法）
TArray<AActor> Actors;
Gameplay::GetAllActorsOfClass(AActor::StaticClass(), Actors);
```

---

## 15. 编辑器脚本

```angelscript
// 编辑器专用代码用 #if EDITOR 包裹
#if EDITOR
    SetActorLabel("Debug Label");
#endif

// 其他预处理宏
#if EDITORONLY_DATA    // 编辑器专用数据
#if RELEASE            // Shipping/Test 构建
#if TEST               // Debug/Development/Test 构建

// Editor/ 目录下的脚本自动在打包时排除
// 测试打包兼容性: 启动参数加 -as-simulate-cooked
```

---

## 16. Timer

```angelscript
// 设置定时器
System::SetTimer(this, n"OnTimerFired", 2.0, bLooping = false);

// 回调必须是 UFUNCTION
UFUNCTION()
private void OnTimerFired()
{
    Print("Timer fired!");
}

// 清除定时器
System::ClearTimer(this, n"OnTimerFired");
```

---

## 17. 快速语法对照表

| C++ | AngelScript | 说明 |
|-----|-------------|------|
| `AActor*` | `AActor` | 无指针 |
| `Actor->Func()` | `Actor.Func()` | 点号访问 |
| `FMath::` | `Math::` | 数学命名空间 |
| `UGameplayStatics::` | `Gameplay::` | 游戏命名空间 |
| `UKismetSystemLibrary::` | `System::` | 系统命名空间 |
| `float` (32-bit) | `float` (64-bit!) | float 默认是 double |
| `float` (32-bit) | `float32` | 显式 32 位 |
| `DECLARE_DYNAMIC_DELEGATE()` | `delegate void F...()` | 委托声明 |
| `DECLARE_DYNAMIC_MULTICAST_DELEGATE()` | `event void F...()` | 多播事件声明 |
| `const FMyStruct&` | 默认行为 | 参数默认只读 |
| `FMyStruct&` | `FMyStruct& Param` | 可修改引用 |
| `FMyStruct& OutParam` | `FMyStruct&out Param` | 蓝图输出引脚 |
| `TEXT("name")` | `n"name"` | FName 字面量 |
| `FString::Printf()` | `f"..."` | 格式化字符串 |
| 构造函数 | `default Comp.Prop = Val;` | 组件默认值 |
| `CreateDefaultSubobject<>()` | `UPROPERTY(DefaultComponent)` | 创建默认组件 |
| `SetupAttachment()` | `Attach = ParentComp` | 组件挂载 |
| `ObjectInitializer.SetDefaultSubobjectClass()` | `UPROPERTY(OverrideComponent = X)` | 子类替换组件 |

---

# Part 2: 项目踩坑记录

以下是本项目实际开发中遇到的问题，作为补充参考。

---

## AS-1: `default:` 块不支持（UE 5.7）
```angelscript
// ❌ 编译报错
class AMyActor : AActor
{
    default:
    bReplicates = false;
}

// ✅ 直接去掉 default 块
class AMyActor : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent Root;
}
```
> commit dd5e57f: EagleWalkTest.as 因 default: 块编译失败

## AS-2: `GetName()` 返回 FName 不是 FString
```angelscript
// ❌ 类型不匹配
FString ClassName = A.GetClass().GetName();

// ✅ 显式转换
FString ClassName = A.GetClass().GetName().ToString();
```
> commit de7b988: WeatherBridge.as 编译错误

## AS-3: `GetAllActorsOfClass` 必须传 ActorClass 参数
```angelscript
// ❌ 省略 ActorClass 参数
Gameplay::GetAllActorsOfClass(OutActors);

// ✅ 必须传 TSubclassOf<AActor> 第一个参数
Gameplay::GetAllActorsOfClass(AActor::StaticClass(), OutActors);
```
> commit b282d3d: 签名是 `(TSubclassOf<AActor>, TArray<AActor>&)`，不能省略

## AS-4: `FMath` 命名空间不存在，用 `Math`
```angelscript
// ❌ C++ 风格
float s = FMath::RandRange(0.0f, 1.0f);
int n = FMath::Min(a, b);
float rad = FMath::DegreesToRadians(angle);

// ✅ AngelScript 用 Math 命名空间
float s = Math::RandRange(0.0f, 1.0f);
int n = Math::Min(a, b);
float rad = Math::DegreesToRadians(angle);
```
> commit 00ce7d5: PCGDemoCreator.as 全部 FMath → Math

## AS-5: `SetTimer` / `ClearTimer` 接受 FString，不是 FName
```angelscript
// ❌ n"name" 是 FName
System::SetTimer(this, n"MyFunc", 0.1f, true);
System::ClearTimer(this, n"MyFunc");

// ✅ 用 FString
System::SetTimer(this, "MyFunc", 0.1f, true);
System::ClearTimer(this, "MyFunc");
```
> commit 00ce7d5: Timer 函数签名是 `(UObject, FString, float, bool)`

## AS-6: 函数参数不支持 `const ref` 自定义 struct
```angelscript
// ❌ 编译器不识别 const ref
void Foo(const FMyStruct& S) { ... }

// ✅ 值传递
void Foo(FMyStruct S) { ... }
```
> commit 00ce7d5: SpawnSingleInstance / SamplePointsInPolygon 参数改值传递

## AS-7: 整数除法精度警告
```angelscript
// ❌ 整数除法会截断，编译器警告
if (progress / 10 > prev / 10) ...

// ✅ 显式用 IntegerDivisionTrunc
if (Math::IntegerDivisionTrunc(progress, 10) > Math::IntegerDivisionTrunc(prev, 10)) ...
```
> commit 00ce7d5: 避免隐式整数截断

---

# Part 3: C++ 侧踩坑记录（写插件 C++ 时注意）

## CPP-1: 废弃 API
```cpp
// ❌ UE 新版已移除
FGenericPlatformHttp::UrlEncode(...)   // → FPlatformHttp::UrlEncode(...)
Context->SourceComponent.IsValid()     // → Context->SourceComponent.Get() != nullptr
RootObject->TryGetStringField(...)     // → HasField() + GetStringField()
```

## CPP-2: 变量名遮蔽类成员
```cpp
// ❌ ActorLabel 遮蔽 AActor::ActorLabel
FString ActorLabel = FString::Printf(...);

// ✅ 换名
FString PolyLabel = FString::Printf(...);
```

## CPP-3: 异步回调捕获 `this` 导致 use-after-free
```cpp
// ❌ 对象可能被 GC
StreamableManager.RequestAsyncLoad(Path,
    FStreamableDelegate::CreateLambda([this, TileID]() { ... }));

// ✅ 用 WeakPtr
TWeakObjectPtr<UMyClass> WeakThis(this);
StreamableManager.RequestAsyncLoad(Path,
    FStreamableDelegate::CreateLambda([WeakThis, TileID]() {
        UMyClass* Self = WeakThis.Get();
        if (!Self) return;
        Self->DoStuff();
    }));
```

## CPP-4: Build.cs API 不存在
```csharp
// ❌ PluginsDirectory 在定制引擎不存在
Directory.Exists(Path.Combine(PluginsDirectory, "CesiumForUnreal"))

// ✅ 用 PluginDirectory（当前插件目录）向上找
string ProjectPluginsDir = Path.GetFullPath(Path.Combine(PluginDirectory, ".."));
```

---

# 附录: 文件目录

```
Script/
├── EagleWalkTest.as        ← 环境测试（已有）
├── Weather/                ← UDS 桥接
├── GIS/                    ← GIS 相关脚本
└── Editor/                 ← 编辑器工具（打包时自动排除）
```
