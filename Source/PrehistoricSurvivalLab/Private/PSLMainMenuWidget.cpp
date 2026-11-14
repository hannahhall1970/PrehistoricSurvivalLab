#include "PSLMainMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "PSLGameInstance.h"
#include "PSLSaveSubsystem.h"
#include "PSLSpeciesSelectWidget.h"
#include "PSLUiButton.h"
#include "PSLUiStyle.h"

#define LOCTEXT_NAMESPACE "PSL"

bool UPSLMainMenuWidget::Initialize()
{
    const bool bResult = Super::Initialize();
    if (bResult && WidgetTree && !WidgetTree->RootWidget)
    {
        BuildTree();
    }
    return bResult;
}

void UPSLMainMenuWidget::BuildTree()
{
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
    WidgetTree->RootWidget = Root;

    // Left-anchored column: the menu scene keeps the right side free for the preview dino.
    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    Panel->SetBrushColor(FPSLUiStyle::Panel());
    Panel->SetPadding(FMargin(32.0f));
    UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
    PanelSlot->SetAnchors(FAnchors(0.0f, 0.5f, 0.0f, 0.5f));
    PanelSlot->SetAlignment(FVector2D(0.0f, 0.5f));
    PanelSlot->SetAutoSize(true);
    PanelSlot->SetPosition(FVector2D(96.0f, 0.0f));
    MainPanel = Panel;

    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    Panel->SetContent(Column);

    UTextBlock* Title = FPSLUiStyle::MakeText(WidgetTree, LOCTEXT("Menu_Title", "PREHISTORIC SURVIVAL"), FPSLUiStyle::TitleSize, FPSLUiStyle::Accent(), true);
    Column->AddChildToVerticalBox(Title)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 24.0f));

    auto AddButton = [this, Column](const FText& Text) -> UPSLUiButton*
    {
        UPSLUiButton* Button = UPSLUiButton::Make(WidgetTree, Text);
        Column->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.0f, 2.0f));
        return Button;
    };
    ContinueButton = AddButton(LOCTEXT("Menu_Continue", "Continue"));
    NewGameButton = AddButton(LOCTEXT("Menu_NewGame", "New Game"));
    SettingsButton = AddButton(LOCTEXT("Menu_Settings", "Settings"));
    QuitButton = AddButton(LOCTEXT("Menu_Quit", "Quit"));

    ContinueButton->OnClicked.AddDynamic(this, &UPSLMainMenuWidget::OnContinue);
    NewGameButton->OnClicked.AddDynamic(this, &UPSLMainMenuWidget::OnNewGame);
    QuitButton->OnClicked.AddDynamic(this, &UPSLMainMenuWidget::OnQuit);
    // Settings screen arrives with M2b2; hidden and unbound until then.
    SettingsButton->SetVisibility(ESlateVisibility::Collapsed);

    // Read from config: the project-settings class lives in EngineSettings, which this module does not link.
    FString ProjectVersion;
    if (!GConfig || !GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), ProjectVersion, GGameIni) || ProjectVersion.IsEmpty())
    {
        ProjectVersion = TEXT("1.0.0.0"); // engine BaseGame.ini default
    }
    VersionText = FPSLUiStyle::MakeText(WidgetTree,
        FText::Format(LOCTEXT("Menu_Version", "Version {0}"), FText::AsCultureInvariant(ProjectVersion)),
        FPSLUiStyle::BodySize, FPSLUiStyle::Disabled());
    Column->AddChildToVerticalBox(VersionText)->SetPadding(FMargin(12.0f, 20.0f, 0.0f, 0.0f));

    // Species screen overlays the whole screen while New Game is open.
    // NewObject + Initialize rather than CreateWidget: the menu must also build outside a world (unit tests).
    SpeciesWidget = NewObject<UPSLSpeciesSelectWidget>(WidgetTree, UPSLSpeciesSelectWidget::StaticClass());
    SpeciesWidget->Initialize();
    SpeciesWidget->SetVisibility(ESlateVisibility::Collapsed);
    SpeciesWidget->GetBackButton()->OnClicked.AddDynamic(this, &UPSLMainMenuWidget::OnSpeciesBack);
    UCanvasPanelSlot* SpeciesSlot = Root->AddChildToCanvas(SpeciesWidget);
    SpeciesSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
    SpeciesSlot->SetOffsets(FMargin(0.0f));
}

void UPSLMainMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();
    SetIsFocusable(true);
    RefreshContinue();
}

void UPSLMainMenuWidget::RefreshContinue()
{
    const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    const UPSLSaveSubsystem* Saves = GI ? GI->GetSubsystem<UPSLSaveSubsystem>() : nullptr;
    ContinueButton->SetActive(Saves && Saves->HasValidSave());
}

void UPSLMainMenuWidget::ShowMain()
{
    SpeciesWidget->SetVisibility(ESlateVisibility::Collapsed);
    MainPanel->SetVisibility(ESlateVisibility::Visible);
    RefreshContinue();
    SetKeyboardFocus();
}

void UPSLMainMenuWidget::OnContinue()
{
    if (UPSLGameInstance* Session = GetWorld() ? Cast<UPSLGameInstance>(GetWorld()->GetGameInstance()) : nullptr)
    {
        Session->ContinueGame();
    }
}

void UPSLMainMenuWidget::OnNewGame()
{
    MainPanel->SetVisibility(ESlateVisibility::Collapsed);
    SpeciesWidget->SetVisibility(ESlateVisibility::Visible);
    SpeciesWidget->SetKeyboardFocus();
}

void UPSLMainMenuWidget::OnSpeciesBack()
{
    ShowMain();
}

void UPSLMainMenuWidget::OnQuit()
{
    if (UPSLGameInstance* Session = GetWorld() ? Cast<UPSLGameInstance>(GetWorld()->GetGameInstance()) : nullptr)
    {
        Session->QuitGame();
    }
}

#undef LOCTEXT_NAMESPACE
