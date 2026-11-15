#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateColor.h"

class UTextBlock;
class UWidgetTree;

enum class EPSLFontWeight : uint8 { Regular, SemiBold, Bold };

/** The only place for UI colours, sizes and fonts ("dusk amber" palette). */
struct PREHISTORICSURVIVALLAB_API FPSLUiStyle
{
    static FLinearColor Panel()    { FLinearColor C = FLinearColor::FromSRGBColor(FColor(0x14, 0x20, 0x1A)); C.A = 0.86f; return C; }
    static FLinearColor Accent()   { return FLinearColor::FromSRGBColor(FColor(0xE3, 0xA6, 0x3B)); }
    static FLinearColor Text()     { return FLinearColor::FromSRGBColor(FColor(0xED, 0xE3, 0xCC)); }
    static FLinearColor Danger()   { return FLinearColor::FromSRGBColor(FColor(0xB5, 0x46, 0x2E)); }
    static FLinearColor Disabled() { return FLinearColor::FromSRGBColor(FColor(0x6E, 0x6A, 0x5E)); }

    static constexpr int32 TitleSize = 64;
    static constexpr int32 HeadingSize = 32;
    static constexpr int32 ButtonSize = 26;
    static constexpr int32 BodySize = 20;

    static constexpr const TCHAR* FontPath = TEXT("/Game/UI/Fonts/F_BeVietnamPro.F_BeVietnamPro");
    static constexpr const TCHAR* ClickSoundPath = TEXT("/Game/Audio/SFX_UI_Click.SFX_UI_Click");
    static constexpr const TCHAR* HoverSoundPath = TEXT("/Game/Audio/SFX_UI_Hover.SFX_UI_Hover");

    // BeVietnamPro (Regular / SemiBold / Bold faces of the composite font) when the asset exists, else the engine default font.
    // The engine default has no SemiBold, so the fallback maps SemiBold to its "Bold" style.
    static FSlateFontInfo Font(int32 Size, EPSLFontWeight Weight = EPSLFontWeight::Regular);

    // Typeface entry name inside the composite font for a weight: "Regular" / "SemiBold" / "Bold".
    static FName TypefaceName(EPSLFontWeight Weight);

    // True when the project font asset loaded (false means every Font() call falls back).
    static bool HasProjectFont();

    // Soft-loaded UI cues; silently skipped when the asset is missing.
    static void PlayClick(const UObject* WorldContext);
    static void PlayHover(const UObject* WorldContext);

    // Label with the style's font and colour. Text must come from LOCTEXT.
    static UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, int32 Size, const FLinearColor& Color, EPSLFontWeight Weight = EPSLFontWeight::Regular);
};
