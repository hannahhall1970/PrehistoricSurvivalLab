#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSLPauseWidget.generated.h"

class UBorder;
class UPSLSettingsWidget;
class UPSLUiButton;
class UTextBlock;

/** Pause menu: Resume / Save / Settings / Main Menu / Quit. Handles Escape and P itself (pause uses UI-only input). */
UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLPauseWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual bool Initialize() override;

    UPSLUiButton* GetResumeButton() const { return ResumeButton; }
    UPSLUiButton* GetSaveButton() const { return SaveButton; }
    UPSLUiButton* GetSettingsButton() const { return SettingsButton; }
    UPSLUiButton* GetMainMenuButton() const { return MainMenuButton; }
    UPSLUiButton* GetQuitButton() const { return QuitButton; }
    UTextBlock* GetToastText() const { return ToastText; }
    UTextBlock* GetWarningText() const { return WarningText; }
    UPSLSettingsWidget* GetSettingsWidget() const { return SettingsWidget; }
    bool IsSettingsOpen() const;

    // Esc / P: Back inside Settings when it is open, else resumes.
    void HandleCancel();

    // Re-sets every static label from its LOCTEXT (called on construction and when the culture changes).
    void RefreshTexts();

    // Shows the toast on its backing strip for FPSLUiStyle::ToastSeconds (success = accent semi-bold, failure = rust).
    void ShowToast(const FText& Text, bool bSuccess);
    // Advances the toast timer; called from NativeTick, public so tests can drive it without a viewport.
    void TickToast(float DeltaSeconds);
    UBorder* GetToastBox() const { return ToastBox; }

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void BuildTree();
    void HandleCultureChanged();
    void RefreshWarning();

    UFUNCTION() void OnResume();
    UFUNCTION() void OnSave();
    UFUNCTION() void OnMainMenu();
    UFUNCTION() void OnQuit();
    UFUNCTION() void OnSettings();
    UFUNCTION() void OnSettingsClosed();

    UPROPERTY(Transient) TObjectPtr<UBorder> ToastBox;
    float ToastRemaining = 0.0f;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> ResumeButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> SaveButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> SettingsButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> MainMenuButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> QuitButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ToastText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> WarningText;
    UPROPERTY(Transient) TObjectPtr<UBorder> MenuPanel;
    UPROPERTY(Transient) TObjectPtr<UPSLSettingsWidget> SettingsWidget;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
    FDelegateHandle CultureHandle;
};
