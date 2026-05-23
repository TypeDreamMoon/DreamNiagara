#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

struct FFileChangeData;

namespace UE::DreamNiagara::Editor::Private
{
	class FDreamNiagaraEditorBridge : public TSharedFromThis<FDreamNiagaraEditorBridge, ESPMode::ThreadSafe>
	{
	public:
		void Startup();
		void Shutdown();

	private:
		void QueueFullScan();
		void QueueSourceFile(const FString& SourceFilePath);
		void OnDirectoryChanged(const TArray<FFileChangeData>& FileChanges);
		bool Tick(float DeltaSeconds);
		void ProcessReadyFiles();
		void ProcessSourceFile(const FString& SourceFilePath);

	private:
		TMap<FString, double> PendingFiles;
		FString WatchedSourceDirectory;
		FDelegateHandle DirectoryWatcherHandle;
		FTSTicker::FDelegateHandle TickerHandle;
		bool bIsShuttingDown = false;
	};
}
