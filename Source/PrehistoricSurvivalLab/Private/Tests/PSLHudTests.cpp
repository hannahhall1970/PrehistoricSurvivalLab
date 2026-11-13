#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "PSLHudWidget.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHudWidgetTest, "PSL.Hud.Widget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLHudWidgetTest::RunTest(const FString& Parameters)
{
    UPSLHudWidget* Widget = NewObject<UPSLHudWidget>(GetTransientPackage(), UPSLHudWidget::StaticClass());
    if (!TestNotNull(TEXT("Widget created"), Widget))
    {
        return false;
    }
    Widget->Initialize();
    if (!TestNotNull(TEXT("WidgetTree built"), Widget->WidgetTree.Get()))
    {
        return false;
    }

    TArray<UWidget*> All;
    Widget->WidgetTree->GetAllWidgets(All);
    int32 Bars = 0;
    for (const UWidget* Child : All)
    {
        Bars += Child->IsA<UProgressBar>() ? 1 : 0;
    }
    TestEqual(TEXT("Widget tree has 2 progress bars"), Bars, 2);

    UProgressBar* Health = Widget->GetHealthBar();
    UProgressBar* Stamina = Widget->GetStaminaBar();
    if (!TestNotNull(TEXT("Health bar"), Health) || !TestNotNull(TEXT("Stamina bar"), Stamina))
    {
        return false;
    }

    Widget->SetValues(25.0f, 100.0f, 50.0f, 100.0f, false, false, false);
    TestEqual(TEXT("Health percent tracks value"), Health->GetPercent(), 0.25f, 1.0e-4f);
    TestEqual(TEXT("Stamina percent tracks value"), Stamina->GetPercent(), 0.5f, 1.0e-4f);

    Widget->SetValues(87.0f, 100.0f, 40.4f, 100.0f, false, false, false);
    TestEqual(TEXT("Health value text"), Widget->GetHealthValueText()->GetText().ToString(), FString(TEXT("87 / 100")));
    TestEqual(TEXT("Stamina value text rounds"), Widget->GetStaminaValueText()->GetText().ToString(), FString(TEXT("40 / 100")));
    TestEqual(TEXT("Kill banner hidden by default"), Widget->GetKillBannerText()->GetVisibility(), ESlateVisibility::Collapsed);
    TestEqual(TEXT("Hit marker hidden by default"), Widget->GetHitMarkerText()->GetVisibility(), ESlateVisibility::Collapsed);
    TestEqual(TEXT("DEAD hidden while alive"), Widget->GetDeadText()->GetVisibility(), ESlateVisibility::Collapsed);

    Widget->SetValues(150.0f, 100.0f, -5.0f, 100.0f, true, true, true);
    TestEqual(TEXT("Health clamps to 1"), Health->GetPercent(), 1.0f, 1.0e-4f);
    TestEqual(TEXT("Stamina clamps to 0"), Stamina->GetPercent(), 0.0f, 1.0e-4f);
    TestNotEqual(TEXT("DEAD shown when dead"), Widget->GetDeadText()->GetVisibility(), ESlateVisibility::Collapsed);
    return true;
}

#endif
