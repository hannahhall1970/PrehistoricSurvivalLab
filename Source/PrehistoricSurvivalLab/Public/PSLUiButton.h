#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "PSLUiButton.generated.h"

class UBorder;
class UTextBlock;
class UWidgetTree;

/** Full-width bar button: label plus a 2 px amber underline on hover or keyboard focus, click/hover cues. */
UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLUiButton : public UButton
{
    GENERATED_BODY()

public:
    // Builds the button and its content in Tree. Text must come from LOCTEXT.
    static UPSLUiButton* Make(UWidgetTree* Tree, const FText& Text);

    UTextBlock* GetLabel() const { return Label; }
    void SetLabelText(const FText& Text);

    // Enabled state plus the disabled colour on the label.
    void SetActive(bool bActive);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    UFUNCTION()
    void HandleClicked();

    UFUNCTION()
    void HandleHovered();

    UFUNCTION()
    ESlateVisibility GetUnderlineVisibility();

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> Label;

    UPROPERTY(Transient)
    TObjectPtr<UBorder> Underline;
};
