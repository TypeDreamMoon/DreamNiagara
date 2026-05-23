#include "DreamNiagaraSystemGeneratorPrivate.h"

#include "DreamNiagaraModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NiagaraScript.h"
#include "ObjectTools.h"

namespace UE::DreamNiagara::SystemEditor::Private
{
	void FGenerationContext::AddWarning(const FString& Warning)
	{
		if (!Warning.IsEmpty())
		{
			Warnings.Add(Warning);
			UE_LOG(LogDreamNiagara, Warning, TEXT("%s"), *Warning);
		}
	}

	static FString TrimPathSlashes(FString Value)
	{
		Value.TrimStartAndEndInline();
		Value.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Value.EndsWith(TEXT("/")))
		{
			Value.LeftChopInline(1, EAllowShrinking::No);
		}
		return Value;
	}

	bool ResolveAssetDestination(
		const FDreamNiagaraSystem& Definition,
		FString& OutPackageName,
		FString& OutAssetName,
		FString& OutObjectPath,
		FString& OutError)
	{
		FString Root = TrimPathSlashes(Definition.Root);
		if (Root.IsEmpty())
		{
			OutError = TEXT("System destination root cannot be empty.");
			return false;
		}

		if (!Root.StartsWith(TEXT("/")))
		{
			Root = TEXT("/Game/") + Root;
		}

		OutAssetName = ObjectTools::SanitizeObjectName(Definition.Name);
		if (OutAssetName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("System name '%s' is not a valid asset name."), *Definition.Name);
			return false;
		}

		const FString RootLeafName = FPackageName::GetShortName(Root);
		if (RootLeafName.Equals(OutAssetName, ESearchCase::CaseSensitive)
			|| (Root.EndsWith(TEXT("/") + OutAssetName, ESearchCase::CaseSensitive) && !Root.EndsWith(TEXT("/"), ESearchCase::CaseSensitive)))
		{
			OutPackageName = Root;
		}
		else
		{
			OutPackageName = FPaths::Combine(Root, OutAssetName);
		}

		OutObjectPath = FString::Printf(TEXT("%s.%s"), *OutPackageName, *OutAssetName);

		FText PathError;
		if (!FPackageName::IsValidObjectPath(OutObjectPath, &PathError))
		{
			OutError = FString::Printf(TEXT("Invalid Niagara system asset path '%s': %s"), *OutObjectPath, *PathError.ToString());
			return false;
		}

		return true;
	}

	EDreamNiagaraStackUsage ParseStackUsage(const FString& StackName)
	{
		FString Normalized = StackName;
		Normalized.ReplaceInline(TEXT("."), TEXT(""));
		Normalized.ReplaceInline(TEXT("_"), TEXT(""));
		Normalized.ReplaceInline(TEXT("-"), TEXT(""));

		if (Normalized.Equals(TEXT("SystemSpawn"), ESearchCase::IgnoreCase))
		{
			return EDreamNiagaraStackUsage::SystemSpawn;
		}
		if (Normalized.Equals(TEXT("SystemUpdate"), ESearchCase::IgnoreCase))
		{
			return EDreamNiagaraStackUsage::SystemUpdate;
		}
		if (Normalized.Equals(TEXT("EmitterSpawn"), ESearchCase::IgnoreCase))
		{
			return EDreamNiagaraStackUsage::EmitterSpawn;
		}
		if (Normalized.Equals(TEXT("EmitterUpdate"), ESearchCase::IgnoreCase))
		{
			return EDreamNiagaraStackUsage::EmitterUpdate;
		}
		if (Normalized.Equals(TEXT("ParticleSpawn"), ESearchCase::IgnoreCase)
			|| Normalized.Equals(TEXT("Spawn"), ESearchCase::IgnoreCase))
		{
			return EDreamNiagaraStackUsage::ParticleSpawn;
		}
		if (Normalized.Equals(TEXT("ParticleUpdate"), ESearchCase::IgnoreCase)
			|| Normalized.Equals(TEXT("Update"), ESearchCase::IgnoreCase))
		{
			return EDreamNiagaraStackUsage::ParticleUpdate;
		}
		if (Normalized.Equals(TEXT("ParticleEvent"), ESearchCase::IgnoreCase)
			|| Normalized.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
		{
			return EDreamNiagaraStackUsage::ParticleEvent;
		}
		if (Normalized.Equals(TEXT("SimulationStage"), ESearchCase::IgnoreCase))
		{
			return EDreamNiagaraStackUsage::SimulationStage;
		}

		return EDreamNiagaraStackUsage::Unknown;
	}

	ENiagaraScriptUsage ToNiagaraScriptUsage(const EDreamNiagaraStackUsage Usage)
	{
		switch (Usage)
		{
		case EDreamNiagaraStackUsage::SystemSpawn:
			return ENiagaraScriptUsage::SystemSpawnScript;
		case EDreamNiagaraStackUsage::SystemUpdate:
			return ENiagaraScriptUsage::SystemUpdateScript;
		case EDreamNiagaraStackUsage::EmitterSpawn:
			return ENiagaraScriptUsage::EmitterSpawnScript;
		case EDreamNiagaraStackUsage::EmitterUpdate:
			return ENiagaraScriptUsage::EmitterUpdateScript;
		case EDreamNiagaraStackUsage::ParticleSpawn:
			return ENiagaraScriptUsage::ParticleSpawnScript;
		case EDreamNiagaraStackUsage::ParticleUpdate:
			return ENiagaraScriptUsage::ParticleUpdateScript;
		case EDreamNiagaraStackUsage::ParticleEvent:
			return ENiagaraScriptUsage::ParticleEventScript;
		case EDreamNiagaraStackUsage::SimulationStage:
			return ENiagaraScriptUsage::ParticleSimulationStageScript;
		default:
			return ENiagaraScriptUsage::Module;
		}
	}

	FString ToInternalUsageSegment(const EDreamNiagaraStackUsage Usage)
	{
		switch (Usage)
		{
		case EDreamNiagaraStackUsage::SystemSpawn:
		case EDreamNiagaraStackUsage::EmitterSpawn:
		case EDreamNiagaraStackUsage::ParticleSpawn:
			return TEXT("Spawn");
		case EDreamNiagaraStackUsage::SystemUpdate:
		case EDreamNiagaraStackUsage::EmitterUpdate:
		case EDreamNiagaraStackUsage::ParticleUpdate:
			return TEXT("Update");
		case EDreamNiagaraStackUsage::ParticleEvent:
			return TEXT("Event");
		case EDreamNiagaraStackUsage::SimulationStage:
			return TEXT("SimulationStage");
		default:
			return TEXT("");
		}
	}

	static FString NormalizeModuleId(FString ModuleId)
	{
		ModuleId.TrimStartAndEndInline();
		ModuleId = ModuleId.TrimQuotes();
		ModuleId.ReplaceInline(TEXT("\\"), TEXT("/"));
		return ModuleId;
	}

	static FString BuildObjectPathFromPackagePath(const FString& PackagePath)
	{
		if (PackagePath.Contains(TEXT(".")))
		{
			return PackagePath;
		}

		const FString AssetName = FPackageName::GetShortName(PackagePath);
		return AssetName.IsEmpty() ? PackagePath : FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName);
	}

	static bool IsSupportedModuleUsage(const UNiagaraScript* Script, const EDreamNiagaraStackUsage StackUsage)
	{
		if (!Script || StackUsage == EDreamNiagaraStackUsage::Unknown)
		{
			return false;
		}

		const FVersionedNiagaraScriptData* ScriptData = Script->GetLatestScriptData();
		if (!ScriptData)
		{
			return false;
		}

		return UNiagaraScript::IsSupportedUsageContextForBitmask(
			ScriptData->ModuleUsageBitmask,
			ToNiagaraScriptUsage(StackUsage),
			true);
	}

	static bool TryResolveDirectModulePath(
		const FString& ModuleId,
		EDreamNiagaraStackUsage StackUsage,
		FResolvedModule& OutModule,
		FString& OutError)
	{
		if (!ModuleId.StartsWith(TEXT("/")))
		{
			return false;
		}

		const FString ObjectPath = BuildObjectPathFromPackagePath(ModuleId);
		UNiagaraScript* Script = Cast<UNiagaraScript>(StaticLoadObject(UNiagaraScript::StaticClass(), nullptr, *ObjectPath));
		if (!Script)
		{
			OutError = FString::Printf(TEXT("Could not load Niagara module asset '%s'."), *ObjectPath);
			return true;
		}

		if (Script->GetUsage() != ENiagaraScriptUsage::Module)
		{
			OutError = FString::Printf(TEXT("Niagara script '%s' is not a module script."), *ObjectPath);
			return true;
		}

		if (!IsSupportedModuleUsage(Script, StackUsage))
		{
			OutError = FString::Printf(TEXT("Module '%s' does not support stack usage '%s'."), *ObjectPath, *ToInternalUsageSegment(StackUsage));
			return true;
		}

		OutModule.Script = Script;
		OutModule.AssetData = FAssetData(Script);
		return true;
	}

	static bool ModuleMatchesInternalAlias(
		const FAssetData& AssetData,
		const FString& AliasSuffix,
		const EDreamNiagaraStackUsage StackUsage)
	{
		const FString ObjectPath = AssetData.GetObjectPathString();
		FString NormalizedObjectPath = ObjectPath;
		NormalizedObjectPath.ReplaceInline(TEXT("\\"), TEXT("/"));

		const FString UsageSegment = ToInternalUsageSegment(StackUsage);
		const FString ExpectedPathFragment = TEXT("/Modules/") + UsageSegment + TEXT("/");
		if (!UsageSegment.IsEmpty() && !NormalizedObjectPath.Contains(ExpectedPathFragment, ESearchCase::IgnoreCase))
		{
			return false;
		}

		FString AliasPath = AliasSuffix;
		AliasPath.ReplaceInline(TEXT("."), TEXT("/"));
		const FString AssetName = AssetData.AssetName.ToString();
		const FString AliasLeafName = FPackageName::GetShortName(AliasPath);
		return NormalizedObjectPath.EndsWith(TEXT("/") + AliasPath + TEXT(".") + AssetName, ESearchCase::IgnoreCase)
			|| NormalizedObjectPath.EndsWith(TEXT("/") + AliasPath + TEXT(".") + FPackageName::GetShortName(AliasPath), ESearchCase::IgnoreCase)
			|| (AssetName.Equals(AliasLeafName, ESearchCase::IgnoreCase)
				&& NormalizedObjectPath.Contains(TEXT("/") + AliasLeafName + TEXT("."), ESearchCase::IgnoreCase));
	}

	bool ResolveModule(
		const FString& InModuleId,
		const EDreamNiagaraStackUsage StackUsage,
		FResolvedModule& OutModule,
		FString& OutError)
	{
		const FString ModuleId = NormalizeModuleId(InModuleId);
		if (ModuleId.IsEmpty())
		{
			OutError = TEXT("Module id cannot be empty.");
			return false;
		}

		if (TryResolveDirectModulePath(ModuleId, StackUsage, OutModule, OutError))
		{
			return OutError.IsEmpty() && OutModule.Script != nullptr;
		}

		FString Alias = ModuleId;
		if (Alias.StartsWith(TEXT("Internal."), ESearchCase::IgnoreCase))
		{
			Alias.RightChopInline(9, EAllowShrinking::No);
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		if (!AssetRegistryModule.Get().IsSearchAllAssets())
		{
			AssetRegistryModule.Get().ScanPathsSynchronous({ TEXT("/Niagara"), TEXT("/Game") }, true);
		}

		TArray<FAssetData> ScriptAssets;
		AssetRegistryModule.Get().GetAssetsByClass(UNiagaraScript::StaticClass()->GetClassPathName(), ScriptAssets, true);

		TArray<FAssetData> Matches;
		for (const FAssetData& AssetData : ScriptAssets)
		{
			UNiagaraScript* Script = Cast<UNiagaraScript>(AssetData.GetAsset());
			if (!Script || Script->GetUsage() != ENiagaraScriptUsage::Module || !IsSupportedModuleUsage(Script, StackUsage))
			{
				continue;
			}

			if (ModuleMatchesInternalAlias(AssetData, Alias, StackUsage))
			{
				Matches.Add(AssetData);
			}
		}

		if (Matches.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Could not resolve Niagara module id '%s' for stack usage '%s'."), *ModuleId, *ToInternalUsageSegment(StackUsage));
			return false;
		}

		Matches.Sort([](const FAssetData& Left, const FAssetData& Right)
		{
			return Left.GetObjectPathString().Len() < Right.GetObjectPathString().Len();
		});

		OutModule.AssetData = Matches[0];
		OutModule.Script = Cast<UNiagaraScript>(Matches[0].GetAsset());
		if (!OutModule.Script)
		{
			OutError = FString::Printf(TEXT("Resolved module '%s' but failed to load it."), *Matches[0].GetObjectPathString());
			return false;
		}

		return true;
	}
}
