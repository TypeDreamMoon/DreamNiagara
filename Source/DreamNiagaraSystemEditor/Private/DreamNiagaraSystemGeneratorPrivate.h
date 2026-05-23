#pragma once

#include "DreamNiagaraSystemGenerator.h"
#include "DreamNiagaraTypes.h"

#include "AssetRegistry/AssetData.h"
#include "NiagaraScript.h"

class UNiagaraEmitter;
class UNiagaraNodeOutput;
class UNiagaraScript;
class UNiagaraSystem;

namespace UE::DreamNiagara::SystemEditor::Private
{
	struct FGenerationContext
	{
		const UE::DreamNiagara::FDreamNiagaraSystem* Definition = nullptr;
		FString SourceFilePath;
		TArray<FString> Warnings;

		void AddWarning(const FString& Warning);
	};

	enum class EDreamNiagaraStackUsage : uint8
	{
		SystemSpawn,
		SystemUpdate,
		EmitterSpawn,
		EmitterUpdate,
		ParticleSpawn,
		ParticleUpdate,
		ParticleEvent,
		SimulationStage,
		Unknown,
	};

	struct FResolvedModule
	{
		FAssetData AssetData;
		UNiagaraScript* Script = nullptr;
	};

	bool ResolveAssetDestination(
		const UE::DreamNiagara::FDreamNiagaraSystem& Definition,
		FString& OutPackageName,
		FString& OutAssetName,
		FString& OutObjectPath,
		FString& OutError);

	EDreamNiagaraStackUsage ParseStackUsage(const FString& StackName);
	ENiagaraScriptUsage ToNiagaraScriptUsage(EDreamNiagaraStackUsage Usage);
	FString ToInternalUsageSegment(EDreamNiagaraStackUsage Usage);

	bool ResolveModule(
		const FString& ModuleId,
		EDreamNiagaraStackUsage StackUsage,
		FResolvedModule& OutModule,
		FString& OutError);
}
