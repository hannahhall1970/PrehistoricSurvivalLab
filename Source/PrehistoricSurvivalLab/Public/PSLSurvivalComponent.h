#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSLSurvivalComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSLStaminaChangedSignature, float, NewStamina);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPSLSprintStateChangedSignature, bool, bIsSprinting);

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

    UFUNCTION(BlueprintPure, Category="Survival|Stamina")
    float GetStamina() const { return Stamina; }

    UFUNCTION(BlueprintPure, Category="Survival|Stamina")
    float GetMaxStamina() const { return MaxStamina; }

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    bool bSprintRequested;
    bool bSprintLocked;
    bool bIsSprinting;
};
