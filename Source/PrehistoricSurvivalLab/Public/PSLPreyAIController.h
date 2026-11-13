#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "Engine/TimerHandle.h"
#include "PSLPreyAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;

UENUM(BlueprintType)
enum class EPSLPreyState : uint8
{
    Idle,
    Wander,
    Flee,
    Dead
};

/** Prey brain: sight perception + navmesh MoveToLocation, no Behavior Tree / Blackboard assets. */
UCLASS()
class PREHISTORICSURVIVALLAB_API APSLPreyAIController : public AAIController
{
    GENERATED_BODY()

public:
    APSLPreyAIController();

    /** Threat inside this distance triggers fleeing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prey")
    float FleeRadius;

    /** A fleeing prey keeps fleeing until the threat is farther than this (hysteresis). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prey")
    float SafeRadius;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prey")
    float WanderRadius;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prey")
    float FleeDistance;

    UFUNCTION(BlueprintPure, Category="Prey")
    EPSLPreyState GetPreyState() const { return PreyState; }

    /** The pawn took damage; the attacker becomes the known threat even if unseen. */
    void NotifyDamaged(AActor* Attacker);

    /** Death: stop thinking, stop moving. */
    void StopAI();

    /** Pure state transition used by the think tick. */
    static EPSLPreyState DecidePreyState(bool bDead, bool bHasThreat, float ThreatDistance, float FleeRadius, float SafeRadius, EPSLPreyState Current, bool bIdleExpired);

    virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

protected:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

    void Think();
    void StartWander();
    void StartFlee();
    void EnterState(EPSLPreyState NewState);

    UPROPERTY(VisibleAnywhere, Category="Prey")
    TObjectPtr<UAIPerceptionComponent> PreyPerception;

    UPROPERTY(Transient)
    TObjectPtr<UAISenseConfig_Sight> SightConfig;

    TWeakObjectPtr<AActor> Threat;
    EPSLPreyState PreyState;
    double IdleUntil;
    FTimerHandle ThinkTimer;
};
