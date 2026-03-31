// Copyright 2020-2024 CesiumGS, Inc. and Contributors

using UnrealBuildTool;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;

public class CesiumRuntime : ModuleRules
{
    /// <summary>
    /// 自动构建 cesium-native：检测 ThirdParty 是否存在，不存在则调用 CMake 构建。
    /// </summary>
    private void EnsureCesiumNativeBuilt(string Config)
    {
        string thirdPartyDir = Path.Combine(ModuleDirectory, "../ThirdParty");
        string includeDir = Path.Combine(thirdPartyDir, "include");
        string externDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../extern"));
        string buildDir = Path.Combine(externDir, "build");

        // 已经构建过则跳过
        if (Directory.Exists(includeDir) && Directory.GetFiles(includeDir, "*.h", SearchOption.AllDirectories).Length > 0)
        {
            return;
        }

        Console.WriteLine("====================================================================");
        Console.WriteLine("  cesium-native ThirdParty not found, auto-building from source...");
        Console.WriteLine("  extern: " + externDir);
        Console.WriteLine("  config: " + Config);
        Console.WriteLine("====================================================================");

        // 查找 cmake 完整路径（UBT 的 PATH 可能不含 cmake）
        string cmake = FindCMake();
        if (string.IsNullOrEmpty(cmake))
        {
            throw new BuildException(
                "Cannot auto-build cesium-native: cmake.exe not found. " +
                "Install CMake 3.15+ and ensure it's in PATH, or run BuildCesiumNative.bat manually.");
        }
        Console.WriteLine("  cmake: " + cmake);

        // 查找 UE 引擎路径
        string engineRoot = FindEngineRoot();
        if (string.IsNullOrEmpty(engineRoot))
        {
            throw new BuildException(
                "Cannot auto-build cesium-native: UE engine root not found. " +
                "Set UE_ENGINE_DIR environment variable, or run BuildCesiumNative.bat manually.");
        }

        // CMake configure
        RunProcess(cmake,
            string.Format("-B \"{0}\" -S \"{1}\" -A x64 -DUNREAL_ENGINE_ROOT=\"{2}\"",
                buildDir, externDir, engineRoot));

        // CMake build
        RunProcess(cmake,
            string.Format("--build \"{0}\" --config {1} --parallel", buildDir, Config));

        // CMake install → Source/ThirdParty/
        RunProcess(cmake,
            string.Format("--install \"{0}\" --config {1}", buildDir, Config));

        Console.WriteLine("====================================================================");
        Console.WriteLine("  cesium-native build complete.");
        Console.WriteLine("====================================================================");
    }

    private string FindEngineRoot()
    {
        // 1. 环境变量 UE_ENGINE_DIR
        string envDir = Environment.GetEnvironmentVariable("UE_ENGINE_DIR");
        if (!string.IsNullOrEmpty(envDir) && Directory.Exists(Path.Combine(envDir, "Engine", "Build")))
        {
            return envDir;
        }

        // 2. 从项目目录推断（项目同级 UnrealEngine/）
        string projectRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../.."));
        string siblingEngine = Path.Combine(projectRoot, "..", "UnrealEngine");
        if (Directory.Exists(Path.Combine(siblingEngine, "Engine", "Build")))
        {
            return Path.GetFullPath(siblingEngine);
        }

        // 3. 从 EngineDirectory 属性（UBT 内置）
        if (!string.IsNullOrEmpty(EngineDirectory) && Directory.Exists(Path.Combine(EngineDirectory, "Build")))
        {
            return Path.GetFullPath(Path.Combine(EngineDirectory, ".."));
        }

        return null;
    }

    private static string FindCMake()
    {
        // 1. 常见安装路径
        string[] candidates = new string[]
        {
            @"C:\Program Files\CMake\bin\cmake.exe",
            @"C:\Program Files (x86)\CMake\bin\cmake.exe",
            @"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
            @"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
            @"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        };
        foreach (var c in candidates)
        {
            if (File.Exists(c)) return c;
        }

        // 2. 从 PATH 环境变量搜索
        string pathEnv = Environment.GetEnvironmentVariable("PATH") ?? "";
        foreach (var dir in pathEnv.Split(Path.PathSeparator))
        {
            string full = Path.Combine(dir.Trim(), "cmake.exe");
            if (File.Exists(full)) return full;
        }

        return null;
    }

    private void RunProcess(string fileName, string arguments)
    {
        Console.WriteLine("> " + fileName + " " + arguments);
        var psi = new ProcessStartInfo
        {
            FileName = fileName,
            Arguments = arguments,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            WorkingDirectory = Path.GetTempPath()
        };

        using (var proc = Process.Start(psi))
        {
            // 异步读取避免死锁
            string stdout = proc.StandardOutput.ReadToEnd();
            string stderr = proc.StandardError.ReadToEnd();
            proc.WaitForExit();

            if (!string.IsNullOrEmpty(stdout)) Console.Write(stdout);
            if (!string.IsNullOrEmpty(stderr)) Console.Error.Write(stderr);

            if (proc.ExitCode != 0)
            {
                throw new BuildException(
                    string.Format("cesium-native build failed: {0} {1}\nExit code: {2}",
                        fileName, arguments, proc.ExitCode));
            }
        }
    }

    public CesiumRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        // 自动构建 cesium-native（首次编译时触发）
        string cmakeConfig = (Target.Configuration == UnrealTargetConfiguration.Debug ||
                              Target.Configuration == UnrealTargetConfiguration.DebugGame) ? "Debug" : "Release";
        EnsureCesiumNativeBuilt(cmakeConfig);

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
