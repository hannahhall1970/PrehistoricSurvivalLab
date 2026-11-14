#include "PSLUiStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"

FSlateFontInfo FPSLUiStyle::Font(int32 Size, bool bBold)
{
    if (const UObject* Composite = LoadObject<UObject>(nullptr, FontPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
    {
        return FSlateFontInfo(Composite, static_cast<float>(Size), bBold ? FName(TEXT("Bold")) : FName(TEXT("Regular")));
    }
    return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
}

bool FPSLUiStyle::HasProjectFont()
{
    return LoadObject<UObject>(nullptr, FontPath, nullptr, LOAD_NoWarn | LOAD_Quiet) != nullptr;
}

static void PlayCue(const UObject* WorldContext, const TCHAR* Path)
{
    if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
    {
        UGameplayStatics::PlaySound2D(WorldContext, Sound);
    }
}

void FPSLUiStyle::PlayClick(const UObject* WorldContext) { PlayCue(WorldContext, ClickSoundPath); }
void FPSLUiStyle::PlayHover(const UObject* WorldContext) { PlayCue(WorldContext, HoverSoundPath); }

UTextBlock* FPSLUiStyle::MakeText(UWidgetTree* Tree, const FText& Text, int32 Size, const FLinearColor& Color, bool bBold)
{
    UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Label->SetText(Text);
    Label->SetColorAndOpacity(FSlateColor(Color));
    Label->SetFont(Font(Size, bBold));
    return Label;
}
