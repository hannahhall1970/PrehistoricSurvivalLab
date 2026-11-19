#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PSLPredatorBrain.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPredatorDecisionTest, "PSL.AI.PredatorDecision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLPredatorDecisionTest::RunTest(const FString& Parameters)
{
    using E = EPSLPredatorState;
    const FPSLPredatorTuning Tuning; // 1800 / 2600 / 220
    const float Detect = Tuning.DetectRadius;
    const float Lose = Tuning.LoseRadius;
    const float Range = Tuning.AttackRange;

    auto Decide = [&Tuning](E Current, bool bDead, bool bVisible, float Distance, bool bLastKnown, bool bAttackReady, bool bExpired)
    {
        FPSLPredatorSenses S;
        S.bDead = bDead;
        S.bTargetVisible = bVisible;
        S.TargetDistance = Distance;
        S.bHasLastKnownPosition = bLastKnown;
        S.bAttackReady = bAttackReady;
        S.bStateTimerExpired = bExpired;
        return DecidePredatorState(S, Tuning, Current);
    };

    // Rule 1: dead dominates everything and is terminal.
    TestEqual(TEXT("bDead dominates a close ready target"), Decide(E::Chase, true, true, 10.0f, true, true, true), E::Dead);
    TestEqual(TEXT("bDead from Attack"), Decide(E::Attack, true, true, 10.0f, true, true, true), E::Dead);
    TestEqual(TEXT("Dead is terminal"), Decide(E::Dead, false, true, 10.0f, true, true, true), E::Dead);

    // Rule 2: an attack always ends in Recover, regardless of senses.
    TestEqual(TEXT("Attack -> Recover with target in range"), Decide(E::Attack, false, true, Range, false, true, false), E::Recover);
    TestEqual(TEXT("Attack -> Recover with target gone"), Decide(E::Attack, false, false, 0.0f, false, false, true), E::Recover);

    // Rule 3: Recover holds until the timer expires, then falls through.
    TestEqual(TEXT("Recover holds despite a ready in-range target"), Decide(E::Recover, false, true, Range, true, true, false), E::Recover);
    TestEqual(TEXT("Recover holds with nothing visible"), Decide(E::Recover, false, false, 0.0f, false, false, false), E::Recover);
    TestEqual(TEXT("Expired Recover falls through to Attack"), Decide(E::Recover, false, true, Range, false, true, true), E::Attack);
    TestEqual(TEXT("Expired Recover falls through to Chase"), Decide(E::Recover, false, true, Detect, false, false, true), E::Chase);
    TestEqual(TEXT("Expired Recover with nothing -> Idle"), Decide(E::Recover, false, false, 0.0f, true, false, true), E::Idle);

    // Rule 4: attack range boundary, ready vs not ready.
    TestEqual(TEXT("At AttackRange and ready -> Attack"), Decide(E::Chase, false, true, Range, false, true, false), E::Attack);
    TestEqual(TEXT("At AttackRange, not ready -> Chase"), Decide(E::Chase, false, true, Range, false, false, false), E::Chase);
    TestEqual(TEXT("Inside AttackRange from Idle, ready -> Attack"), Decide(E::Idle, false, true, 50.0f, false, true, false), E::Attack);
    TestEqual(TEXT("Inside AttackRange from Idle, not ready -> Chase"), Decide(E::Idle, false, true, 50.0f, false, false, false), E::Chase);
    TestEqual(TEXT("Just beyond AttackRange is not an attack"), Decide(E::Chase, false, true, Range + 1.0f, false, true, false), E::Chase);
    TestEqual(TEXT("Not visible and ready never attacks"), Decide(E::Chase, false, false, 10.0f, false, true, true), E::Idle);

    // Rule 5: detect/lose hysteresis.
    TestEqual(TEXT("Idle at DetectRadius -> Chase"), Decide(E::Idle, false, true, Detect, false, false, false), E::Chase);
    TestEqual(TEXT("Idle just beyond DetectRadius stays Idle"), Decide(E::Idle, false, true, Detect + 1.0f, false, false, false), E::Idle);
    TestEqual(TEXT("Idle inside hysteresis band does not start a chase"), Decide(E::Idle, false, true, (Detect + Lose) * 0.5f, false, false, false), E::Idle);
    TestEqual(TEXT("Patrol inside hysteresis band does not start a chase"), Decide(E::Patrol, false, true, (Detect + Lose) * 0.5f, false, false, false), E::Patrol);
    TestEqual(TEXT("Investigate inside hysteresis band does not start a chase"), Decide(E::Investigate, false, true, (Detect + Lose) * 0.5f, true, false, false), E::Investigate);
    TestEqual(TEXT("Chase holds just beyond DetectRadius"), Decide(E::Chase, false, true, Detect + 1.0f, false, false, false), E::Chase);
    TestEqual(TEXT("Chase holds inside hysteresis band"), Decide(E::Chase, false, true, (Detect + Lose) * 0.5f, false, false, false), E::Chase);
    TestEqual(TEXT("Chase holds at LoseRadius"), Decide(E::Chase, false, true, Lose, false, false, false), E::Chase);
    TestEqual(TEXT("Chase ends just beyond LoseRadius"), Decide(E::Chase, false, true, Lose + 1.0f, false, false, false), E::Idle);
    TestEqual(TEXT("Chase beyond LoseRadius with last known position -> Investigate"), Decide(E::Chase, false, true, Lose + 1.0f, true, false, false), E::Investigate);

    // Rule 6: Investigate after losing the target.
    TestEqual(TEXT("Chase, target lost, last known -> Investigate"), Decide(E::Chase, false, false, 0.0f, true, false, false), E::Investigate);
    TestEqual(TEXT("Investigate continues while timer runs"), Decide(E::Investigate, false, false, 0.0f, true, false, false), E::Investigate);
    TestEqual(TEXT("Investigate ends when timer expires"), Decide(E::Investigate, false, false, 0.0f, true, false, true), E::Idle);
    TestEqual(TEXT("Chase, target lost, timer expired -> Idle"), Decide(E::Chase, false, false, 0.0f, true, false, true), E::Idle);
    TestEqual(TEXT("Chase, target lost, no last known -> Idle"), Decide(E::Chase, false, false, 0.0f, false, false, false), E::Idle);
    TestEqual(TEXT("Investigate, no last known -> Idle"), Decide(E::Investigate, false, false, 0.0f, false, false, false), E::Idle);
    TestEqual(TEXT("Idle never investigates"), Decide(E::Idle, false, false, 0.0f, true, false, false), E::Idle);
    TestEqual(TEXT("Patrol never investigates"), Decide(E::Patrol, false, false, 0.0f, true, false, false), E::Patrol);

    // Heard noise: investigate its source from calm states; never interrupts a chase, an attack or a recovery.
    auto Heard = [&Tuning](E Current, bool bExpired)
    {
        FPSLPredatorSenses S;
        S.bHeardNoise = true;
        S.bHasLastKnownPosition = true;
        S.bStateTimerExpired = bExpired;
        return DecidePredatorState(S, Tuning, Current);
    };
    TestEqual(TEXT("Idle hears noise -> Investigate"), Heard(E::Idle, false), E::Investigate);
    TestEqual(TEXT("Patrol hears noise -> Investigate"), Heard(E::Patrol, false), E::Investigate);
    TestEqual(TEXT("Investigate hears noise after timer -> keeps investigating"), Heard(E::Investigate, true), E::Investigate);
    TestEqual(TEXT("Recover holds despite noise"), Heard(E::Recover, false), E::Recover);
    TestEqual(TEXT("Attack -> Recover despite noise"), Heard(E::Attack, false), E::Recover);
    TestEqual(TEXT("Chase, target lost, timer expired: noise does not extend the chase"), Heard(E::Chase, true), E::Idle);

    // Rule 7: Idle/Patrol timer cycle.
    TestEqual(TEXT("Idle waits for the timer"), Decide(E::Idle, false, false, 0.0f, false, false, false), E::Idle);
    TestEqual(TEXT("Idle -> Patrol on timer"), Decide(E::Idle, false, false, 0.0f, false, false, true), E::Patrol);
    TestEqual(TEXT("Patrol continues until the timer"), Decide(E::Patrol, false, false, 0.0f, false, false, false), E::Patrol);
    TestEqual(TEXT("Patrol -> Idle on timer"), Decide(E::Patrol, false, false, 0.0f, false, false, true), E::Idle);

    return true;
}

#endif
