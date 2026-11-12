#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSLStatsComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPSLHealthChangedSignature, float, NewHealth, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPSLDeathSignature);

UCLASS(ClassGroup=(Survival), meta=(BlueprintSpawnableComponent))
class PREHISTORICSURVIVALLAB_API UPSLStatsComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSLStatsComponent();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats", meta=(ClampMin="1.0", UIMin="1.0"))
    float MaxHealth;

    UPROPERTY(BlueprintReadOnly, Category="Stats")
    float Health;

    UPROPERTY(BlueprintAssignable, Category="Stats")
    FPSLHealthChangedSignature OnHealthChanged;

    UPROPERTY(BlueprintAssignable, Category="Stats")
    FPSLDeathSignature OnDeath;

    UFUNCTION(BlueprintCallable, Category="Stats")
    float ApplyDamage(float Amount);

    UFUNCTION(BlueprintPure, Category="Stats")
    bool IsDead() const { return bIsDead; }

    UFUNCTION(BlueprintPure, Category="Stats")
    float GetHealth() const { return Health; }

    UFUNCTION(BlueprintPure, Category="Stats")
    float GetMaxHealth() const { return MaxHealth; }

protected:
    virtual void InitializeComponent() override;

private:
    bool bIsDead;
};
