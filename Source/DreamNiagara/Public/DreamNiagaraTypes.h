#pragma once

#include "CoreMinimal.h"

namespace UE::DreamNiagara
{
	enum class EDreamNiagaraSimTarget : uint8
	{
		Cpu,
		Gpu,
	};

	enum class EDreamNiagaraValueKind : uint8
	{
		Expression,
		Block,
	};

	struct FDreamNiagaraValue
	{
		EDreamNiagaraValueKind Kind = EDreamNiagaraValueKind::Expression;
		FString Text;
	};

	struct FDreamNiagaraAssignment
	{
		FString Name;
		FDreamNiagaraValue Value;
		int32 Line = 1;
	};

	struct FDreamNiagaraUserParameter
	{
		FString Name;
		FString Type;
		FDreamNiagaraValue DefaultValue;
		bool bHasDefaultValue = false;
		int32 Line = 1;
	};

	struct FDreamNiagaraModuleCall
	{
		FString ModuleId;
		TArray<FDreamNiagaraAssignment> Inputs;
		int32 Line = 1;
	};

	struct FDreamNiagaraStack
	{
		FString Name;
		TArray<FDreamNiagaraModuleCall> Modules;
		int32 Line = 1;
	};

	struct FDreamNiagaraRenderer
	{
		FString Type;
		TArray<FDreamNiagaraAssignment> Properties;
		int32 Line = 1;
	};

	struct FDreamNiagaraEmitter
	{
		FString Name;
		EDreamNiagaraSimTarget SimTarget = EDreamNiagaraSimTarget::Cpu;
		TArray<FDreamNiagaraStack> Stacks;
		TArray<FDreamNiagaraRenderer> Renderers;
		int32 Line = 1;
	};

	struct FDreamNiagaraSystem
	{
		FString Name;
		FString Root;
		TArray<FDreamNiagaraUserParameter> UserParameters;
		TArray<FDreamNiagaraEmitter> Emitters;
		int32 Line = 1;
	};

	DREAMNIAGARA_API FString ToString(EDreamNiagaraSimTarget InSimTarget);
}
