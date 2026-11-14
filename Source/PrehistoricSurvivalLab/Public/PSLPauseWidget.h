#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSLPauseWidget.generated.h"

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

    // Esc / P: resumes.
    void HandleCancel();

protected:
    virtual void NativeConstruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
    void BuildTree();
    void RefreshWarning();

    UFUNCTION() void OnResume();
    UFUNCTION() void OnSave();
    UFUNCTION() void OnMainMenu();
    UFUNCTION() void OnQuit();

    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> ResumeButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> SaveButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> SettingsButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> MainMenuButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> QuitButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ToastText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> WarningText;
};
