using UnrealBuildTool;

public class PrehistoricSurvivalLabEditorTarget : TargetRules
{
    public PrehistoricSurvivalLabEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("PrehistoricSurvivalLab");
    }
}
