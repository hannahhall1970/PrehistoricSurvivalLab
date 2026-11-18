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
#include "EngineUtils.h"
#include "Engine/World.h"
#include "PSLPreyCharacter.h"
#include "PSLCharacter.h"
#include "PSLHUD.h"
#include "PSLUiStyle.h"
#include "PSLStatsComponent.h"
#include "PSLDinoSpecies.h"
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

        UTextBlock* CaptionLabel = MakeLabel(Tree, Caption, 16, FPSLUiStyle::Text(), EPSLFontWeight::SemiBold);
        OutCaptions.Add(CaptionLabel);
        UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(CaptionLabel);
        LabelSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
        LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        LabelSlot->SetVerticalAlignment(VAlign_Center);

        OutValue = MakeLabel(Tree, FText::GetEmpty(), 16, FPSLUiStyle::Text());
        Row->AddChildToHorizontalBox(OutValue)->SetVerticalAlignment(VAlign_Center);
        Parent->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 2.0f));

        UProgressBar* Bar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
        FPSLUiStyle::StyleBar(Bar, Fill);
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
    Panel->SetBrushColor(FPSLUiStyle::Backing());
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
    HealthBar = MakeBar(WidgetTree, Column, LOCTEXT("HudHealth", "HEALTH"), LOCTEXT("HudHealthGlyph", "+"), FPSLUiStyle::HealthFill(), HealthValue, CaptionTexts, GlyphTexts);
    StaminaBar = MakeBar(WidgetTree, Column, LOCTEXT("HudStamina", "STAMINA"), LOCTEXT("HudStaminaGlyph", ">"), FPSLUiStyle::StaminaFill(), StaminaValue, CaptionTexts, GlyphTexts);
    UTextBlock* HungerValue = nullptr;
    HungerBar = MakeBar(WidgetTree, Column, LOCTEXT("HudHunger", "HUNGER"), LOCTEXT("HudHungerGlyph", "o"), FPSLUiStyle::Accent(), HungerValue, CaptionTexts, GlyphTexts);
    HealthValueText = HealthValue;
    StaminaValueText = StaminaValue;
    HungerValueText = HungerValue;

    StateText = MakeLabel(WidgetTree, FText::GetEmpty(), 14, FPSLUiStyle::Text(), EPSLFontWeight::SemiBold);
    // vi "ĐANG CHẠY NƯỚC RÚT" (19 chars) fits the 296 px content width at 14 SemiBold (checked in PSL.UI.VietnameseLayout); wrap is a safety net.
    StateText->SetAutoWrapText(true);
    Column->AddChildToVerticalBox(StateText)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));

    // Prompt/diet hint ("Bite to eat"): own umber strip with parchment text, bottom centre above the message strip, so it
    // stays legible over bright ground. Collapsed while empty. 296 keeps the vi wrap bound pinned by PSL.UI.VietnameseLayout.
    HintBox = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HintBox"));
    HintBox->SetBrushColor(FPSLUiStyle::Backing());
    HintBox->SetPadding(FMargin(14.0f, 6.0f));
    HintBox->SetVisibility(ESlateVisibility::Collapsed);
    HintText = MakeLabel(WidgetTree, FText::GetEmpty(), 14, FPSLUiStyle::Text());
    // No AutoWrapText: with it, FSlateTextBlockLayout::CalculateWrappingWidth takes Min(WrapTextAt, CachedSize.X) (SlateTextBlockLayout.cpp:402-404)
    // and CachedSize is the previous allotted geometry (:240), which inside the auto-sized canvas slot is a stale narrow width ("Bite to" / "eat").
    // WrapTextAt alone bounds wrapping by the fixed 296 px, independent of layout.
    HintText->SetWrapTextAt(296.0f);
    HintBox->SetContent(HintText);
    UCanvasPanelSlot* HintSlot = Root->AddChildToCanvas(HintBox);
    HintSlot->SetAnchors(FAnchors(0.5f, 1.0f, 0.5f, 1.0f));
    HintSlot->SetAlignment(FVector2D(0.5f, 1.0f));
    HintSlot->SetAutoSize(true);
    HintSlot->SetPosition(FVector2D(0.0f, -176.0f));

    // Centered DEAD text.
    DeadText = MakeLabel(WidgetTree, LOCTEXT("HudDead", "DEAD"), 64, FPSLUiStyle::Danger(), EPSLFontWeight::Bold);
    DeadText->SetVisibility(ESlateVisibility::Collapsed);
    UCanvasPanelSlot* DeadSlot = Root->AddChildToCanvas(DeadText);
    DeadSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
    DeadSlot->SetAlignment(FVector2D(0.5f, 0.5f));
    DeadSlot->SetAutoSize(true);
    DeadSlot->SetPosition(FVector2D::ZeroVector);

    // Kill banner, top centre; hidden until a prey dies to the owning player.
    KillBannerText = MakeLabel(WidgetTree, LOCTEXT("HudPreyDown", "PREY DOWN"), 32, FPSLUiStyle::Accent(), EPSLFontWeight::Bold);
    KillBannerText->SetVisibility(ESlateVisibility::Collapsed);
    UCanvasPanelSlot* BannerSlot = Root->AddChildToCanvas(KillBannerText);
    BannerSlot->SetAnchors(FAnchors(0.5f, 0.0f, 0.5f, 0.0f));
    BannerSlot->SetAlignment(FVector2D(0.5f, 0.0f));
    BannerSlot->SetAutoSize(true);
    BannerSlot->SetPosition(FVector2D(0.0f, 60.0f));

    // Hit marker, just right of and below the crosshair (screen centre).
    HitMarkerText = MakeLabel(WidgetTree, LOCTEXT("HudHit", "HIT"), 16, FPSLUiStyle::Accent(), EPSLFontWeight::SemiBold);
    HitMarkerText->SetVisibility(ESlateVisibility::Collapsed);
    UCanvasPanelSlot* HitSlot = Root->AddChildToCanvas(HitMarkerText);
    HitSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
    HitSlot->SetAlignment(FVector2D(0.0f, 0.0f));
    HitSlot->SetAutoSize(true);
    HitSlot->SetPosition(FVector2D(24.0f, 16.0f));

    // Hunting/feeding message, bottom centre on its own backing; hidden until ShowMessage.
    MessageBox = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MessageBox"));
    MessageBox->SetBrushColor(FPSLUiStyle::Backing());
    MessageBox->SetPadding(FMargin(20.0f, 8.0f));
    MessageBox->SetVisibility(ESlateVisibility::Collapsed);
    MessageText = MakeLabel(WidgetTree, FText::GetEmpty(), FPSLUiStyle::BodySize, FPSLUiStyle::Text(), EPSLFontWeight::SemiBold);
    MessageBox->SetContent(MessageText);
    UCanvasPanelSlot* MessageSlot = Root->AddChildToCanvas(MessageBox);
    MessageSlot->SetAnchors(FAnchors(0.5f, 1.0f, 0.5f, 1.0f));
    MessageSlot->SetAlignment(FVector2D(0.5f, 1.0f));
    MessageSlot->SetAutoSize(true);
    MessageSlot->SetPosition(FVector2D(0.0f, -120.0f));
}

FText UPSLHudWidget::MessageForKey(FName Key)
{
    if (Key == FName(TEXT("PreyNearby"))) { return LOCTEXT("HudMsg_PreyNearby", "Prey nearby"); }
    if (Key == FName(TEXT("EatCarcass"))) { return LOCTEXT("HudMsg_EatCarcass", "Hold to eat the carcass"); }
    if (Key == FName(TEXT("PreyEscaped"))) { return LOCTEXT("HudMsg_PreyEscaped", "The prey got away"); }
    return FText::GetEmpty();
}

bool UPSLHudWidget::ShowMessage(FName Key)
{
    const FText Text = MessageForKey(Key);
    if (!MessageBox || !MessageText || Text.IsEmpty())
    {
        return false;
    }
    MessageKey = Key;
    MessageRemaining = FPSLUiStyle::ToastSeconds;
    MessageText->SetText(Text);
    MessageBox->SetVisibility(ESlateVisibility::HitTestInvisible);
    return true;
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
    if (MessageBox && MessageText && MessageKey != NAME_None)
    {
        MessageText->SetText(MessageForKey(MessageKey));
    }
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

    HealthBar->SetFillColorAndOpacity(bDead ? FPSLUiStyle::Disabled() : FPSLUiStyle::HealthFill());
    StaminaBar->SetFillColorAndOpacity(bExhausted ? FPSLUiStyle::SpentFill() : FPSLUiStyle::StaminaFill());

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
    if (HintBox)
    {
        HintBox->SetVisibility(Hint.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    }
}

void UPSLHudWidget::HandlePreyKilled(APSLPreyCharacter* Prey, AActor* Killer)
{
    if (KillBannerText && Killer && Killer == GetOwningPlayerPawn())
    {
        KillBannerRemaining = KillBannerSeconds;
        KillBannerText->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    if (Killer && Killer == GetOwningPlayerPawn() && bPreyNear)
    {
        bKilledWhileNear = true;
        bHitWhileNear = false;
    }
}

void UPSLHudWidget::HandleBiteHit()
{
    if (HitMarkerText)
    {
        HitMarkerRemaining = HitMarkerSeconds;
        HitMarkerText->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    if (bPreyNear)
    {
        bHitWhileNear = true;
    }
}

bool UPSLHudWidget::NextPreyNear(bool bWasNear, float NearestDist)
{
    return bWasNear ? NearestDist < PreyFarRadius : NearestDist <= PreyNearRadius;
}

FName UPSLHudWidget::DecideHuntMessage(bool bWasNear, bool bNowNear, bool bHitWhileNear, bool bKilledWhileNear)
{
    if (!bWasNear && bNowNear)
    {
        return FName(TEXT("PreyNearby"));
    }
    if (bWasNear && !bNowNear && bHitWhileNear && !bKilledWhileNear)
    {
        return FName(TEXT("PreyEscaped"));
    }
    return NAME_None;
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
    }
    BoundCharacter.Reset();
    Super::NativeDestruct();
}

void UPSLHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (MessageRemaining > 0.0f)
    {
        MessageRemaining -= InDeltaTime;
        if (MessageRemaining <= 0.0f && MessageBox)
        {
            MessageBox->SetVisibility(ESlateVisibility::Collapsed);
            MessageKey = NAME_None;
        }
    }
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
        bPreyNear = bHitWhileNear = bKilledWhileNear = false;
        if (Character)
        {
            Character->OnBiteHit.AddDynamic(this, &UPSLHudWidget::HandleBiteHit);
        }
    }

    // Hunt messages: carnivore pawns only.
    const UPSLDinoSpecies* Species = Character ? Character->GetSpecies() : nullptr;
    if (Species && Species->Diet == EPSLDiet::Carnivore)
    {
        float NearestSq = TNumericLimits<float>::Max();
        const FVector Here = Character->GetActorLocation();
        for (TActorIterator<APSLPreyCharacter> It(GetWorld()); It; ++It)
        {
            if (!It->IsDead())
            {
                NearestSq = FMath::Min(NearestSq, static_cast<float>(FVector::DistSquared2D(Here, It->GetActorLocation())));
            }
        }
        const float Nearest = NearestSq == TNumericLimits<float>::Max() ? NearestSq : FMath::Sqrt(NearestSq);
        const bool bNowNear = NextPreyNear(bPreyNear, Nearest);
        const FName Message = DecideHuntMessage(bPreyNear, bNowNear, bHitWhileNear, bKilledWhileNear);
        if (!Message.IsNone())
        {
            ShowMessage(Message);
        }
        if (bNowNear != bPreyNear)
        {
            bHitWhileNear = bKilledWhileNear = false;
        }
        bPreyNear = bNowNear;
    }
    else
    {
        bPreyNear = bHitWhileNear = bKilledWhileNear = false;
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
