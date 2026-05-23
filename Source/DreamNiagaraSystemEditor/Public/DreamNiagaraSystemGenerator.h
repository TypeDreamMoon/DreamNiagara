#pragma once

#include "CoreMinimal.h"

namespace UE::DreamNiagara
{
	struct FDreamNiagaraSystem;
}

namespace UE::DreamNiagara::SystemEditor
{
	struct FDreamNiagaraSystemGenerateResult
	{
		bool bSucceeded = false;
		FString Message;
		TArray<FString> Warnings;
	};

	class DREAMNIAGARASYSTEMEDITOR_API FSystemGenerator
	{
	public:
		static FDreamNiagaraSystemGenerateResult GenerateFromFile(const FString& SourceFilePath, bool bForce = false);
		static FDreamNiagaraSystemGenerateResult GenerateFromDefinition(
			const UE::DreamNiagara::FDreamNiagaraSystem& Definition,
			const FString& SourceFilePath,
			bool bForce = false);
	};
}
