#include "DreamNiagaraSystemGenerator.h"

#include "DreamNiagaraModule.h"
#include "DreamNiagaraParser.h"
#include "DreamNiagaraSystemGeneratorPrivate.h"
#include "DreamNiagaraSystemLiteralParser.h"
#include "DreamNiagaraSystemValueApplier.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraGPUSortInfo.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

namespace UE::DreamNiagara::SystemEditor
{
	namespace
	{
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

				UNiagaraNodeOutput* OutputNode = FindOutputNodeForStack(Emitter, StackUsage);
				if (!OutputNode)
				{
					OutError = FString::Printf(TEXT("Could not find Niagara output node for stack '%s' in emitter '%s'."), *Stack.Name, *EmitterDefinition.Name);
					return false;
				}

				FVersionedNiagaraEmitterData* EmitterData = Emitter.GetLatestEmitterData();
				UNiagaraScript* TargetScript = nullptr;
				if (EmitterData)
				{
					switch (StackUsage)
					{
					case Private::EDreamNiagaraStackUsage::EmitterSpawn:
						TargetScript = EmitterData->EmitterSpawnScriptProps.Script;
						break;
					case Private::EDreamNiagaraStackUsage::EmitterUpdate:
						TargetScript = EmitterData->EmitterUpdateScriptProps.Script;
						break;
					case Private::EDreamNiagaraStackUsage::ParticleSpawn:
						TargetScript = EmitterData->SpawnScriptProps.Script;
						break;
					case Private::EDreamNiagaraStackUsage::ParticleUpdate:
						TargetScript = EmitterData->UpdateScriptProps.Script;
						break;
					default:
						break;
					}
				}
				if (!TargetScript)
				{
					OutError = FString::Printf(TEXT("Could not resolve target Niagara script for stack '%s' in emitter '%s'."), *Stack.Name, *EmitterDefinition.Name);
					return false;
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

					UNiagaraNodeFunctionCall* ModuleNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
						ResolvedModule.Script,
						*OutputNode);
					if (!ModuleNode)
					{
						Context.AddWarning(FString::Printf(
							TEXT("Failed to add module '%s' to stack '%s' in emitter '%s'."),
							*ModuleCall.ModuleId,
							*Stack.Name,
							*EmitterDefinition.Name));
						continue;
					}

					const FVersionedNiagaraEmitter VersionedEmitter(&Emitter, Emitter.GetExposedVersion().VersionGuid);
					Private::FModuleInputApplier::ApplyInputs(
						Context,
						Emitter.GetUniqueEmitterName(),
						System,
						VersionedEmitter,
						*TargetScript,
						*ModuleNode,
						ModuleCall.Inputs);
				}
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

		FDreamNiagaraSystem Definition;
		FString ParseError;
		if (!FDreamNiagaraParser::ParseSystem(SourceText, Definition, ParseError))
		{
			Result.Message = FString::Printf(TEXT("Failed to parse '%s': %s"), *NormalizedSourceFilePath, *ParseError);
			return Result;
		}

		return GenerateFromDefinition(Definition, NormalizedSourceFilePath, bForce);
	}

	FDreamNiagaraSystemGenerateResult FSystemGenerator::GenerateFromDefinition(
		const FDreamNiagaraSystem& Definition,
		const FString& SourceFilePath,
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
