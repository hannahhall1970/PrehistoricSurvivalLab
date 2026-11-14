#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSLSpeciesSelectWidget.generated.h"

class UPSLDinoSpecies;
class UPSLUiButton;
class UProgressBar;
class UTextBlock;

/** Species screen: A/D or arrows switch the preview, Confirm starts a new game, Back returns to the main menu. */
UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLSpeciesSelectWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    static constexpr int32 StatCount = 6;

    virtual bool Initialize() override;

    UPSLUiButton* GetPrevButton() const { return PrevButton; }
    UPSLUiButton* GetNextButton() const { return NextButton; }
    UPSLUiButton* GetConfirmButton() const { return ConfirmButton; }
    UPSLUiButton* GetBackButton() const { return BackButton; }
    UTextBlock* GetNameText() const { return NameText; }
    UTextBlock* GetDescriptionText() const { return DescriptionText; }
    UTextBlock* GetDietText() const { return DietText; }
    UProgressBar* GetStatBar(int32 Index) const { return StatBars.IsValidIndex(Index) ? StatBars[Index].Get() : nullptr; }

    FName GetCurrentSpeciesId() const;

    // Cycle the selection (wraps) and refresh the stats and the menu preview actor.
    void SelectNext();
    void SelectPrevious();

protected:
    virtual void NativeConstruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
    void BuildTree();
    void Refresh();

    UFUNCTION() void OnPrev();
    UFUNCTION() void OnNext();
    UFUNCTION() void OnConfirm();

    int32 CurrentIndex = 0;

    UPROPERTY(Transient) TArray<TObjectPtr<UPSLDinoSpecies>> Species;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> PrevButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> NextButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> ConfirmButton;
    UPROPERTY(Transient) TObjectPtr<UPSLUiButton> BackButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DescriptionText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DietText;
    UPROPERTY(Transient) TArray<TObjectPtr<UProgressBar>> StatBars;
};
