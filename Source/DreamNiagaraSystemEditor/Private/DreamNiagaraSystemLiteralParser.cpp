#include "DreamNiagaraSystemLiteralParser.h"

#include "DreamNiagaraTypes.h"

#include "NiagaraDataInterfaceColorCurve.h"
#include "NiagaraDataInterfaceCurve.h"
#include "NiagaraDataInterfaceVector2DCurve.h"
#include "NiagaraDataInterfaceVector4Curve.h"
#include "NiagaraDataInterfaceVectorCurve.h"
#include "NiagaraTypes.h"
#include "Misc/DefaultValueHelper.h"

namespace UE::DreamNiagara::SystemEditor::Private
{
	namespace
	{
		struct FCurveKeyValue
		{
			float Time = 0.0f;
			TArray<float> Values;
			FRichCurveKey TemplateKey;
		};

		class FCurveBodyParser
		{
		public:
			explicit FCurveBodyParser(const FString& InSource)
				: Source(InSource)
			{
			}

			bool Parse(const ECurveLiteralKind InKind, FCurveLiteral& OutCurve, FString& OutError)
			{
				Kind = InKind;
				const int32 ChannelCount = GetChannelCount(Kind);
				OutCurve.Kind = Kind;
				OutCurve.Channels.SetNum(ChannelCount);

				bool bHasAnyKeys = false;
				bool bUsedChannelBlocks = false;
				while (true)
				{
					SkipIgnored();
					if (IsAtEnd())
					{
						break;
					}

					FString Identifier;
					if (ParseIdentifier(Identifier))
					{
						SkipIgnored();
						if (Peek() == TCHAR('{'))
						{
							const int32 ChannelIndex = ChannelIndexFromName(Identifier, Kind);
							if (ChannelIndex == INDEX_NONE)
							{
								OutError = FString::Printf(TEXT("Channel block '%s' is not valid for this curve type."), *Identifier);
								return false;
							}

							FString ChannelBody;
							if (!ExtractBlock(ChannelBody, OutError))
							{
								return false;
							}

							FCurveBodyParser ChannelParser(ChannelBody);
							TArray<FCurveKeyValue> ChannelKeys;
							if (!ChannelParser.ParseKeyList(1, ChannelKeys, OutError))
							{
								return false;
							}
							if (ChannelKeys.IsEmpty())
							{
								OutError = FString::Printf(TEXT("Channel block '%s' does not contain any curve keys."), *Identifier);
								return false;
							}

							for (const FCurveKeyValue& KeyValue : ChannelKeys)
							{
								AddKey(OutCurve.Channels[ChannelIndex], KeyValue, 0);
							}
							bHasAnyKeys = true;
							bUsedChannelBlocks = true;
							ConsumeStatementSeparator();
							continue;
						}

						Index = IdentifierStart;
					}

					FCurveKeyValue KeyValue;
					if (!ParseKey(ChannelCount, KeyValue, OutError))
					{
						return false;
					}

					for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
					{
						AddKey(OutCurve.Channels[ChannelIndex], KeyValue, ChannelIndex);
					}
					bHasAnyKeys = true;
					ConsumeStatementSeparator();
				}

				if (!bHasAnyKeys)
				{
					OutError = TEXT("Curve literal does not contain any keys.");
					return false;
				}

				if (bUsedChannelBlocks)
				{
					for (int32 ChannelIndex = 0; ChannelIndex < OutCurve.Channels.Num(); ++ChannelIndex)
					{
						if (OutCurve.Channels[ChannelIndex].IsEmpty())
						{
							OutError = TEXT("Curve literal uses channel blocks but does not define every required channel.");
							return false;
						}
					}
				}

				for (FRichCurve& ChannelCurve : OutCurve.Channels)
				{
					if (!ChannelCurve.IsEmpty())
					{
						TArray<FRichCurveKey> SortedKeys = ChannelCurve.GetCopyOfKeys();
						SortedKeys.Sort([](const FRichCurveKey& Left, const FRichCurveKey& Right)
						{
							return Left.Time < Right.Time;
						});
						ChannelCurve.SetKeys(SortedKeys);
					}
				}
				return true;
			}

			bool ParseKeyList(const int32 ExpectedValueCount, TArray<FCurveKeyValue>& OutKeys, FString& OutError)
			{
				while (true)
				{
					SkipIgnored();
					if (IsAtEnd())
					{
						return true;
					}

					FCurveKeyValue KeyValue;
					if (!ParseKey(ExpectedValueCount, KeyValue, OutError))
					{
						return false;
					}

					OutKeys.Add(MoveTemp(KeyValue));
					ConsumeStatementSeparator();
				}
			}

		private:
			const FString& Source;
			int32 Index = 0;
			int32 IdentifierStart = 0;
			ECurveLiteralKind Kind = ECurveLiteralKind::Float;

			bool IsAtEnd() const
			{
				return Index >= Source.Len();
			}

			TCHAR Peek(const int32 Offset = 0) const
			{
				const int32 TargetIndex = Index + Offset;
				return Source.IsValidIndex(TargetIndex) ? Source[TargetIndex] : TCHAR('\0');
			}

			void Advance()
			{
				if (!IsAtEnd())
				{
					++Index;
				}
			}

			void SkipIgnored()
			{
				while (!IsAtEnd())
				{
					const TCHAR Char = Peek();
					const TCHAR Next = Peek(1);
					if (FChar::IsWhitespace(Char) || Char == TCHAR(',') || Char == TCHAR(';'))
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

			void ConsumeStatementSeparator()
			{
				while (!IsAtEnd() && (FChar::IsWhitespace(Peek()) || Peek() == TCHAR(';') || Peek() == TCHAR(',')))
				{
					Advance();
				}
			}

			static int32 GetChannelCount(const ECurveLiteralKind CurveKind)
			{
				switch (CurveKind)
				{
				case ECurveLiteralKind::Float:
					return 1;
				case ECurveLiteralKind::Vec2:
					return 2;
				case ECurveLiteralKind::Vec3:
					return 3;
				case ECurveLiteralKind::Vec4:
				case ECurveLiteralKind::Color:
					return 4;
				default:
					return 0;
				}
			}

			static int32 ChannelIndexFromName(const FString& Name, const ECurveLiteralKind CurveKind)
			{
				if (CurveKind == ECurveLiteralKind::Float)
				{
					return INDEX_NONE;
				}

				if (CurveKind == ECurveLiteralKind::Color)
				{
					if (Name.Equals(TEXT("r"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("red"), ESearchCase::IgnoreCase))
					{
						return 0;
					}
					if (Name.Equals(TEXT("g"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("green"), ESearchCase::IgnoreCase))
					{
						return 1;
					}
					if (Name.Equals(TEXT("b"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("blue"), ESearchCase::IgnoreCase))
					{
						return 2;
					}
					if (Name.Equals(TEXT("a"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("alpha"), ESearchCase::IgnoreCase))
					{
						return 3;
					}
					return INDEX_NONE;
				}

				if (Name.Equals(TEXT("x"), ESearchCase::IgnoreCase))
				{
					return 0;
				}
				if (Name.Equals(TEXT("y"), ESearchCase::IgnoreCase))
				{
					return 1;
				}
				if (Name.Equals(TEXT("z"), ESearchCase::IgnoreCase) && GetChannelCount(CurveKind) >= 3)
				{
					return 2;
				}
				if (Name.Equals(TEXT("w"), ESearchCase::IgnoreCase) && GetChannelCount(CurveKind) >= 4)
				{
					return 3;
				}
				return INDEX_NONE;
			}

			static void AddKey(FRichCurve& Curve, const FCurveKeyValue& KeyValue, const int32 ValueIndex)
			{
				FRichCurveKey Key = KeyValue.TemplateKey;
				Key.Time = KeyValue.Time;
				Key.Value = KeyValue.Values.IsValidIndex(ValueIndex) ? KeyValue.Values[ValueIndex] : 0.0f;
				Curve.Keys.Add(Key);
			}

			bool ParseIdentifier(FString& OutIdentifier)
			{
				SkipIgnored();
				if (!(FChar::IsAlpha(Peek()) || Peek() == TCHAR('_')))
				{
					return false;
				}

				IdentifierStart = Index;
				while (!IsAtEnd() && (FChar::IsAlnum(Peek()) || Peek() == TCHAR('_')))
				{
					Advance();
				}

				OutIdentifier = Source.Mid(IdentifierStart, Index - IdentifierStart);
				return true;
			}

			bool Expect(const TCHAR Expected, FString& OutError)
			{
				SkipIgnored();
				if (Peek() == Expected)
				{
					Advance();
					return true;
				}

				OutError = FString::Printf(TEXT("Expected '%c' while parsing curve literal."), Expected);
				return false;
			}

			bool ExtractBlock(FString& OutBlock, FString& OutError)
			{
				SkipIgnored();
				if (Peek() != TCHAR('{'))
				{
					OutError = TEXT("Expected channel block in curve literal.");
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
					}
					else if (Char == TCHAR('}'))
					{
						--Depth;
						if (Depth == 0)
						{
							OutBlock = Source.Mid(Start, Index - Start);
							Advance();
							return true;
						}
					}
					Advance();
				}

				OutError = TEXT("Unterminated channel block in curve literal.");
				return false;
			}

			bool ParseKey(const int32 ExpectedValueCount, FCurveKeyValue& OutKeyValue, FString& OutError)
			{
				SkipIgnored();
				if (Peek() == TCHAR('('))
				{
					return ParseShorthandKey(ExpectedValueCount, OutKeyValue, OutError);
				}

				FString Identifier;
				if (!ParseIdentifier(Identifier) || !Identifier.Equals(TEXT("key"), ESearchCase::IgnoreCase))
				{
					OutError = TEXT("Expected curve key shorthand '(time, value)' or key(...).");
					return false;
				}

				if (!Expect(TCHAR('('), OutError))
				{
					return false;
				}

				TMap<FString, FString> Fields;
				if (!ParseFieldList(Fields, OutError))
				{
					return false;
				}

				return BuildKeyFromFields(Fields, ExpectedValueCount, OutKeyValue, OutError);
			}

			bool ParseShorthandKey(const int32 ExpectedValueCount, FCurveKeyValue& OutKeyValue, FString& OutError)
			{
				if (!Expect(TCHAR('('), OutError))
				{
					return false;
				}

				TArray<FString> Args;
				if (!ParseArgumentList(Args, OutError))
				{
					return false;
				}

				if (Args.Num() != 2)
				{
					OutError = TEXT("Curve key shorthand requires exactly time and value.");
					return false;
				}

				if (!FLiteralParser::ParseFloat(Args[0], OutKeyValue.Time))
				{
					OutError = FString::Printf(TEXT("Invalid curve key time '%s'."), *Args[0]);
					return false;
				}

				if (!ParseValueList(Args[1], ExpectedValueCount, OutKeyValue.Values, OutError))
				{
					return false;
				}

				return true;
			}

			bool ParseArgumentList(TArray<FString>& OutArgs, FString& OutError)
			{
				return ParseDelimitedList(OutArgs, OutError);
			}

			bool ParseFieldList(TMap<FString, FString>& OutFields, FString& OutError)
			{
				TArray<FString> Entries;
				if (!ParseDelimitedList(Entries, OutError))
				{
					return false;
				}

				for (const FString& Entry : Entries)
				{
					FString Name;
					FString Value;
					if (!SplitTopLevelEquals(Entry, Name, Value))
					{
						OutError = FString::Printf(TEXT("Curve key field '%s' must use name=value."), *Entry);
						return false;
					}
					Name.TrimStartAndEndInline();
					Value.TrimStartAndEndInline();
					if (Name.IsEmpty() || Value.IsEmpty())
					{
						OutError = FString::Printf(TEXT("Curve key field '%s' must use name=value."), *Entry);
						return false;
					}
					OutFields.Add(Name.ToLower(), Value);
				}

				return true;
			}

			bool ParseDelimitedList(TArray<FString>& OutItems, FString& OutError)
			{
				int32 Start = Index;
				int32 ParenthesisDepth = 0;
				int32 BraceDepth = 0;
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
						if (ParenthesisDepth == 0 && BraceDepth == 0)
						{
							AddDelimitedItem(Source.Mid(Start, Index - Start), OutItems);
							Advance();
							return true;
						}
						ParenthesisDepth = FMath::Max(0, ParenthesisDepth - 1);
						Advance();
						continue;
					}
					if (Char == TCHAR('{'))
					{
						++BraceDepth;
						Advance();
						continue;
					}
					if (Char == TCHAR('}'))
					{
						BraceDepth = FMath::Max(0, BraceDepth - 1);
						Advance();
						continue;
					}
					if (Char == TCHAR(',') && ParenthesisDepth == 0 && BraceDepth == 0)
					{
						AddDelimitedItem(Source.Mid(Start, Index - Start), OutItems);
						Advance();
						Start = Index;
						continue;
					}
					Advance();
				}

				OutError = TEXT("Unterminated curve key argument list.");
				return false;
			}

			static void AddDelimitedItem(FString Item, TArray<FString>& OutItems)
			{
				Item.TrimStartAndEndInline();
				if (!Item.IsEmpty())
				{
					OutItems.Add(Item);
				}
			}

			static bool SplitTopLevelEquals(const FString& Text, FString& OutLeft, FString& OutRight)
			{
				int32 ParenthesisDepth = 0;
				bool bInString = false;
				for (int32 CharIndex = 0; CharIndex < Text.Len(); ++CharIndex)
				{
					const TCHAR Char = Text[CharIndex];
					if (bInString)
					{
						if (Char == TCHAR('\\') && Text.IsValidIndex(CharIndex + 1))
						{
							++CharIndex;
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
					if (Char == TCHAR('=') && ParenthesisDepth == 0)
					{
						OutLeft = Text.Left(CharIndex);
						OutRight = Text.Mid(CharIndex + 1);
						return true;
					}
				}
				return false;
			}

			bool BuildKeyFromFields(const TMap<FString, FString>& Fields, const int32 ExpectedValueCount, FCurveKeyValue& OutKeyValue, FString& OutError)
			{
				const FString* TimeText = Fields.Find(TEXT("time"));
				const FString* ValueText = Fields.Find(TEXT("value"));
				if (!TimeText)
				{
					OutError = TEXT("Curve key is missing required field 'time'.");
					return false;
				}
				if (!ValueText)
				{
					OutError = TEXT("Curve key is missing required field 'value'.");
					return false;
				}

				if (!FLiteralParser::ParseFloat(*TimeText, OutKeyValue.Time))
				{
					OutError = FString::Printf(TEXT("Invalid curve key time '%s'."), **TimeText);
					return false;
				}
				if (!ParseValueList(*ValueText, ExpectedValueCount, OutKeyValue.Values, OutError))
				{
					return false;
				}

				if (const FString* InterpText = Fields.Find(TEXT("interp")))
				{
					if (!ParseInterpMode(*InterpText, OutKeyValue.TemplateKey.InterpMode, OutError))
					{
						return false;
					}
				}
				if (const FString* TangentText = Fields.Find(TEXT("tangent")))
				{
					if (!ParseTangentMode(*TangentText, OutKeyValue.TemplateKey.TangentMode, OutError))
					{
						return false;
					}
				}
				if (const FString* WeightText = Fields.Find(TEXT("weight")))
				{
					if (!ParseWeightMode(*WeightText, OutKeyValue.TemplateKey.TangentWeightMode, OutError))
					{
						return false;
					}
				}

				ParseOptionalFloatField(Fields, TEXT("arrive"), OutKeyValue.TemplateKey.ArriveTangent, OutError);
				if (!OutError.IsEmpty())
				{
					return false;
				}
				ParseOptionalFloatField(Fields, TEXT("leave"), OutKeyValue.TemplateKey.LeaveTangent, OutError);
				if (!OutError.IsEmpty())
				{
					return false;
				}
				ParseOptionalFloatField(Fields, TEXT("arriveweight"), OutKeyValue.TemplateKey.ArriveTangentWeight, OutError);
				if (!OutError.IsEmpty())
				{
					return false;
				}
				ParseOptionalFloatField(Fields, TEXT("leaveweight"), OutKeyValue.TemplateKey.LeaveTangentWeight, OutError);
				return OutError.IsEmpty();
			}

			static FString NormalizeToken(FString Text)
			{
				Text.TrimStartAndEndInline();
				Text = Text.TrimQuotes();
				Text.ReplaceInline(TEXT("_"), TEXT(""));
				Text.ReplaceInline(TEXT("-"), TEXT(""));
				Text.ReplaceInline(TEXT(" "), TEXT(""));
				return Text.ToLower();
			}

			static bool ParseInterpMode(const FString& Text, TEnumAsByte<ERichCurveInterpMode>& OutMode, FString& OutError)
			{
				const FString Token = NormalizeToken(Text);
				if (Token == TEXT("linear"))
				{
					OutMode = RCIM_Linear;
					return true;
				}
				if (Token == TEXT("constant"))
				{
					OutMode = RCIM_Constant;
					return true;
				}
				if (Token == TEXT("cubic"))
				{
					OutMode = RCIM_Cubic;
					return true;
				}
				OutError = FString::Printf(TEXT("Invalid curve interp mode '%s'."), *Text);
				return false;
			}

			static bool ParseTangentMode(const FString& Text, TEnumAsByte<ERichCurveTangentMode>& OutMode, FString& OutError)
			{
				const FString Token = NormalizeToken(Text);
				if (Token == TEXT("auto"))
				{
					OutMode = RCTM_Auto;
					return true;
				}
				if (Token == TEXT("user"))
				{
					OutMode = RCTM_User;
					return true;
				}
				if (Token == TEXT("break"))
				{
					OutMode = RCTM_Break;
					return true;
				}
				if (Token == TEXT("smartauto"))
				{
					OutMode = RCTM_SmartAuto;
					return true;
				}
				if (Token == TEXT("none"))
				{
					OutMode = RCTM_None;
					return true;
				}
				OutError = FString::Printf(TEXT("Invalid curve tangent mode '%s'."), *Text);
				return false;
			}

			static bool ParseWeightMode(const FString& Text, TEnumAsByte<ERichCurveTangentWeightMode>& OutMode, FString& OutError)
			{
				const FString Token = NormalizeToken(Text);
				if (Token == TEXT("none"))
				{
					OutMode = RCTWM_WeightedNone;
					return true;
				}
				if (Token == TEXT("arrive"))
				{
					OutMode = RCTWM_WeightedArrive;
					return true;
				}
				if (Token == TEXT("leave"))
				{
					OutMode = RCTWM_WeightedLeave;
					return true;
				}
				if (Token == TEXT("both"))
				{
					OutMode = RCTWM_WeightedBoth;
					return true;
				}
				OutError = FString::Printf(TEXT("Invalid curve tangent weight mode '%s'."), *Text);
				return false;
			}

			static void ParseOptionalFloatField(const TMap<FString, FString>& Fields, const TCHAR* FieldName, float& OutValue, FString& OutError)
			{
				const FString* Text = Fields.Find(FieldName);
				if (!Text)
				{
					return;
				}
				if (!FLiteralParser::ParseFloat(*Text, OutValue))
				{
					OutError = FString::Printf(TEXT("Invalid curve key field '%s=%s'."), FieldName, **Text);
				}
			}

			static bool ParseValueList(const FString& Text, const int32 ExpectedValueCount, TArray<float>& OutValues, FString& OutError)
			{
				TArray<float> Values;
				if (ExpectedValueCount == 1)
				{
					float FloatValue = 0.0f;
					if (!FLiteralParser::ParseFloat(Text, FloatValue))
					{
						TArray<float> ParsedValues;
						if (!FLiteralParser::ParseVector(Text, ParsedValues) || ParsedValues.Num() != 1)
						{
							OutError = FString::Printf(TEXT("Curve value '%s' is not a float."), *Text);
							return false;
						}
						FloatValue = ParsedValues[0];
					}
					Values.Add(FloatValue);
				}
				else
				{
					if (!FLiteralParser::ParseVector(Text, Values))
					{
						OutError = FString::Printf(TEXT("Curve value '%s' is not a vector or color literal."), *Text);
						return false;
					}
					if (Values.Num() != ExpectedValueCount)
					{
						OutError = FString::Printf(TEXT("Curve value '%s' has %d channels but expected %d."), *Text, Values.Num(), ExpectedValueCount);
						return false;
					}
				}

				OutValues = MoveTemp(Values);
				return true;
			}
		};

		bool SplitCurveWrapper(const FString& Text, FString& OutName, FString& OutBody)
		{
			const FString Trimmed = Text.TrimStartAndEnd();
			const int32 OpenBrace = Trimmed.Find(TEXT("{"));
			const int32 CloseBrace = Trimmed.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (OpenBrace == INDEX_NONE || CloseBrace == INDEX_NONE || CloseBrace <= OpenBrace)
			{
				return false;
			}

			OutName = Trimmed.Left(OpenBrace).TrimStartAndEnd();
			OutBody = Trimmed.Mid(OpenBrace + 1, CloseBrace - OpenBrace - 1);
			return !OutName.IsEmpty();
		}

		bool TryParseCurveKind(const FString& Text, ECurveLiteralKind& OutKind)
		{
			const FString Normalized = Text.TrimStartAndEnd().ToLower();
			if (Normalized == TEXT("curve") || Normalized == TEXT("floatcurve"))
			{
				OutKind = ECurveLiteralKind::Float;
				return true;
			}
			if (Normalized == TEXT("vec2curve") || Normalized == TEXT("vector2curve") || Normalized == TEXT("float2curve"))
			{
				OutKind = ECurveLiteralKind::Vec2;
				return true;
			}
			if (Normalized == TEXT("vec3curve") || Normalized == TEXT("vectorcurve") || Normalized == TEXT("vector3curve") || Normalized == TEXT("float3curve"))
			{
				OutKind = ECurveLiteralKind::Vec3;
				return true;
			}
			if (Normalized == TEXT("vec4curve") || Normalized == TEXT("vector4curve") || Normalized == TEXT("float4curve"))
			{
				OutKind = ECurveLiteralKind::Vec4;
				return true;
			}
			if (Normalized == TEXT("colorcurve"))
			{
				OutKind = ECurveLiteralKind::Color;
				return true;
			}
			return false;
		}

		int32 GetCurveChannelCount(const ECurveLiteralKind Kind)
		{
			switch (Kind)
			{
			case ECurveLiteralKind::Float:
				return 1;
			case ECurveLiteralKind::Vec2:
				return 2;
			case ECurveLiteralKind::Vec3:
				return 3;
			case ECurveLiteralKind::Vec4:
			case ECurveLiteralKind::Color:
				return 4;
			default:
				return 0;
			}
		}

		bool DoesCurveKindMatchClass(const ECurveLiteralKind Kind, const UClass* Class)
		{
			return Class && Class->IsChildOf(FLiteralParser::GetCurveClass(Kind));
		}

		void RefreshCurveDataInterface(UNiagaraDataInterfaceCurveBase& DataInterface)
		{
			DataInterface.UpdateTimeRanges();
#if WITH_EDITORONLY_DATA
			DataInterface.UpdateLUT();
#endif
		}
	}

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
			|| Trimmed.StartsWith(TEXT("vector"), ESearchCase::IgnoreCase)
			|| Trimmed.StartsWith(TEXT("float2"), ESearchCase::IgnoreCase)
			|| Trimmed.StartsWith(TEXT("float3"), ESearchCase::IgnoreCase)
			|| Trimmed.StartsWith(TEXT("float4"), ESearchCase::IgnoreCase)
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

	bool FLiteralParser::ResolveCurveType(const FString& Text, FNiagaraTypeDefinition& OutType)
	{
		ECurveLiteralKind Kind = ECurveLiteralKind::Float;
		if (!TryParseCurveKind(Text, Kind))
		{
			return false;
		}

		OutType = FNiagaraTypeDefinition(GetCurveClass(Kind));
		return OutType.IsValid();
	}

	bool FLiteralParser::TryParseCurve(const FDreamNiagaraValue& Value, FCurveLiteral& OutCurve, FString& OutError)
	{
		FString WrapperName;
		FString Body;
		if (!SplitCurveWrapper(Value.Text, WrapperName, Body))
		{
			return false;
		}

		ECurveLiteralKind Kind = ECurveLiteralKind::Float;
		if (!TryParseCurveKind(WrapperName, Kind))
		{
			return false;
		}

		FCurveBodyParser Parser(Body);
		return Parser.Parse(Kind, OutCurve, OutError);
	}

	UClass* FLiteralParser::GetCurveClass(const ECurveLiteralKind Kind)
	{
		switch (Kind)
		{
		case ECurveLiteralKind::Float:
			return UNiagaraDataInterfaceCurve::StaticClass();
		case ECurveLiteralKind::Vec2:
			return UNiagaraDataInterfaceVector2DCurve::StaticClass();
		case ECurveLiteralKind::Vec3:
			return UNiagaraDataInterfaceVectorCurve::StaticClass();
		case ECurveLiteralKind::Vec4:
			return UNiagaraDataInterfaceVector4Curve::StaticClass();
		case ECurveLiteralKind::Color:
			return UNiagaraDataInterfaceColorCurve::StaticClass();
		default:
			return nullptr;
		}
	}

	bool FLiteralParser::IsCurveDataInterfaceType(const FNiagaraTypeDefinition& Type)
	{
		return Type.IsDataInterface()
			&& Type.GetClass()
			&& Type.GetClass()->IsChildOf(UNiagaraDataInterfaceCurveBase::StaticClass());
	}

	bool FLiteralParser::ApplyCurveToDataInterface(const FCurveLiteral& Curve, UNiagaraDataInterfaceCurveBase& DataInterface, FString& OutError)
	{
		if (!DoesCurveKindMatchClass(Curve.Kind, DataInterface.GetClass()))
		{
			OutError = FString::Printf(
				TEXT("Curve literal type does not match data interface class '%s'."),
				*DataInterface.GetClass()->GetName());
			return false;
		}

		if (Curve.Channels.Num() != GetCurveChannelCount(Curve.Kind))
		{
			OutError = TEXT("Curve literal channel count does not match its type.");
			return false;
		}

		if (UNiagaraDataInterfaceCurve* FloatCurve = Cast<UNiagaraDataInterfaceCurve>(&DataInterface))
		{
			FloatCurve->Curve = Curve.Channels[0];
			RefreshCurveDataInterface(*FloatCurve);
			return true;
		}
		if (UNiagaraDataInterfaceVector2DCurve* Vec2Curve = Cast<UNiagaraDataInterfaceVector2DCurve>(&DataInterface))
		{
			Vec2Curve->XCurve = Curve.Channels[0];
			Vec2Curve->YCurve = Curve.Channels[1];
			RefreshCurveDataInterface(*Vec2Curve);
			return true;
		}
		if (UNiagaraDataInterfaceVectorCurve* Vec3Curve = Cast<UNiagaraDataInterfaceVectorCurve>(&DataInterface))
		{
			Vec3Curve->XCurve = Curve.Channels[0];
			Vec3Curve->YCurve = Curve.Channels[1];
			Vec3Curve->ZCurve = Curve.Channels[2];
			RefreshCurveDataInterface(*Vec3Curve);
			return true;
		}
		if (UNiagaraDataInterfaceVector4Curve* Vec4Curve = Cast<UNiagaraDataInterfaceVector4Curve>(&DataInterface))
		{
			Vec4Curve->XCurve = Curve.Channels[0];
			Vec4Curve->YCurve = Curve.Channels[1];
			Vec4Curve->ZCurve = Curve.Channels[2];
			Vec4Curve->WCurve = Curve.Channels[3];
			RefreshCurveDataInterface(*Vec4Curve);
			return true;
		}
		if (UNiagaraDataInterfaceColorCurve* ColorCurve = Cast<UNiagaraDataInterfaceColorCurve>(&DataInterface))
		{
			ColorCurve->RedCurve = Curve.Channels[0];
			ColorCurve->GreenCurve = Curve.Channels[1];
			ColorCurve->BlueCurve = Curve.Channels[2];
			ColorCurve->AlphaCurve = Curve.Channels[3];
			RefreshCurveDataInterface(*ColorCurve);
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported curve data interface class '%s'."), *DataInterface.GetClass()->GetName());
		return false;
	}
}
