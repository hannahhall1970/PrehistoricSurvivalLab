#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSLGameOverWidget.generated.h"

class UPSLUiButton;
class UTextBlock;

/** Game Over: cause title, run stats, Retry / Continue / Main Menu. Handles keys itself (Game Over uses UI-only input). */
UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLGameOverWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual bool Initialize() override;

    UPSLUiButton* GetRetryButton() const { return RetryButton; }
    UPSLUiButton* GetContinueButton() const { return ContinueButton; }
    UPSLUiButton* GetMainMenuButton() const { return MainMenuButton; }
    UTextBlock* GetTitleText() const { return TitleText; }
    UTextBlock* GetStatsText() const { return StatsText; }

    // Death cause chosen by the game mode at the moment of death (the pawn may already be unpossessed later).
    void SetStarved(bool bInStarved);

    // Formats seconds as mm:ss (minutes are not capped at 59).
    static FString FormatSurvivalTime(float Seconds);

protected:
    virtual void NativeConstruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
    void BuildTree();
    void RefreshTexts();

    UFUNCTION() void OnRetry();
    UFUNCTION() void OnContinue();
    UFUNCTION() void OnMainMenu();

    bool bStarved = false;

    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> RetryButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> ContinueButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> MainMenuButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatsText;
};
