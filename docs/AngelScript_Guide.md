# UE AngelScript 语法指南

写 `.as` 前必读。UE AngelScript ≠ 原版 AngelScript，也 ≠ C++。

## 项目中实际踩过的坑

### AS-1: `default:` 块不支持（UE 5.7）
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

### AS-2: `GetName()` 返回 FName 不是 FString
```angelscript
// ❌ 类型不匹配
FString ClassName = A.GetClass().GetName();

// ✅ 显式转换
FString ClassName = A.GetClass().GetName().ToString();
```
> commit de7b988: WeatherBridge.as 编译错误

### AS-3: `GetAllActorsOfClass` 必须传 ActorClass 参数
```angelscript
// ❌ 省略 ActorClass 参数
Gameplay::GetAllActorsOfClass(OutActors);

// ✅ 必须传 TSubclassOf<AActor> 第一个参数
Gameplay::GetAllActorsOfClass(AActor::StaticClass(), OutActors);
```
> commit b282d3d: 签名是 `(TSubclassOf<AActor>, TArray<AActor>&)`，不能省略

### AS-4: `FMath` 命名空间不存在，用 `Math`
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

### AS-5: `SetTimer` / `ClearTimer` 接受 FString，不是 FName
```angelscript
// ❌ n"name" 是 FName
System::SetTimer(this, n"MyFunc", 0.1f, true);
System::ClearTimer(this, n"MyFunc");

// ✅ 用 FString
System::SetTimer(this, "MyFunc", 0.1f, true);
System::ClearTimer(this, "MyFunc");
```
> commit 00ce7d5: Timer 函数签名是 `(UObject, FString, float, bool)`

### AS-6: 函数参数不支持 `const ref` 自定义 struct
```angelscript
// ❌ 编译器不识别 const ref
void Foo(const FMyStruct& S) { ... }

// ✅ 值传递
void Foo(FMyStruct S) { ... }
```
> commit 00ce7d5: SpawnSingleInstance / SamplePointsInPolygon 参数改值传递

### AS-7: 整数除法精度警告
```angelscript
// ❌ 整数除法会截断，编译器警告
if (progress / 10 > prev / 10) ...

// ✅ 显式用 IntegerDivisionTrunc
if (Math::IntegerDivisionTrunc(progress, 10) > Math::IntegerDivisionTrunc(prev, 10)) ...
```
> commit 00ce7d5: 避免隐式整数截断

## C++ 侧实际踩过的坑（写插件 C++ 时注意）

### CPP-1: 废弃 API
```cpp
// ❌ UE 新版已移除
FGenericPlatformHttp::UrlEncode(...)   // → FPlatformHttp::UrlEncode(...)
Context->SourceComponent.IsValid()     // → Context->SourceComponent.Get() != nullptr
RootObject->TryGetStringField(...)     // → HasField() + GetStringField()
```

### CPP-2: 变量名遮蔽类成员
```cpp
// ❌ ActorLabel 遮蔽 AActor::ActorLabel
FString ActorLabel = FString::Printf(...);

// ✅ 换名
FString PolyLabel = FString::Printf(...);
```

### CPP-3: 异步回调捕获 `this` 导致 use-after-free
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

### CPP-4: Build.cs API 不存在
```csharp
// ❌ PluginsDirectory 在定制引擎不存在
Directory.Exists(Path.Combine(PluginsDirectory, "CesiumForUnreal"))

// ✅ 用 PluginDirectory（当前插件目录）向上找
string ProjectPluginsDir = Path.GetFullPath(Path.Combine(PluginDirectory, ".."));
```

## 文件目录

```
Script/
├── EagleWalkTest.as        ← 环境测试（已有）
├── Weather/                ← UDS 桥接
├── GIS/                    ← GIS 相关脚本
└── Editor/                 ← 编辑器工具
```
