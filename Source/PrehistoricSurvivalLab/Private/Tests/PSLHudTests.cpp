#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetTree.h"
#include "PSLUiStyle.h"
#include "Components/Border.h"
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
    TestEqual(TEXT("Widget tree has 3 progress bars (health, stamina, hunger)"), Bars, 3);

    UProgressBar* Health = Widget->GetHealthBar();
    UProgressBar* Stamina = Widget->GetStaminaBar();
    UProgressBar* Hunger = Widget->GetHungerBar();
    if (!TestNotNull(TEXT("Health bar"), Health) || !TestNotNull(TEXT("Stamina bar"), Stamina) || !TestNotNull(TEXT("Hunger bar"), Hunger)
        || !TestNotNull(TEXT("Hunger value text"), Widget->GetHungerValueText()))
    {
        return false;
    }
    TestTrue(TEXT("Health, stamina and hunger are three distinct bars"), Health != Stamina && Stamina != Hunger && Health != Hunger);

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

    // Hunger is driven separately and must not disturb health/stamina.
    Widget->SetHunger(30.0f, 100.0f, false, FText::GetEmpty());
    TestEqual(TEXT("Hunger percent tracks value"), Hunger->GetPercent(), 0.3f, 1.0e-4f);
    TestEqual(TEXT("Hunger value text"), Widget->GetHungerValueText()->GetText().ToString(), FString(TEXT("30 / 100")));
    TestEqual(TEXT("Health untouched by SetHunger"), Health->GetPercent(), 1.0f, 1.0e-4f);
    TestEqual(TEXT("Stamina untouched by SetHunger"), Stamina->GetPercent(), 0.0f, 1.0e-4f);
    Widget->SetHunger(-5.0f, 100.0f, true, FText::GetEmpty());
    TestEqual(TEXT("Hunger clamps to 0"), Hunger->GetPercent(), 0.0f, 1.0e-4f);
    Widget->SetHunger(250.0f, 100.0f, false, FText::GetEmpty());
    TestEqual(TEXT("Hunger clamps to 1"), Hunger->GetPercent(), 1.0f, 1.0e-4f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHudMessageTest, "PSL.Hud.Message", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLHudMessageTest::RunTest(const FString& Parameters)
{
    UPSLHudWidget* Widget = NewObject<UPSLHudWidget>(GetTransientPackage(), UPSLHudWidget::StaticClass());
    if (!TestNotNull(TEXT("Widget created"), Widget))
    {
        return false;
    }
    Widget->Initialize();
    if (!TestNotNull(TEXT("Message box"), Widget->GetMessageBox()) || !TestNotNull(TEXT("Message text"), Widget->GetMessageText()))
    {
        return false;
    }
    TestEqual(TEXT("Message hidden by default"), Widget->GetMessageBox()->GetVisibility(), ESlateVisibility::Collapsed);
    TestFalse(TEXT("Unknown key is refused"), Widget->ShowMessage(FName(TEXT("NoSuchKey"))));
    TestEqual(TEXT("Unknown key shows nothing"), Widget->GetMessageBox()->GetVisibility(), ESlateVisibility::Collapsed);

    for (const TCHAR* Key : {TEXT("PreyNearby"), TEXT("EatCarcass"), TEXT("PreyEscaped")})
    {
        TestTrue(FString::Printf(TEXT("%s accepted"), Key), Widget->ShowMessage(FName(Key)));
        TestNotEqual(FString::Printf(TEXT("%s visible"), Key), Widget->GetMessageBox()->GetVisibility(), ESlateVisibility::Collapsed);
        TestEqual(FString::Printf(TEXT("%s text"), Key), Widget->GetMessageText()->GetText().ToString(), UPSLHudWidget::MessageForKey(FName(Key)).ToString());
        TestFalse(FString::Printf(TEXT("%s text non-empty"), Key), Widget->GetMessageText()->GetText().IsEmpty());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHudHintBackingTest, "PSL.Hud.HintBacking", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLHudHintBackingTest::RunTest(const FString& Parameters)
{
    UPSLHudWidget* Widget = NewObject<UPSLHudWidget>(GetTransientPackage(), UPSLHudWidget::StaticClass());
    if (!TestNotNull(TEXT("Widget created"), Widget))
    {
        return false;
    }
    Widget->Initialize();
    if (!TestNotNull(TEXT("Hint box"), Widget->GetHintBox()) || !TestNotNull(TEXT("Hint text"), Widget->GetHintText()))
    {
        return false;
    }
    TestEqual(TEXT("Hint strip hidden while empty"), Widget->GetHintBox()->GetVisibility(), ESlateVisibility::Collapsed);
    const FText Bite = NSLOCTEXT("PSL", "HintBiteToEat", "Bite to eat");
    Widget->SetHunger(50.0f, 100.0f, false, Bite);
    TestNotEqual(TEXT("Hint strip shown with a hint"), Widget->GetHintBox()->GetVisibility(), ESlateVisibility::Collapsed);
    TestEqual(TEXT("Hint text kept"), Widget->GetHintText()->GetText().ToString(), Bite.ToString());
    TestFalse(TEXT("Hint text does not auto-wrap to allotted geometry"), Widget->GetHintText()->GetAutoWrapText());
    TestEqual(TEXT("Hint text wraps at the fixed 296 px bound"), Widget->GetHintText()->GetWrapTextAt(), 296.0f);
    TestTrue(TEXT("Hint strip has the umber backing"), Widget->GetHintBox()->GetBrushColor().Equals(FPSLUiStyle::Backing()));
    TestTrue(TEXT("Hint text is parchment"), Widget->GetHintText()->GetColorAndOpacity().GetSpecifiedColor().Equals(FPSLUiStyle::Text()));
    Widget->SetHunger(50.0f, 100.0f, false, FText::GetEmpty());
    TestEqual(TEXT("Hint strip hides when cleared"), Widget->GetHintBox()->GetVisibility(), ESlateVisibility::Collapsed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHudHuntDecisionTest, "PSL.Hud.HuntDecision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLHudHuntDecisionTest::RunTest(const FString& Parameters)
{
    const FName Nearby(TEXT("PreyNearby"));
    const FName Escaped(TEXT("PreyEscaped"));
    TestEqual(TEXT("Rising edge -> PreyNearby"), UPSLHudWidget::DecideHuntMessage(false, true, false, false), Nearby);
    TestEqual(TEXT("Falling edge, hit, no kill -> PreyEscaped"), UPSLHudWidget::DecideHuntMessage(true, false, true, false), Escaped);
    TestEqual(TEXT("Falling edge without hit -> none"), UPSLHudWidget::DecideHuntMessage(true, false, false, false), NAME_None);
    TestEqual(TEXT("Falling edge with kill -> none"), UPSLHudWidget::DecideHuntMessage(true, false, true, true), NAME_None);
    TestEqual(TEXT("Falling edge with kill, no hit -> none"), UPSLHudWidget::DecideHuntMessage(true, false, false, true), NAME_None);
    TestEqual(TEXT("Steady near -> none"), UPSLHudWidget::DecideHuntMessage(true, true, true, false), NAME_None);
    TestEqual(TEXT("Steady far -> none"), UPSLHudWidget::DecideHuntMessage(false, false, true, false), NAME_None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHudPreyNearHysteresisTest, "PSL.Hud.PreyNearHysteresis", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLHudPreyNearHysteresisTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("2400 from far -> near"), UPSLHudWidget::NextPreyNear(false, 2400.0f));
    TestTrue(TEXT("3000 while near stays near"), UPSLHudWidget::NextPreyNear(true, 3000.0f));
    TestFalse(TEXT("3000 from far stays far"), UPSLHudWidget::NextPreyNear(false, 3000.0f));
    TestFalse(TEXT("3600 while near -> far"), UPSLHudWidget::NextPreyNear(true, 3600.0f));
    TestFalse(TEXT("No live prey (Max) from far -> far"), UPSLHudWidget::NextPreyNear(false, TNumericLimits<float>::Max()));
    TestFalse(TEXT("No live prey (Max) while near -> far"), UPSLHudWidget::NextPreyNear(true, TNumericLimits<float>::Max()));
    return true;
}

#endif
