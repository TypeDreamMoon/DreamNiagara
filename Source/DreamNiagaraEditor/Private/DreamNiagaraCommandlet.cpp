#include "DreamNiagaraCommandlet.h"

#include "DreamNiagaraModule.h"
#include "DreamNiagaraSystemGenerator.h"

#include "HAL/FileManager.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* GetDreamNiagaraCommandletUsage()
	{
		return TEXT(
			"Usage:\n"
			"  -run=DreamNiagara compile -Source=\"C:/Project/DNiagara/File.dns\" [-Force]\n"
			"  -run=DreamNiagara compile -All [-Force]");
	}

	FString NormalizeValue(FString Value)
	{
		Value.TrimStartAndEndInline();
		Value = Value.TrimQuotes();
		Value.TrimStartAndEndInline();
		return Value;
	}

	FString NormalizeKey(FString Key)
	{
		Key.TrimStartAndEndInline();
		while (Key.StartsWith(TEXT("-")))
		{
			Key.RightChopInline(1, EAllowShrinking::No);
		}
		Key.TrimStartAndEndInline();
		return Key;
	}

	bool TrySplitAssignment(const FString& Text, FString& OutKey, FString& OutValue)
	{
		FString Key;
		FString Value;
		if (!Text.Split(TEXT("="), &Key, &Value))
		{
			return false;
		}

		OutKey = NormalizeKey(Key);
		OutValue = NormalizeValue(Value);
		return !OutKey.IsEmpty();
	}

	bool TryGetParam(
		const TArray<FString>& Tokens,
		const TArray<FString>& Switches,
		const TMap<FString, FString>& Params,
		const FString& Name,
		FString& OutValue)
	{
		for (const TPair<FString, FString>& Param : Params)
		{
			if (NormalizeKey(Param.Key).Equals(Name, ESearchCase::IgnoreCase))
			{
				OutValue = NormalizeValue(Param.Value);
				return !OutValue.IsEmpty();
			}
		}

		for (const FString& Switch : Switches)
		{
			FString Key;
			FString Value;
			if (TrySplitAssignment(Switch, Key, Value) && Key.Equals(Name, ESearchCase::IgnoreCase))
			{
				OutValue = MoveTemp(Value);
				return !OutValue.IsEmpty();
			}
		}

		for (const FString& Token : Tokens)
		{
			FString Key;
			FString Value;
			if (TrySplitAssignment(Token, Key, Value) && Key.Equals(Name, ESearchCase::IgnoreCase))
			{
				OutValue = MoveTemp(Value);
				return !OutValue.IsEmpty();
			}
		}

		return false;
	}

	bool HasFlag(const TArray<FString>& Tokens, const TArray<FString>& Switches, const FString& Name)
	{
		for (const FString& Switch : Switches)
		{
			FString Key;
			FString Value;
			if (!TrySplitAssignment(Switch, Key, Value))
			{
				Key = NormalizeKey(Switch);
			}

			if (Key.Equals(Name, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		for (const FString& Token : Tokens)
		{
			FString Key;
			FString Value;
			if (!TrySplitAssignment(Token, Key, Value))
			{
				Key = NormalizeKey(Token);
			}

			if (Key.Equals(Name, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	FString ResolveSourceFilePath(const FString& InSourceFilePath)
	{
		FString SourceFilePath = NormalizeValue(InSourceFilePath);
		if (SourceFilePath.IsEmpty() || !FPaths::IsRelative(SourceFilePath))
		{
			return UE::DreamNiagara::NormalizeSourceFilePath(SourceFilePath);
		}

		const FString SourceDirectoryCandidate = UE::DreamNiagara::NormalizeSourceFilePath(
			FPaths::Combine(UE::DreamNiagara::GetSourceDirectory(), SourceFilePath));
		if (IFileManager::Get().FileExists(*SourceDirectoryCandidate))
		{
			return SourceDirectoryCandidate;
		}

		return UE::DreamNiagara::NormalizeSourceFilePath(FPaths::Combine(FPaths::ProjectDir(), SourceFilePath));
	}
}

UDreamNiagaraCommandlet::UDreamNiagaraCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UDreamNiagaraCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamMap);

	if (!Tokens.IsEmpty() && !Tokens[0].Equals(TEXT("compile"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogDreamNiagara, Error, TEXT("%s"), GetDreamNiagaraCommandletUsage());
		return 1;
	}

	const bool bForce = HasFlag(Tokens, Switches, TEXT("Force"));
	const bool bAll = HasFlag(Tokens, Switches, TEXT("All"));

	TArray<FString> SourceFiles;
	FString SourceFilePath;
	if (TryGetParam(Tokens, Switches, ParamMap, TEXT("Source"), SourceFilePath)
		|| TryGetParam(Tokens, Switches, ParamMap, TEXT("File"), SourceFilePath))
	{
		SourceFiles.Add(ResolveSourceFilePath(SourceFilePath));
	}
	else if (bAll)
	{
		IFileManager::Get().FindFilesRecursive(SourceFiles, *UE::DreamNiagara::GetSourceDirectory(), TEXT("*.dns"), true, false);
	}
	else
	{
		UE_LOG(LogDreamNiagara, Error, TEXT("%s"), GetDreamNiagaraCommandletUsage());
		return 1;
	}

	if (SourceFiles.IsEmpty())
	{
		UE_LOG(LogDreamNiagara, Warning, TEXT("DreamNiagara found no .dns files to compile."));
		return 0;
	}

	bool bSucceeded = true;
	for (const FString& SourceFile : SourceFiles)
	{
		const UE::DreamNiagara::SystemEditor::FDreamNiagaraSystemGenerateResult Result =
			UE::DreamNiagara::SystemEditor::FSystemGenerator::GenerateFromFile(SourceFile, bForce);
		if (Result.bSucceeded)
		{
			UE_LOG(LogDreamNiagara, Display, TEXT("%s"), *Result.Message);
		}
		else
		{
			UE_LOG(LogDreamNiagara, Error, TEXT("%s"), *Result.Message);
			bSucceeded = false;
		}
	}

	return bSucceeded ? 0 : 1;
}
