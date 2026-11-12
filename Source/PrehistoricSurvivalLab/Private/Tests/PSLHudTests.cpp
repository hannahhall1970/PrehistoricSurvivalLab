#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PSLHUD.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHudBarFractionTest, "PSL.Hud.BarFraction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLHudBarFractionTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Normal value"), APSLHUD::BarFraction(25.0f, 100.0f), 0.25f);
    TestEqual(TEXT("Negative value clamps to 0"), APSLHUD::BarFraction(-5.0f, 100.0f), 0.0f);
    TestEqual(TEXT("Value above max clamps to 1"), APSLHUD::BarFraction(150.0f, 100.0f), 1.0f);
    TestEqual(TEXT("Zero max returns 0"), APSLHUD::BarFraction(50.0f, 0.0f), 0.0f);
    TestEqual(TEXT("Negative max returns 0"), APSLHUD::BarFraction(50.0f, -100.0f), 0.0f);
    return true;
}

#endif
