#include "PSLPredatorBrain.h"

EPSLPredatorState DecidePredatorState(const FPSLPredatorSenses& Senses, const FPSLPredatorTuning& Tuning, EPSLPredatorState Current)
{
    using E = EPSLPredatorState;

    if (Senses.bDead || Current == E::Dead)
    {
        return E::Dead;
    }

    // An attack holds through its wind-up and strike, then always ends in a recovery window (a fair opening).
    if (Current == E::Attack)
    {
        return Senses.bStateTimerExpired ? E::Recover : E::Attack;
    }

    if (Current == E::Recover && !Senses.bStateTimerExpired)
    {
        return E::Recover;
    }

    if (Senses.bTargetVisible)
    {
        if (Senses.TargetDistance <= Tuning.AttackRange)
        {
            return Senses.bAttackReady ? E::Attack : E::Chase;
        }

        // Hysteresis: start chasing inside DetectRadius, keep chasing until beyond LoseRadius.
        const float Limit = (Current == E::Chase) ? Tuning.LoseRadius : Tuning.DetectRadius;
        if (Senses.TargetDistance <= Limit)
        {
            return E::Chase;
        }
    }

    // A noise sends the predator to check its source; a running chase is handled by the rule below.
    if (Senses.bHeardNoise && Senses.bHasLastKnownPosition && Current != E::Chase)
    {
        return E::Investigate;
    }

    if ((Current == E::Chase || Current == E::Investigate) && Senses.bHasLastKnownPosition && !Senses.bStateTimerExpired)
    {
        return E::Investigate;
    }

    switch (Current)
    {
    case E::Idle:
        return Senses.bStateTimerExpired ? E::Patrol : E::Idle;
    case E::Patrol:
        return Senses.bStateTimerExpired ? E::Idle : E::Patrol;
    default:
        return E::Idle;
    }
}
