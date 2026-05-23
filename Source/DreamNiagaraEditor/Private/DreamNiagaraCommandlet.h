#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "DreamNiagaraCommandlet.generated.h"

UCLASS()
class UDreamNiagaraCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDreamNiagaraCommandlet();

	virtual int32 Main(const FString& Params) override;
};
