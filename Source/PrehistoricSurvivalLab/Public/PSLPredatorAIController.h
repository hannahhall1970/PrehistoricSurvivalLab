#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "Engine/TimerHandle.h"
#include "PSLPredatorBrain.h"
#include "PSLPredatorAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;

/** One recorded state change (see APSLPredatorAIController::GetTransitions). */
struct FPSLPredatorTransition
{
    double WorldTime = 0.0;
    EPSLPredatorState From = EPSLPredatorState::Idle;
    EPSLPredatorState To = EPSLPredatorState::Idle;
    FString Reason;
};

/**
 * Predator brain: sight + hearing perception, navmesh moves, no Behavior Tree / Blackboard.
 * The target position is learned only from perception stimuli; every state change goes through DecidePredatorState.
 */
UCLASS()
class PREHISTORICSURVIVALLAB_API APSLPredatorAIController : public AAIController
{
    GENERATED_BODY()

public:
    APSLPredatorAIController();

    /** Seconds spent in each timed state; editable so tests can shorten them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Predator")
    float IdleSeconds = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Predator")
    float PatrolSeconds = 10.0f;

    /** Search time at the last known position before giving up. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Predator")
    float InvestigateSeconds = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Predator")
    float RecoverSeconds = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Predator")
    float PatrolRadius = 1500.0f;

    /** Telegraph: seconds the predator turns toward the target and flashes before the strike direction freezes (balance hypothesis, spec 002 R2). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Predator|Attack")
    float AttackWindupSeconds = 1.0f;

    /** Seconds from the frozen strike direction to the impact. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Predator|Attack")
    float AttackStrikeSeconds = 0.25f;

    /** Half-angle of the strike cone around the frozen direction. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Predator|Attack")
    float AttackConeDegrees = 45.0f;

    /** Yaw turn rate toward the target while winding up. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Predator|Attack")
    float AttackTurnRateDegPerSec = 180.0f;

    EPSLPredatorState GetPredatorState() const { return PredatorState; }
    bool HasLastKnownPosition() const { return bHasLastKnown; }
    FVector GetLastKnownPosition() const { return LastKnownPosition; }

    /** Most recent transitions, oldest first, capped at MaxTransitions. */
    const TArray<FPSLPredatorTransition>& GetTransitions() const { return Transitions; }

    static constexpr int32 MaxTransitions = 64;

    /** Death: stop thinking, stop moving, drop perception. */
    void StopAI();

    /** True from entering Attack until the strike direction is frozen; the pawn turns toward the target meanwhile. */
    bool IsAttackWindingUp() const { return bAttackWindingUp; }

    /** True from the end of the wind-up until Attack ends; the strike direction is frozen. */
    bool IsStrikeCommitted() const { return bStrikeCommitted; }

    /** 2D unit direction the strike was committed to; valid while IsStrikeCommitted(). */
    FVector GetFrozenStrikeDirection() const { return FrozenStrikeDir; }

    /** Strikes that dealt damage / ended without damage (out_of_reach or out_of_cone). */
    int32 GetHitCount() const { return HitCount; }
    int32 GetMissCount() const { return MissCount; }

    /** Reason of the most recent miss ("out_of_reach" / "out_of_cone"); nullptr before the first miss. Static literal. */
    const TCHAR* GetLastMissReason() const { return LastMissReason; }

protected:
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

    void Think();
    void EnterState(EPSLPredatorState NewState, const TCHAR* Reason, float Distance, bool bVisible);
    void StartPatrolMove();
    void ResolveAttackHit();
    void CommitStrike();
    void ClearAttackTimers();
    float StateDuration(EPSLPredatorState State) const;

    UPROPERTY(VisibleAnywhere, Category="Predator")
    TObjectPtr<UAIPerceptionComponent> PredatorPerception;

    UPROPERTY(Transient)
    TObjectPtr<UAISenseConfig_Sight> SightConfig;

    UPROPERTY(Transient)
    TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

    TWeakObjectPtr<AActor> Target;
    FVector LastKnownPosition = FVector::ZeroVector;
    bool bHasLastKnown = false;
    bool bNoisePending = false;

    FVector HomeLocation = FVector::ZeroVector;
    EPSLPredatorState PredatorState = EPSLPredatorState::Idle;
    double StateEnteredAt = 0.0;
    double StateEndsAt = 0.0;
    double NextAttackAt = 0.0;

    TArray<FPSLPredatorTransition> Transitions;

    FTimerHandle ThinkTimer;
    FTimerHandle AttackHitTimer;
    FTimerHandle AttackAnimTimer;

    bool bAttackWindingUp = false;
    bool bStrikeCommitted = false;
    bool bClipLengthLogged = false;
    double WindupEndsAt = 0.0;
    FVector FrozenStrikeDir = FVector::ForwardVector;
    int32 HitCount = 0;
    int32 MissCount = 0;
    const TCHAR* LastMissReason = nullptr;
};
