using UnrealBuildTool;

public class Endless : ModuleRules
{
    public Endless(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "EnhancedInput", "InputCore",
            "NavigationSystem", "AIModule", "UMG", "Slate", "SlateCore", "AnimGraphRuntime"
        });
    }
}
