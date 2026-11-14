#include "PSLSpeciesSelectWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "PSLDinoSpecies.h"
#include "PSLGameInstance.h"
#include "PSLMenuPreviewActor.h"
#include "PSLUiButton.h"
#include "PSLUiStyle.h"

#define LOCTEXT_NAMESPACE "PSL"

namespace
{
    // Raw stat values in StatCount order: Health, Stamina, Speed, Bite (damage per second), Hunger endurance (seconds), Diet.
    void RawStats(const UPSLDinoSpecies& S, float (&Out)[UPSLSpeciesSelectWidget::StatCount])
    {
        Out[0] = S.MaxHealth;
        Out[1] = S.MaxStamina;
        Out[2] = S.SprintSpeed;
        Out[3] = S.BiteCooldown > KINDA_SMALL_NUMBER ? S.BiteDamage / S.BiteCooldown : S.BiteDamage;
        Out[4] = S.HungerDrainPerSecond > KINDA_SMALL_NUMBER ? S.MaxHunger / S.HungerDrainPerSecond : S.MaxHunger;
        Out[5] = S.Diet == EPSLDiet::Carnivore ? 1.0f : 0.5f;
    }
}

bool UPSLSpeciesSelectWidget::Initialize()
{
    const bool bResult = Super::Initialize();
    if (bResult && WidgetTree && !WidgetTree->RootWidget)
    {
        BuildTree();
    }
    return bResult;
}

void UPSLSpeciesSelectWidget::BuildTree()
{
    Species.Add(UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::TRexId, this));
    Species.Add(UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::ParaId, this));

    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
    WidgetTree->RootWidget = Root;

    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    Panel->SetBrushColor(FPSLUiStyle::Panel());
    Panel->SetPadding(FMargin(32.0f));
    UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
    PanelSlot->SetAnchors(FAnchors(0.0f, 0.5f, 0.0f, 0.5f));
    PanelSlot->SetAlignment(FVector2D(0.0f, 0.5f));
    PanelSlot->SetAutoSize(true);
    PanelSlot->SetPosition(FVector2D(96.0f, 0.0f));

    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    Panel->SetContent(Column);

    UTextBlock* Heading = FPSLUiStyle::MakeText(WidgetTree, LOCTEXT("Species_Heading", "CHOOSE YOUR DINO"), FPSLUiStyle::HeadingSize, FPSLUiStyle::Accent(), true);
    Column->AddChildToVerticalBox(Heading)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));

    // < Name >
    UHorizontalBox* NameRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    Column->AddChildToVerticalBox(NameRow);
    PrevButton = UPSLUiButton::Make(WidgetTree, LOCTEXT("Species_Prev", "<"));
    NameRow->AddChildToHorizontalBox(PrevButton);
    NameText = FPSLUiStyle::MakeText(WidgetTree, FText::GetEmpty(), FPSLUiStyle::ButtonSize, FPSLUiStyle::Text(), true);
    UHorizontalBoxSlot* NameSlot = NameRow->AddChildToHorizontalBox(NameText);
    NameSlot->SetPadding(FMargin(16.0f, 6.0f));
    NameSlot->SetVerticalAlignment(VAlign_Center);
    NextButton = UPSLUiButton::Make(WidgetTree, LOCTEXT("Species_Next", ">"));
    NameRow->AddChildToHorizontalBox(NextButton);

    DietText = FPSLUiStyle::MakeText(WidgetTree, FText::GetEmpty(), FPSLUiStyle::BodySize, FPSLUiStyle::Accent(), true);
    Column->AddChildToVerticalBox(DietText)->SetPadding(FMargin(12.0f, 4.0f));
    DescriptionText = FPSLUiStyle::MakeText(WidgetTree, FText::GetEmpty(), FPSLUiStyle::BodySize, FPSLUiStyle::Text());
    DescriptionText->SetAutoWrapText(true);
    Column->AddChildToVerticalBox(DescriptionText)->SetPadding(FMargin(12.0f, 4.0f, 12.0f, 12.0f));

    const FText StatNames[StatCount] = {
        LOCTEXT("Stat_Health", "Health"), LOCTEXT("Stat_Stamina", "Stamina"), LOCTEXT("Stat_Speed", "Speed"),
        LOCTEXT("Stat_Bite", "Bite"), LOCTEXT("Stat_Hunger", "Hunger endurance"), LOCTEXT("Stat_Diet", "Diet")};
    for (int32 Index = 0; Index < StatCount; ++Index)
    {
        UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        Column->AddChildToVerticalBox(Row)->SetPadding(FMargin(12.0f, 2.0f));
        UTextBlock* Label = FPSLUiStyle::MakeText(WidgetTree, StatNames[Index], FPSLUiStyle::BodySize, FPSLUiStyle::Text());
        UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Label);
        LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        LabelSlot->SetVerticalAlignment(VAlign_Center);
        UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
        Bar->SetFillColorAndOpacity(FPSLUiStyle::Accent());
        UHorizontalBoxSlot* BarSlot = Row->AddChildToHorizontalBox(Bar);
        BarSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        BarSlot->SetVerticalAlignment(VAlign_Center);
        StatBars.Add(Bar);
    }

    ConfirmButton = UPSLUiButton::Make(WidgetTree, LOCTEXT("Species_Confirm", "Confirm"));
    Column->AddChildToVerticalBox(ConfirmButton)->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 2.0f));
    BackButton = UPSLUiButton::Make(WidgetTree, LOCTEXT("Species_Back", "Back"));
    Column->AddChildToVerticalBox(BackButton)->SetPadding(FMargin(0.0f, 2.0f));

    PrevButton->OnClicked.AddDynamic(this, &UPSLSpeciesSelectWidget::OnPrev);
    NextButton->OnClicked.AddDynamic(this, &UPSLSpeciesSelectWidget::OnNext);
    ConfirmButton->OnClicked.AddDynamic(this, &UPSLSpeciesSelectWidget::OnConfirm);

    Refresh();
}

void UPSLSpeciesSelectWidget::NativeConstruct()
{
    Super::NativeConstruct();
    SetIsFocusable(true);
    Refresh();
}

FName UPSLSpeciesSelectWidget::GetCurrentSpeciesId() const
{
    return Species.IsValidIndex(CurrentIndex) && Species[CurrentIndex] ? Species[CurrentIndex]->SpeciesId : UPSLDinoSpecies::TRexId;
}

void UPSLSpeciesSelectWidget::SelectNext()
{
    CurrentIndex = (CurrentIndex + 1) % Species.Num();
    Refresh();
}

void UPSLSpeciesSelectWidget::SelectPrevious()
{
    CurrentIndex = (CurrentIndex + Species.Num() - 1) % Species.Num();
    Refresh();
}

void UPSLSpeciesSelectWidget::Refresh()
{
    if (!Species.IsValidIndex(CurrentIndex) || !NameText)
    {
        return;
    }
    const UPSLDinoSpecies& Current = *Species[CurrentIndex];
    NameText->SetText(Current.DisplayName);
    DescriptionText->SetText(Current.Description);
    DietText->SetText(Current.Diet == EPSLDiet::Carnivore ? LOCTEXT("Diet_Carnivore", "CARNIVORE") : LOCTEXT("Diet_Herbivore", "HERBIVORE"));

    // Each stat is normalized against the larger of the two species, so the best one fills its bar.
    float Values[StatCount];
    RawStats(Current, Values);
    float Maxima[StatCount];
    FMemory::Memcpy(Maxima, Values);
    for (const UPSLDinoSpecies* Other : Species)
    {
        float OtherValues[StatCount];
        RawStats(*Other, OtherValues);
        for (int32 Index = 0; Index < StatCount; ++Index)
        {
            Maxima[Index] = FMath::Max(Maxima[Index], OtherValues[Index]);
        }
    }
    for (int32 Index = 0; Index < StatCount; ++Index)
    {
        StatBars[Index]->SetPercent(Maxima[Index] > KINDA_SMALL_NUMBER ? FMath::Clamp(Values[Index] / Maxima[Index], 0.0f, 1.0f) : 0.0f);
    }

    if (const UWorld* World = GetWorld())
    {
        for (TActorIterator<APSLMenuPreviewActor> It(World); It; ++It)
        {
            It->SetSpecies(Current.SpeciesId);
        }
    }
}

FReply UPSLSpeciesSelectWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    const FKey Key = InKeyEvent.GetKey();
    if (Key == EKeys::A || Key == EKeys::Left)
    {
        SelectPrevious();
        return FReply::Handled();
    }
    if (Key == EKeys::D || Key == EKeys::Right)
    {
        SelectNext();
        return FReply::Handled();
    }
    if (Key == EKeys::Enter)
    {
        OnConfirm();
        return FReply::Handled();
    }
    if (Key == EKeys::Escape)
    {
        BackButton->OnClicked.Broadcast();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UPSLSpeciesSelectWidget::OnPrev()
{
    SelectPrevious();
}

void UPSLSpeciesSelectWidget::OnNext()
{
    SelectNext();
}

void UPSLSpeciesSelectWidget::OnConfirm()
{
    if (UPSLGameInstance* Session = GetWorld() ? Cast<UPSLGameInstance>(GetWorld()->GetGameInstance()) : nullptr)
    {
        Session->StartNewGame(GetCurrentSpeciesId());
    }
}

#undef LOCTEXT_NAMESPACE
