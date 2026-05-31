#include "DreamNiagaraParserInternal.h"

#include "Misc/Char.h"

namespace UE::DreamNiagara::Private
{
	FDnsScanner::FDnsScanner(const FString& InSource)
		: Source(InSource)
	{
	}

	bool FDnsScanner::IsAtEnd() const
	{
		return Index >= Source.Len();
	}

	TCHAR FDnsScanner::Peek(const int32 Offset) const
	{
		const int32 TargetIndex = Index + Offset;
		return Source.IsValidIndex(TargetIndex) ? Source[TargetIndex] : TCHAR('\0');
	}

	void FDnsScanner::Advance()
	{
		if (IsAtEnd())
		{
			return;
		}

		if (Source[Index] == TCHAR('\n'))
		{
			++Line;
		}
		++Index;
	}

	void FDnsScanner::SkipIgnored()
	{
		while (!IsAtEnd())
		{
			const TCHAR Char = Peek();
			const TCHAR Next = Peek(1);

			if (FChar::IsWhitespace(Char))
			{
				Advance();
				continue;
			}

			if (Char == TCHAR('/') && Next == TCHAR('/'))
			{
				while (!IsAtEnd() && Peek() != TCHAR('\n'))
				{
					Advance();
				}
				continue;
			}

			if (Char == TCHAR('/') && Next == TCHAR('*'))
			{
				Advance();
				Advance();
				while (!IsAtEnd())
				{
					if (Peek() == TCHAR('*') && Peek(1) == TCHAR('/'))
					{
						Advance();
						Advance();
						break;
					}
					Advance();
				}
				continue;
			}

			break;
		}
	}

	bool FDnsScanner::TryConsume(const TCHAR Expected)
	{
		SkipIgnored();
		if (Peek() != Expected)
		{
			return false;
		}
		Advance();
		return true;
	}

	bool FDnsScanner::TryConsumeKeyword(const TCHAR* Keyword)
	{
		SkipIgnored();

		const int32 Start = Index;
		const int32 KeywordLength = FCString::Strlen(Keyword);
		if (!Source.Mid(Start, KeywordLength).Equals(Keyword, ESearchCase::IgnoreCase))
		{
			return false;
		}

		const auto IsIdentifierChar = [](const TCHAR Char)
		{
			return FChar::IsAlnum(Char) || Char == TCHAR('_');
		};

		const TCHAR Before = Source.IsValidIndex(Start - 1) ? Source[Start - 1] : TCHAR('\0');
		const TCHAR After = Source.IsValidIndex(Start + KeywordLength) ? Source[Start + KeywordLength] : TCHAR('\0');
		if (IsIdentifierChar(Before) || IsIdentifierChar(After))
		{
			return false;
		}

		for (int32 Count = 0; Count < KeywordLength; ++Count)
		{
			Advance();
		}
		return true;
	}

	bool FDnsScanner::Expect(const TCHAR Expected, FString& OutError)
	{
		if (TryConsume(Expected))
		{
			return true;
		}

		OutError = BuildError(FString::Printf(TEXT("Expected '%c'."), Expected));
		return false;
	}

	bool FDnsScanner::ExpectKeyword(const TCHAR* Keyword, FString& OutError)
	{
		if (TryConsumeKeyword(Keyword))
		{
			return true;
		}

		OutError = BuildError(FString::Printf(TEXT("Expected keyword '%s'."), Keyword));
		return false;
	}

	bool FDnsScanner::ParseIdentifier(FString& OutIdentifier, FString& OutError)
	{
		SkipIgnored();
		if (!(FChar::IsAlpha(Peek()) || Peek() == TCHAR('_')))
		{
			OutError = BuildError(TEXT("Expected identifier."));
			return false;
		}

		const int32 Start = Index;
		while (!IsAtEnd() && (FChar::IsAlnum(Peek()) || Peek() == TCHAR('_')))
		{
			Advance();
		}

		OutIdentifier = Source.Mid(Start, Index - Start);
		return true;
	}

	bool FDnsScanner::ParseDottedIdentifier(FString& OutIdentifier, FString& OutError)
	{
		FString Result;
		if (!ParseIdentifier(Result, OutError))
		{
			return false;
		}

		while (true)
		{
			const int32 SavedIndex = Index;
			const int32 SavedLine = Line;
			if (!TryConsume(TCHAR('.')))
			{
				break;
			}

			FString Segment;
			if (!ParseIdentifier(Segment, OutError))
			{
				Index = SavedIndex;
				Line = SavedLine;
				OutError = BuildError(TEXT("Expected identifier after '.'."));
				return false;
			}

			Result += TEXT(".");
			Result += Segment;
		}

		OutIdentifier = MoveTemp(Result);
		return true;
	}

	bool FDnsScanner::ParsePathOrString(FString& OutText, FString& OutError)
	{
		SkipIgnored();
		if (Peek() == TCHAR('"'))
		{
			const int32 Start = Index;
			Advance();
			bool bClosed = false;
			while (!IsAtEnd())
			{
				if (Peek() == TCHAR('\\') && Source.IsValidIndex(Index + 1))
				{
					Advance();
					Advance();
					continue;
				}

				if (Peek() == TCHAR('"'))
				{
					Advance();
					bClosed = true;
					break;
				}

				Advance();
			}

			if (!bClosed)
			{
				OutError = BuildError(TEXT("Unterminated string literal."));
				return false;
			}

			OutText = Unquote(Source.Mid(Start, Index - Start));
			return true;
		}

		const int32 Start = Index;
		while (!IsAtEnd())
		{
			const TCHAR Char = Peek();
			if (FChar::IsWhitespace(Char) || Char == TCHAR('{') || Char == TCHAR('}') || Char == TCHAR(';'))
			{
				break;
			}
			Advance();
		}

		OutText = Source.Mid(Start, Index - Start).TrimStartAndEnd();
		if (OutText.IsEmpty())
		{
			OutError = BuildError(TEXT("Expected path or string."));
			return false;
		}

		return true;
	}

	bool FDnsScanner::ExtractBalancedBlock(FString& OutBlock, FString& OutError)
	{
		SkipIgnored();
		if (Peek() != TCHAR('{'))
		{
			OutError = BuildError(TEXT("Expected block starting with '{'."));
			return false;
		}

		Advance();
		const int32 Start = Index;
		int32 Depth = 1;
		bool bInString = false;

		while (!IsAtEnd())
		{
			const TCHAR Char = Peek();
			if (bInString)
			{
				if (Char == TCHAR('\\') && Source.IsValidIndex(Index + 1))
				{
					Advance();
					Advance();
					continue;
				}
				if (Char == TCHAR('"'))
				{
					bInString = false;
				}
				Advance();
				continue;
			}

			if (Char == TCHAR('"'))
			{
				bInString = true;
				Advance();
				continue;
			}

			if (Char == TCHAR('{'))
			{
				++Depth;
				Advance();
				continue;
			}

			if (Char == TCHAR('}'))
			{
				--Depth;
				if (Depth == 0)
				{
					OutBlock = Source.Mid(Start, Index - Start);
					Advance();
					return true;
				}
				Advance();
				continue;
			}

			Advance();
		}

		OutError = BuildError(TEXT("Unterminated block."));
		return false;
	}

	bool FDnsScanner::ParseAssignmentValue(FDreamNiagaraValue& OutValue, FString& OutError)
	{
		SkipIgnored();
		const int32 Start = Index;

		int32 ParenthesisDepth = 0;
		int32 BracketDepth = 0;
		bool bInString = false;
		while (!IsAtEnd())
		{
			const TCHAR Char = Peek();
			if (bInString)
			{
				if (Char == TCHAR('\\') && Source.IsValidIndex(Index + 1))
				{
					Advance();
					Advance();
					continue;
				}
				if (Char == TCHAR('"'))
				{
					bInString = false;
				}
				Advance();
				continue;
			}

			if (Char == TCHAR('"'))
			{
				bInString = true;
				Advance();
				continue;
			}

			if (Char == TCHAR('('))
			{
				++ParenthesisDepth;
				Advance();
				continue;
			}

			if (Char == TCHAR(')'))
			{
				ParenthesisDepth = FMath::Max(0, ParenthesisDepth - 1);
				Advance();
				continue;
			}

			if (Char == TCHAR('['))
			{
				++BracketDepth;
				Advance();
				continue;
			}

			if (Char == TCHAR(']'))
			{
				BracketDepth = FMath::Max(0, BracketDepth - 1);
				Advance();
				continue;
			}

			if (ParenthesisDepth == 0 && BracketDepth == 0)
			{
				if (Char == TCHAR(';') || Char == TCHAR('\n') || Char == TCHAR('}'))
				{
					FString Prefix = Source.Mid(Start, Index - Start).TrimStartAndEnd();
					if (!Prefix.IsEmpty())
					{
						const int32 SavedIndex = Index;
						const int32 SavedLine = Line;
						SkipIgnored();
						if (Peek() == TCHAR('{'))
						{
							FString Block;
							if (!ExtractBalancedBlock(Block, OutError))
							{
								return false;
							}
							OutValue.Kind = EDreamNiagaraValueKind::Block;
							OutValue.Text = FString::Printf(TEXT("%s {%s}"), *Prefix, *Block);
							SkipIgnored();
							TryConsume(TCHAR(';'));
							return true;
						}
						Index = SavedIndex;
						Line = SavedLine;
					}
					break;
				}
				if (Char == TCHAR('{'))
				{
					const FString Prefix = Source.Mid(Start, Index - Start).TrimStartAndEnd();
					FString Block;
					if (!ExtractBalancedBlock(Block, OutError))
					{
						return false;
					}
					OutValue.Kind = EDreamNiagaraValueKind::Block;
					if (Prefix.IsEmpty())
					{
						OutValue.Text = Block.TrimStartAndEnd();
					}
					else
					{
						OutValue.Text = FString::Printf(TEXT("%s {%s}"), *Prefix, *Block);
					}
					SkipIgnored();
					TryConsume(TCHAR(';'));
					return true;
				}
			}

			Advance();
		}

		OutValue.Kind = EDreamNiagaraValueKind::Expression;
		OutValue.Text = Source.Mid(Start, Index - Start).TrimStartAndEnd();
		if (OutValue.Text.IsEmpty())
		{
			OutError = BuildError(TEXT("Assignment value cannot be empty."));
			return false;
		}

		if (Peek() == TCHAR(';') || Peek() == TCHAR('\n'))
		{
			Advance();
		}
		return true;
	}

	FString FDnsScanner::BuildError(const FString& Message) const
	{
		return FString::Printf(TEXT("Line %d: %s"), Line, *Message);
	}

	FString Unquote(const FString& InText)
	{
		FString Text = InText.TrimStartAndEnd();
		if (Text.Len() >= 2 && Text[0] == TCHAR('"') && Text[Text.Len() - 1] == TCHAR('"'))
		{
			Text = Text.Mid(1, Text.Len() - 2);
			Text.ReplaceInline(TEXT("\\\""), TEXT("\""));
			Text.ReplaceInline(TEXT("\\\\"), TEXT("\\"));
			Text.ReplaceInline(TEXT("\\n"), TEXT("\n"));
			Text.ReplaceInline(TEXT("\\t"), TEXT("\t"));
		}
		return Text;
	}
}
