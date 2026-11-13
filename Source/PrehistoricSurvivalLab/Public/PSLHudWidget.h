#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSLHudWidget.generated.h"

class UProgressBar;
class UTextBlock;

/**
 * Code-built HUD: bottom-left Health/Stamina bars, sprint state, centered DEAD text.
 * No UMG asset; the WidgetTree is built in Initialize(). Reads the owning player's APSLCharacter every tick.
 */
UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLHudWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // Pushes values into the widgets; bar percent uses APSLHUD::BarFraction (clamped to [0,1]).
    void SetValues(float Health, float MaxHealth, float Stamina, float MaxStamina, bool bSprinting, bool bExhausted, bool bDead);

    UProgressBar* GetHealthBar() const { return HealthBar; }
    UProgressBar* GetStaminaBar() const { return StaminaBar; }
    UTextBlock* GetStateText() const { return StateText; }
    UTextBlock* GetDeadText() const { return DeadText; }

    // Builds the widget tree when none exists; NativeOnInitialized would need a PlayerContext.
    virtual bool Initialize() override;

protected:
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void BuildTree();

    UPROPERTY(Transient)
    TObjectPtr<UProgressBar> HealthBar;

    UPROPERTY(Transient)
    TObjectPtr<UProgressBar> StaminaBar;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> StateText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> DeadText;
};
