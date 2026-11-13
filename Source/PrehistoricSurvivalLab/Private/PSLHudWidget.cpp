#include "PSLHudWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Fonts/SlateFontInfo.h"
#include "PSLCharacter.h"
#include "PSLHUD.h"
#include "PSLStatsComponent.h"
#include "PSLSurvivalComponent.h"

namespace
{
    UTextBlock* MakeLabel(UWidgetTree* Tree, const TCHAR* Text, int32 Size, const FLinearColor& Color)
    {
        UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Label->SetText(FText::FromString(Text));
        Label->SetColorAndOpacity(FSlateColor(Color));
        FSlateFontInfo Font = Label->GetFont();
        Font.Size = Size;
        Label->SetFont(Font);
        return Label;
    }

    UProgressBar* MakeBar(UWidgetTree* Tree, UVerticalBox* Parent, const TCHAR* Caption, const FLinearColor& Fill)
    {
        UVerticalBoxSlot* LabelSlot = Parent->AddChildToVerticalBox(MakeLabel(Tree, Caption, 12, FLinearColor::White));
        LabelSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 1.0f));

        UProgressBar* Bar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
        Bar->SetFillColorAndOpacity(Fill);
        Bar->SetPercent(1.0f);
        UVerticalBoxSlot* BarSlot = Parent->AddChildToVerticalBox(Bar);
        BarSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
        return Bar;
    }
}

bool UPSLHudWidget::Initialize()
{
    const bool bResult = Super::Initialize();
    // NativeOnInitialized only runs with a PlayerContext, so build the tree here to work without one.
    if (bResult && WidgetTree && !WidgetTree->RootWidget)
    {
        BuildTree();
    }
    return bResult;
}

void UPSLHudWidget::BuildTree()
{
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
    WidgetTree->RootWidget = Root;

    // Bottom-left panel, 260 x ~120.
    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    Panel->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.6f));
    Panel->SetPadding(FMargin(12.0f));
    UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
    PanelSlot->SetAnchors(FAnchors(0.0f, 1.0f, 0.0f, 1.0f));
    PanelSlot->SetAlignment(FVector2D(0.0f, 1.0f));
    PanelSlot->SetPosition(FVector2D(32.0f, -32.0f));
    PanelSlot->SetSize(FVector2D(260.0f, 120.0f));

    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    Panel->SetContent(Column);

    HealthBar = MakeBar(WidgetTree, Column, TEXT("HEALTH"), FLinearColor(0.8f, 0.1f, 0.1f, 1.0f));
    StaminaBar = MakeBar(WidgetTree, Column, TEXT("STAMINA"), FLinearColor(0.2f, 0.8f, 0.3f, 1.0f));

    StateText = MakeLabel(WidgetTree, TEXT(""), 12, FLinearColor::White);
    Column->AddChildToVerticalBox(StateText)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));

    // Centered DEAD text.
    DeadText = MakeLabel(WidgetTree, TEXT("DEAD"), 64, FLinearColor(0.85f, 0.1f, 0.1f, 1.0f));
    DeadText->SetVisibility(ESlateVisibility::Collapsed);
    UCanvasPanelSlot* DeadSlot = Root->AddChildToCanvas(DeadText);
    DeadSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
    DeadSlot->SetAlignment(FVector2D(0.5f, 0.5f));
    DeadSlot->SetAutoSize(true);
    DeadSlot->SetPosition(FVector2D::ZeroVector);
}

void UPSLHudWidget::SetValues(float Health, float MaxHealth, float Stamina, float MaxStamina, bool bSprinting, bool bExhausted, bool bDead)
{
    if (!HealthBar || !StaminaBar || !StateText || !DeadText)
    {
        return;
    }

    HealthBar->SetPercent(APSLHUD::BarFraction(Health, MaxHealth));
    StaminaBar->SetPercent(APSLHUD::BarFraction(Stamina, MaxStamina));

    HealthBar->SetFillColorAndOpacity(bDead ? FLinearColor(0.4f, 0.4f, 0.4f, 1.0f) : FLinearColor(0.8f, 0.1f, 0.1f, 1.0f));
    StaminaBar->SetFillColorAndOpacity(bExhausted ? FLinearColor(0.9f, 0.5f, 0.0f, 1.0f) : FLinearColor(0.2f, 0.8f, 0.3f, 1.0f));

    const TCHAR* State = bExhausted ? TEXT("EXHAUSTED") : (bSprinting ? TEXT("SPRINTING") : TEXT(""));
    StateText->SetText(FText::FromString(State));
    DeadText->SetVisibility(bDead ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UPSLHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    // Pawn can be null or replaced (respawn); resolve it every tick.
    const APSLCharacter* Character = Cast<APSLCharacter>(GetOwningPlayerPawn());
    const UPSLStatsComponent* Stats = Character ? Character->GetStatsComponent() : nullptr;
    const UPSLSurvivalComponent* Survival = Character ? Character->GetSurvivalComponent() : nullptr;
    if (!Stats || !Survival)
    {
        return;
    }

    SetValues(Stats->GetHealth(), Stats->GetMaxHealth(), Survival->GetStamina(), Survival->GetMaxStamina(),
        Survival->IsSprinting(), Survival->IsSprintLocked(), Stats->IsDead());
}
