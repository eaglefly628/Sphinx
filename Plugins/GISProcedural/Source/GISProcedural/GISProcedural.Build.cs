// GISProcedural.Build.cs
using UnrealBuildTool;
using System.IO;

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
            "HTTP",                // ArcGIS REST API 调用
        });

        // Cesium 软依赖：仅在 CesiumRuntime 模块存在时启用
        // PluginDirectory 指向当前插件目录，向上两级到项目 Plugins/
        string ProjectPluginsDir = Path.GetFullPath(Path.Combine(PluginDirectory, ".."));
        bool bHasCesium = Directory.Exists(Path.Combine(ProjectPluginsDir, "CesiumForUnreal")) ||
                          Directory.Exists(Path.Combine(EngineDirectory, "Plugins", "Marketplace", "CesiumForUnreal"));

        if (bHasCesium)
        {
            PrivateDependencyModuleNames.Add("CesiumRuntime");
            PublicDefinitions.Add("WITH_CESIUM=1");
        }
        else
        {
            PublicDefinitions.Add("WITH_CESIUM=0");
        }
    }
}
