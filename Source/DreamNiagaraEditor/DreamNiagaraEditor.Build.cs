using UnrealBuildTool;

public class DreamNiagaraEditor : ModuleRules
{
	public DreamNiagaraEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"DirectoryWatcher",
				"DreamNiagara",
				"DreamNiagaraSystemEditor",
				"Engine",
				"Projects",
				"Slate",
				"SlateCore",
				"UnrealEd"
			});
	}
}
