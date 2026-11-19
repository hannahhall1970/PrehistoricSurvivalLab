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
#include "Internationalization/Internationalization.h"
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
    UTextBlock* MakeLabel(UWidgetTree* Tree, const FText& Text, int32 Size, const FLinearColor& Color, EPSLFontWeight Weight = EPSLFontWeight::Regular)
    {
        return FPSLUiStyle::MakeText(Tree, Text, Size, Color, Weight);
    }

    // 22x22 rounded icon in the bar colour with a centred glyph.
    UWidget* MakeIcon(UWidgetTree* Tree, const FText& Glyph, const FLinearColor& Color, UTextBlock*& OutGlyph)
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
        OutGlyph = MakeLabel(Tree, Glyph, 16, FLinearColor::White);
        Icon->SetContent(OutGlyph);
        Box->SetContent(Icon);
        return Box;
    }

    // Row `[icon] LABEL ... value / max` plus its bar; returns the bar and the value text.
    UProgressBar* MakeBar(UWidgetTree* Tree, UVerticalBox* Parent, const FText& Caption, const FText& Glyph, const FLinearColor& Fill, UTextBlock*& OutValue,
        TArray<TObjectPtr<UTextBlock>>& OutCaptions, TArray<TObjectPtr<UTextBlock>>& OutGlyphs)
    {
        UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        UTextBlock* GlyphLabel = nullptr;
        Row->AddChildToHorizontalBox(MakeIcon(Tree, Glyph, Fill, GlyphLabel))->SetVerticalAlignment(VAlign_Center);
        OutGlyphs.Add(GlyphLabel);

        UTextBlock* CaptionLabel = MakeLabel(Tree, Caption, 16, FLinearColor::White, EPSLFontWeight::SemiBold);
        OutCaptions.Add(CaptionLabel);
        UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(CaptionLabel);
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
    PanelSlot->SetSize(FVector2D(320.0f, 218.0f));

    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    Panel->SetContent(Column);

    UTextBlock* HealthValue = nullptr;
    UTextBlock* StaminaValue = nullptr;
    HealthBar = MakeBar(WidgetTree, Column, LOCTEXT("HudHealth", "HEALTH"), LOCTEXT("HudHealthGlyph", "+"), FLinearColor(0.8f, 0.1f, 0.1f, 1.0f), HealthValue, CaptionTexts, GlyphTexts);
    // Ghost strip right under the health bar: a separate shape (not only a colour) that trails each hit.
    GhostBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("GhostBar"));
    GhostBar->SetFillColorAndOpacity(FPSLUiStyle::Accent());
    GhostBar->SetPercent(0.0f);
    GhostBar->SetVisibility(ESlateVisibility::Hidden);
    USizeBox* GhostBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
    GhostBox->SetHeightOverride(4.0f);
    GhostBox->SetContent(GhostBar);
    Column->InsertChildAt(Column->GetChildIndex(HealthBar) + 1, GhostBox);
    StaminaBar = MakeBar(WidgetTree, Column, LOCTEXT("HudStamina", "STAMINA"), LOCTEXT("HudStaminaGlyph", ">"), FLinearColor(0.2f, 0.8f, 0.3f, 1.0f), StaminaValue, CaptionTexts, GlyphTexts);
    UTextBlock* HungerValue = nullptr;
    HungerBar = MakeBar(WidgetTree, Column, LOCTEXT("HudHunger", "HUNGER"), LOCTEXT("HudHungerGlyph", "o"), FPSLUiStyle::Accent(), HungerValue, CaptionTexts, GlyphTexts);
    HealthValueText = HealthValue;
    StaminaValueText = StaminaValue;
    HungerValueText = HungerValue;

    StateText = MakeLabel(WidgetTree, FText::GetEmpty(), 14, FLinearColor::White, EPSLFontWeight::SemiBold);
    // vi "ĐANG CHẠY NƯỚC RÚT" (19 chars) fits the 296 px content width at 14 SemiBold (checked in PSL.UI.VietnameseLayout); wrap is a safety net.
    StateText->SetAutoWrapText(true);
    Column->AddChildToVerticalBox(StateText)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));

    // Diet hint (e.g. "Carnivores can't eat plants"); empty until a bite touched the wrong food.
    HintText = MakeLabel(WidgetTree, FText::GetEmpty(), 14, FPSLUiStyle::Accent());
    // vi hint is ~48 chars: wrap inside the 296 px panel content width instead of overflowing it.
    HintText->SetAutoWrapText(true);
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
    if (!CultureHandle.IsValid())
    {
        CultureHandle = FInternationalization::Get().OnCultureChanged().AddUObject(this, &UPSLHudWidget::HandleCultureChanged);
    }
}

void UPSLHudWidget::HandleCultureChanged()
{
    RefreshTexts();
}

void UPSLHudWidget::RefreshTexts()
{
    if (!HealthBar || !HungerBar || !DeadText || !KillBannerText || !HitMarkerText || CaptionTexts.Num() != 3 || GlyphTexts.Num() != 3)
    {
        return;
    }
    CaptionTexts[0]->SetText(LOCTEXT("HudHealth", "HEALTH"));
    CaptionTexts[1]->SetText(LOCTEXT("HudStamina", "STAMINA"));
    CaptionTexts[2]->SetText(LOCTEXT("HudHunger", "HUNGER"));
    GlyphTexts[0]->SetText(LOCTEXT("HudHealthGlyph", "+"));
    GlyphTexts[1]->SetText(LOCTEXT("HudStaminaGlyph", ">"));
    GlyphTexts[2]->SetText(LOCTEXT("HudHungerGlyph", "o"));
    DeadText->SetText(LOCTEXT("HudDead", "DEAD"));
    KillBannerText->SetText(LOCTEXT("HudPreyDown", "PREY DOWN"));
    HitMarkerText->SetText(LOCTEXT("HudHit", "HIT"));
    // The food hint is passed in by the HUD every tick, so only the value/state texts need replaying.
    if (bHaveValues)
    {
        SetValues(LastHealth, LastMaxHealth, LastStamina, LastMaxStamina, bLastSprinting, bLastExhausted, bLastDead);
    }
    if (bHaveHunger)
    {
        HungerValueText->SetText(FText::Format(LOCTEXT("HudValueFmt", "{0} / {1}"), FText::AsNumber(FMath::RoundToInt(LastHunger)), FText::AsNumber(FMath::RoundToInt(LastMaxHunger))));
    }
}

void UPSLHudWidget::SetValues(float Health, float MaxHealth, float Stamina, float MaxStamina, bool bSprinting, bool bExhausted, bool bDead)
{
    if (!HealthBar || !StaminaBar || !StateText || !DeadText || !HealthValueText || !StaminaValueText)
    {
        return;
    }

    bHaveValues = true;
    LastHealth = Health;
    LastMaxHealth = MaxHealth;
    LastStamina = Stamina;
    LastMaxStamina = MaxStamina;
    bLastSprinting = bSprinting;
    bLastExhausted = bExhausted;
    bLastDead = bDead;

    HealthBar->SetPercent(APSLHUD::BarFraction(Health, MaxHealth));
    StaminaBar->SetPercent(APSLHUD::BarFraction(Stamina, MaxStamina));
    HealthValueText->SetText(FText::Format(LOCTEXT("HudValueFmt", "{0} / {1}"), FText::AsNumber(FMath::RoundToInt(Health)), FText::AsNumber(FMath::RoundToInt(MaxHealth))));
    StaminaValueText->SetText(FText::Format(LOCTEXT("HudValueFmt", "{0} / {1}"), FText::AsNumber(FMath::RoundToInt(Stamina)), FText::AsNumber(FMath::RoundToInt(MaxStamina))));

    // The hit flash (high-contrast text colour, <= 0.2 s) wins over the base red; dead stays grey.
    HealthBar->SetFillColorAndOpacity(bDead ? FLinearColor(0.4f, 0.4f, 0.4f, 1.0f) : (FlashRemaining > 0.0f ? FPSLUiStyle::Text() : FLinearColor(0.8f, 0.1f, 0.1f, 1.0f)));
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
    bHaveHunger = true;
    LastHunger = Hunger;
    LastMaxHunger = MaxHunger;
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

void UPSLHudWidget::HandleDamageTaken(float Amount, float NewHealth, EPSLDamageKind Kind, FVector CauserLocation, bool bHasCauser)
{
    const APSLCharacter* Character = BoundCharacter.Get();
    const UPSLStatsComponent* Stats = Character ? Character->GetStatsComponent() : nullptr;
    if (Amount <= 0.0f || !HealthBar || !Stats)
    {
        return;
    }
    // At most 3 flashes per second; a faster second hit still moves the ghost target below.
    if (PSLDamageFeedback::ShouldFlash(HudTime, LastFlashTime))
    {
        LastFlashTime = HudTime;
        FlashRemaining = PSLDamageFeedback::FlashSeconds;
    }
    PSLDamageFeedback::GhostOnHit(Ghost, APSLHUD::BarFraction(NewHealth, Stats->GetMaxHealth()));
}

void UPSLHudWidget::StepDamageFeedback(float RealFraction, bool bDead, float DeltaSeconds)
{
    if (bDead)
    {
        // Death wins: no flash, no trail.
        FlashRemaining = 0.0f;
        Ghost = PSLDamageFeedback::FGhostBar{RealFraction, 0.0f, 0.0f};
    }
    else
    {
        FlashRemaining = FMath::Max(0.0f, FlashRemaining - DeltaSeconds);
        PSLDamageFeedback::GhostStep(Ghost, RealFraction, DeltaSeconds);
    }
    if (GhostBar)
    {
        GhostBar->SetPercent(Ghost.Fraction);
        GhostBar->SetVisibility(Ghost.Fraction > RealFraction ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
    }
}

void UPSLHudWidget::NativeDestruct()
{
    if (CultureHandle.IsValid())
    {
        FInternationalization::Get().OnCultureChanged().Remove(CultureHandle);
        CultureHandle.Reset();
    }
    APSLPreyCharacter::OnAnyPreyKilled().Remove(PreyKilledHandle);
    if (APSLCharacter* Bound = BoundCharacter.Get())
    {
        Bound->OnBiteHit.RemoveAll(this);
        Bound->OnDamageTaken.RemoveAll(this);
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
            Old->OnDamageTaken.RemoveAll(this);
        }
        BoundCharacter = Character;
        FlashRemaining = 0.0f;
        Ghost = PSLDamageFeedback::FGhostBar{};
        if (Character)
        {
            Character->OnBiteHit.AddDynamic(this, &UPSLHudWidget::HandleBiteHit);
            Character->OnDamageTaken.AddDynamic(this, &UPSLHudWidget::HandleDamageTaken);
        }
    }
    const UPSLStatsComponent* Stats = Character ? Character->GetStatsComponent() : nullptr;
    const UPSLSurvivalComponent* Survival = Character ? Character->GetSurvivalComponent() : nullptr;
    if (!Stats || !Survival)
    {
        return;
    }

    HudTime += InDeltaTime;
    StepDamageFeedback(APSLHUD::BarFraction(Stats->GetHealth(), Stats->GetMaxHealth()), Stats->IsDead(), InDeltaTime);
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
