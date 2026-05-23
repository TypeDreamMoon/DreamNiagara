#include "DreamNiagaraSystemLiteralParser.h"

#include "Misc/DefaultValueHelper.h"

namespace UE::DreamNiagara::SystemEditor::Private
{
	bool FLiteralParser::ParseFloat(const FString& Text, float& OutValue)
	{
		const FString Trimmed = Text.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return false;
		}

		return FDefaultValueHelper::ParseFloat(Trimmed, OutValue);
	}

	bool FLiteralParser::ParseInt(const FString& Text, int32& OutValue)
	{
		const FString Trimmed = Text.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return false;
		}

		TCHAR* EndPtr = nullptr;
		const int32 Parsed = FCString::Strtoi(*Trimmed, &EndPtr, 10);
		if (EndPtr == *Trimmed || (EndPtr && *EndPtr != TCHAR('\0')))
		{
			return false;
		}

		OutValue = Parsed;
		return true;
	}

	bool FLiteralParser::ParseBool(const FString& Text, bool& OutValue)
	{
		const FString Trimmed = Text.TrimStartAndEnd().ToLower();
		if (Trimmed == TEXT("true") || Trimmed == TEXT("1") || Trimmed == TEXT("yes") || Trimmed == TEXT("on"))
		{
			OutValue = true;
			return true;
		}
		if (Trimmed == TEXT("false") || Trimmed == TEXT("0") || Trimmed == TEXT("no") || Trimmed == TEXT("off"))
		{
			OutValue = false;
			return true;
		}
		return false;
	}

	bool FLiteralParser::ParseVector(const FString& Text, TArray<float>& OutValues)
	{
		FString Trimmed = Text.TrimStartAndEnd();
		if (Trimmed.StartsWith(TEXT("vec2"), ESearchCase::IgnoreCase)
			|| Trimmed.StartsWith(TEXT("vec3"), ESearchCase::IgnoreCase)
			|| Trimmed.StartsWith(TEXT("vec4"), ESearchCase::IgnoreCase)
			|| Trimmed.StartsWith(TEXT("color"), ESearchCase::IgnoreCase))
		{
			const int32 OpenParen = Trimmed.Find(TEXT("("));
			const int32 CloseParen = Trimmed.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (OpenParen != INDEX_NONE && CloseParen > OpenParen)
			{
				Trimmed = Trimmed.Mid(OpenParen + 1, CloseParen - OpenParen - 1);
			}
		}
		else if (Trimmed.StartsWith(TEXT("(")) && Trimmed.EndsWith(TEXT(")")))
		{
			Trimmed = Trimmed.Mid(1, Trimmed.Len() - 2);
		}
		else
		{
			return false;
		}

		TArray<FString> Parts;
		Trimmed.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.IsEmpty() || Parts.Num() > 4)
		{
			return false;
		}

		TArray<float> Values;
		for (const FString& Part : Parts)
		{
			float Value = 0.0f;
			if (!ParseFloat(Part, Value))
			{
				return false;
			}
			Values.Add(Value);
		}

		OutValues = MoveTemp(Values);
		return true;
	}

	bool FLiteralParser::ParseHexColor(const FString& Text, FLinearColor& OutColor)
	{
		FString Trimmed = Text.TrimStartAndEnd();
		if (!Trimmed.StartsWith(TEXT("#")))
		{
			return false;
		}

		Trimmed.RightChopInline(1, EAllowShrinking::No);
		if (Trimmed.Len() != 6 && Trimmed.Len() != 8)
		{
			return false;
		}

		auto HexByte = [](const FString& Part, uint8& OutByte)
		{
			if (Part.Len() != 2)
			{
				return false;
			}

			for (const TCHAR Char : Part)
			{
				if (!FChar::IsHexDigit(Char))
				{
					return false;
				}
			}

			OutByte = static_cast<uint8>(FCString::Strtoi(*Part, nullptr, 16));
			return true;
		};

		uint8 R = 0;
		uint8 G = 0;
		uint8 B = 0;
		uint8 A = 255;
		if (!HexByte(Trimmed.Mid(0, 2), R)
			|| !HexByte(Trimmed.Mid(2, 2), G)
			|| !HexByte(Trimmed.Mid(4, 2), B)
			|| (Trimmed.Len() == 8 && !HexByte(Trimmed.Mid(6, 2), A)))
		{
			return false;
		}

		OutColor = FLinearColor(
			static_cast<float>(R) / 255.0f,
			static_cast<float>(G) / 255.0f,
			static_cast<float>(B) / 255.0f,
			static_cast<float>(A) / 255.0f);
		return true;
	}

	bool FLiteralParser::ParseAssetPath(const FString& Text, FString& OutAssetPath)
	{
		FString Trimmed = Text.TrimStartAndEnd();
		if (Trimmed.StartsWith(TEXT("asset("), ESearchCase::IgnoreCase) && Trimmed.EndsWith(TEXT(")")))
		{
			Trimmed = Trimmed.Mid(6, Trimmed.Len() - 7).TrimStartAndEnd();
		}

		Trimmed = Trimmed.TrimQuotes().TrimStartAndEnd();
		if (Trimmed.IsEmpty() || !Trimmed.StartsWith(TEXT("/")))
		{
			return false;
		}

		OutAssetPath = MoveTemp(Trimmed);
		return true;
	}
}
