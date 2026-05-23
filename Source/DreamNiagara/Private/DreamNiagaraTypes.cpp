#include "DreamNiagaraTypes.h"

namespace UE::DreamNiagara
{
	FString ToString(const EDreamNiagaraSimTarget InSimTarget)
	{
		switch (InSimTarget)
		{
		case EDreamNiagaraSimTarget::Cpu:
			return TEXT("cpu");
		case EDreamNiagaraSimTarget::Gpu:
			return TEXT("gpu");
		default:
			return TEXT("unknown");
		}
	}
}
