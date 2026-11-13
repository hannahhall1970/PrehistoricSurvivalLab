#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Engine/TimerHandle.h"
#include "PSLPreyCharacter.generated.h"

class UPSLStatsComponent;
class UStaticMeshComponent;

/** Small prey animal: wanders, flees from the player dino, dies through UPSLStatsComponent. */
UCLASS()
class PREHISTORICSURVIVALLAB_API APSLPreyCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    APSLPreyCharacter();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prey")
    float WanderSpeed;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prey")
    float FleeSpeed;

    /** Seconds the prey is stunned after a hit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prey")
    float StaggerDuration;

    UPROPERTY(BlueprintReadOnly, Category="Prey")
    int32 HitCount;

    UFUNCTION(BlueprintPure, Category="Prey")
    UPSLStatsComponent* GetStatsComponent() const { return StatsComponent; }

    UFUNCTION(BlueprintPure, Category="Prey")
    bool IsStaggered() const { return bStaggered; }

    virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleDeath();

    void EndStagger();

    UPROPERTY(VisibleAnywhere, Category="Components")
    TObjectPtr<UPSLStatsComponent> StatsComponent;

    UPROPERTY(VisibleAnywhere, Category="Visual")
    TObjectPtr<UStaticMeshComponent> BodyMesh;

    UPROPERTY(VisibleAnywhere, Category="Visual")
    TObjectPtr<UStaticMeshComponent> HeadMesh;

    FTimerHandle StaggerTimer;
    bool bStaggered;
    bool bDeathHandled;
};
