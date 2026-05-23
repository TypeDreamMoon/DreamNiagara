#pragma once

#include "DreamNiagaraTypes.h"

class UNiagaraNodeFunctionCall;
class UNiagaraScript;
class UNiagaraSystem;
struct FVersionedNiagaraEmitter;

namespace UE::DreamNiagara::SystemEditor::Private
{
	struct FGenerationContext;

	class FModuleInputApplier
	{
	public:
		static void ApplyInputs(
			FGenerationContext& Context,
			const FString& UniqueEmitterName,
			UNiagaraSystem& System,
			const FVersionedNiagaraEmitter& VersionedEmitter,
			UNiagaraScript& TargetScript,
			UNiagaraNodeFunctionCall& ModuleNode,
			const TArray<UE::DreamNiagara::FDreamNiagaraAssignment>& Inputs);
	};
}
