#pragma once

#include "DreamNiagaraParser.h"

namespace UE::DreamNiagara::Private
{
	struct FDnsScanner
	{
		explicit FDnsScanner(const FString& InSource);

		const FString& Source;
		int32 Index = 0;
		int32 Line = 1;

		bool IsAtEnd() const;
		TCHAR Peek(int32 Offset = 0) const;
		void SkipIgnored();
		bool TryConsume(TCHAR Expected);
		bool TryConsumeKeyword(const TCHAR* Keyword);
		bool Expect(TCHAR Expected, FString& OutError);
		bool ExpectKeyword(const TCHAR* Keyword, FString& OutError);
		bool ParseIdentifier(FString& OutIdentifier, FString& OutError);
		bool ParseDottedIdentifier(FString& OutIdentifier, FString& OutError);
		bool ParsePathOrString(FString& OutText, FString& OutError);
		bool ExtractBalancedBlock(FString& OutBlock, FString& OutError);
		bool ParseAssignmentValue(FDreamNiagaraValue& OutValue, FString& OutError);
		FString BuildError(const FString& Message) const;

	private:
		void Advance();
	};

	TArray<FString> SplitTopLevelStatements(const FString& InText);
	bool SplitTopLevelAssignment(const FString& InText, FString& OutLeft, FString& OutRight);
	FString Unquote(const FString& InText);
}
