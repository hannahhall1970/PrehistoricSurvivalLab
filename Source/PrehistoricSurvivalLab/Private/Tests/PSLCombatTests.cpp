#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PSLCharacter.h"
#include "PSLDinoAnimInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLBiteCooldownTest, "PSL.Combat.BiteCooldown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLBiteCooldownTest::RunTest(const FString& Parameters)
{
    const double Never = TNumericLimits<double>::Lowest() / 2.0; // matches APSLCharacter::LastBiteTime's initial value
    TestTrue(TEXT("First bite is allowed"), APSLCharacter::CanBite(0.0, Never, 1.0, false));
    TestFalse(TEXT("Refused inside the cooldown"), APSLCharacter::CanBite(10.5, 10.0, 1.0, false));
    TestFalse(TEXT("Refused just before the cooldown ends"), APSLCharacter::CanBite(10.99, 10.0, 1.0, false));
    TestTrue(TEXT("Allowed exactly when the cooldown ends"), APSLCharacter::CanBite(11.0, 10.0, 1.0, false));
    TestTrue(TEXT("Allowed after the cooldown"), APSLCharacter::CanBite(20.0, 10.0, 1.0, false));
    TestFalse(TEXT("Dead never bites"), APSLCharacter::CanBite(20.0, 10.0, 1.0, true));
    TestFalse(TEXT("Dead never bites even on the first bite"), APSLCharacter::CanBite(0.0, Never, 1.0, true));

    const APSLCharacter* Defaults = GetDefault<APSLCharacter>();
    TestEqual(TEXT("Cooldown default"), Defaults->BiteCooldown, 1.0f);
    TestEqual(TEXT("Hit delay default"), Defaults->BiteHitDelay, 0.35f);
    TestEqual(TEXT("Reach default"), Defaults->BiteReach, 220.0f);
    TestEqual(TEXT("Radius default"), Defaults->BiteRadius, 120.0f);
    TestEqual(TEXT("Damage default"), Defaults->BiteDamage, 25.0f);

    constexpr float Tol = KINDA_SMALL_NUMBER;
    constexpr float Clip = 1.167f;
    TestEqual(TEXT("Action weight 0 at start"), UPSLDinoAnimInstance::ComputeActionWeight(0.0f, Clip, false), 0.0f, Tol);
    TestEqual(TEXT("Action weight 1 mid clip"), UPSLDinoAnimInstance::ComputeActionWeight(0.5f, Clip, false), 1.0f, Tol);
    TestEqual(TEXT("Action weight 0 at clip end"), UPSLDinoAnimInstance::ComputeActionWeight(Clip, Clip, false), 0.0f, Tol);
    TestEqual(TEXT("Held action stays at 1 past the clip"), UPSLDinoAnimInstance::ComputeActionWeight(5.0f, Clip, true), 1.0f, Tol);
    return true;
}

#endif
