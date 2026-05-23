#include "DreamNiagaraEditorBridge.h"

#include "DreamNiagaraModule.h"
#include "DreamNiagaraSystemGenerator.h"

#include "DirectoryWatcherModule.h"
#include "HAL/FileManager.h"
#include "IDirectoryWatcher.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"

namespace UE::DreamNiagara::Editor::Private
{
	namespace
	{
		constexpr double FileDebounceSeconds = 0.35;

		double GetSecondsNow()
		{
			return FPlatformTime::Seconds();
		}

		FString ResolveChangedFilePath(const FString& WatchedDirectory, const FString& ChangedFilePath)
		{
			FString Path = ChangedFilePath;
			if (FPaths::IsRelative(Path))
			{
				Path = FPaths::Combine(WatchedDirectory, Path);
			}
			return UE::DreamNiagara::NormalizeSourceFilePath(Path);
		}
	}

	void FDreamNiagaraEditorBridge::Startup()
	{
		bIsShuttingDown = false;
		WatchedSourceDirectory = UE::DreamNiagara::GetSourceDirectory();
		IFileManager::Get().MakeDirectory(*WatchedSourceDirectory, true);

		FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
		if (IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get())
		{
			const bool bWatcherRegistered = DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
				WatchedSourceDirectory,
				IDirectoryWatcher::FDirectoryChanged::CreateSP(AsShared(), &FDreamNiagaraEditorBridge::OnDirectoryChanged),
				DirectoryWatcherHandle,
				IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges);
			if (!bWatcherRegistered)
			{
				UE_LOG(LogDreamNiagara, Warning, TEXT("DreamNiagara could not register directory watcher for '%s'."), *WatchedSourceDirectory);
			}
		}
		else
		{
			UE_LOG(LogDreamNiagara, Warning, TEXT("DreamNiagara directory watcher module is loaded but did not provide a watcher."));
		}

		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateSP(AsShared(), &FDreamNiagaraEditorBridge::Tick),
			0.1f);

		QueueFullScan();
		UE_LOG(LogDreamNiagara, Display, TEXT("DreamNiagara watching '%s'."), *WatchedSourceDirectory);
	}

	void FDreamNiagaraEditorBridge::Shutdown()
	{
		bIsShuttingDown = true;

		if (TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			TickerHandle.Reset();
		}

		if (DirectoryWatcherHandle.IsValid())
		{
			if (FDirectoryWatcherModule* DirectoryWatcherModule = FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")))
			{
				if (IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule->Get())
				{
					DirectoryWatcher->UnregisterDirectoryChangedCallback_Handle(WatchedSourceDirectory, DirectoryWatcherHandle);
				}
			}
			DirectoryWatcherHandle.Reset();
		}

		PendingFiles.Empty();
	}

	void FDreamNiagaraEditorBridge::QueueFullScan()
	{
		TArray<FString> SourceFiles;
		IFileManager::Get().FindFilesRecursive(SourceFiles, *WatchedSourceDirectory, TEXT("*.dns"), true, false);
		for (const FString& SourceFile : SourceFiles)
		{
			QueueSourceFile(SourceFile);
		}
	}

	void FDreamNiagaraEditorBridge::QueueSourceFile(const FString& SourceFilePath)
	{
		const FString NormalizedPath = ResolveChangedFilePath(WatchedSourceDirectory, SourceFilePath);
		if (!UE::DreamNiagara::IsDreamNiagaraSystemFile(NormalizedPath))
		{
			return;
		}

		PendingFiles.Add(NormalizedPath, GetSecondsNow() + FileDebounceSeconds);
	}

	void FDreamNiagaraEditorBridge::OnDirectoryChanged(const TArray<FFileChangeData>& FileChanges)
	{
		for (const FFileChangeData& Change : FileChanges)
		{
			QueueSourceFile(Change.Filename);
		}
	}

	bool FDreamNiagaraEditorBridge::Tick(float DeltaSeconds)
	{
		if (bIsShuttingDown)
		{
			return false;
		}

		ProcessReadyFiles();
		return true;
	}

	void FDreamNiagaraEditorBridge::ProcessReadyFiles()
	{
		const double Now = GetSecondsNow();
		TArray<FString> ReadyFiles;
		for (const TPair<FString, double>& PendingFile : PendingFiles)
		{
			if (PendingFile.Value <= Now)
			{
				ReadyFiles.Add(PendingFile.Key);
			}
		}

		for (const FString& ReadyFile : ReadyFiles)
		{
			PendingFiles.Remove(ReadyFile);
			ProcessSourceFile(ReadyFile);
		}
	}

	void FDreamNiagaraEditorBridge::ProcessSourceFile(const FString& SourceFilePath)
	{
		if (!IFileManager::Get().FileExists(*SourceFilePath))
		{
			return;
		}

		const UE::DreamNiagara::SystemEditor::FDreamNiagaraSystemGenerateResult Result =
			UE::DreamNiagara::SystemEditor::FSystemGenerator::GenerateFromFile(SourceFilePath);
		if (!Result.bSucceeded)
		{
			UE_LOG(LogDreamNiagara, Error, TEXT("%s"), *Result.Message);
		}
	}
}
