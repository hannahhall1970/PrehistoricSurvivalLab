#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSLMainMenuWidget.generated.h"

class UPSLSpeciesSelectWidget;
class UPSLUiButton;
class UWidget;
class UTextBlock;

/** Main menu: title, version, Continue / New Game / Settings / Quit. New Game opens the species screen over it. */
UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLMainMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual bool Initialize() override;

    UPSLUiButton* GetContinueButton() const { return ContinueButton; }
    UPSLUiButton* GetNewGameButton() const { return NewGameButton; }
    UPSLUiButton* GetSettingsButton() const { return SettingsButton; }
    UPSLUiButton* GetQuitButton() const { return QuitButton; }
    UTextBlock* GetVersionText() const { return VersionText; }
    UPSLSpeciesSelectWidget* GetSpeciesWidget() const { return SpeciesWidget; }

    // Re-reads the save slot: Continue is active only when a valid save exists.
    void RefreshContinue();

    // Called by the species screen's Back button.
    void ShowMain();

protected:
    virtual void NativeConstruct() override;

private:
    void BuildTree();

    UFUNCTION() void OnContinue();
    UFUNCTION() void OnNewGame();
    UFUNCTION() void OnSpeciesBack();
    UFUNCTION() void OnQuit();

    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> ContinueButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> NewGameButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> SettingsButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> QuitButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> VersionText;
    UPROPERTY(Transient) TObjectPtr<UPSLSpeciesSelectWidget> SpeciesWidget;
    UPROPERTY(Transient) TObjectPtr<UWidget> MainPanel;
};
