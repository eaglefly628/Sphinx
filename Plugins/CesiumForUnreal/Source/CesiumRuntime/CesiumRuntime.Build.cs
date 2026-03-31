// Copyright 2020-2024 CesiumGS, Inc. and Contributors

using UnrealBuildTool;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;

public class CesiumRuntime : ModuleRules
{
    /// <summary>
    /// 检测 cesium-native ThirdParty 是否已构建，未构建则报错提示。
    /// </summary>
    private void EnsureCesiumNativeBuilt(string Config)
    {
        string thirdPartyDir = Path.Combine(ModuleDirectory, "../ThirdParty");
        string includeDir = Path.Combine(thirdPartyDir, "include");

        // 已构建则跳过
        if (Directory.Exists(includeDir) && Directory.GetFiles(includeDir, "*.h", SearchOption.AllDirectories).Length > 0)
        {
            return;
        }

        // 未构建，给出清晰的操作指引
        string projectRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../.."));
        throw new BuildException(
            "\n====================================================================\n" +
            "  cesium-native ThirdParty not found!\n" +
            "  Please run BuildCesiumNative.bat once before building:\n\n" +
            "    cd " + projectRoot + "\n" +
            "    BuildCesiumNative.bat\n\n" +
            "  This only needs to be done once. Subsequent builds will skip it.\n" +
            "====================================================================");
    }

    public CesiumRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        // 检测 cesium-native 是否已构建
        EnsureCesiumNativeBuilt("Release");

        PublicIncludePaths.AddRange(
            new string[] {
                Path.Combine(ModuleDirectory, "../ThirdParty/include")
            }
        );

        PrivateIncludePaths.AddRange(
            new string[] {
              Path.Combine(GetModuleDirectory("Renderer"), "Private"),
              Path.Combine(GetModuleDirectory("Renderer"), "Internal")
            }
        );

        string platform;
        string libSearchPattern;
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            platform = "Windows-AMD64-";
            libSearchPattern = "*.lib";
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            platform = "Darwin-universal-";
            libSearchPattern = "lib*.a";

            PublicFrameworks.Add("SystemConfiguration");
        }
        else if (Target.Platform == UnrealTargetPlatform.Android)
        {
            platform = "Android-aarch64-";
            libSearchPattern = "lib*.a";
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            platform = "Linux-x86_64-";
            libSearchPattern = "lib*.a";
        }
        else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            platform = "iOS-ARM64-";
            libSearchPattern = "lib*.a";
        }
        else
        {
            throw new InvalidOperationException("Cesium for Unreal does not support this platform.");
        }

        string libPathBase = Path.Combine(ModuleDirectory, "../ThirdParty/lib/" + platform);
        string libPathDebug = libPathBase + "Debug";
        string libPathRelease = libPathBase + "Release";

        bool useDebug = false;
        if (Target.Configuration == UnrealTargetConfiguration.Debug || Target.Configuration == UnrealTargetConfiguration.DebugGame)
        {
            if (Directory.Exists(libPathDebug))
            {
                useDebug = true;
            }
        }

        string libPath = useDebug ? libPathDebug : libPathRelease;

        string[] allLibs = Directory.Exists(libPath) ? Directory.GetFiles(libPath, libSearchPattern) : new string[0];

        PublicAdditionalLibraries.AddRange(allLibs);
        // On Linux, cpp-httplib uses getaddrinfo_a, which is in the anl library.
        if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicSystemLibraries.Add("anl");
        }

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "RHI",
                "CoreUObject",
                "Engine",
                "MeshDescription",
                "StaticMeshDescription",
                "HTTP",
                "LevelSequence",
                "Projects",
                "RenderCore",
                "SunPosition",
                "DeveloperSettings",
                "UMG",
                "Renderer",
                "OpenSSL",
                "Json",
                "JsonUtilities",
                "Slate",
                "SlateCore",
                "Niagara",
                "ChaosCore"
            }
        );
        // TinyXML2 is provided on Linux and Windows so use it,
        // instead of the vcpkg version, to prevent conflicts with other
        // plugins that link with the Unreal version.
        if (Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDependencyModuleNames.Add("TinyXML2");
        }

        // Use UE's MikkTSpace on most platforms, except Android and iOS.
        // On those platforms, UE's isn't available, so we use our own.
        if (Target.Platform != UnrealTargetPlatform.Android && Target.Platform != UnrealTargetPlatform.IOS)
        {
            PrivateDependencyModuleNames.Add("MikkTSpace");
        }
        else
        {
            PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "../ThirdParty/include/mikktspace"));
        }

        PublicDefinitions.AddRange(
            new string[]
            {
                "SPDLOG_COMPILED_LIB",
                "LIBASYNC_STATIC",
                "GLM_FORCE_XYZW_ONLY",
                "GLM_FORCE_EXPLICIT_CTOR",
                "GLM_ENABLE_EXPERIMENTAL",
                "TIDY_STATIC",
                "URI_STATIC_BUILD",
                "SWL_VARIANT_NO_CONSTEXPR_EMPLACE",
                // Define to record the state of every tile, every frame, to a SQLite database.
                // The database will be found in [Project Dir]/Saved/CesiumDebugTileStateDatabase.
                // "CESIUM_DEBUG_TILE_STATES",
            }
        );

        PrivateDependencyModuleNames.Add("Chaos");

        if (Target.bBuildEditor == true)
        {
            PublicDependencyModuleNames.AddRange(
                new string[] {
                    "UnrealEd",
                    "WorldBrowser",
                    "ContentBrowser",
                    "MaterialEditor"
                }
            );
        }

        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
                // ... add any modules that your module loads dynamically here ...
            }
        );

#if UE_5_7_OR_LATER
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Off;
#else
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
        ShadowVariableWarningLevel = WarningLevel.Off;
#endif
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        CppStandard = CppStandardVersion.Cpp20;
        bEnableExceptions = true;
    }
}
