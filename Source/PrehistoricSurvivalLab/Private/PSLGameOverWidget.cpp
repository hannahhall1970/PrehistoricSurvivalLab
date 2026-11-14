#include "PSLGameOverWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Internationalization/Internationalization.h"
#include "PSLGameInstance.h"
#include "PSLGameMode.h"
#include "PSLSaveSubsystem.h"
#include "PSLUiButton.h"
#include "PSLUiStyle.h"

#define LOCTEXT_NAMESPACE "PSL"

bool UPSLGameOverWidget::Initialize()
{
    const bool bResult = Super::Initialize();
    if (bResult && WidgetTree && !WidgetTree->RootWidget)
    {
        BuildTree();
    }
    return bResult;
}

void UPSLGameOverWidget::BuildTree()
{
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
    WidgetTree->RootWidget = Root;

    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    Panel->SetBrushColor(FPSLUiStyle::Panel());
    Panel->SetPadding(FMargin(32.0f));
    UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
    PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
    PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
    PanelSlot->SetAutoSize(true);
    PanelSlot->SetPosition(FVector2D::ZeroVector);

    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    Panel->SetContent(Column);

    TitleText = FPSLUiStyle::MakeText(WidgetTree, FText::GetEmpty(), FPSLUiStyle::TitleSize, FPSLUiStyle::Danger(), true);
    Column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
    StatsText = FPSLUiStyle::MakeText(WidgetTree, FText::GetEmpty(), FPSLUiStyle::BodySize, FPSLUiStyle::Text());
    Column->AddChildToVerticalBox(StatsText)->SetPadding(FMargin(12.0f, 0.0f, 12.0f, 16.0f));

    auto AddButton = [this, Column](const FText& Text) -> UPSLUiButton*
    {
        UPSLUiButton* Button = UPSLUiButton::Make(WidgetTree, Text);
        Column->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.0f, 2.0f));
        return Button;
    };
    RetryButton = AddButton(LOCTEXT("GameOver_Retry", "Retry"));
    ContinueButton = AddButton(LOCTEXT("GameOver_Continue", "Continue"));
    MainMenuButton = AddButton(LOCTEXT("GameOver_MainMenu", "Main Menu"));

    RetryButton->OnClicked.AddDynamic(this, &UPSLGameOverWidget::OnRetry);
    ContinueButton->OnClicked.AddDynamic(this, &UPSLGameOverWidget::OnContinue);
    MainMenuButton->OnClicked.AddDynamic(this, &UPSLGameOverWidget::OnMainMenu);

    RefreshTexts();
}

void UPSLGameOverWidget::SetStarved(bool bInStarved)
{
    bStarved = bInStarved;
    RefreshTexts();
}

FString UPSLGameOverWidget::FormatSurvivalTime(float Seconds)
{
    const int32 Total = FMath::Max(0, FMath::FloorToInt(Seconds));
    return FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60);
}

void UPSLGameOverWidget::RefreshTexts()
{
    if (!TitleText || !StatsText || !RetryButton || !ContinueButton || !MainMenuButton)
    {
        return;
    }
    RetryButton->GetLabel()->SetText(LOCTEXT("GameOver_Retry", "Retry"));
    ContinueButton->GetLabel()->SetText(LOCTEXT("GameOver_Continue", "Continue"));
    MainMenuButton->GetLabel()->SetText(LOCTEXT("GameOver_MainMenu", "Main Menu"));
    TitleText->SetText(bStarved ? LOCTEXT("GameOver_Starved", "YOU STARVED") : LOCTEXT("GameOver_Died", "YOU DIED"));

    const UPSLGameInstance* Session = GetWorld() ? Cast<UPSLGameInstance>(GetWorld()->GetGameInstance()) : nullptr;
    const float Seconds = Session ? Session->SurvivalSeconds : 0.0f;
    const int32 Prey = Session ? Session->PreyKilled : 0;
    const float Food = Session ? Session->FoodEaten : 0.0f;
    StatsText->SetText(FText::Format(
        LOCTEXT("GameOver_Stats", "Survived {0}\nPrey killed: {1}\nFood eaten: {2}"),
        FText::AsCultureInvariant(FormatSurvivalTime(Seconds)), FText::AsNumber(Prey), FText::AsNumber(FMath::RoundToInt(Food))));
}

void UPSLGameOverWidget::NativeConstruct()
{
    Super::NativeConstruct();
    SetIsFocusable(true);
    if (!CultureHandle.IsValid())
    {
        CultureHandle = FInternationalization::Get().OnCultureChanged().AddUObject(this, &UPSLGameOverWidget::HandleCultureChanged);
    }
    RefreshTexts();

    const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    const UPSLSaveSubsystem* Saves = GI ? GI->GetSubsystem<UPSLSaveSubsystem>() : nullptr;
    ContinueButton->SetActive(Saves && Saves->HasValidSave());
    SetKeyboardFocus();
}

void UPSLGameOverWidget::NativeDestruct()
{
    if (CultureHandle.IsValid())
    {
        FInternationalization::Get().OnCultureChanged().Remove(CultureHandle);
        CultureHandle.Reset();
    }
    Super::NativeDestruct();
}

void UPSLGameOverWidget::HandleCultureChanged()
{
    RefreshTexts();
}

FReply UPSLGameOverWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    const FKey Key = InKeyEvent.GetKey();
    if (Key == EKeys::Enter)
    {
        OnRetry();
        return FReply::Handled();
    }
    if (Key == EKeys::Escape || Key == EKeys::P)
    {
        OnMainMenu();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UPSLGameOverWidget::OnRetry()
{
    if (APSLGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<APSLGameMode>() : nullptr)
    {
        GameMode->Retry();
    }
}

void UPSLGameOverWidget::OnContinue()
{
    if (UPSLGameInstance* Session = GetWorld() ? Cast<UPSLGameInstance>(GetWorld()->GetGameInstance()) : nullptr)
    {
        Session->ContinueGame();
    }
}

void UPSLGameOverWidget::OnMainMenu()
{
    if (UPSLGameInstance* Session = GetWorld() ? Cast<UPSLGameInstance>(GetWorld()->GetGameInstance()) : nullptr)
    {
        Session->ReturnToMenu();
    }
}

#undef LOCTEXT_NAMESPACE
