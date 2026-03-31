# UE AngelScript 语法规范

UE AngelScript ≠ 原版 AngelScript，是类 C++ 的脚本语言，写 `.as` 前必读此文档。

## 基础示例

```angelscript
class AMyActor : AActor
{
    default:
    bReplicates = false;

    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent Root;

    UPROPERTY(DefaultComponent, Attach = Root)
    UStaticMeshComponent Mesh;

    UPROPERTY(BlueprintReadWrite, Category = "GIS")
    float WeatherIntensity = 0.0f;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Print("Hello from AngelScript!");
    }

    UFUNCTION(BlueprintCallable, Category = "GIS")
    void SetWeather(float Intensity)
    {
        WeatherIntensity = Intensity;
    }
}
```

## 常见错误清单

| C++ 写法（错） | AngelScript 写法（对） |
|----------------|----------------------|
| `auto x = ...` | 写明确类型 `FVector x = ...` |
| `#include "xxx.h"` | 无 include，所有类型自动可见 |
| `UE_LOG(LogGIS, Log, TEXT("..."))` | `Print("...")` 或 `Log("...")` |
| `virtual void Foo() override` | `UFUNCTION(BlueprintOverride) void Foo()` |
| `this->Member` | 直接访问 `Member` |
| `FString::Printf(TEXT("x=%d"), x)` | `"x=" + x` 字符串拼接 |
| `.h` + `.cpp` 分文件 | 单个 `.as` 文件包含完整类 |
| `UPROPERTY()` 在函数体内 | 只在类成员层级写 |

## 关键语法点

- 继承用 `:` 不用 `public`
- `default:` 块设置 CDO 属性（冒号结尾，不是大括号）
- `Cast<T>(Obj)` 和 C++ 一致
- `n"FuncName"` 是 FName 字面量
- `System::SetTimer(this, n"MyFunc", 1.0f, true)` 定时器
- 反射调用 BP-only 插件：`Obj.CallFunction(n"FuncName", Args...)`

## 文件目录

```
Script/
├── EagleWalkTest.as        ← 环境测试（已有）
├── Weather/                ← UDS 桥接
├── GIS/                    ← GIS 相关脚本
└── Editor/                 ← 编辑器工具
```
