#include "Modules/ModuleManager.h"

class FDreamNiagaraSystemEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FDreamNiagaraSystemEditorModule, DreamNiagaraSystemEditor)
