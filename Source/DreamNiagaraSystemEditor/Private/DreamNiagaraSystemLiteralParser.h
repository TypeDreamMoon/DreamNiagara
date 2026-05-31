#pragma once

#include "CoreMinimal.h"
#include "Curves/RichCurve.h"
#include "NiagaraTypes.h"

class UNiagaraDataInterfaceCurveBase;
class UClass;

namespace UE::DreamNiagara
{
	struct FDreamNiagaraValue;
}

namespace UE::DreamNiagara::SystemEditor::Private
{
	enum class ECurveLiteralKind : uint8
	{
		Float,
		Vec2,
		Vec3,
		Vec4,
		Color,
	};

	struct FCurveLiteral
	{
		ECurveLiteralKind Kind = ECurveLiteralKind::Float;
		TArray<FRichCurve> Channels;
	};

	class FLiteralParser
	{
	public:
		static bool ParseFloat(const FString& Text, float& OutValue);
		static bool ParseInt(const FString& Text, int32& OutValue);
		static bool ParseBool(const FString& Text, bool& OutValue);
		static bool ParseVector(const FString& Text, TArray<float>& OutValues);
		static bool ParseHexColor(const FString& Text, FLinearColor& OutColor);
		static bool ParseAssetPath(const FString& Text, FString& OutAssetPath);

		static bool ResolveCurveType(const FString& Text, FNiagaraTypeDefinition& OutType);
		static bool TryParseCurve(const UE::DreamNiagara::FDreamNiagaraValue& Value, FCurveLiteral& OutCurve, FString& OutError);
		static UClass* GetCurveClass(ECurveLiteralKind Kind);
		static bool IsCurveDataInterfaceType(const FNiagaraTypeDefinition& Type);
		static bool ApplyCurveToDataInterface(const FCurveLiteral& Curve, UNiagaraDataInterfaceCurveBase& DataInterface, FString& OutError);
	};
}
