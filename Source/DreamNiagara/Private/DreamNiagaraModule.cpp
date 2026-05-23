#include "DreamNiagaraModule.h"

#include "HAL/FileManager.h"
#include "Misc/Char.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogDreamNiagara);

namespace UE::DreamNiagara
{
	FString GetSourceDirectory()
	{
		FString Result = FPaths::Combine(FPaths::ProjectDir(), TEXT("DNiagara"));
		FPaths::NormalizeFilename(Result);
		FPaths::MakeStandardFilename(Result);
		return Result;
	}

	FString NormalizeSourceFilePath(const FString& InPath)
	{
		FString Result = FPaths::ConvertRelativePathToFull(InPath);
		FPaths::NormalizeFilename(Result);
		FPaths::MakeStandardFilename(Result);
		return Result;
	}

	bool IsDreamNiagaraSystemFile(const FString& InPath)
	{
		return FPaths::GetExtension(InPath, true).Equals(TEXT(".dns"), ESearchCase::IgnoreCase);
	}

	FString SanitizeIdentifier(const FString& InText)
	{
		FString Result;
		Result.Reserve(InText.Len() + 1);

		for (TCHAR Char : InText)
		{
			if (FChar::IsAlnum(Char) || Char == TCHAR('_'))
			{
				Result.AppendChar(Char);
			}
			else
			{
				Result.AppendChar(TEXT('_'));
			}
		}

		if (Result.IsEmpty())
		{
			Result = TEXT("DreamNiagaraSymbol");
		}

		if (!(FChar::IsAlpha(Result[0]) || Result[0] == TCHAR('_')))
		{
			Result.InsertAt(0, TCHAR('_'));
		}

		for (int32 Index = Result.Len() - 1; Index > 0; --Index)
		{
			if (Result[Index] == TCHAR('_') && Result[Index - 1] == TCHAR('_'))
			{
				Result.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}

		return Result;
	}
}

void FDreamNiagaraModule::StartupModule()
{
	IFileManager::Get().MakeDirectory(*UE::DreamNiagara::GetSourceDirectory(), true);
}

void FDreamNiagaraModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FDreamNiagaraModule, DreamNiagara);
