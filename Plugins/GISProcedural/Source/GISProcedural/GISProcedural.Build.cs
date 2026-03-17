// GISProcedural.Build.cs
using UnrealBuildTool;

public class GISProcedural : ModuleRules
{
    public GISProcedural(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Json",
            "JsonUtilities",
            "PCG",              // PCG 框架
            "GeometryCore",     // 计算几何（多边形运算）
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "GeometryAlgorithms",  // 三角剖分、面域提取等
            "Projects",
            "ImageWrapper",        // PNG heightmap 解码
        });

        // 不依赖 ArcGIS SDK — 保持插件独立性
        // ArcGIS 的坐标转换在项目层面做，不在插件里
    }
}
