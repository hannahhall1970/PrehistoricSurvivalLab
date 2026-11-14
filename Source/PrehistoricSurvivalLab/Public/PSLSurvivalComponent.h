#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSLSurvivalComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSLStaminaChangedSignature, float, NewStamina);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSLSprintStateChangedSignature, bool, bIsSprinting);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSLHungerChangedSignature, float, NewHunger);

UCLASS(ClassGroup=(Survival), meta=(BlueprintSpawnableComponent))
class PREHISTORICSURVIVALLAB_API UPSLSurvivalComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSLSurvivalComponent();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Stamina", meta=(ClampMin="1.0", UIMin="1.0"))
    float MaxStamina;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Stamina", meta=(ClampMin="0.0", UIMin="0.0"))
    float SprintDrainPerSecond;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Stamina", meta=(ClampMin="0.0", UIMin="0.0"))
    float StaminaRegenPerSecond;

    UPROPERTY(BlueprintReadOnly, Category="Survival|Stamina")
    float Stamina;

    UPROPERTY(BlueprintAssignable, Category="Survival|Stamina")
    FPSLStaminaChangedSignature OnStaminaChanged;

    UPROPERTY(BlueprintAssignable, Category="Survival|Stamina")
    FPSLSprintStateChangedSignature OnSprintStateChanged;

    UFUNCTION(BlueprintCallable, Category="Survival|Stamina")
    void SetSprintRequested(bool bRequested);

    UFUNCTION(BlueprintCallable, Category="Survival|Stamina")
    void UpdateStamina(float DeltaSeconds, bool bIsMoving);

    UFUNCTION(BlueprintPure, Category="Survival|Stamina")
    bool IsSprinting() const { return bIsSprinting; }

    // True from stamina hitting 0 until sprint is released; HUD uses it for the exhausted tint.
    UFUNCTION(BlueprintPure, Category="Survival|Stamina")
    bool IsSprintLocked() const { return bSprintLocked; }

    UFUNCTION(BlueprintPure, Category="Survival|Stamina")
    float GetStamina() const { return Stamina; }

    UFUNCTION(BlueprintPure, Category="Survival|Stamina")
    float GetMaxStamina() const { return MaxStamina; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Survival|Hunger", meta=(ClampMin="1.0", UIMin="1.0"))
    float MaxHunger;

    UPROPERTY(BlueprintReadOnly, Category="Survival|Hunger")
    float Hunger;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Survival|Hunger", meta=(ClampMin="0.0", UIMin="0.0"))
    float HungerDrainPerSecond;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Survival|Hunger", meta=(ClampMin="0.0", UIMin="0.0"))
    float StarveDamagePerSecond;

    UPROPERTY(BlueprintAssignable, Category="Survival|Hunger")
    FPSLHungerChangedSignature OnHungerChanged;

    // Drains hunger (2x while sprinting) and, at 0, damages the owner's stats component. No-op once the owner is dead.
    UFUNCTION(BlueprintCallable, Category="Survival|Hunger")
    void UpdateHunger(float DeltaSeconds);

    // Adds Amount (clamped to [0, MaxHunger]); non-positive amounts are ignored.
    UFUNCTION(BlueprintCallable, Category="Survival|Hunger")
    void AddFood(float Amount);

    UFUNCTION(BlueprintPure, Category="Survival|Hunger")
    bool IsStarving() const { return Hunger <= 0.0f; }

    UFUNCTION(BlueprintPure, Category="Survival|Hunger")
    float GetHunger() const { return Hunger; }

    UFUNCTION(BlueprintPure, Category="Survival|Hunger")
    float GetMaxHunger() const { return MaxHunger; }

    // Restores saved stamina/hunger (clamped to the max values) and keeps ResetVitalsForPlay from overwriting them.
    void RestoreState(float InStamina, float InHunger);

    // BeginPlay hook: fills stamina/hunger to their maxima unless RestoreState ran first.
    void ResetVitalsForPlay();

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    bool bSprintRequested;
    bool bSprintLocked;
    bool bIsSprinting;

public:
    // True once starvation has damaged the owner; the Game Over screen uses it for the death cause.
    bool DiedOfStarvation() const { return bStarvationDamageDealt; }

private:
    bool bStarvationDamageDealt = false;
    bool bStateRestored = false;
};
