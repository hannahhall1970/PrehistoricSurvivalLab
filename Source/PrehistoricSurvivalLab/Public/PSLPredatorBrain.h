#pragma once

#include "CoreMinimal.h"
#include "PSLPredatorBrain.generated.h"

UENUM(BlueprintType)
enum class EPSLPredatorState : uint8
{
    Idle,
    Patrol,
    Investigate,
    Chase,
    Attack,
    Recover,
    Dead
};

/** Everything the predator decision reads; filled by the (future) controller each think tick. */
struct FPSLPredatorSenses
{
    bool bDead = false;
    bool bTargetVisible = false;
    float TargetDistance = 0.0f;
    bool bHasLastKnownPosition = false;
    bool bAttackReady = false;
    bool bStateTimerExpired = false;
};

struct FPSLPredatorTuning
{
    /** Visible target inside this distance starts a chase. */
    float DetectRadius = 1800.0f;

    /** A chasing predator keeps chasing until the visible target is farther than this (hysteresis). */
    float LoseRadius = 2600.0f;

    /** Visible target at or inside this distance can be attacked. */
    float AttackRange = 220.0f;
};

/** Pure state transition: reads only its arguments. */
PREHISTORICSURVIVALLAB_API EPSLPredatorState DecidePredatorState(const FPSLPredatorSenses& Senses, const FPSLPredatorTuning& Tuning, EPSLPredatorState Current);
