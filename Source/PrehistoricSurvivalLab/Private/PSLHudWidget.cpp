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
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Engine/World.h"
#include "PSLPreyCharacter.h"
#include "PSLCharacter.h"
#include "PSLHUD.h"
#include "PSLUiStyle.h"
#include "PSLStatsComponent.h"
#include "PSLSurvivalComponent.h"

#define LOCTEXT_NAMESPACE "PSL"

namespace
{
    UTextBlock* MakeLabel(UWidgetTree* Tree, const FText& Text, int32 Size, const FLinearColor& Color)
    {
        UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Label->SetText(Text);
        Label->SetColorAndOpacity(FSlateColor(Color));
        FSlateFontInfo Font = Label->GetFont();
        Font.Size = Size;
        Label->SetFont(Font);
        return Label;
    }

    // 22x22 rounded icon in the bar colour with a centred glyph.
    UWidget* MakeIcon(UWidgetTree* Tree, const FText& Glyph, const FLinearColor& Color)
    {
        USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
        Box->SetWidthOverride(22.0f);
        Box->SetHeightOverride(22.0f);

        UBorder* Icon = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
        FSlateBrush Brush;
        Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
        Brush.TintColor = FSlateColor(Color);
        Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
        Brush.OutlineSettings.CornerRadii = FVector4(5.0f, 5.0f, 5.0f, 5.0f);
        Icon->SetBrush(Brush);
        Icon->SetHorizontalAlignment(HAlign_Center);
        Icon->SetVerticalAlignment(VAlign_Center);
        Icon->SetContent(MakeLabel(Tree, Glyph, 16, FLinearColor::White));
        Box->SetContent(Icon);
        return Box;
    }

    // Row `[icon] LABEL ... value / max` plus its bar; returns the bar and the value text.
    UProgressBar* MakeBar(UWidgetTree* Tree, UVerticalBox* Parent, const FText& Caption, const FText& Glyph, const FLinearColor& Fill, UTextBlock*& OutValue)
    {
        UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        Row->AddChildToHorizontalBox(MakeIcon(Tree, Glyph, Fill))->SetVerticalAlignment(VAlign_Center);

        UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(MakeLabel(Tree, Caption, 16, FLinearColor::White));
        LabelSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
        LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        LabelSlot->SetVerticalAlignment(VAlign_Center);

        OutValue = MakeLabel(Tree, FText::GetEmpty(), 16, FLinearColor::White);
        Row->AddChildToHorizontalBox(OutValue)->SetVerticalAlignment(VAlign_Center);
        Parent->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 2.0f));

        UProgressBar* Bar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
        Bar->SetFillColorAndOpacity(Fill);
        Bar->SetPercent(1.0f);
        Parent->AddChildToVerticalBox(Bar)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
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

    // Bottom-left panel, 320 wide.
    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    Panel->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.6f));
    Panel->SetPadding(FMargin(12.0f));
    UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
    PanelSlot->SetAnchors(FAnchors(0.0f, 1.0f, 0.0f, 1.0f));
    PanelSlot->SetAlignment(FVector2D(0.0f, 1.0f));
    PanelSlot->SetPosition(FVector2D(32.0f, -32.0f));
    PanelSlot->SetSize(FVector2D(320.0f, 210.0f));

    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    Panel->SetContent(Column);

    UTextBlock* HealthValue = nullptr;
    UTextBlock* StaminaValue = nullptr;
    HealthBar = MakeBar(WidgetTree, Column, LOCTEXT("HudHealth", "HEALTH"), LOCTEXT("HudHealthGlyph", "+"), FLinearColor(0.8f, 0.1f, 0.1f, 1.0f), HealthValue);
    StaminaBar = MakeBar(WidgetTree, Column, LOCTEXT("HudStamina", "STAMINA"), LOCTEXT("HudStaminaGlyph", ">"), FLinearColor(0.2f, 0.8f, 0.3f, 1.0f), StaminaValue);
    UTextBlock* HungerValue = nullptr;
    HungerBar = MakeBar(WidgetTree, Column, LOCTEXT("HudHunger", "HUNGER"), LOCTEXT("HudHungerGlyph", "o"), FPSLUiStyle::Accent(), HungerValue);
    HealthValueText = HealthValue;
    StaminaValueText = StaminaValue;
    HungerValueText = HungerValue;

    StateText = MakeLabel(WidgetTree, FText::GetEmpty(), 14, FLinearColor::White);
    Column->AddChildToVerticalBox(StateText)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));

    // Diet hint (e.g. "Carnivores can't eat plants"); empty until a bite touched the wrong food.
    HintText = MakeLabel(WidgetTree, FText::GetEmpty(), 14, FPSLUiStyle::Accent());
    Column->AddChildToVerticalBox(HintText)->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));

    // Centered DEAD text.
    DeadText = MakeLabel(WidgetTree, LOCTEXT("HudDead", "DEAD"), 64, FLinearColor(0.85f, 0.1f, 0.1f, 1.0f));
    DeadText->SetVisibility(ESlateVisibility::Collapsed);
    UCanvasPanelSlot* DeadSlot = Root->AddChildToCanvas(DeadText);
    DeadSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
    DeadSlot->SetAlignment(FVector2D(0.5f, 0.5f));
    DeadSlot->SetAutoSize(true);
    DeadSlot->SetPosition(FVector2D::ZeroVector);

    // Kill banner, top centre; hidden until a prey dies to the owning player.
    KillBannerText = MakeLabel(WidgetTree, LOCTEXT("HudPreyDown", "PREY DOWN"), 32, FLinearColor(1.0f, 0.75f, 0.2f, 1.0f));
    KillBannerText->SetVisibility(ESlateVisibility::Collapsed);
    UCanvasPanelSlot* BannerSlot = Root->AddChildToCanvas(KillBannerText);
    BannerSlot->SetAnchors(FAnchors(0.5f, 0.0f, 0.5f, 0.0f));
    BannerSlot->SetAlignment(FVector2D(0.5f, 0.0f));
    BannerSlot->SetAutoSize(true);
    BannerSlot->SetPosition(FVector2D(0.0f, 60.0f));

    // Hit marker, just right of and below the crosshair (screen centre).
    HitMarkerText = MakeLabel(WidgetTree, LOCTEXT("HudHit", "HIT"), 16, FLinearColor(1.0f, 0.9f, 0.3f, 1.0f));
    HitMarkerText->SetVisibility(ESlateVisibility::Collapsed);
    UCanvasPanelSlot* HitSlot = Root->AddChildToCanvas(HitMarkerText);
    HitSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
    HitSlot->SetAlignment(FVector2D(0.0f, 0.0f));
    HitSlot->SetAutoSize(true);
    HitSlot->SetPosition(FVector2D(24.0f, 16.0f));
}

void UPSLHudWidget::NativeConstruct()
{
    Super::NativeConstruct();
    // Re-adding to the viewport re-runs NativeConstruct; drop any earlier binding first.
    APSLPreyCharacter::OnAnyPreyKilled().Remove(PreyKilledHandle);
    PreyKilledHandle = APSLPreyCharacter::OnAnyPreyKilled().AddUObject(this, &UPSLHudWidget::HandlePreyKilled);
}

void UPSLHudWidget::SetValues(float Health, float MaxHealth, float Stamina, float MaxStamina, bool bSprinting, bool bExhausted, bool bDead)
{
    if (!HealthBar || !StaminaBar || !StateText || !DeadText || !HealthValueText || !StaminaValueText)
    {
        return;
    }

    HealthBar->SetPercent(APSLHUD::BarFraction(Health, MaxHealth));
    StaminaBar->SetPercent(APSLHUD::BarFraction(Stamina, MaxStamina));
    HealthValueText->SetText(FText::Format(LOCTEXT("HudValueFmt", "{0} / {1}"), FText::AsNumber(FMath::RoundToInt(Health)), FText::AsNumber(FMath::RoundToInt(MaxHealth))));
    StaminaValueText->SetText(FText::Format(LOCTEXT("HudValueFmt", "{0} / {1}"), FText::AsNumber(FMath::RoundToInt(Stamina)), FText::AsNumber(FMath::RoundToInt(MaxStamina))));

    HealthBar->SetFillColorAndOpacity(bDead ? FLinearColor(0.4f, 0.4f, 0.4f, 1.0f) : FLinearColor(0.8f, 0.1f, 0.1f, 1.0f));
    StaminaBar->SetFillColorAndOpacity(bExhausted ? FLinearColor(0.9f, 0.5f, 0.0f, 1.0f) : FLinearColor(0.2f, 0.8f, 0.3f, 1.0f));

    const FText State = bExhausted ? LOCTEXT("HudExhausted", "EXHAUSTED") : (bSprinting ? LOCTEXT("HudSprinting", "SPRINTING") : FText::GetEmpty());
    StateText->SetText(State);
    DeadText->SetVisibility(bDead ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UPSLHudWidget::SetHunger(float Hunger, float MaxHunger, bool bStarving, const FText& Hint)
{
    if (!HungerBar || !HungerValueText || !HintText)
    {
        return;
    }
    HungerBar->SetPercent(APSLHUD::BarFraction(Hunger, MaxHunger));
    HungerValueText->SetText(FText::Format(LOCTEXT("HudValueFmt", "{0} / {1}"), FText::AsNumber(FMath::RoundToInt(Hunger)), FText::AsNumber(FMath::RoundToInt(MaxHunger))));
    bStarvingFlash = bStarving;
    if (!bStarving)
    {
        HungerBar->SetFillColorAndOpacity(FPSLUiStyle::Accent());
    }
    HintText->SetText(Hint);
}

void UPSLHudWidget::HandlePreyKilled(APSLPreyCharacter* Prey, AActor* Killer)
{
    if (KillBannerText && Killer && Killer == GetOwningPlayerPawn())
    {
        KillBannerRemaining = KillBannerSeconds;
        KillBannerText->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
}

void UPSLHudWidget::HandleBiteHit()
{
    if (HitMarkerText)
    {
        HitMarkerRemaining = HitMarkerSeconds;
        HitMarkerText->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
}

void UPSLHudWidget::NativeDestruct()
{
    APSLPreyCharacter::OnAnyPreyKilled().Remove(PreyKilledHandle);
    if (APSLCharacter* Bound = BoundCharacter.Get())
    {
        Bound->OnBiteHit.RemoveAll(this);
    }
    BoundCharacter.Reset();
    Super::NativeDestruct();
}

void UPSLHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (KillBannerRemaining > 0.0f)
    {
        KillBannerRemaining -= InDeltaTime;
        if (KillBannerRemaining <= 0.0f && KillBannerText)
        {
            KillBannerText->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    if (HitMarkerRemaining > 0.0f)
    {
        HitMarkerRemaining -= InDeltaTime;
        if (HitMarkerRemaining <= 0.0f && HitMarkerText)
        {
            HitMarkerText->SetVisibility(ESlateVisibility::Collapsed);
        }
    }

    // Pawn can be null or replaced (respawn); resolve it every tick.
    APSLCharacter* Character = Cast<APSLCharacter>(GetOwningPlayerPawn());
    if (Character != BoundCharacter.Get())
    {
        if (APSLCharacter* Old = BoundCharacter.Get())
        {
            Old->OnBiteHit.RemoveAll(this);
        }
        BoundCharacter = Character;
        if (Character)
        {
            Character->OnBiteHit.AddDynamic(this, &UPSLHudWidget::HandleBiteHit);
        }
    }
    const UPSLStatsComponent* Stats = Character ? Character->GetStatsComponent() : nullptr;
    const UPSLSurvivalComponent* Survival = Character ? Character->GetSurvivalComponent() : nullptr;
    if (!Stats || !Survival)
    {
        return;
    }

    SetValues(Stats->GetHealth(), Stats->GetMaxHealth(), Survival->GetStamina(), Survival->GetMaxStamina(),
        Survival->IsSprinting(), Survival->IsSprintLocked(), Stats->IsDead());
    SetHunger(Survival->GetHunger(), Survival->GetMaxHunger(), Survival->IsStarving() && !Stats->IsDead(), Character->GetFoodHint());
    if (bStarvingFlash && HungerBar)
    {
        FlashTime += InDeltaTime;
        const bool bRust = FMath::Fmod(FlashTime, 0.8f) < 0.4f;
        HungerBar->SetFillColorAndOpacity(bRust ? FPSLUiStyle::Danger() : FPSLUiStyle::Accent());
    }
}

#undef LOCTEXT_NAMESPACE
