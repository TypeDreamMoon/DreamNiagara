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

	static bool TryStripKnownInternalRoot(FString& InOutAlias)
	{
		TArray<FString> Segments;
		InOutAlias.ParseIntoArray(Segments, TEXT("."), true);
		if (Segments.Num() <= 1)
		{
			return false;
		}

		const FString& RootSegment = Segments[0];
		if (!RootSegment.Equals(TEXT("Emitter"), ESearchCase::IgnoreCase)
			&& !RootSegment.Equals(TEXT("System"), ESearchCase::IgnoreCase)
			&& !RootSegment.Equals(TEXT("Solvers"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		Segments.RemoveAt(0, 1, EAllowShrinking::No);
		InOutAlias = FString::Join(Segments, TEXT("."));
		return true;
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

	static UNiagaraScript* ResolveDeprecationRecommendation(UNiagaraScript* Script)
	{
		TSet<UNiagaraScript*> VisitedScripts;
		while (Script && !VisitedScripts.Contains(Script))
		{
			VisitedScripts.Add(Script);
			FVersionedNiagaraScriptData* ScriptData = Script->GetLatestScriptData();
			if (!ScriptData || !ScriptData->bDeprecated || !ScriptData->DeprecationRecommendation)
			{
				return Script;
			}
			Script = ScriptData->DeprecationRecommendation;
		}

		return nullptr;
	}

	static bool IsDeprecatedModuleScript(const UNiagaraScript* Script)
	{
		const FVersionedNiagaraScriptData* ScriptData = Script ? Script->GetLatestScriptData() : nullptr;
		return ScriptData && ScriptData->bDeprecated;
	}

	static int32 GetAliasMatchScore(const FAssetData& AssetData, const FString& AliasSuffix)
	{
		FString AliasPath = AliasSuffix;
		TryStripKnownInternalRoot(AliasPath);
		AliasPath.ReplaceInline(TEXT("."), TEXT("/"));

		const FString ObjectPath = AssetData.GetObjectPathString();
		const FString AssetName = AssetData.AssetName.ToString();
		const FString AliasLeafName = FPackageName::GetShortName(AliasPath);

		int32 Score = 0;
		if (ObjectPath.Contains(TEXT("/V2/"), ESearchCase::IgnoreCase)
			|| ObjectPath.Contains(TEXT("/V3/"), ESearchCase::IgnoreCase))
		{
			Score -= 80;
		}
		if (ObjectPath.EndsWith(TEXT("/") + AliasPath + TEXT(".") + AssetName, ESearchCase::IgnoreCase))
		{
			Score -= 40;
		}
		if (AssetName.Equals(AliasLeafName, ESearchCase::IgnoreCase))
		{
			Score -= 20;
		}
		Score += ObjectPath.Len();
		return Score;
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

		Script = ResolveDeprecationRecommendation(Script);
		if (!Script)
		{
			OutError = FString::Printf(TEXT("Niagara module asset '%s' is deprecated and has no usable replacement."), *ObjectPath);
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
		FString AliasPath = AliasSuffix;
		const bool bUsesKnownInternalRoot = TryStripKnownInternalRoot(AliasPath);
		if (!bUsesKnownInternalRoot
			&& !UsageSegment.IsEmpty()
			&& !NormalizedObjectPath.Contains(ExpectedPathFragment, ESearchCase::IgnoreCase))
		{
			return false;
		}

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
			Script = ResolveDeprecationRecommendation(Script);
			if (!Script
				|| Script->GetUsage() != ENiagaraScriptUsage::Module
				|| IsDeprecatedModuleScript(Script)
				|| !IsSupportedModuleUsage(Script, StackUsage))
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
			return Left.GetObjectPathString() < Right.GetObjectPathString();
		});
		Matches.Sort([&Alias](const FAssetData& Left, const FAssetData& Right)
		{
			return GetAliasMatchScore(Left, Alias) < GetAliasMatchScore(Right, Alias);
		});

		UNiagaraScript* MatchedScript = Cast<UNiagaraScript>(Matches[0].GetAsset());
		MatchedScript = ResolveDeprecationRecommendation(MatchedScript);
		OutModule.Script = MatchedScript;
		OutModule.AssetData = MatchedScript ? FAssetData(MatchedScript) : Matches[0];
		if (!OutModule.Script)
		{
			OutError = FString::Printf(TEXT("Resolved module '%s' but failed to load it."), *Matches[0].GetObjectPathString());
			return false;
		}

		return true;
	}

	static bool IsDependencyEvaluatedInUsage(const FNiagaraModuleDependency& Dependency, const EDreamNiagaraStackUsage StackUsage)
	{
		ENiagaraModuleDependencyUsage DependencyUsage = ENiagaraModuleDependencyUsage::None;
		switch (StackUsage)
		{
		case EDreamNiagaraStackUsage::EmitterSpawn:
		case EDreamNiagaraStackUsage::ParticleSpawn:
		case EDreamNiagaraStackUsage::SystemSpawn:
			DependencyUsage = ENiagaraModuleDependencyUsage::Spawn;
			break;
		case EDreamNiagaraStackUsage::EmitterUpdate:
		case EDreamNiagaraStackUsage::ParticleUpdate:
		case EDreamNiagaraStackUsage::SystemUpdate:
			DependencyUsage = ENiagaraModuleDependencyUsage::Update;
			break;
		case EDreamNiagaraStackUsage::ParticleEvent:
			DependencyUsage = ENiagaraModuleDependencyUsage::Event;
			break;
		case EDreamNiagaraStackUsage::SimulationStage:
			DependencyUsage = ENiagaraModuleDependencyUsage::SimulationStage;
			break;
		default:
			return false;
		}

		return (Dependency.OnlyEvaluateInScriptUsage & (1 << static_cast<int32>(DependencyUsage))) != 0;
	}

	static bool ScriptProvidesDependency(
		const UNiagaraScript* Script,
		const FNiagaraModuleDependency& Dependency,
		const EDreamNiagaraStackUsage StackUsage)
	{
		const FVersionedNiagaraScriptData* ScriptData = Script ? Script->GetLatestScriptData() : nullptr;
		return ScriptData
			&& ScriptData->ProvidedDependencies.Contains(Dependency.Id)
			&& Dependency.IsVersionAllowed(ScriptData->Version)
			&& IsSupportedModuleUsage(Script, StackUsage);
	}

	static int32 GetDependencyProviderScore(const FAssetData& AssetData)
	{
		const FString ObjectPath = AssetData.GetObjectPathString();
		int32 Score = ObjectPath.Len();
		if (ObjectPath.Contains(TEXT("/Solvers/"), ESearchCase::IgnoreCase))
		{
			Score -= 60;
		}
		if (ObjectPath.Contains(TEXT("/V2/"), ESearchCase::IgnoreCase)
			|| ObjectPath.Contains(TEXT("/V3/"), ESearchCase::IgnoreCase))
		{
			Score -= 40;
		}
		return Score;
	}

	bool ResolveDependencyProvider(
		const FNiagaraModuleDependency& Dependency,
		const EDreamNiagaraStackUsage SourceStackUsage,
		FResolvedDependencyProvider& OutProvider,
		FString& OutError)
	{
		if (Dependency.Id.IsNone())
		{
			OutError = TEXT("Dependency id cannot be empty.");
			return false;
		}

		if (!IsDependencyEvaluatedInUsage(Dependency, SourceStackUsage))
		{
			OutError.Reset();
			return false;
		}

		TArray<EDreamNiagaraStackUsage> CandidateUsages;
		CandidateUsages.Add(SourceStackUsage);
		if (Dependency.ScriptConstraint == ENiagaraModuleDependencyScriptConstraint::AllScripts)
		{
			if (SourceStackUsage == EDreamNiagaraStackUsage::ParticleUpdate)
			{
				CandidateUsages.Add(EDreamNiagaraStackUsage::ParticleSpawn);
			}
			else if (SourceStackUsage == EDreamNiagaraStackUsage::ParticleSpawn)
			{
				CandidateUsages.Add(EDreamNiagaraStackUsage::ParticleUpdate);
			}
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		if (!AssetRegistryModule.Get().IsSearchAllAssets())
		{
			AssetRegistryModule.Get().ScanPathsSynchronous({ TEXT("/Niagara"), TEXT("/Game") }, true);
		}

		TArray<FAssetData> ScriptAssets;
		AssetRegistryModule.Get().GetAssetsByClass(UNiagaraScript::StaticClass()->GetClassPathName(), ScriptAssets, true);

		struct FCandidate
		{
			FAssetData AssetData;
			UNiagaraScript* Script = nullptr;
			EDreamNiagaraStackUsage StackUsage = EDreamNiagaraStackUsage::Unknown;
		};

		TArray<FCandidate> Candidates;
		for (const FAssetData& AssetData : ScriptAssets)
		{
			UNiagaraScript* Script = Cast<UNiagaraScript>(AssetData.GetAsset());
			Script = ResolveDeprecationRecommendation(Script);
			if (!Script || Script->GetUsage() != ENiagaraScriptUsage::Module || IsDeprecatedModuleScript(Script))
			{
				continue;
			}

			for (const EDreamNiagaraStackUsage CandidateUsage : CandidateUsages)
			{
				if (ScriptProvidesDependency(Script, Dependency, CandidateUsage))
				{
					FCandidate Candidate;
					Candidate.AssetData = FAssetData(Script);
					Candidate.Script = Script;
					Candidate.StackUsage = CandidateUsage;
					Candidates.Add(Candidate);
					break;
				}
			}
		}

		if (Candidates.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Could not find module providing dependency '%s'."), *Dependency.Id.ToString());
			return false;
		}

		Candidates.Sort([](const FCandidate& Left, const FCandidate& Right)
		{
			return Left.AssetData.GetObjectPathString() < Right.AssetData.GetObjectPathString();
		});
		Candidates.Sort([](const FCandidate& Left, const FCandidate& Right)
		{
			return GetDependencyProviderScore(Left.AssetData) < GetDependencyProviderScore(Right.AssetData);
		});

		OutProvider.Module.AssetData = Candidates[0].AssetData;
		OutProvider.Module.Script = Candidates[0].Script;
		OutProvider.StackUsage = Candidates[0].StackUsage;
		return true;
	}
}
