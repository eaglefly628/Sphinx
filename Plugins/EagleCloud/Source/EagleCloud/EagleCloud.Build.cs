// EagleCloud.Build.cs
using UnrealBuildTool;

public class EagleCloud : ModuleRules
{
    public EagleCloud(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "RenderCore",   // FTextureRenderTarget access
            "RHI",          // render target formats
        });
    }
}
