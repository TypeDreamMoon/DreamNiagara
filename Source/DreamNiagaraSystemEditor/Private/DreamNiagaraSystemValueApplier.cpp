#include "DreamNiagaraSystemValueApplier.h"

#include "DreamNiagaraSystemGeneratorPrivate.h"
#include "DreamNiagaraSystemLiteralParser.h"

#include "EdGraph/EdGraphPin.h"
#include "NiagaraCommon.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceCurveBase.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

namespace UE::DreamNiagara::SystemEditor::Private
{
	namespace
	{
		template <typename ValueType>
		void SetRapidIterationValue(
			const FString& UniqueEmitterName,
			UNiagaraScript& TargetScript,
			UNiagaraNodeFunctionCall& ModuleNode,
			const FName InputName,
			const FNiagaraTypeDefinition& InputType,
			const ValueType& Value)
		{
			FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(InputName);
			FNiagaraParameterHandle AliasedInputHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, &ModuleNode);
			FNiagaraVariable InputVariable(InputType, AliasedInputHandle.GetParameterHandleString());
			FNiagaraVariable RapidIterationParameter = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
				InputVariable,
				(UNiagaraScript::IsSystemSpawnScript(TargetScript.GetUsage()) || UNiagaraScript::IsSystemUpdateScript(TargetScript.GetUsage()) || UniqueEmitterName.IsEmpty())
					? nullptr
					: *UniqueEmitterName,
				TargetScript.GetUsage());
			RapidIterationParameter.SetValue(Value);

			const bool bAddParameterIfMissing = true;
			TargetScript.RapidIterationParameters.SetParameterData(
				RapidIterationParameter.GetData(),
				RapidIterationParameter,
				bAddParameterIfMissing);
		}

		bool FindInputType(
			const FVersionedNiagaraEmitter& VersionedEmitter,
			UNiagaraScript& TargetScript,
			UNiagaraNodeFunctionCall& ModuleNode,
			const FString& InputName,
			FNiagaraTypeDefinition& OutType)
		{
			TArray<FNiagaraVariable> InputVariables;
			TSet<FNiagaraVariable> HiddenVariables;
			FCompileConstantResolver ConstantResolver(VersionedEmitter, TargetScript.GetUsage());
			FNiagaraStackGraphUtilities::GetStackFunctionInputs(
				ModuleNode,
				InputVariables,
				HiddenVariables,
				ConstantResolver,
				FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::AllInputs,
				false);

			for (const FNiagaraVariable& Variable : InputVariables)
			{
				const FString FullName = Variable.GetName().ToString();
				const FString ShortName = FNiagaraParameterHandle(Variable.GetName()).GetName().ToString();
				if (FullName.Equals(InputName, ESearchCase::IgnoreCase)
					|| ShortName.Equals(InputName, ESearchCase::IgnoreCase))
				{
					OutType = Variable.GetType();
					return OutType.IsValid();
				}
			}

			return false;
		}

		FNiagaraParameterHandle MakeAliasedInputHandle(UNiagaraNodeFunctionCall& ModuleNode, const FName InputName)
		{
			const FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(InputName);
			return FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, &ModuleNode);
		}

		bool IsUserParameterExpression(const FString& Text)
		{
			return Text.TrimStartAndEnd().StartsWith(TEXT("User."), ESearchCase::IgnoreCase);
		}

		bool FindKnownParameter(UNiagaraSystem& System, const FName ParameterName, FNiagaraVariableBase& OutParameter)
		{
			TArray<FNiagaraVariable> UserParameters;
			System.GetExposedParameters().GetUserParameters(UserParameters);

			for (FNiagaraVariable& UserParameter : UserParameters)
			{
				FNiagaraUserRedirectionParameterStore::MakeUserVariable(UserParameter);
				if (UserParameter.GetName() == ParameterName)
				{
					OutParameter = UserParameter;
					return true;
				}
			}

			return false;
		}

		bool ApplyLinkedUserParameter(
			FGenerationContext& Context,
			UNiagaraSystem& System,
			const FNiagaraTypeDefinition& InputType,
			UNiagaraNodeFunctionCall& ModuleNode,
			const FDreamNiagaraAssignment& Input)
		{
			const FString ParameterText = Input.Value.Text.TrimStartAndEnd();
			if (!IsUserParameterExpression(ParameterText))
			{
				return false;
			}

			FNiagaraVariableBase LinkedParameter(InputType, FName(*ParameterText));
			FNiagaraUserRedirectionParameterStore::MakeUserVariable(LinkedParameter);

			FNiagaraVariableBase KnownParameter;
			if (!FindKnownParameter(System, LinkedParameter.GetName(), KnownParameter))
			{
				Context.AddWarning(FString::Printf(
					TEXT("Module '%s' references unknown user parameter '%s'."),
					*ModuleNode.GetFunctionName(),
					*ParameterText));
				return true;
			}

			if (KnownParameter.GetType() != InputType)
			{
				Context.AddWarning(FString::Printf(
					TEXT("Module '%s' input '%s' expects '%s' but user parameter '%s' is '%s'."),
					*ModuleNode.GetFunctionName(),
					*Input.Name,
					*InputType.GetName(),
					*ParameterText,
					*KnownParameter.GetType().GetName()));
				return true;
			}

			UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
				ModuleNode,
				MakeAliasedInputHandle(ModuleNode, FName(*Input.Name)),
				InputType,
				FGuid(),
				FGuid());
			if (OverridePin.LinkedTo.Num() != 0)
			{
				Context.AddWarning(FString::Printf(
					TEXT("Module '%s' input '%s' already has an override graph link; user parameter '%s' was not applied."),
					*ModuleNode.GetFunctionName(),
					*Input.Name,
					*ParameterText));
				return true;
			}

			TSet<FNiagaraVariableBase> KnownParameters;
			KnownParameters.Add(KnownParameter);

			FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(OverridePin, KnownParameter, KnownParameters);
			return true;
		}

		bool ApplyBasicLiteral(
			const FString& UniqueEmitterName,
			const FNiagaraTypeDefinition& InputType,
			UNiagaraScript& TargetScript,
			UNiagaraNodeFunctionCall& ModuleNode,
			const FDreamNiagaraAssignment& Input)
		{
			const FString Text = Input.Value.Text.TrimStartAndEnd();

			bool BoolValue = false;
			if (InputType == FNiagaraTypeDefinition::GetBoolDef() && FLiteralParser::ParseBool(Text, BoolValue))
			{
				FNiagaraBool NiagaraBool(BoolValue);
				SetRapidIterationValue(UniqueEmitterName, TargetScript, ModuleNode, FName(*Input.Name), FNiagaraTypeDefinition::GetBoolDef(), NiagaraBool);
				return true;
			}

			int32 IntValue = 0;
			if (InputType == FNiagaraTypeDefinition::GetIntDef() && FLiteralParser::ParseInt(Text, IntValue))
			{
				SetRapidIterationValue(UniqueEmitterName, TargetScript, ModuleNode, FName(*Input.Name), FNiagaraTypeDefinition::GetIntDef(), IntValue);
				return true;
			}

			float FloatValue = 0.0f;
			if ((InputType == FNiagaraTypeDefinition::GetFloatDef() || InputType == FNiagaraTypeDefinition::GetGenericNumericDef())
				&& FLiteralParser::ParseFloat(Text, FloatValue))
			{
				SetRapidIterationValue(UniqueEmitterName, TargetScript, ModuleNode, FName(*Input.Name), FNiagaraTypeDefinition::GetFloatDef(), FloatValue);
				return true;
			}

			FLinearColor ColorValue;
			if (InputType == FNiagaraTypeDefinition::GetColorDef() && FLiteralParser::ParseHexColor(Text, ColorValue))
			{
				SetRapidIterationValue(UniqueEmitterName, TargetScript, ModuleNode, FName(*Input.Name), FNiagaraTypeDefinition::GetColorDef(), ColorValue);
				return true;
			}

			TArray<float> VectorValues;
			if (FLiteralParser::ParseVector(Text, VectorValues))
			{
				const int32 ComponentCount = VectorValues.Num();
				while (VectorValues.Num() < 4)
				{
					VectorValues.Add(VectorValues.Num() == 3 ? 1.0f : 0.0f);
				}

				if (InputType == FNiagaraTypeDefinition::GetVec2Def() && ComponentCount >= 2)
				{
					SetRapidIterationValue(
						UniqueEmitterName,
						TargetScript,
						ModuleNode,
						FName(*Input.Name),
						FNiagaraTypeDefinition::GetVec2Def(),
						FVector2f(VectorValues[0], VectorValues[1]));
				}
				else if (InputType == FNiagaraTypeDefinition::GetVec3Def() && ComponentCount >= 3)
				{
					SetRapidIterationValue(
						UniqueEmitterName,
						TargetScript,
						ModuleNode,
						FName(*Input.Name),
						FNiagaraTypeDefinition::GetVec3Def(),
						FVector3f(VectorValues[0], VectorValues[1], VectorValues[2]));
				}
				else if (InputType == FNiagaraTypeDefinition::GetPositionDef() && ComponentCount >= 3)
				{
					SetRapidIterationValue(
						UniqueEmitterName,
						TargetScript,
						ModuleNode,
						FName(*Input.Name),
						FNiagaraTypeDefinition::GetPositionDef(),
						FNiagaraPosition(VectorValues[0], VectorValues[1], VectorValues[2]));
				}
				else if (InputType == FNiagaraTypeDefinition::GetVec4Def() && ComponentCount >= 4)
				{
					SetRapidIterationValue(
						UniqueEmitterName,
						TargetScript,
						ModuleNode,
						FName(*Input.Name),
						FNiagaraTypeDefinition::GetVec4Def(),
						FVector4f(VectorValues[0], VectorValues[1], VectorValues[2], VectorValues[3]));
				}
				else
				{
					return false;
				}
				return true;
			}

			return false;
		}

		bool ApplyCurveLiteral(
			FGenerationContext& Context,
			const FNiagaraTypeDefinition& InputType,
			UNiagaraNodeFunctionCall& ModuleNode,
			const FDreamNiagaraAssignment& Input)
		{
			if (!FLiteralParser::IsCurveDataInterfaceType(InputType))
			{
				return false;
			}

			FCurveLiteral CurveLiteral;
			FString CurveError;
			if (!FLiteralParser::TryParseCurve(Input.Value, CurveLiteral, CurveError))
			{
				Context.AddWarning(FString::Printf(
					TEXT("Module '%s' input '%s' expects a Niagara curve data interface, but value '%s' is not a valid inline curve literal%s%s."),
					*ModuleNode.GetFunctionName(),
					*Input.Name,
					*Input.Value.Text,
					CurveError.IsEmpty() ? TEXT("") : TEXT(": "),
					CurveError.IsEmpty() ? TEXT("") : *CurveError));
				return true;
			}

			if (!InputType.GetClass() || !InputType.GetClass()->IsChildOf(FLiteralParser::GetCurveClass(CurveLiteral.Kind)))
			{
				Context.AddWarning(FString::Printf(
					TEXT("Module '%s' input '%s' expects curve data interface class '%s', but value '%s' uses the wrong curve channel type."),
					*ModuleNode.GetFunctionName(),
					*Input.Name,
					*InputType.GetName(),
					*Input.Value.Text));
				return true;
			}

			UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
				ModuleNode,
				MakeAliasedInputHandle(ModuleNode, FName(*Input.Name)),
				InputType,
				FGuid(),
				FGuid());
			if (OverridePin.LinkedTo.Num() != 0)
			{
				Context.AddWarning(FString::Printf(
					TEXT("Module '%s' input '%s' already has an override graph link; inline curve literal was not applied."),
					*ModuleNode.GetFunctionName(),
					*Input.Name));
				return true;
			}

			UNiagaraDataInterface* DataInterface = nullptr;
			FNiagaraStackGraphUtilities::SetDataInterfaceValueForFunctionInput(
				OverridePin,
				InputType.GetClass(),
				MakeAliasedInputHandle(ModuleNode, FName(*Input.Name)).GetParameterHandleString().ToString(),
				DataInterface);

			UNiagaraDataInterfaceCurveBase* CurveDataInterface = Cast<UNiagaraDataInterfaceCurveBase>(DataInterface);
			if (!CurveDataInterface)
			{
				Context.AddWarning(FString::Printf(
					TEXT("Module '%s' input '%s' could not create Niagara curve data interface override."),
					*ModuleNode.GetFunctionName(),
					*Input.Name));
				return true;
			}

			if (!FLiteralParser::ApplyCurveToDataInterface(CurveLiteral, *CurveDataInterface, CurveError))
			{
				Context.AddWarning(FString::Printf(
					TEXT("Module '%s' input '%s' inline curve literal could not be applied: %s"),
					*ModuleNode.GetFunctionName(),
					*Input.Name,
					*CurveError));
			}
			return true;
		}
	}

	void FModuleInputApplier::ApplyInputs(
		FGenerationContext& Context,
		const FString& UniqueEmitterName,
		UNiagaraSystem& System,
		const FVersionedNiagaraEmitter& VersionedEmitter,
		UNiagaraScript& TargetScript,
		UNiagaraNodeFunctionCall& ModuleNode,
		const TArray<FDreamNiagaraAssignment>& Inputs)
	{
		if (Inputs.IsEmpty())
		{
			return;
		}

		// 0.1.0 intentionally records inputs without forcing unsafe graph edits.
		// Typed Niagara override setters are isolated here for future expansion.
		for (const FDreamNiagaraAssignment& Input : Inputs)
		{
			if (Input.Name.IsEmpty())
			{
				Context.AddWarning(FString::Printf(TEXT("Module '%s' has an empty input name."), *ModuleNode.GetFunctionName()));
				continue;
			}

			FNiagaraTypeDefinition InputType;
			if (!FindInputType(VersionedEmitter, TargetScript, ModuleNode, Input.Name, InputType))
			{
				Context.AddWarning(FString::Printf(
					TEXT("Could not resolve input type for '%s' on module '%s'; value '%s' was not applied."),
					*Input.Name,
					*ModuleNode.GetFunctionName(),
					*Input.Value.Text));
				continue;
			}

			if (ApplyLinkedUserParameter(Context, System, InputType, ModuleNode, Input))
			{
				continue;
			}

			if (ApplyCurveLiteral(Context, InputType, ModuleNode, Input))
			{
				continue;
			}

			if (ApplyBasicLiteral(UniqueEmitterName, InputType, TargetScript, ModuleNode, Input))
			{
				continue;
			}

			Context.AddWarning(FString::Printf(
				TEXT("Input override '%s = %s' for module '%s' is not a supported 0.1.0 literal yet."),
				*Input.Name,
				*Input.Value.Text,
				*ModuleNode.GetFunctionName()));
		}
	}
}
