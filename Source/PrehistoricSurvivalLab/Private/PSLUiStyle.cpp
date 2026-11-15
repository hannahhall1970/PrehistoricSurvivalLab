#include "PSLUiStyle.h"

#include "Blueprint/WidgetTree.h"
#include "PSLAudio.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"

FName FPSLUiStyle::TypefaceName(EPSLFontWeight Weight)
{
    switch (Weight)
    {
    case EPSLFontWeight::SemiBold: return FName(TEXT("SemiBold"));
    case EPSLFontWeight::Bold:     return FName(TEXT("Bold"));
    default:                       return FName(TEXT("Regular"));
    }
}

FSlateFontInfo FPSLUiStyle::Font(int32 Size, EPSLFontWeight Weight)
{
    if (const UObject* Composite = LoadObject<UObject>(nullptr, FontPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
    {
        return FSlateFontInfo(Composite, static_cast<float>(Size), TypefaceName(Weight));
    }
    // The engine default font has no SemiBold style: use its Bold.
    return FCoreStyle::GetDefaultFontStyle(Weight == EPSLFontWeight::Regular ? "Regular" : "Bold", Size);
}

bool FPSLUiStyle::HasProjectFont()
{
    return LoadObject<UObject>(nullptr, FontPath, nullptr, LOAD_NoWarn | LOAD_Quiet) != nullptr;
}

static void PlayCue(const UObject* WorldContext, const TCHAR* Path)
{
    if (USoundBase* Sound = PSLAudio::Load(Path))
    {
        UGameplayStatics::PlaySound2D(WorldContext, Sound);
    }
}

void FPSLUiStyle::PlayClick(const UObject* WorldContext) { PlayCue(WorldContext, ClickSoundPath); }
void FPSLUiStyle::PlayHover(const UObject* WorldContext) { PlayCue(WorldContext, HoverSoundPath); }

UTextBlock* FPSLUiStyle::MakeText(UWidgetTree* Tree, const FText& Text, int32 Size, const FLinearColor& Color, EPSLFontWeight Weight)
{
    UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Label->SetText(Text);
    Label->SetColorAndOpacity(FSlateColor(Color));
    Label->SetFont(Font(Size, Weight));
    return Label;
}
