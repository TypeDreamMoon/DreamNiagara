#pragma once

#include "DreamNiagaraTypes.h"

namespace UE::DreamNiagara
{
	class DREAMNIAGARA_API FDreamNiagaraParser
	{
	public:
		static bool ParseSystem(const FString& SourceText, FDreamNiagaraSystem& OutSystem, FString& OutError);
	};
}
