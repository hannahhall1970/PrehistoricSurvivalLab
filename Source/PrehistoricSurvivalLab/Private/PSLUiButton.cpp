#include "PSLUiButton.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "PSLUiStyle.h"

UPSLUiButton* UPSLUiButton::Make(UWidgetTree* Tree, const FText& Text)
{
    UPSLUiButton* Button = Tree->ConstructWidget<UPSLUiButton>(UPSLUiButton::StaticClass());

    FButtonStyle Style;
    FSlateBrush Clear;
    Clear.DrawAs = ESlateBrushDrawType::NoDrawType;
    Style.SetNormal(Clear).SetHovered(Clear).SetPressed(Clear).SetDisabled(Clear);
    Button->SetStyle(Style);

    UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    Button->Label = FPSLUiStyle::MakeText(Tree, Text, FPSLUiStyle::ButtonSize, FPSLUiStyle::Text(), EPSLFontWeight::SemiBold);
    Column->AddChildToVerticalBox(Button->Label)->SetPadding(FMargin(12.0f, 6.0f, 12.0f, 4.0f));

    Button->Underline = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
    Button->Underline->SetBrushColor(FPSLUiStyle::Accent());
    Button->Underline->SetPadding(FMargin(0.0f, 1.0f));
    Button->Underline->VisibilityDelegate.BindUFunction(Button, GET_FUNCTION_NAME_CHECKED(UPSLUiButton, GetUnderlineVisibility));
    Column->AddChildToVerticalBox(Button->Underline);

    Button->SetContent(Column);
    return Button;
}

void UPSLUiButton::SetLabelText(const FText& Text)
{
    if (Label)
    {
        Label->SetText(Text);
    }
}

void UPSLUiButton::SetActive(bool bActive)
{
    SetIsEnabled(bActive);
    if (Label)
    {
        Label->SetColorAndOpacity(FSlateColor(bActive ? FPSLUiStyle::Text() : FPSLUiStyle::Disabled()));
    }
}

TSharedRef<SWidget> UPSLUiButton::RebuildWidget()
{
    OnClicked.AddUniqueDynamic(this, &UPSLUiButton::HandleClicked);
    OnHovered.AddUniqueDynamic(this, &UPSLUiButton::HandleHovered);
    return Super::RebuildWidget();
}

void UPSLUiButton::HandleClicked()
{
    FPSLUiStyle::PlayClick(this);
}

void UPSLUiButton::HandleHovered()
{
    FPSLUiStyle::PlayHover(this);
}

ESlateVisibility UPSLUiButton::GetUnderlineVisibility()
{
    return GetIsEnabled() && (IsHovered() || HasKeyboardFocus()) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden;
}
