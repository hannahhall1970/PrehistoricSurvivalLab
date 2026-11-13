#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PSLPreyAIController.h"
#include "PSLPreyCharacter.h"
#include "PSLStatsComponent.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPreyDecisionTest, "PSL.AI.PreyDecision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLPreyDecisionTest::RunTest(const FString& Parameters)
{
    using E = EPSLPreyState;
    constexpr float Flee = 900.0f;
    constexpr float Safe = 1600.0f;
    auto Decide = [](bool bDead, bool bThreat, float Distance, E Current, bool bIdleExpired)
    {
        return APSLPreyAIController::DecidePreyState(bDead, bThreat, Distance, Flee, Safe, Current, bIdleExpired);
    };

    TestEqual(TEXT("Dead dominates a close threat"), Decide(true, true, 100.0f, E::Flee, true), E::Dead);
    TestEqual(TEXT("Dead is terminal"), Decide(false, true, 100.0f, E::Dead, true), E::Dead);

    TestEqual(TEXT("Threat inside flee radius -> Flee"), Decide(false, true, 500.0f, E::Idle, false), E::Flee);
    TestEqual(TEXT("Threat inside flee radius interrupts Wander"), Decide(false, true, 500.0f, E::Wander, false), E::Flee);
    TestEqual(TEXT("Threat between radii does not start a flee"), Decide(false, true, 1200.0f, E::Idle, false), E::Idle);
    TestEqual(TEXT("Fleeing continues between radii (hysteresis)"), Decide(false, true, 1200.0f, E::Flee, false), E::Flee);
    TestEqual(TEXT("Flee ends beyond safe radius"), Decide(false, true, 1700.0f, E::Flee, false), E::Idle);
    TestEqual(TEXT("Flee ends when the threat is lost"), Decide(false, false, 0.0f, E::Flee, false), E::Idle);

    TestEqual(TEXT("Idle stays idle until the timer expires"), Decide(false, false, 0.0f, E::Idle, false), E::Idle);
    TestEqual(TEXT("Idle -> Wander after the timer"), Decide(false, false, 0.0f, E::Idle, true), E::Wander);
    TestEqual(TEXT("Wander continues until the move completes"), Decide(false, false, 0.0f, E::Wander, true), E::Wander);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLPreyDamageDeathTest, "PSL.Combat.PreyDamageDeath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLPreyDamageDeathTest::RunTest(const FString& Parameters)
{
    const APSLPreyCharacter* Defaults = GetDefault<APSLPreyCharacter>();
    const UPSLStatsComponent* DefaultStats = Defaults->GetStatsComponent();
    if (!TestNotNull(TEXT("Prey CDO has a stats component"), DefaultStats))
    {
        return false;
    }
    TestEqual(TEXT("Prey max health"), DefaultStats->GetMaxHealth(), 60.0f);
    TestEqual(TEXT("Capsule radius"), Defaults->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), 40.0f);
    TestEqual(TEXT("Capsule half height"), Defaults->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), 60.0f);
    TestEqual(TEXT("Wander speed"), Defaults->WanderSpeed, 200.0f);
    TestEqual(TEXT("Flee speed"), Defaults->FleeSpeed, 450.0f);
    TestTrue(TEXT("Flees faster than it wanders"), Defaults->FleeSpeed > Defaults->WanderSpeed);
    TestTrue(TEXT("Uses the prey AI controller"), Defaults->AIControllerClass == APSLPreyAIController::StaticClass());

    // A bite deals 25; three bites must kill a 60 HP prey, exactly once.
    UPSLStatsComponent* Stats = NewObject<UPSLStatsComponent>(GetTransientPackage());
    Stats->MaxHealth = DefaultStats->GetMaxHealth();
    Stats->Health = DefaultStats->GetMaxHealth();

    TestEqual(TEXT("First bite applied"), Stats->ApplyDamage(25.0f), 25.0f);
    TestFalse(TEXT("Alive after one bite"), Stats->IsDead());
    TestEqual(TEXT("Second bite applied"), Stats->ApplyDamage(25.0f), 25.0f);
    TestFalse(TEXT("Alive after two bites"), Stats->IsDead());
    TestEqual(TEXT("Lethal bite is clamped to remaining health"), Stats->ApplyDamage(25.0f), 10.0f);
    TestTrue(TEXT("Dead after three bites"), Stats->IsDead());
    TestEqual(TEXT("Health is zero"), Stats->GetHealth(), 0.0f);
    TestEqual(TEXT("Damage after death is ignored"), Stats->ApplyDamage(25.0f), 0.0f);
    return true;
}

#endif
