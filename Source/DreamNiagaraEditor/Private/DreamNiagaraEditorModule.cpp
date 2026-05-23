#include "DreamNiagaraEditorBridge.h"

#include "CoreGlobals.h"
#include "Modules/ModuleManager.h"

class FDreamNiagaraEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (IsRunningCommandlet())
		{
			return;
		}

		Bridge = MakeShared<UE::DreamNiagara::Editor::Private::FDreamNiagaraEditorBridge, ESPMode::ThreadSafe>();
		Bridge->Startup();
	}

	virtual void ShutdownModule() override
	{
		if (Bridge)
		{
			Bridge->Shutdown();
			Bridge.Reset();
		}
	}

private:
	TSharedPtr<UE::DreamNiagara::Editor::Private::FDreamNiagaraEditorBridge, ESPMode::ThreadSafe> Bridge;
};

IMPLEMENT_MODULE(FDreamNiagaraEditorModule, DreamNiagaraEditor)
