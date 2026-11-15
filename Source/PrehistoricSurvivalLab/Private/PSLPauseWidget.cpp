#include "PSLPauseWidget.h"

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
#include "PSLGameInstance.h"
#include "PSLGameMode.h"
#include "PSLSaveSubsystem.h"
#include "PSLSettingsWidget.h"
#include "PSLUiButton.h"
#include "Internationalization/Internationalization.h"
#include "PSLUiStyle.h"

#define LOCTEXT_NAMESPACE "PSL"

bool UPSLPauseWidget::Initialize()
{
    const bool bResult = Super::Initialize();
    if (bResult && WidgetTree && !WidgetTree->RootWidget)
    {
        BuildTree();
    }
    return bResult;
}

void UPSLPauseWidget::BuildTree()
{
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
    WidgetTree->RootWidget = Root;

    // Centered panel, 480 wide.
    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    Panel->SetBrushColor(FPSLUiStyle::Panel());
    Panel->SetPadding(FMargin(24.0f));
    UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
    PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
    PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
    PanelSlot->SetAutoSize(true);
    PanelSlot->SetPosition(FVector2D::ZeroVector);
    MenuPanel = Panel;

    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    Panel->SetContent(Column);

    TitleText = FPSLUiStyle::MakeText(WidgetTree, LOCTEXT("Pause_Title", "PAUSED"), FPSLUiStyle::HeadingSize, FPSLUiStyle::Accent(), EPSLFontWeight::Bold);
    Column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));

    auto AddButton = [this, Column](const FText& Text) -> UPSLUiButton*
    {
        UPSLUiButton* Button = UPSLUiButton::Make(WidgetTree, Text);
        Column->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.0f, 2.0f));
        return Button;
    };
    ResumeButton = AddButton(LOCTEXT("Pause_Resume", "Resume"));
    SaveButton = AddButton(LOCTEXT("Pause_Save", "Save"));
    SettingsButton = AddButton(LOCTEXT("Pause_Settings", "Settings"));
    MainMenuButton = AddButton(LOCTEXT("Pause_MainMenu", "Main Menu"));
    QuitButton = AddButton(LOCTEXT("Pause_Quit", "Quit"));

    ResumeButton->OnClicked.AddDynamic(this, &UPSLPauseWidget::OnResume);
    SaveButton->OnClicked.AddDynamic(this, &UPSLPauseWidget::OnSave);
    SettingsButton->OnClicked.AddDynamic(this, &UPSLPauseWidget::OnSettings);
    MainMenuButton->OnClicked.AddDynamic(this, &UPSLPauseWidget::OnMainMenu);
    QuitButton->OnClicked.AddDynamic(this, &UPSLPauseWidget::OnQuit);

    // The panel is 480 wide with 24 padding: wrap the long vi toast/warning texts at the 432 px content width.
    ToastText = FPSLUiStyle::MakeText(WidgetTree, FText::GetEmpty(), FPSLUiStyle::BodySize, FPSLUiStyle::Accent());
    ToastText->SetAutoWrapText(true);
    ToastText->SetWrapTextAt(432.0f);
    Column->AddChildToVerticalBox(ToastText)->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 0.0f));
    WarningText = FPSLUiStyle::MakeText(WidgetTree, FText::GetEmpty(), FPSLUiStyle::BodySize, FPSLUiStyle::Danger());
    WarningText->SetAutoWrapText(true);
    WarningText->SetWrapTextAt(432.0f);
    Column->AddChildToVerticalBox(WarningText)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));

    // Settings replaces the pause panel while open. NewObject + Initialize: the tree must build outside a world (unit tests).
    SettingsWidget = NewObject<UPSLSettingsWidget>(WidgetTree, UPSLSettingsWidget::StaticClass());
    SettingsWidget->Initialize();
    SettingsWidget->SetVisibility(ESlateVisibility::Collapsed);
    SettingsWidget->OnClosed.AddDynamic(this, &UPSLPauseWidget::OnSettingsClosed);
    UCanvasPanelSlot* SettingsSlot = Root->AddChildToCanvas(SettingsWidget);
    SettingsSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
    SettingsSlot->SetOffsets(FMargin(0.0f));
}

void UPSLPauseWidget::NativeConstruct()
{
    Super::NativeConstruct();
    SetIsFocusable(true);
    if (!CultureHandle.IsValid())
    {
        CultureHandle = FInternationalization::Get().OnCultureChanged().AddUObject(this, &UPSLPauseWidget::HandleCultureChanged);
    }
    if (ToastText)
    {
        ToastText->SetText(FText::GetEmpty());
    }
    RefreshTexts();
    SetKeyboardFocus();
}

void UPSLPauseWidget::NativeDestruct()
{
    if (CultureHandle.IsValid())
    {
        FInternationalization::Get().OnCultureChanged().Remove(CultureHandle);
        CultureHandle.Reset();
    }
    Super::NativeDestruct();
}

void UPSLPauseWidget::HandleCultureChanged()
{
    RefreshTexts();
}

void UPSLPauseWidget::RefreshTexts()
{
    if (!TitleText || !ResumeButton || !SaveButton || !SettingsButton || !MainMenuButton || !QuitButton || !WarningText)
    {
        return; // tree not built yet (Initialize builds it)
    }
    TitleText->SetText(LOCTEXT("Pause_Title", "PAUSED"));
    ResumeButton->GetLabel()->SetText(LOCTEXT("Pause_Resume", "Resume"));
    SaveButton->GetLabel()->SetText(LOCTEXT("Pause_Save", "Save"));
    SettingsButton->GetLabel()->SetText(LOCTEXT("Pause_Settings", "Settings"));
    MainMenuButton->GetLabel()->SetText(LOCTEXT("Pause_MainMenu", "Main Menu"));
    QuitButton->GetLabel()->SetText(LOCTEXT("Pause_Quit", "Quit"));
    RefreshWarning();
}

void UPSLPauseWidget::RefreshWarning()
{
    FText Warning = LOCTEXT("Pause_NeverSaved", "This run has never been saved.");
    const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    const UPSLSaveSubsystem* Saves = GI ? GI->GetSubsystem<UPSLSaveSubsystem>() : nullptr;
    FPSLSaveData Data;
    FText Error;
    if (Saves && Saves->LoadAndValidate(Data, Error))
    {
        Warning = FText::Format(LOCTEXT("Pause_UnsavedSince", "Unsaved progress since {0}."), FText::AsTime(Data.SavedAtUtc));
    }
    WarningText->SetText(Warning);
}

bool UPSLPauseWidget::IsSettingsOpen() const
{
    return SettingsWidget && SettingsWidget->GetVisibility() != ESlateVisibility::Collapsed;
}

void UPSLPauseWidget::HandleCancel()
{
    if (IsSettingsOpen())
    {
        SettingsWidget->RequestBack();
        return;
    }
    OnResume();
}

void UPSLPauseWidget::OnSettings()
{
    SettingsWidget->LoadFromSettings();
    SettingsWidget->SelectTab(UPSLSettingsWidget::ETab::Graphics);
    MenuPanel->SetVisibility(ESlateVisibility::Collapsed);
    SettingsWidget->SetVisibility(ESlateVisibility::Visible);
    SettingsWidget->SetKeyboardFocus();
}

void UPSLPauseWidget::OnSettingsClosed()
{
    SettingsWidget->SetVisibility(ESlateVisibility::Collapsed);
    MenuPanel->SetVisibility(ESlateVisibility::Visible);
    SetKeyboardFocus();
}

FReply UPSLPauseWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::P)
    {
        HandleCancel();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UPSLPauseWidget::OnResume()
{
    if (APSLGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<APSLGameMode>() : nullptr)
    {
        GameMode->ResumeGame();
    }
}

void UPSLPauseWidget::OnSave()
{
    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    UPSLSaveSubsystem* Saves = GI ? GI->GetSubsystem<UPSLSaveSubsystem>() : nullptr;
    const bool bSaved = Saves && Saves->SaveCheckpoint(EPSLSaveReason::Pause);
    ToastText->SetText(bSaved ? LOCTEXT("Pause_Saved", "Saved") : LOCTEXT("Pause_SaveRefused", "Can't save right now: the dino is starving or badly hurt."));
    RefreshWarning();
}

void UPSLPauseWidget::OnMainMenu()
{
    if (UPSLGameInstance* Session = GetWorld() ? Cast<UPSLGameInstance>(GetWorld()->GetGameInstance()) : nullptr)
    {
        Session->ReturnToMenu();
    }
}

void UPSLPauseWidget::OnQuit()
{
    if (UPSLGameInstance* Session = GetWorld() ? Cast<UPSLGameInstance>(GetWorld()->GetGameInstance()) : nullptr)
    {
        Session->QuitGame();
    }
}

#undef LOCTEXT_NAMESPACE
