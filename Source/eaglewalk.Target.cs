using UnrealBuildTool;

public class eaglewalkTarget : TargetRules
{
    public eaglewalkTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("eaglewalk");
    }
}
