using UnrealBuildTool;

public class PrehistoricSurvivalLabTarget : TargetRules
{
    public PrehistoricSurvivalLabTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("PrehistoricSurvivalLab");
    }
}
