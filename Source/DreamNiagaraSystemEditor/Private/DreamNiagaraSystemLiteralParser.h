#pragma once

#include "CoreMinimal.h"

namespace UE::DreamNiagara::SystemEditor::Private
{
	class FLiteralParser
	{
	public:
		static bool ParseFloat(const FString& Text, float& OutValue);
		static bool ParseInt(const FString& Text, int32& OutValue);
		static bool ParseBool(const FString& Text, bool& OutValue);
		static bool ParseVector(const FString& Text, TArray<float>& OutValues);
		static bool ParseHexColor(const FString& Text, FLinearColor& OutColor);
		static bool ParseAssetPath(const FString& Text, FString& OutAssetPath);
	};
}
