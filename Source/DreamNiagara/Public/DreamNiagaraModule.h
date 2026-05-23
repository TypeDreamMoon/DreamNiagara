#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMNIAGARA_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamNiagara, Log, All);

class FDreamNiagaraModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

namespace UE::DreamNiagara
{
	DREAMNIAGARA_API FString GetSourceDirectory();
	DREAMNIAGARA_API FString NormalizeSourceFilePath(const FString& InPath);
	DREAMNIAGARA_API bool IsDreamNiagaraSystemFile(const FString& InPath);
	DREAMNIAGARA_API FString SanitizeIdentifier(const FString& InText);
}
