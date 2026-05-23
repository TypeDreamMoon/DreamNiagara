using UnrealBuildTool;

public class DreamNiagaraSystemEditor : ModuleRules
{
	public DreamNiagaraSystemEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"DreamNiagara"
			});

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"AssetRegistry",
				"CoreUObject",
				"Engine",
				"Niagara",
				"NiagaraCore",
				"NiagaraEditor",
				"NiagaraEditorWidgets",
				"Projects",
				"UnrealEd"
			});
	}
}
