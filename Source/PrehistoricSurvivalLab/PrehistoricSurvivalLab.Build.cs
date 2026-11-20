using UnrealBuildTool;

public class PrehistoricSurvivalLab : ModuleRules
{
    public PrehistoricSurvivalLab(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "AIModule",
            "NavigationSystem",
            "GameplayTasks",
            "UMG",
            "Slate",
            "SlateCore",
            "Landscape"
        });

        PrivateDependencyModuleNames.Add("RHI"); // Perf test links RHIGetGPUFrameCycles (r7b LINK_ERROR _GDynamicRHI)

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "Foliage" });
        }
    }
}
