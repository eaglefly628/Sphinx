using UnrealBuildTool;

public class EagleWalkMassAI : ModuleRules
{
	public EagleWalkMassAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"RenderCore",
			"RHI",
		});
	}
}
