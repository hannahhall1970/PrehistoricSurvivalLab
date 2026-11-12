#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"
#include "PSLStatsComponent.h"
#include "PSLSurvivalComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLStatsInitialValuesTest, "PSL.Stats.InitialValues", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLStatsInitialValuesTest::RunTest(const FString& Parameters)
{
    UPSLStatsComponent* Stats = NewObject<UPSLStatsComponent>();
    TestEqual(TEXT("Default max health"), Stats->GetMaxHealth(), 100.0f);
    TestEqual(TEXT("Default health"), Stats->GetHealth(), 100.0f);
    TestFalse(TEXT("Initial state is alive"), Stats->IsDead());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSurvivalStaminaTest, "PSL.Survival.Stamina", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSurvivalStaminaTest::RunTest(const FString& Parameters)
{
    UPSLSurvivalComponent* Survival = NewObject<UPSLSurvivalComponent>();
    TestEqual(TEXT("Default stamina"), Survival->GetStamina(), 100.0f);

    Survival->SetSprintRequested(true);
    Survival->UpdateStamina(1.0f, true);
    TestTrue(TEXT("Sprint starts while moving"), Survival->IsSprinting());
    TestEqual(TEXT("One second sprint drain"), Survival->GetStamina(), 80.0f);

    Survival->UpdateStamina(1.0f, false);
    TestFalse(TEXT("Sprint stops while stationary"), Survival->IsSprinting());
    TestEqual(TEXT("One second regeneration"), Survival->GetStamina(), 92.0f);

    Survival->UpdateStamina(10.0f, false);
    TestEqual(TEXT("Stamina clamps at max"), Survival->GetStamina(), Survival->GetMaxStamina());

    Survival->SetSprintRequested(false);
    Survival->UpdateStamina(10.0f, true);
    TestEqual(TEXT("Stationary release keeps full stamina"), Survival->GetStamina(), Survival->GetMaxStamina());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSurvivalZeroStaminaTest, "PSL.Survival.ZeroStaminaRequiresRelease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSurvivalZeroStaminaTest::RunTest(const FString& Parameters)
{
    UPSLSurvivalComponent* Survival = NewObject<UPSLSurvivalComponent>();
    Survival->SetSprintRequested(true);
    Survival->UpdateStamina(5.0f, true);
    TestFalse(TEXT("Zero stamina stops sprint"), Survival->IsSprinting());
    TestEqual(TEXT("Stamina reaches zero"), Survival->GetStamina(), 0.0f);
    TestTrue(TEXT("Empty stamina locks sprint"), Survival->IsSprintLocked());

    Survival->UpdateStamina(1.0f, true);
    TestFalse(TEXT("Held request remains locked"), Survival->IsSprinting());
    TestEqual(TEXT("Locked sprint regenerates stamina"), Survival->GetStamina(), 12.0f);

    Survival->SetSprintRequested(false);
    Survival->SetSprintRequested(true);
    Survival->UpdateStamina(0.1f, true);
    TestTrue(TEXT("Releasing and pressing sprint unlocks it"), Survival->IsSprinting());
    TestFalse(TEXT("Release clears the lock"), Survival->IsSprintLocked());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLStatsDamageTest, "PSL.Stats.DamageAndDeath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLStatsDamageTest::RunTest(const FString& Parameters)
{
    UPSLStatsComponent* Stats = NewObject<UPSLStatsComponent>();
    TestEqual(TEXT("Positive damage returns applied amount"), Stats->ApplyDamage(25.0f), 25.0f);
    TestEqual(TEXT("Health decreases by applied damage"), Stats->GetHealth(), 75.0f);
    TestEqual(TEXT("Overkill returns remaining health"), Stats->ApplyDamage(100.0f), 75.0f);
    TestEqual(TEXT("Health clamps at zero"), Stats->GetHealth(), 0.0f);
    TestTrue(TEXT("Lethal damage marks dead"), Stats->IsDead());
    TestEqual(TEXT("Damage after death is ignored"), Stats->ApplyDamage(10.0f), 0.0f);
    TestEqual(TEXT("Zero damage is ignored"), Stats->ApplyDamage(0.0f), 0.0f);
    TestEqual(TEXT("Negative damage is ignored"), Stats->ApplyDamage(-10.0f), 0.0f);
    return true;
}

#endif
