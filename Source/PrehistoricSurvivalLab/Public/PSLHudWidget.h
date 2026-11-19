#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PSLCharacter.h"
#include "PSLDamageFeedback.h"
#include "PSLHudWidget.generated.h"

class UProgressBar;
class UTextBlock;

/**
 * Code-built HUD: bottom-left rows `[icon] LABEL ... value / max` for Health/Stamina, sprint state, centered DEAD text,
 * top-centre PREY DOWN kill banner and a HIT marker by the crosshair.
 * No UMG asset; the WidgetTree is built in Initialize(). Reads the owning player's APSLCharacter every tick.
 */
UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLHudWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // Pushes values into the widgets; bar percent uses APSLHUD::BarFraction (clamped to [0,1]).
    void SetValues(float Health, float MaxHealth, float Stamina, float MaxStamina, bool bSprinting, bool bExhausted, bool bDead);

    // Hunger row and diet hint. Starving flashes the bar rust; Hint is the localized food hint (may be empty).
    void SetHunger(float Hunger, float MaxHunger, bool bStarving, const FText& Hint);

    UProgressBar* GetHungerBar() const { return HungerBar; }
    UTextBlock* GetHungerValueText() const { return HungerValueText; }
    UTextBlock* GetHintText() const { return HintText; }

    UProgressBar* GetHealthBar() const { return HealthBar; }
    UProgressBar* GetGhostBar() const { return GhostBar; }
    UProgressBar* GetStaminaBar() const { return StaminaBar; }
    UTextBlock* GetStateText() const { return StateText; }
    UTextBlock* GetDeadText() const { return DeadText; }
    UTextBlock* GetHealthValueText() const { return HealthValueText; }
    UTextBlock* GetStaminaValueText() const { return StaminaValueText; }
    UTextBlock* GetKillBannerText() const { return KillBannerText; }
    UTextBlock* GetHitMarkerText() const { return HitMarkerText; }

    // Builds the widget tree when none exists; NativeOnInitialized would need a PlayerContext.
    virtual bool Initialize() override;

    // Re-sets every static label (HEALTH, DEAD, PREY DOWN, HIT...) and replays the last value/state texts (culture change).
    void RefreshTexts();

protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual void NativeDestruct() override;


    // Banner/marker display times (seconds).
    static constexpr float KillBannerSeconds = 2.5f;
    static constexpr float HitMarkerSeconds = 0.4f;
private:
    void BuildTree();
    void HandlePreyKilled(class APSLPreyCharacter* Prey, AActor* Killer);
    void HandleCultureChanged();

    UFUNCTION()
    void HandleBiteHit();

    // Sole source of damage feedback (bar flash, ghost trail). Heal, starvation and SetHealth restore never reach it.
    UFUNCTION()
    void HandleDamageTaken(float Amount, float NewHealth, EPSLDamageKind Kind, FVector CauserLocation, bool bHasCauser);

    // Per-tick: ages the flash, steps the ghost toward the real health fraction and sizes the ghost segment.
    void StepDamageFeedback(float RealFraction, bool bDead, float DeltaSeconds);
    FDelegateHandle CultureHandle;

    // Last pushed values, replayed by RefreshTexts so value/state texts follow a culture change without waiting for a tick.
    bool bHaveValues = false;
    float LastHealth = 0.0f, LastMaxHealth = 0.0f, LastStamina = 0.0f, LastMaxStamina = 0.0f;
    bool bLastSprinting = false, bLastExhausted = false, bLastDead = false;
    bool bHaveHunger = false;
    float LastHunger = 0.0f, LastMaxHunger = 0.0f;

    // Caption and icon-glyph labels in Health, Stamina, Hunger order.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> CaptionTexts;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> GlyphTexts;

    FDelegateHandle PreyKilledHandle;
    TWeakObjectPtr<class APSLCharacter> BoundCharacter;
    float KillBannerRemaining = 0.0f;
    float HitMarkerRemaining = 0.0f;
    bool bStarvingFlash = false;
    float FlashTime = 0.0f;

    // Hit feedback state. HudTime is accumulated tick time (no wall clock) so the 3 Hz flash cap is deterministic.
    PSLDamageFeedback::FGhostBar Ghost;
    float FlashRemaining = 0.0f;
    double HudTime = 0.0;
    double LastFlashTime = -1.0e9;

    // Thin strip under the health bar that trails the real value after a hit; hidden while idle.
    UPROPERTY(Transient)
    TObjectPtr<UProgressBar> GhostBar;

    UPROPERTY(Transient)
    TObjectPtr<UProgressBar> HungerBar;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> HungerValueText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> HintText;

    UPROPERTY(Transient)
    TObjectPtr<UProgressBar> HealthBar;

    UPROPERTY(Transient)
    TObjectPtr<UProgressBar> StaminaBar;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> StateText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> DeadText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> HealthValueText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> StaminaValueText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> KillBannerText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> HitMarkerText;
};
