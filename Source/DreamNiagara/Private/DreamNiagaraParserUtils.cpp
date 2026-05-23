#include "DreamNiagaraParserInternal.h"

namespace UE::DreamNiagara::Private
{
	TArray<FString> SplitTopLevelStatements(const FString& InText)
	{
		TArray<FString> Statements;
		FString Current;
		int32 ParenthesisDepth = 0;
		int32 BracketDepth = 0;
		int32 BraceDepth = 0;
		bool bInString = false;

		for (int32 Index = 0; Index < InText.Len(); ++Index)
		{
			const TCHAR Char = InText[Index];

			if (bInString)
			{
				Current.AppendChar(Char);
				if (Char == TCHAR('\\') && InText.IsValidIndex(Index + 1))
				{
					Current.AppendChar(InText[++Index]);
				}
				else if (Char == TCHAR('"'))
				{
					bInString = false;
				}
				continue;
			}

			if (Char == TCHAR('"'))
			{
				bInString = true;
				Current.AppendChar(Char);
				continue;
			}

			if (Char == TCHAR('('))
			{
				++ParenthesisDepth;
			}
			else if (Char == TCHAR(')'))
			{
				ParenthesisDepth = FMath::Max(0, ParenthesisDepth - 1);
			}
			else if (Char == TCHAR('['))
			{
				++BracketDepth;
			}
			else if (Char == TCHAR(']'))
			{
				BracketDepth = FMath::Max(0, BracketDepth - 1);
			}
			else if (Char == TCHAR('{'))
			{
				++BraceDepth;
			}
			else if (Char == TCHAR('}'))
			{
				BraceDepth = FMath::Max(0, BraceDepth - 1);
			}

			if ((Char == TCHAR(';') || Char == TCHAR('\n'))
				&& ParenthesisDepth == 0
				&& BracketDepth == 0
				&& BraceDepth == 0)
			{
				Current.TrimStartAndEndInline();
				if (!Current.IsEmpty())
				{
					Statements.Add(Current);
				}
				Current.Reset();
				continue;
			}

			Current.AppendChar(Char);
		}

		Current.TrimStartAndEndInline();
		if (!Current.IsEmpty())
		{
			Statements.Add(Current);
		}

		return Statements;
	}

	bool SplitTopLevelAssignment(const FString& InText, FString& OutLeft, FString& OutRight)
	{
		int32 ParenthesisDepth = 0;
		int32 BracketDepth = 0;
		int32 BraceDepth = 0;
		bool bInString = false;

		for (int32 Index = 0; Index < InText.Len(); ++Index)
		{
			const TCHAR Char = InText[Index];

			if (bInString)
			{
				if (Char == TCHAR('\\') && InText.IsValidIndex(Index + 1))
				{
					++Index;
				}
				else if (Char == TCHAR('"'))
				{
					bInString = false;
				}
				continue;
			}

			if (Char == TCHAR('"'))
			{
				bInString = true;
				continue;
			}

			if (Char == TCHAR('('))
			{
				++ParenthesisDepth;
				continue;
			}

			if (Char == TCHAR(')'))
			{
				ParenthesisDepth = FMath::Max(0, ParenthesisDepth - 1);
				continue;
			}

			if (Char == TCHAR('['))
			{
				++BracketDepth;
				continue;
			}

			if (Char == TCHAR(']'))
			{
				BracketDepth = FMath::Max(0, BracketDepth - 1);
				continue;
			}

			if (Char == TCHAR('{'))
			{
				++BraceDepth;
				continue;
			}

			if (Char == TCHAR('}'))
			{
				BraceDepth = FMath::Max(0, BraceDepth - 1);
				continue;
			}

			if (Char == TCHAR('=')
				&& ParenthesisDepth == 0
				&& BracketDepth == 0
				&& BraceDepth == 0)
			{
				OutLeft = InText.Left(Index).TrimStartAndEnd();
				OutRight = InText.Mid(Index + 1).TrimStartAndEnd();
				return !OutLeft.IsEmpty();
			}
		}

		return false;
	}
}
