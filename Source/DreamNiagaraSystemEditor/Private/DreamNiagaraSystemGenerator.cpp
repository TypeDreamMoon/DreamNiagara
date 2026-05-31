#include "DreamNiagaraSystemGenerator.h"

#include "DreamNiagaraModule.h"
#include "DreamNiagaraParser.h"
#include "DreamNiagaraSystemGeneratorPrivate.h"
#include "DreamNiagaraSystemLiteralParser.h"
#include "DreamNiagaraSystemValueApplier.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraGPUSortInfo.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceCurveBase.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectGlobals.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

namespace UE::DreamNiagara::SystemEditor
{
	namespace
	{
		struct FAddedModuleInfo
		{
			Private::EDreamNiagaraStackUsage StackUsage = Private::EDreamNiagaraStackUsage::Unknown;
			UNiagaraScript* Script = nullptr;
			int32 Order = INDEX_NONE;
		};

		struct FPendingDependency
		{
			FNiagaraModuleDependency Dependency;
			Private::EDreamNiagaraStackUsage SourceStackUsage = Private::EDreamNiagaraStackUsage::Unknown;
			int32 SourceOrder = INDEX_NONE;
			FString SourceModuleName;
		};

		struct FStackBuildState
		{
			TMap<Private::EDreamNiagaraStackUsage, UNiagaraNodeOutput*> OutputNodes;
			TMap<Private::EDreamNiagaraStackUsage, UNiagaraScript*> TargetScripts;
			TArray<FAddedModuleInfo> AddedModules;
			TArray<FPendingDependency> PendingPostDependencies;
			TSet<FString> ResolvingDependencies;
			int32 NextOrder = 0;
		};

		UNiagaraNodeOutput* FindOutputNodeForStack(UNiagaraEmitter& Emitter, const Private::EDreamNiagaraStackUsage Usage)
		{
			FVersionedNiagaraEmitterData* EmitterData = Emitter.GetLatestEmitterData();
			if (!EmitterData)
			{
				return nullptr;
			}

			UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterData->GraphSource);
			if (!Source || !Source->NodeGraph)
			{
				return nullptr;
			}

			FGuid UsageId;
			switch (Usage)
			{
			case Private::EDreamNiagaraStackUsage::EmitterSpawn:
				UsageId = EmitterData->EmitterSpawnScriptProps.Script ? EmitterData->EmitterSpawnScriptProps.Script->GetUsageId() : FGuid();
				break;
			case Private::EDreamNiagaraStackUsage::EmitterUpdate:
				UsageId = EmitterData->EmitterUpdateScriptProps.Script ? EmitterData->EmitterUpdateScriptProps.Script->GetUsageId() : FGuid();
				break;
			case Private::EDreamNiagaraStackUsage::ParticleSpawn:
				UsageId = EmitterData->SpawnScriptProps.Script ? EmitterData->SpawnScriptProps.Script->GetUsageId() : FGuid();
				break;
			case Private::EDreamNiagaraStackUsage::ParticleUpdate:
				UsageId = EmitterData->UpdateScriptProps.Script ? EmitterData->UpdateScriptProps.Script->GetUsageId() : FGuid();
				break;
			default:
				break;
			}

			return Source->NodeGraph->FindEquivalentOutputNode(Private::ToNiagaraScriptUsage(Usage), UsageId);
		}

		UNiagaraScript* FindTargetScriptForStack(UNiagaraEmitter& Emitter, const Private::EDreamNiagaraStackUsage Usage)
		{
			FVersionedNiagaraEmitterData* EmitterData = Emitter.GetLatestEmitterData();
			if (!EmitterData)
			{
				return nullptr;
			}

			switch (Usage)
			{
			case Private::EDreamNiagaraStackUsage::EmitterSpawn:
				return EmitterData->EmitterSpawnScriptProps.Script;
			case Private::EDreamNiagaraStackUsage::EmitterUpdate:
				return EmitterData->EmitterUpdateScriptProps.Script;
			case Private::EDreamNiagaraStackUsage::ParticleSpawn:
				return EmitterData->SpawnScriptProps.Script;
			case Private::EDreamNiagaraStackUsage::ParticleUpdate:
				return EmitterData->UpdateScriptProps.Script;
			default:
				return nullptr;
			}
		}

		bool ResolveStackTarget(
			UNiagaraEmitter& Emitter,
			FStackBuildState& State,
			const Private::EDreamNiagaraStackUsage StackUsage,
			UNiagaraNodeOutput*& OutOutputNode,
			UNiagaraScript*& OutTargetScript,
			FString& OutError)
		{
			if (UNiagaraNodeOutput** CachedOutputNode = State.OutputNodes.Find(StackUsage))
			{
				OutOutputNode = *CachedOutputNode;
			}
			else
			{
				OutOutputNode = FindOutputNodeForStack(Emitter, StackUsage);
				if (!OutOutputNode)
				{
					OutError = FString::Printf(TEXT("Could not find Niagara output node for stack usage '%s' in emitter '%s'."), *Private::ToInternalUsageSegment(StackUsage), *Emitter.GetUniqueEmitterName());
					return false;
				}
				State.OutputNodes.Add(StackUsage, OutOutputNode);
			}

			if (UNiagaraScript** CachedTargetScript = State.TargetScripts.Find(StackUsage))
			{
				OutTargetScript = *CachedTargetScript;
			}
			else
			{
				OutTargetScript = FindTargetScriptForStack(Emitter, StackUsage);
				if (!OutTargetScript)
				{
					OutError = FString::Printf(TEXT("Could not resolve target Niagara script for stack usage '%s' in emitter '%s'."), *Private::ToInternalUsageSegment(StackUsage), *Emitter.GetUniqueEmitterName());
					return false;
				}
				State.TargetScripts.Add(StackUsage, OutTargetScript);
			}

			return true;
		}

		bool IsDependencyEvaluatedInUsage(const FNiagaraModuleDependency& Dependency, const Private::EDreamNiagaraStackUsage StackUsage)
		{
			ENiagaraModuleDependencyUsage DependencyUsage = ENiagaraModuleDependencyUsage::None;
			switch (StackUsage)
			{
			case Private::EDreamNiagaraStackUsage::EmitterSpawn:
			case Private::EDreamNiagaraStackUsage::ParticleSpawn:
			case Private::EDreamNiagaraStackUsage::SystemSpawn:
				DependencyUsage = ENiagaraModuleDependencyUsage::Spawn;
				break;
			case Private::EDreamNiagaraStackUsage::EmitterUpdate:
			case Private::EDreamNiagaraStackUsage::ParticleUpdate:
			case Private::EDreamNiagaraStackUsage::SystemUpdate:
				DependencyUsage = ENiagaraModuleDependencyUsage::Update;
				break;
			case Private::EDreamNiagaraStackUsage::ParticleEvent:
				DependencyUsage = ENiagaraModuleDependencyUsage::Event;
				break;
			case Private::EDreamNiagaraStackUsage::SimulationStage:
				DependencyUsage = ENiagaraModuleDependencyUsage::SimulationStage;
				break;
			default:
				return false;
			}

			return (Dependency.OnlyEvaluateInScriptUsage & (1 << static_cast<int32>(DependencyUsage))) != 0;
		}

		bool ModuleProvidesDependency(
			const FAddedModuleInfo& ModuleInfo,
			const FNiagaraModuleDependency& Dependency,
			const Private::EDreamNiagaraStackUsage SourceStackUsage,
			const int32 SourceOrder)
		{
			const FVersionedNiagaraScriptData* ScriptData = ModuleInfo.Script ? ModuleInfo.Script->GetLatestScriptData() : nullptr;
			if (!ScriptData || !ScriptData->ProvidedDependencies.Contains(Dependency.Id) || !Dependency.IsVersionAllowed(ScriptData->Version))
			{
				return false;
			}

			if (Dependency.ScriptConstraint == ENiagaraModuleDependencyScriptConstraint::SameScript
				&& ModuleInfo.StackUsage != SourceStackUsage)
			{
				return false;
			}

			if (Dependency.Type == ENiagaraModuleDependencyType::PreDependency)
			{
				return ModuleInfo.Order < SourceOrder;
			}
			if (Dependency.Type == ENiagaraModuleDependencyType::PostDependency)
			{
				return ModuleInfo.Order > SourceOrder;
			}

			return false;
		}

		bool IsDependencySatisfied(
			const FStackBuildState& State,
			const FNiagaraModuleDependency& Dependency,
			const Private::EDreamNiagaraStackUsage SourceStackUsage,
			const int32 SourceOrder)
		{
			for (const FAddedModuleInfo& ModuleInfo : State.AddedModules)
			{
				if (ModuleProvidesDependency(ModuleInfo, Dependency, SourceStackUsage, SourceOrder))
				{
					return true;
				}
			}

			return false;
		}

		FString MakeDependencyResolutionKey(
			const FNiagaraModuleDependency& Dependency,
			const Private::EDreamNiagaraStackUsage SourceStackUsage,
			const int32 SourceOrder)
		{
			return FString::Printf(
				TEXT("%s|%d|%d|%d"),
				*Dependency.Id.ToString(),
				static_cast<int32>(Dependency.Type),
				static_cast<int32>(SourceStackUsage),
				SourceOrder);
		}

		bool AddResolvedModuleWithDependencies(
			Private::FGenerationContext& Context,
			UNiagaraSystem& System,
			UNiagaraEmitter& Emitter,
			FStackBuildState& State,
			const Private::FResolvedModule& ResolvedModule,
			const Private::EDreamNiagaraStackUsage StackUsage,
			const TArray<FDreamNiagaraAssignment>* Inputs,
			const FString& SourceLabel,
			FString& OutError)
		{
			if (!ResolvedModule.Script)
			{
				Context.AddWarning(FString::Printf(TEXT("Skipping module '%s' because it resolved to a null Niagara script."), *SourceLabel));
				return true;
			}

			FVersionedNiagaraScriptData* ScriptData = ResolvedModule.Script->GetLatestScriptData();
			if (!ScriptData)
			{
				Context.AddWarning(FString::Printf(TEXT("Skipping module '%s' because its Niagara script data could not be loaded."), *SourceLabel));
				return true;
			}

			for (const FNiagaraModuleDependency& Dependency : ScriptData->RequiredDependencies)
			{
				if (!IsDependencyEvaluatedInUsage(Dependency, StackUsage)
					|| Dependency.Type != ENiagaraModuleDependencyType::PreDependency)
				{
					continue;
				}

				if (IsDependencySatisfied(State, Dependency, StackUsage, State.NextOrder))
				{
					continue;
				}

				const FString DependencyKey = MakeDependencyResolutionKey(Dependency, StackUsage, State.NextOrder);
				if (State.ResolvingDependencies.Contains(DependencyKey))
				{
					Context.AddWarning(FString::Printf(
						TEXT("Skipping recursive pre-dependency '%s' while adding '%s'."),
						*Dependency.Id.ToString(),
						*SourceLabel));
					continue;
				}

				State.ResolvingDependencies.Add(DependencyKey);
				Private::FResolvedDependencyProvider Provider;
				FString ResolveError;
				if (Private::ResolveDependencyProvider(Dependency, StackUsage, Provider, ResolveError))
				{
					UE_LOG(LogDreamNiagara, Display,
						TEXT("Auto-adding pre-dependency '%s' via module '%s' before '%s'."),
						*Dependency.Id.ToString(),
						*GetPathNameSafe(Provider.Module.Script),
						*SourceLabel);
					if (!AddResolvedModuleWithDependencies(Context, System, Emitter, State, Provider.Module, Provider.StackUsage, nullptr, Provider.Module.Script->GetName(), OutError))
					{
						State.ResolvingDependencies.Remove(DependencyKey);
						return false;
					}
				}
				else if (!ResolveError.IsEmpty())
				{
					Context.AddWarning(FString::Printf(
						TEXT("Could not auto-add pre-dependency '%s' for module '%s': %s"),
						*Dependency.Id.ToString(),
						*SourceLabel,
						*ResolveError));
				}
				State.ResolvingDependencies.Remove(DependencyKey);
			}

			UNiagaraNodeOutput* OutputNode = nullptr;
			UNiagaraScript* TargetScript = nullptr;
			if (!ResolveStackTarget(Emitter, State, StackUsage, OutputNode, TargetScript, OutError))
			{
				return false;
			}

			UNiagaraNodeFunctionCall* ModuleNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
				ResolvedModule.Script,
				*OutputNode);
			if (!ModuleNode)
			{
				Context.AddWarning(FString::Printf(
					TEXT("Failed to add module '%s' to stack usage '%s' in emitter '%s'."),
					*SourceLabel,
					*Private::ToInternalUsageSegment(StackUsage),
					*Emitter.GetUniqueEmitterName()));
				return true;
			}

			const int32 AddedOrder = State.NextOrder++;
			FAddedModuleInfo AddedModule;
			AddedModule.StackUsage = StackUsage;
			AddedModule.Script = ResolvedModule.Script;
			AddedModule.Order = AddedOrder;
			State.AddedModules.Add(AddedModule);

			if (Inputs && !Inputs->IsEmpty())
			{
				const FVersionedNiagaraEmitter VersionedEmitter(&Emitter, Emitter.GetExposedVersion().VersionGuid);
				Private::FModuleInputApplier::ApplyInputs(
					Context,
					Emitter.GetUniqueEmitterName(),
					System,
					VersionedEmitter,
					*TargetScript,
					*ModuleNode,
					*Inputs);
			}

			for (const FNiagaraModuleDependency& Dependency : ScriptData->RequiredDependencies)
			{
				if (!IsDependencyEvaluatedInUsage(Dependency, StackUsage)
					|| Dependency.Type != ENiagaraModuleDependencyType::PostDependency)
				{
					continue;
				}

				if (IsDependencySatisfied(State, Dependency, StackUsage, AddedOrder))
				{
					continue;
				}

				FPendingDependency PendingDependency;
				PendingDependency.Dependency = Dependency;
				PendingDependency.SourceStackUsage = StackUsage;
				PendingDependency.SourceOrder = AddedOrder;
				PendingDependency.SourceModuleName = SourceLabel;
				State.PendingPostDependencies.Add(PendingDependency);
			}

			return true;
		}

		bool ResolvePendingPostDependencies(
			Private::FGenerationContext& Context,
			UNiagaraSystem& System,
			UNiagaraEmitter& Emitter,
			FStackBuildState& State,
			FString& OutError)
		{
			for (int32 Index = 0; Index < State.PendingPostDependencies.Num(); ++Index)
			{
				const FPendingDependency PendingDependency = State.PendingPostDependencies[Index];
				if (IsDependencySatisfied(State, PendingDependency.Dependency, PendingDependency.SourceStackUsage, PendingDependency.SourceOrder))
				{
					continue;
				}

				const FString DependencyKey = MakeDependencyResolutionKey(
					PendingDependency.Dependency,
					PendingDependency.SourceStackUsage,
					PendingDependency.SourceOrder);
				if (State.ResolvingDependencies.Contains(DependencyKey))
				{
					Context.AddWarning(FString::Printf(
						TEXT("Skipping recursive post-dependency '%s' while resolving '%s'."),
						*PendingDependency.Dependency.Id.ToString(),
						*PendingDependency.SourceModuleName));
					continue;
				}

				State.ResolvingDependencies.Add(DependencyKey);
				Private::FResolvedDependencyProvider Provider;
				FString ResolveError;
				if (Private::ResolveDependencyProvider(PendingDependency.Dependency, PendingDependency.SourceStackUsage, Provider, ResolveError))
				{
					UE_LOG(LogDreamNiagara, Display,
						TEXT("Auto-adding post-dependency '%s' via module '%s' after '%s'."),
						*PendingDependency.Dependency.Id.ToString(),
						*GetPathNameSafe(Provider.Module.Script),
						*PendingDependency.SourceModuleName);
					if (!AddResolvedModuleWithDependencies(Context, System, Emitter, State, Provider.Module, Provider.StackUsage, nullptr, Provider.Module.Script->GetName(), OutError))
					{
						State.ResolvingDependencies.Remove(DependencyKey);
						return false;
					}
				}
				else if (!ResolveError.IsEmpty())
				{
					Context.AddWarning(FString::Printf(
						TEXT("Could not auto-add post-dependency '%s' for module '%s': %s"),
						*PendingDependency.Dependency.Id.ToString(),
						*PendingDependency.SourceModuleName,
						*ResolveError));
				}
				State.ResolvingDependencies.Remove(DependencyKey);
			}

			return true;
		}

		void ConfigureEmitterSimTarget(UNiagaraEmitter& Emitter, const EDreamNiagaraSimTarget SimTarget)
		{
			if (FVersionedNiagaraEmitterData* EmitterData = Emitter.GetLatestEmitterData())
			{
				EmitterData->SimTarget = SimTarget == EDreamNiagaraSimTarget::Gpu
					? ENiagaraSimTarget::GPUComputeSim
					: ENiagaraSimTarget::CPUSim;
			}
		}

		FString NormalizeEnumToken(FString Text)
		{
			Text.TrimStartAndEndInline();
			if (Text.Contains(TEXT("::")))
			{
				FString Left;
				FString Right;
				Text.Split(TEXT("::"), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				Text = Right;
			}
			Text.ReplaceInline(TEXT(" "), TEXT(""));
			Text.ReplaceInline(TEXT("-"), TEXT(""));
			Text.ReplaceInline(TEXT("_"), TEXT(""));
			return Text;
		}

		bool TryParseSpriteFacingMode(const FString& Text, ENiagaraSpriteFacingMode& OutValue)
		{
			const FString Token = NormalizeEnumToken(Text);
			if (Token.Equals(TEXT("Camera"), ESearchCase::IgnoreCase) || Token.Equals(TEXT("FaceCamera"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSpriteFacingMode::FaceCamera;
				return true;
			}
			if (Token.Equals(TEXT("CameraPlane"), ESearchCase::IgnoreCase) || Token.Equals(TEXT("FaceCameraPlane"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSpriteFacingMode::FaceCameraPlane;
				return true;
			}
			if (Token.Equals(TEXT("CustomFacingVector"), ESearchCase::IgnoreCase) || Token.Equals(TEXT("Custom"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSpriteFacingMode::CustomFacingVector;
				return true;
			}
			if (Token.Equals(TEXT("CameraPosition"), ESearchCase::IgnoreCase) || Token.Equals(TEXT("FaceCameraPosition"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSpriteFacingMode::FaceCameraPosition;
				return true;
			}
			if (Token.Equals(TEXT("CameraDistanceBlend"), ESearchCase::IgnoreCase) || Token.Equals(TEXT("FaceCameraDistanceBlend"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSpriteFacingMode::FaceCameraDistanceBlend;
				return true;
			}
			if (Token.Equals(TEXT("Automatic"), ESearchCase::IgnoreCase) || Token.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSpriteFacingMode::Automatic;
				return true;
			}
			return false;
		}

		bool TryParseSpriteAlignment(const FString& Text, ENiagaraSpriteAlignment& OutValue)
		{
			const FString Token = NormalizeEnumToken(Text);
			if (Token.Equals(TEXT("Unaligned"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSpriteAlignment::Unaligned;
				return true;
			}
			if (Token.Equals(TEXT("VelocityAligned"), ESearchCase::IgnoreCase) || Token.Equals(TEXT("Velocity"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSpriteAlignment::VelocityAligned;
				return true;
			}
			if (Token.Equals(TEXT("CustomAlignment"), ESearchCase::IgnoreCase) || Token.Equals(TEXT("Custom"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSpriteAlignment::CustomAlignment;
				return true;
			}
			if (Token.Equals(TEXT("Automatic"), ESearchCase::IgnoreCase) || Token.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSpriteAlignment::Automatic;
				return true;
			}
			return false;
		}

		bool TryParseSortMode(const FString& Text, ENiagaraSortMode& OutValue)
		{
			const FString Token = NormalizeEnumToken(Text);
			if (Token.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSortMode::None;
				return true;
			}
			if (Token.Equals(TEXT("ViewDepth"), ESearchCase::IgnoreCase) || Token.Equals(TEXT("Depth"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSortMode::ViewDepth;
				return true;
			}
			if (Token.Equals(TEXT("ViewDistance"), ESearchCase::IgnoreCase) || Token.Equals(TEXT("Distance"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSortMode::ViewDistance;
				return true;
			}
			if (Token.Equals(TEXT("CustomAscending"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSortMode::CustomAscending;
				return true;
			}
			if (Token.Equals(TEXT("CustomDescending"), ESearchCase::IgnoreCase) || Token.Equals(TEXT("CustomDecending"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraSortMode::CustomDecending;
				return true;
			}
			return false;
		}

		bool TryParseRendererSourceMode(const FString& Text, ENiagaraRendererSourceDataMode& OutValue)
		{
			const FString Token = NormalizeEnumToken(Text);
			if (Token.Equals(TEXT("Particles"), ESearchCase::IgnoreCase) || Token.Equals(TEXT("Particle"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraRendererSourceDataMode::Particles;
				return true;
			}
			if (Token.Equals(TEXT("Emitter"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraRendererSourceDataMode::Emitter;
				return true;
			}
			return false;
		}

		bool TryParseRendererSortPrecision(const FString& Text, ENiagaraRendererSortPrecision& OutValue)
		{
			const FString Token = NormalizeEnumToken(Text);
			if (Token.Equals(TEXT("Default"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraRendererSortPrecision::Default;
				return true;
			}
			if (Token.Equals(TEXT("Low"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraRendererSortPrecision::Low;
				return true;
			}
			if (Token.Equals(TEXT("High"), ESearchCase::IgnoreCase))
			{
				OutValue = ENiagaraRendererSortPrecision::High;
				return true;
			}
			return false;
		}


		bool LoadMaterialFromAssignment(const FDreamNiagaraAssignment& Property, UMaterialInterface*& OutMaterial)
		{
			FString AssetPath;
			if (!Private::FLiteralParser::ParseAssetPath(Property.Value.Text, AssetPath))
			{
				return false;
			}

			if (!AssetPath.Contains(TEXT(".")))
			{
				AssetPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *FPackageName::GetShortName(AssetPath));
			}

			OutMaterial = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *AssetPath));
			return OutMaterial != nullptr;
		}

		void ApplySpriteRendererProperty(
			Private::FGenerationContext& Context,
			UNiagaraSpriteRendererProperties& SpriteRenderer,
			const FDreamNiagaraEmitter& EmitterDefinition,
			const FDreamNiagaraAssignment& Property)
		{
			if (Property.Name.Equals(TEXT("Material"), ESearchCase::IgnoreCase))
			{
				UMaterialInterface* Material = nullptr;
				if (LoadMaterialFromAssignment(Property, Material))
				{
					SpriteRenderer.Material = Material;
				}
				else
				{
					Context.AddWarning(FString::Printf(
						TEXT("Could not load sprite material '%s' for emitter '%s'."),
						*Property.Value.Text,
						*EmitterDefinition.Name));
				}
				return;
			}

			if (Property.Name.Equals(TEXT("FacingMode"), ESearchCase::IgnoreCase))
			{
				ENiagaraSpriteFacingMode FacingMode = ENiagaraSpriteFacingMode::Automatic;
				if (TryParseSpriteFacingMode(Property.Value.Text, FacingMode))
				{
					SpriteRenderer.FacingMode = FacingMode;
				}
				else
				{
					Context.AddWarning(FString::Printf(TEXT("Unsupported FacingMode '%s' on sprite renderer for emitter '%s'."), *Property.Value.Text, *EmitterDefinition.Name));
				}
				return;
			}

			if (Property.Name.Equals(TEXT("Alignment"), ESearchCase::IgnoreCase))
			{
				ENiagaraSpriteAlignment Alignment = ENiagaraSpriteAlignment::Automatic;
				if (TryParseSpriteAlignment(Property.Value.Text, Alignment))
				{
					SpriteRenderer.Alignment = Alignment;
				}
				else
				{
					Context.AddWarning(FString::Printf(TEXT("Unsupported Alignment '%s' on sprite renderer for emitter '%s'."), *Property.Value.Text, *EmitterDefinition.Name));
				}
				return;
			}

			if (Property.Name.Equals(TEXT("SortMode"), ESearchCase::IgnoreCase))
			{
				ENiagaraSortMode SortMode = ENiagaraSortMode::None;
				if (TryParseSortMode(Property.Value.Text, SortMode))
				{
					SpriteRenderer.SortMode = SortMode;
				}
				else
				{
					Context.AddWarning(FString::Printf(TEXT("Unsupported SortMode '%s' on sprite renderer for emitter '%s'."), *Property.Value.Text, *EmitterDefinition.Name));
				}
				return;
			}

			if (Property.Name.Equals(TEXT("SourceMode"), ESearchCase::IgnoreCase))
			{
				ENiagaraRendererSourceDataMode SourceMode = ENiagaraRendererSourceDataMode::Particles;
				if (TryParseRendererSourceMode(Property.Value.Text, SourceMode))
				{
					SpriteRenderer.SourceMode = SourceMode;
				}
				else
				{
					Context.AddWarning(FString::Printf(TEXT("Unsupported SourceMode '%s' on sprite renderer for emitter '%s'."), *Property.Value.Text, *EmitterDefinition.Name));
				}
				return;
			}

			if (Property.Name.Equals(TEXT("SortPrecision"), ESearchCase::IgnoreCase))
			{
				ENiagaraRendererSortPrecision SortPrecision = ENiagaraRendererSortPrecision::Default;
				if (TryParseRendererSortPrecision(Property.Value.Text, SortPrecision))
				{
					SpriteRenderer.SortPrecision = SortPrecision;
				}
				else
				{
					Context.AddWarning(FString::Printf(TEXT("Unsupported SortPrecision '%s' on sprite renderer for emitter '%s'."), *Property.Value.Text, *EmitterDefinition.Name));
				}
				return;
			}

			float FloatValue = 0.0f;
			if (Property.Name.Equals(TEXT("MacroUVRadius"), ESearchCase::IgnoreCase)
				&& Private::FLiteralParser::ParseFloat(Property.Value.Text, FloatValue))
			{
				SpriteRenderer.MacroUVRadius = FloatValue;
				return;
			}

			if (Property.Name.Equals(TEXT("PixelCoverageBlend"), ESearchCase::IgnoreCase)
				&& Private::FLiteralParser::ParseFloat(Property.Value.Text, FloatValue))
			{
				SpriteRenderer.PixelCoverageBlend = FloatValue;
				return;
			}

			TArray<float> VectorValues;
			if (Property.Name.Equals(TEXT("SubImageSize"), ESearchCase::IgnoreCase)
				&& Private::FLiteralParser::ParseVector(Property.Value.Text, VectorValues)
				&& VectorValues.Num() >= 2)
			{
				SpriteRenderer.SubImageSize = FVector2D(VectorValues[0], VectorValues[1]);
				return;
			}

			if (Property.Name.Equals(TEXT("PivotInUVSpace"), ESearchCase::IgnoreCase)
				&& Private::FLiteralParser::ParseVector(Property.Value.Text, VectorValues)
				&& VectorValues.Num() >= 2)
			{
				SpriteRenderer.PivotInUVSpace = FVector2D(VectorValues[0], VectorValues[1]);
				return;
			}

			bool BoolValue = false;
			if (Property.Name.Equals(TEXT("SubImageBlend"), ESearchCase::IgnoreCase)
				&& Private::FLiteralParser::ParseBool(Property.Value.Text, BoolValue))
			{
				SpriteRenderer.bSubImageBlend = BoolValue;
				return;
			}

			if (Property.Name.Equals(TEXT("SortOnlyWhenTranslucent"), ESearchCase::IgnoreCase)
				&& Private::FLiteralParser::ParseBool(Property.Value.Text, BoolValue))
			{
				SpriteRenderer.bSortOnlyWhenTranslucent = BoolValue;
				return;
			}

			Context.AddWarning(FString::Printf(
				TEXT("Renderer property '%s = %s' for emitter '%s' is not supported in DreamNiagara 0.1.0."),
				*Property.Name,
				*Property.Value.Text,
				*EmitterDefinition.Name));
		}

		FString MakeUserParameterName(const FString& Name)
		{
			FString ParameterName = Name.TrimStartAndEnd();
			if (!ParameterName.StartsWith(TEXT("User."), ESearchCase::IgnoreCase))
			{
				ParameterName = TEXT("User.") + ParameterName;
			}
			return ParameterName;
		}

		bool ResolveUserParameterType(const FString& TypeText, FNiagaraTypeDefinition& OutType)
		{
			const FString Normalized = TypeText.TrimStartAndEnd().ToLower();
			if (Normalized == TEXT("float") || Normalized == TEXT("double") || Normalized == TEXT("number"))
			{
				OutType = FNiagaraTypeDefinition::GetFloatDef();
				return true;
			}
			if (Normalized == TEXT("int") || Normalized == TEXT("int32") || Normalized == TEXT("integer"))
			{
				OutType = FNiagaraTypeDefinition::GetIntDef();
				return true;
			}
			if (Normalized == TEXT("bool") || Normalized == TEXT("boolean"))
			{
				OutType = FNiagaraTypeDefinition::GetBoolDef();
				return true;
			}
			if (Normalized == TEXT("vec2") || Normalized == TEXT("float2") || Normalized == TEXT("vector2"))
			{
				OutType = FNiagaraTypeDefinition::GetVec2Def();
				return true;
			}
			if (Normalized == TEXT("vec3") || Normalized == TEXT("float3") || Normalized == TEXT("vector") || Normalized == TEXT("vector3"))
			{
				OutType = FNiagaraTypeDefinition::GetVec3Def();
				return true;
			}
			if (Normalized == TEXT("position"))
			{
				OutType = FNiagaraTypeDefinition::GetPositionDef();
				return true;
			}
			if (Normalized == TEXT("vec4") || Normalized == TEXT("float4") || Normalized == TEXT("vector4"))
			{
				OutType = FNiagaraTypeDefinition::GetVec4Def();
				return true;
			}
			if (Normalized == TEXT("color") || Normalized == TEXT("linearcolor"))
			{
				OutType = FNiagaraTypeDefinition::GetColorDef();
				return true;
			}
			if (Private::FLiteralParser::ResolveCurveType(TypeText, OutType))
			{
				return true;
			}

			return false;
		}

		template <typename ValueType>
		void SetUserParameterData(FNiagaraUserRedirectionParameterStore& Store, FNiagaraVariable& Variable, const ValueType& Value)
		{
			Variable.SetValue(Value);
			Store.SetParameterData(Variable.GetData(), Variable, true);
		}

		bool ApplyUserParameterDefault(
			Private::FGenerationContext& Context,
			FNiagaraUserRedirectionParameterStore& Store,
			FNiagaraVariable& Variable,
			const FDreamNiagaraUserParameter& Parameter)
		{
			if (!Parameter.bHasDefaultValue)
			{
				return true;
			}

			const FString Text = Parameter.DefaultValue.Text.TrimStartAndEnd();
			const FNiagaraTypeDefinition& Type = Variable.GetType();

			if (Private::FLiteralParser::IsCurveDataInterfaceType(Type))
			{
				Private::FCurveLiteral CurveLiteral;
				FString CurveError;
				if (!Private::FLiteralParser::TryParseCurve(Parameter.DefaultValue, CurveLiteral, CurveError))
				{
					Context.AddWarning(FString::Printf(
						TEXT("Default value '%s' for curve user parameter '%s' could not be parsed: %s"),
						*Text,
						*Parameter.Name,
						CurveError.IsEmpty() ? TEXT("expected curve { ... } literal") : *CurveError));
					return false;
				}

				if (!Type.GetClass() || !Type.GetClass()->IsChildOf(Private::FLiteralParser::GetCurveClass(CurveLiteral.Kind)))
				{
					Context.AddWarning(FString::Printf(
						TEXT("Default value for user parameter '%s' is a curve literal with the wrong channel type for '%s'."),
						*Parameter.Name,
						*Parameter.Type));
					return false;
				}

				UNiagaraDataInterfaceCurveBase* CurveDataInterface = Cast<UNiagaraDataInterfaceCurveBase>(Store.GetDataInterface(Variable));
				if (!CurveDataInterface || CurveDataInterface->GetClass() != Type.GetClass())
				{
					UObject* DataInterfaceOwner = Store.GetOwner() ? Store.GetOwner() : GetTransientPackage();
					const EObjectFlags DataInterfaceObjectFlags = UNiagaraDataInterface::BuildObjectFlagsForOwner(DataInterfaceOwner, RF_Transactional);
					CurveDataInterface = Cast<UNiagaraDataInterfaceCurveBase>(
						NewObject<UNiagaraDataInterface>(DataInterfaceOwner, Type.GetClass(), NAME_None, DataInterfaceObjectFlags));
				}

				if (!CurveDataInterface)
				{
					Context.AddWarning(FString::Printf(TEXT("Could not create curve data interface for user parameter '%s'."), *Parameter.Name));
					return false;
				}

				if (!Private::FLiteralParser::ApplyCurveToDataInterface(CurveLiteral, *CurveDataInterface, CurveError))
				{
					Context.AddWarning(FString::Printf(
						TEXT("Default value for user parameter '%s' could not be applied: %s"),
						*Parameter.Name,
						*CurveError));
					return false;
				}

				Store.SetDataInterface(CurveDataInterface, Variable);
				return true;
			}

			bool BoolValue = false;
			if (Type == FNiagaraTypeDefinition::GetBoolDef() && Private::FLiteralParser::ParseBool(Text, BoolValue))
			{
				SetUserParameterData(Store, Variable, FNiagaraBool(BoolValue));
				return true;
			}

			int32 IntValue = 0;
			if (Type == FNiagaraTypeDefinition::GetIntDef() && Private::FLiteralParser::ParseInt(Text, IntValue))
			{
				SetUserParameterData(Store, Variable, IntValue);
				return true;
			}

			float FloatValue = 0.0f;
			if (Type == FNiagaraTypeDefinition::GetFloatDef() && Private::FLiteralParser::ParseFloat(Text, FloatValue))
			{
				SetUserParameterData(Store, Variable, FloatValue);
				return true;
			}

			FLinearColor ColorValue;
			if (Type == FNiagaraTypeDefinition::GetColorDef() && Private::FLiteralParser::ParseHexColor(Text, ColorValue))
			{
				SetUserParameterData(Store, Variable, ColorValue);
				return true;
			}

			TArray<float> VectorValues;
			if (Private::FLiteralParser::ParseVector(Text, VectorValues))
			{
				const int32 ComponentCount = VectorValues.Num();
				while (VectorValues.Num() < 4)
				{
					VectorValues.Add(VectorValues.Num() == 3 ? 1.0f : 0.0f);
				}

				if (Type == FNiagaraTypeDefinition::GetVec2Def() && ComponentCount >= 2)
				{
					SetUserParameterData(Store, Variable, FVector2f(VectorValues[0], VectorValues[1]));
					return true;
				}
				if (Type == FNiagaraTypeDefinition::GetVec3Def() && ComponentCount >= 3)
				{
					SetUserParameterData(Store, Variable, FVector3f(VectorValues[0], VectorValues[1], VectorValues[2]));
					return true;
				}
				if (Type == FNiagaraTypeDefinition::GetPositionDef() && ComponentCount >= 3)
				{
					SetUserParameterData(Store, Variable, FNiagaraPosition(VectorValues[0], VectorValues[1], VectorValues[2]));
					return true;
				}
				if (Type == FNiagaraTypeDefinition::GetVec4Def() && ComponentCount >= 4)
				{
					SetUserParameterData(Store, Variable, FVector4f(VectorValues[0], VectorValues[1], VectorValues[2], VectorValues[3]));
					return true;
				}
			}

			Context.AddWarning(FString::Printf(
				TEXT("Default value '%s' for user parameter '%s' could not be applied as type '%s'."),
				*Text,
				*Parameter.Name,
				*Parameter.Type));
			return false;
		}

		void ConfigureRenderers(Private::FGenerationContext& Context, UNiagaraEmitter& Emitter, const FDreamNiagaraEmitter& EmitterDefinition)
		{
			if (FVersionedNiagaraEmitterData* EmitterData = Emitter.GetLatestEmitterData())
			{
				const FGuid EmitterVersion = Emitter.GetExposedVersion().VersionGuid;
				TArray<UNiagaraRendererProperties*> ExistingRenderers = EmitterData->GetRenderers();
				for (UNiagaraRendererProperties* ExistingRenderer : ExistingRenderers)
				{
					if (ExistingRenderer)
					{
						Emitter.RemoveRenderer(ExistingRenderer, EmitterVersion);
					}
				}
			}

			for (const FDreamNiagaraRenderer& Renderer : EmitterDefinition.Renderers)
			{
				if (Renderer.Type.Equals(TEXT("Sprite"), ESearchCase::IgnoreCase))
				{
					UNiagaraSpriteRendererProperties* SpriteRenderer = NewObject<UNiagaraSpriteRendererProperties>(&Emitter, NAME_None, RF_Transactional);
					if (!SpriteRenderer)
					{
						Context.AddWarning(FString::Printf(TEXT("Failed to create sprite renderer for emitter '%s'."), *EmitterDefinition.Name));
						continue;
					}

					Emitter.AddRenderer(SpriteRenderer, Emitter.GetExposedVersion().VersionGuid);
					for (const FDreamNiagaraAssignment& Property : Renderer.Properties)
					{
						ApplySpriteRendererProperty(Context, *SpriteRenderer, EmitterDefinition, Property);
					}
				}
				else
				{
					Context.AddWarning(FString::Printf(
						TEXT("Renderer type '%s' on emitter '%s' is not supported in DreamNiagara 0.1.0."),
						*Renderer.Type,
						*EmitterDefinition.Name));
				}
			}
		}

		bool AddEmitterStacks(
			Private::FGenerationContext& Context,
			UNiagaraSystem& System,
			UNiagaraEmitter& Emitter,
			const FDreamNiagaraEmitter& EmitterDefinition,
			FString& OutError)
		{
			FStackBuildState State;
			for (const FDreamNiagaraStack& Stack : EmitterDefinition.Stacks)
			{
				const Private::EDreamNiagaraStackUsage StackUsage = Private::ParseStackUsage(Stack.Name);
				if (StackUsage == Private::EDreamNiagaraStackUsage::Unknown)
				{
					Context.AddWarning(FString::Printf(TEXT("Emitter '%s' contains unknown stack '%s'."), *EmitterDefinition.Name, *Stack.Name));
					continue;
				}

				if (StackUsage == Private::EDreamNiagaraStackUsage::SystemSpawn
					|| StackUsage == Private::EDreamNiagaraStackUsage::SystemUpdate
					|| StackUsage == Private::EDreamNiagaraStackUsage::ParticleEvent
					|| StackUsage == Private::EDreamNiagaraStackUsage::SimulationStage)
				{
					Context.AddWarning(FString::Printf(
						TEXT("Stack '%s' in emitter '%s' is parsed but not supported by the 0.1.0 system generator."),
						*Stack.Name,
						*EmitterDefinition.Name));
					continue;
				}

				for (const FDreamNiagaraModuleCall& ModuleCall : Stack.Modules)
				{
					Private::FResolvedModule ResolvedModule;
					FString ResolveError;
					if (!Private::ResolveModule(ModuleCall.ModuleId, StackUsage, ResolvedModule, ResolveError))
					{
						Context.AddWarning(FString::Printf(
							TEXT("Skipping module '%s' in emitter '%s': %s"),
							*ModuleCall.ModuleId,
							*EmitterDefinition.Name,
							*ResolveError));
						continue;
					}

					if (!AddResolvedModuleWithDependencies(
						Context,
						System,
						Emitter,
						State,
						ResolvedModule,
						StackUsage,
						&ModuleCall.Inputs,
						ModuleCall.ModuleId,
						OutError))
					{
						return false;
					}
				}
			}

			if (!ResolvePendingPostDependencies(Context, System, Emitter, State, OutError))
			{
				return false;
			}

			if (FVersionedNiagaraEmitterData* EmitterData = Emitter.GetLatestEmitterData())
			{
				if (UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterData->GraphSource))
				{
					if (Source->NodeGraph)
					{
						Source->NodeGraph->NotifyGraphChanged();
					}
				}
			}

			return true;
		}

		bool ConfigureSystemUserParameters(
			Private::FGenerationContext& Context,
			UNiagaraSystem& System,
			const FDreamNiagaraSystem& Definition)
		{
			FNiagaraUserRedirectionParameterStore& ExposedParameters = System.GetExposedParameters();
			ExposedParameters.Empty();

			for (const FDreamNiagaraUserParameter& Parameter : Definition.UserParameters)
			{
				FNiagaraTypeDefinition ParameterType;
				if (!ResolveUserParameterType(Parameter.Type, ParameterType))
				{
					Context.AddWarning(FString::Printf(
						TEXT("Unsupported user parameter type '%s' for '%s'."),
						*Parameter.Type,
						*Parameter.Name));
					continue;
				}

				FNiagaraVariable Variable(ParameterType, FName(*MakeUserParameterName(Parameter.Name)));
				ExposedParameters.AddParameter(Variable, true, true);
				ApplyUserParameterDefault(Context, ExposedParameters, Variable, Parameter);
			}

			ExposedParameters.SanityCheckData(false);
			return true;
		}

		void RemoveExistingEmitters(UNiagaraSystem& System)
		{
			TSet<FGuid> HandleIds;
			for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
			{
				HandleIds.Add(Handle.GetId());
			}

			if (!HandleIds.IsEmpty())
			{
				System.RemoveEmitterHandlesById(HandleIds);
			}
		}

		bool GenerateIntoSystem(
			Private::FGenerationContext& Context,
			UNiagaraSystem& System,
			const bool bForceCompile,
			FString& OutError)
		{
			const FDreamNiagaraSystem& Definition = *Context.Definition;
			System.Modify();
			RemoveExistingEmitters(System);

			UNiagaraSystemFactoryNew::InitializeSystem(&System, true);
			ConfigureSystemUserParameters(Context, System, Definition);

			for (const FDreamNiagaraEmitter& EmitterDefinition : Definition.Emitters)
			{
				const FName EmitterObjectName(*UE::DreamNiagara::SanitizeIdentifier(EmitterDefinition.Name));
				UNiagaraEmitter* Emitter = NewObject<UNiagaraEmitter>(GetTransientPackage(), EmitterObjectName, RF_Transactional);
				if (!Emitter)
				{
					OutError = FString::Printf(TEXT("Failed to create emitter '%s'."), *EmitterDefinition.Name);
					return false;
				}

				UNiagaraEmitterFactoryNew::InitializeEmitter(Emitter, false);
				Emitter->SetUniqueEmitterName(EmitterDefinition.Name);
				ConfigureEmitterSimTarget(*Emitter, EmitterDefinition.SimTarget);

				if (!AddEmitterStacks(Context, System, *Emitter, EmitterDefinition, OutError))
				{
					return false;
				}

				ConfigureRenderers(Context, *Emitter, EmitterDefinition);

				FNiagaraEditorUtilities::AddEmitterToSystem(System, *Emitter, Emitter->GetExposedVersion().VersionGuid, true);
			}

			System.RequestCompile(bForceCompile);
			System.MarkPackageDirty();
			return true;
		}

		FString BuildSourceHash(const FString& SourceText)
		{
			return FString::Printf(TEXT("%08x"), FCrc::StrCrc32(*SourceText));
		}

		FString GetSourceMetadataValue(UObject* Asset, const TCHAR* Key)
		{
			if (!Asset)
			{
				return FString();
			}

			UPackage* Package = Asset->GetOutermost();
			if (!Package)
			{
				return FString();
			}

			return Package->GetMetaData().GetValue(Asset, Key);
		}

		bool IsGeneratedAssetSourceCurrent(UObject* Asset, const FString& SourceFilePath, const FString& SourceHash)
		{
			if (!Asset || SourceHash.IsEmpty())
			{
				return false;
			}

			const FString ExistingSourceFileRaw = GetSourceMetadataValue(Asset, TEXT("DreamNiagara.SourceFile"));
			if (ExistingSourceFileRaw.IsEmpty())
			{
				return false;
			}

			const FString ExistingSourceFile = UE::DreamNiagara::NormalizeSourceFilePath(ExistingSourceFileRaw);
			const FString ExistingSourceHash = GetSourceMetadataValue(Asset, TEXT("DreamNiagara.SourceHash"));

			return ExistingSourceFile.Equals(UE::DreamNiagara::NormalizeSourceFilePath(SourceFilePath), ESearchCase::IgnoreCase)
				&& ExistingSourceHash.Equals(SourceHash, ESearchCase::CaseSensitive);
		}

		void ApplySourceMetadata(UObject* Asset, const FString& SourceFilePath, const FString& SourceHash)
		{
			if (!Asset)
			{
				return;
			}

			UPackage* Package = Asset->GetOutermost();
			if (!Package)
			{
				return;
			}

			FMetaData& MetaData = Package->GetMetaData();
			MetaData.SetValue(Asset, TEXT("DreamNiagara.SourceFile"), *UE::DreamNiagara::NormalizeSourceFilePath(SourceFilePath));
			if (!SourceHash.IsEmpty())
			{
				MetaData.SetValue(Asset, TEXT("DreamNiagara.SourceHash"), *SourceHash);
				MetaData.SetValue(Asset, TEXT("DreamNiagara.GeneratedAtUtc"), *FDateTime::UtcNow().ToIso8601());
			}
		}

		FDreamNiagaraSystemGenerateResult GenerateFromDefinitionInternal(
			const FDreamNiagaraSystem& Definition,
			const FString& SourceFilePath,
			const FString& SourceHash,
			const bool bForce)
		{
			FDreamNiagaraSystemGenerateResult Result;

			Private::FGenerationContext Context;
			Context.Definition = &Definition;
			Context.SourceFilePath = SourceFilePath;

			FString PackageName;
			FString AssetName;
			FString ObjectPath;
			FString Error;
			if (!Private::ResolveAssetDestination(Definition, PackageName, AssetName, ObjectPath, Error))
			{
				Result.Message = Error;
				return Result;
			}

			UObject* ExistingObject = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
			if (!ExistingObject)
			{
				ExistingObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
			}

			UPackage* Package = ExistingObject ? ExistingObject->GetOutermost() : CreatePackage(*PackageName);
			if (!Package)
			{
				Result.Message = FString::Printf(TEXT("Failed to create package '%s'."), *PackageName);
				return Result;
			}

			if (ExistingObject && !ExistingObject->IsA<UNiagaraSystem>())
			{
				Result.Message = FString::Printf(
					TEXT("Cannot generate Niagara system '%s' because the target asset name is already used by '%s'."),
					*ObjectPath,
					*ExistingObject->GetClass()->GetName());
				return Result;
			}

			UNiagaraSystem* System = Cast<UNiagaraSystem>(ExistingObject);
			if (!bForce && IsGeneratedAssetSourceCurrent(System, SourceFilePath, SourceHash))
			{
				Result.bSucceeded = true;
				Result.Message = FString::Printf(TEXT("Skipped Niagara system '%s' from '%s'; source hash is unchanged."), *ObjectPath, *SourceFilePath);
				UE_LOG(LogDreamNiagara, Verbose, TEXT("%s"), *Result.Message);
				return Result;
			}

			const bool bCreatedAsset = System == nullptr;
			if (!System)
			{
				System = NewObject<UNiagaraSystem>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
			}

			if (!System)
			{
				Result.Message = FString::Printf(TEXT("Failed to create Niagara system asset '%s'."), *ObjectPath);
				return Result;
			}

			if (!GenerateIntoSystem(Context, *System, bForce, Error))
			{
				Result.Message = FString::Printf(TEXT("Failed to generate Niagara system '%s': %s"), *ObjectPath, *Error);
				Result.Warnings = MoveTemp(Context.Warnings);
				return Result;
			}

			if (bCreatedAsset)
			{
				FAssetRegistryModule::AssetCreated(System);
			}

			ApplySourceMetadata(System, SourceFilePath, SourceHash);

			TArray<UPackage*> PackagesToSave;
			PackagesToSave.Add(Package);
			if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true))
			{
				Result.Message = FString::Printf(TEXT("Generated '%s' but failed to save its package."), *ObjectPath);
				Result.Warnings = MoveTemp(Context.Warnings);
				return Result;
			}

			Result.bSucceeded = true;
			Result.Warnings = MoveTemp(Context.Warnings);
			Result.Message = FString::Printf(
				TEXT("Generated Niagara system '%s' from '%s'%s."),
				*ObjectPath,
				*SourceFilePath,
				bForce ? TEXT(" with force") : TEXT(""));
			UE_LOG(LogDreamNiagara, Display, TEXT("%s"), *Result.Message);
			return Result;
		}
	}

	bool FSystemGenerator::IsGeneratedAssetCurrent(const FString& SourceFilePath)
	{
		const FString NormalizedSourceFilePath = UE::DreamNiagara::NormalizeSourceFilePath(SourceFilePath);
		if (!UE::DreamNiagara::IsDreamNiagaraSystemFile(NormalizedSourceFilePath))
		{
			return true;
		}

		FString SourceText;
		if (!FFileHelper::LoadFileToString(SourceText, *NormalizedSourceFilePath))
		{
			return false;
		}

		const FString SourceHash = BuildSourceHash(SourceText);

		FDreamNiagaraSystem Definition;
		FString ParseError;
		if (!FDreamNiagaraParser::ParseSystem(SourceText, Definition, ParseError))
		{
			return false;
		}

		FString PackageName;
		FString AssetName;
		FString ObjectPath;
		FString Error;
		if (!Private::ResolveAssetDestination(Definition, PackageName, AssetName, ObjectPath, Error))
		{
			return false;
		}

		UObject* ExistingObject = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
		if (!ExistingObject)
		{
			ExistingObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
		}

		UNiagaraSystem* ExistingSystem = Cast<UNiagaraSystem>(ExistingObject);
		return IsGeneratedAssetSourceCurrent(ExistingSystem, NormalizedSourceFilePath, SourceHash);
	}

	FDreamNiagaraSystemGenerateResult FSystemGenerator::GenerateFromFile(const FString& SourceFilePath, const bool bForce)
	{
		FDreamNiagaraSystemGenerateResult Result;

		const FString NormalizedSourceFilePath = UE::DreamNiagara::NormalizeSourceFilePath(SourceFilePath);
		if (!UE::DreamNiagara::IsDreamNiagaraSystemFile(NormalizedSourceFilePath))
		{
			Result.Message = FString::Printf(TEXT("DreamNiagara system source must be a .dns file: %s"), *NormalizedSourceFilePath);
			return Result;
		}

		FString SourceText;
		if (!FFileHelper::LoadFileToString(SourceText, *NormalizedSourceFilePath))
		{
			Result.Message = FString::Printf(TEXT("Failed to read DreamNiagara system source '%s'."), *NormalizedSourceFilePath);
			return Result;
		}

		const FString SourceHash = BuildSourceHash(SourceText);

		FDreamNiagaraSystem Definition;
		FString ParseError;
		if (!FDreamNiagaraParser::ParseSystem(SourceText, Definition, ParseError))
		{
			Result.Message = FString::Printf(TEXT("Failed to parse '%s': %s"), *NormalizedSourceFilePath, *ParseError);
			return Result;
		}

		Result = GenerateFromDefinitionInternal(Definition, NormalizedSourceFilePath, SourceHash, bForce);
		return Result;
	}

	FDreamNiagaraSystemGenerateResult FSystemGenerator::GenerateFromDefinition(
		const FDreamNiagaraSystem& Definition,
		const FString& SourceFilePath,
		const bool bForce)
	{
		FString SourceText;
		const FString NormalizedSourceFilePath = UE::DreamNiagara::NormalizeSourceFilePath(SourceFilePath);
		const FString SourceHash = FFileHelper::LoadFileToString(SourceText, *NormalizedSourceFilePath)
			? BuildSourceHash(SourceText)
			: FString();

		return GenerateFromDefinitionInternal(Definition, NormalizedSourceFilePath, SourceHash, bForce);
	}
}
