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

    // 22x22 rounded icon in the bar colour with a centred glyph.
    UWidget* MakeIcon(UWidgetTree* Tree, const TCHAR* Glyph, const FLinearColor& Color)
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
    UProgressBar* MakeBar(UWidgetTree* Tree, UVerticalBox* Parent, const TCHAR* Caption, const TCHAR* Glyph, const FLinearColor& Fill, UTextBlock*& OutValue)
    {
        UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        Row->AddChildToHorizontalBox(MakeIcon(Tree, Glyph, Fill))->SetVerticalAlignment(VAlign_Center);

        UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(MakeLabel(Tree, Caption, 16, FLinearColor::White));
        LabelSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
        LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        LabelSlot->SetVerticalAlignment(VAlign_Center);

        OutValue = MakeLabel(Tree, TEXT("0 / 0"), 16, FLinearColor::White);
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
    PanelSlot->SetSize(FVector2D(320.0f, 150.0f));

    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    Panel->SetContent(Column);

    UTextBlock* HealthValue = nullptr;
    UTextBlock* StaminaValue = nullptr;
    HealthBar = MakeBar(WidgetTree, Column, TEXT("HEALTH"), TEXT("+"), FLinearColor(0.8f, 0.1f, 0.1f, 1.0f), HealthValue);
    StaminaBar = MakeBar(WidgetTree, Column, TEXT("STAMINA"), TEXT(">"), FLinearColor(0.2f, 0.8f, 0.3f, 1.0f), StaminaValue);
    HealthValueText = HealthValue;
    StaminaValueText = StaminaValue;

    StateText = MakeLabel(WidgetTree, TEXT(""), 14, FLinearColor::White);
    Column->AddChildToVerticalBox(StateText)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));

    // Centered DEAD text.
    DeadText = MakeLabel(WidgetTree, TEXT("DEAD"), 64, FLinearColor(0.85f, 0.1f, 0.1f, 1.0f));
    DeadText->SetVisibility(ESlateVisibility::Collapsed);
    UCanvasPanelSlot* DeadSlot = Root->AddChildToCanvas(DeadText);
    DeadSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
    DeadSlot->SetAlignment(FVector2D(0.5f, 0.5f));
    DeadSlot->SetAutoSize(true);
    DeadSlot->SetPosition(FVector2D::ZeroVector);

    // Kill banner, top centre; hidden until a prey dies to the owning player.
    KillBannerText = MakeLabel(WidgetTree, TEXT("PREY DOWN"), 32, FLinearColor(1.0f, 0.75f, 0.2f, 1.0f));
    KillBannerText->SetVisibility(ESlateVisibility::Collapsed);
    UCanvasPanelSlot* BannerSlot = Root->AddChildToCanvas(KillBannerText);
    BannerSlot->SetAnchors(FAnchors(0.5f, 0.0f, 0.5f, 0.0f));
    BannerSlot->SetAlignment(FVector2D(0.5f, 0.0f));
    BannerSlot->SetAutoSize(true);
    BannerSlot->SetPosition(FVector2D(0.0f, 60.0f));

    // Hit marker, just right of and below the crosshair (screen centre).
    HitMarkerText = MakeLabel(WidgetTree, TEXT("HIT"), 16, FLinearColor(1.0f, 0.9f, 0.3f, 1.0f));
    HitMarkerText->SetVisibility(ESlateVisibility::Collapsed);
    UCanvasPanelSlot* HitSlot = Root->AddChildToCanvas(HitMarkerText);
    HitSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
    HitSlot->SetAlignment(FVector2D(0.0f, 0.0f));
    HitSlot->SetAutoSize(true);
    HitSlot->SetPosition(FVector2D(24.0f, 16.0f));

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
    HealthValueText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Health), FMath::RoundToInt(MaxHealth))));
    StaminaValueText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Stamina), FMath::RoundToInt(MaxStamina))));

    HealthBar->SetFillColorAndOpacity(bDead ? FLinearColor(0.4f, 0.4f, 0.4f, 1.0f) : FLinearColor(0.8f, 0.1f, 0.1f, 1.0f));
    StaminaBar->SetFillColorAndOpacity(bExhausted ? FLinearColor(0.9f, 0.5f, 0.0f, 1.0f) : FLinearColor(0.2f, 0.8f, 0.3f, 1.0f));

    const TCHAR* State = bExhausted ? TEXT("EXHAUSTED") : (bSprinting ? TEXT("SPRINTING") : TEXT(""));
    StateText->SetText(FText::FromString(State));
    DeadText->SetVisibility(bDead ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
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
}
