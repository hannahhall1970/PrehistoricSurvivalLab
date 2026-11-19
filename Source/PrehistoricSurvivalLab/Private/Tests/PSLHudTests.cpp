#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "PSLHudWidget.h"
#include "PSLHUD.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "PSLCharacter.h"
#include "PSLDamageFeedback.h"
#include "PSLStatsComponent.h"
#include "PSLVitalsListener.h"

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
    TestEqual(TEXT("Widget tree has 4 progress bars (health, ghost, stamina, hunger)"), Bars, 4);
    if (TestNotNull(TEXT("Ghost bar"), Widget->GetGhostBar()))
    {
        TestEqual(TEXT("Ghost bar hidden after Initialize"), Widget->GetGhostBar()->GetVisibility(), ESlateVisibility::Hidden);
    }

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLHudDamageFeedbackTest, "PSL.Hud.DamageFeedback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLHudDamageFeedbackTest::RunTest(const FString& Parameters)
{
    using namespace PSLDamageFeedback;

    // Severity clamps to [0,1] and rejects non-damage / bad max.
    TestEqual(TEXT("20/100 severity"), Severity(20.0f, 100.0f), 0.2f, 1.0e-4f);
    TestEqual(TEXT("12/70 severity"), Severity(12.0f, 70.0f), 12.0f / 70.0f, 1.0e-4f);
    TestEqual(TEXT("Overkill clamps to 1"), Severity(150.0f, 100.0f), 1.0f);
    TestEqual(TEXT("Negative amount is 0"), Severity(-5.0f, 100.0f), 0.0f);
    TestEqual(TEXT("Zero max is 0"), Severity(10.0f, 0.0f), 0.0f);

    // Bearing within +-15 deg of the true relative bearing at 8 compass angles, for two pawn yaws.
    for (const float PawnYaw : { 0.0f, 30.0f })
    {
        for (int32 Step = 0; Step < 8; ++Step)
        {
            const float AngleDeg = 45.0f * Step;
            const FVector Causer(500.0f * FMath::Cos(FMath::DegreesToRadians(AngleDeg)), 500.0f * FMath::Sin(FMath::DegreesToRadians(AngleDeg)), 0.0f);
            const float Expected = FMath::UnwindDegrees(AngleDeg - PawnYaw);
            const float Bearing = BearingDegrees(FVector::ZeroVector, PawnYaw, Causer);
            TestTrue(*FString::Printf(TEXT("Bearing yaw %.0f angle %.0f: got %.1f want %.1f"), PawnYaw, AngleDeg, Bearing, Expected),
                FMath::Abs(FMath::FindDeltaAngleDegrees(Expected, Bearing)) <= 15.0f);
        }
    }
    TestEqual(TEXT("Causer to the right is +90"), BearingDegrees(FVector::ZeroVector, 0.0f, FVector(0.0f, 100.0f, 0.0f)), 90.0f, 1.0e-3f);
    TestEqual(TEXT("Coincident points give 0"), BearingDegrees(FVector(5.0f, 5.0f, 0.0f), 40.0f, FVector(5.0f, 5.0f, 90.0f)), 0.0f);

    // Ghost: holds 0.4 s, drains over 0.5 s, a new hit mid-drain restarts the hold from the current ghost value.
    FGhostBar Ghost;
    GhostOnHit(Ghost, 0.8f);
    GhostStep(Ghost, 0.8f, 0.39f);
    TestEqual(TEXT("Ghost still holds at 0.39 s"), Ghost.Fraction, 1.0f);
    GhostStep(Ghost, 0.8f, 0.26f);
    TestEqual(TEXT("Ghost halfway through the drain"), Ghost.Fraction, 0.9f, 1.0e-4f);
    GhostOnHit(Ghost, 0.6f);
    TestEqual(TEXT("Second hit keeps the current ghost value"), Ghost.Fraction, 0.9f, 1.0e-4f);
    GhostStep(Ghost, 0.6f, 0.39f);
    TestEqual(TEXT("Second hit restarted the hold"), Ghost.Fraction, 0.9f, 1.0e-4f);
    GhostStep(Ghost, 0.6f, 10.0f);
    TestEqual(TEXT("Ghost reaches the new value and stops"), Ghost.Fraction, 0.6f);
    TestEqual(TEXT("Ghost idle after drain"), Ghost.DrainPerSecond, 0.0f);
    FGhostBar Healed{0.5f, 0.2f, 1.0f};
    GhostStep(Healed, 1.0f, 0.016f);
    TestEqual(TEXT("Heal snaps the ghost, no trail"), Healed.Fraction, 1.0f);
    FGhostBar Starved;
    GhostStep(Starved, 0.9f, 0.1f);
    TestEqual(TEXT("A drop without a hit (starvation, restore) has no trail"), Starved.Fraction, 0.9f);
    TestTrue(TEXT("Flash fits inside the 3 Hz interval"), FlashSeconds <= MinFlashIntervalSeconds);
    TestFalse(TEXT("Second flash within 0.3 s is gated"), ShouldFlash(0.3, 0.0));
    TestTrue(TEXT("Flash allowed after 0.34 s"), ShouldFlash(0.34, 0.0));

    // Low-health hysteresis 25 / 35 %.
    TestTrue(TEXT("Enters at 24 %"), NextLowHealth(false, 24.0f, 100.0f, false));
    TestTrue(TEXT("Enters at exactly 25 %"), NextLowHealth(false, 25.0f, 100.0f, false));
    TestFalse(TEXT("Does not enter at 26 %"), NextLowHealth(false, 26.0f, 100.0f, false));
    TestFalse(TEXT("Does not enter at 36 %"), NextLowHealth(false, 36.0f, 100.0f, false));
    TestTrue(TEXT("Stays low at 34 %"), NextLowHealth(true, 34.0f, 100.0f, false));
    TestFalse(TEXT("Exits above 35 %"), NextLowHealth(true, 36.0f, 100.0f, false));
    TestFalse(TEXT("Never low when dead"), NextLowHealth(true, 0.0f, 100.0f, true));
    TestFalse(TEXT("Zero max is not low"), NextLowHealth(false, 0.0f, 0.0f, false));

    // Hit-stop gate: severity >= 0.15 and at most once per 0.5 s.
    TestTrue(TEXT("20/100 bite qualifies"), ShouldHitStop(0.2f, 10.0, -1.0e9));
    TestFalse(TEXT("10/100 scratch does not"), ShouldHitStop(0.1f, 10.0, -1.0e9));
    TestFalse(TEXT("Rate limited inside 0.5 s"), ShouldHitStop(0.2f, 10.3, 10.0));
    TestTrue(TEXT("Allowed again after 0.5 s"), ShouldHitStop(0.2f, 10.5, 10.0));

    // OnDamageTaken: only TakeDamage raises it. Starvation (UPSLStatsComponent::ApplyDamage), heal and SetHealth restore must not.
    // The live predator bite path is covered by PSL.PIE.* (T013), not here.
    if (!TestNotNull(TEXT("GEngine available"), GEngine))
    {
        return false;
    }
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);
    // APawn::ShouldTakeDamage rejects all damage without an authority GameMode (Pawn.cpp:569). Use the plain engine
    // GameModeBase, not PSLGameMode, whose BeginPlay spawns predators. SetGameMode needs a GameInstance (World.cpp:5931).
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL GameUrl;
    GameUrl.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    TestTrue(TEXT("Test world has an authority GameMode"), World->SetGameMode(GameUrl));
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APSLCharacter* Player = World->SpawnActor<APSLCharacter>(APSLCharacter::StaticClass(), FTransform::Identity, SpawnParams);
    APSLCharacter* Causer = World->SpawnActor<APSLCharacter>(APSLCharacter::StaticClass(), FTransform(FVector(0.0f, 300.0f, 0.0f)), SpawnParams);
    for (APSLCharacter* Actor : { Player, Causer })
    {
        if (Actor && !Actor->HasActorBegunPlay())
        {
            Actor->DispatchBeginPlay();
        }
    }

    if (TestNotNull(TEXT("Player spawned"), Player) && TestNotNull(TEXT("Causer spawned"), Causer))
    {
        UPSLStatsComponent* Stats = Player->GetStatsComponent();
        UPSLVitalsListener* Listener = NewObject<UPSLVitalsListener>();
        Player->OnDamageTaken.AddDynamic(Listener, &UPSLVitalsListener::OnDamage);

        const float Full = Stats->GetHealth();
        Stats->ApplyDamage(5.0f);
        TestEqual(TEXT("Starvation-style ApplyDamage lowered health"), Stats->GetHealth(), Full - 5.0f);
        TestEqual(TEXT("Starvation-style ApplyDamage raises no damage event"), Listener->DamageCalls, 0);
        Stats->SetHealth(Full);
        TestEqual(TEXT("Heal / restore raises no damage event"), Listener->DamageCalls, 0);

        UGameplayStatics::ApplyDamage(Player, 20.0f, nullptr, Causer, UDamageType::StaticClass());
        TestEqual(TEXT("TakeDamage raises exactly one damage event"), Listener->DamageCalls, 1);
        TestEqual(TEXT("Event amount"), Listener->LastDamage, 20.0f);
        TestEqual(TEXT("Event carries the remaining health"), Listener->LastDamageHealth, Stats->GetHealth());
        TestTrue(TEXT("Event kind is Bite with a causer"), Listener->LastDamageKind == EPSLDamageKind::Bite);
        TestTrue(TEXT("Event has a causer"), Listener->bLastHadCauser);
        TestTrue(TEXT("Event carries the causer location"), Listener->LastCauserLocation.Equals(Causer->GetActorLocation(), 1.0f));

        UGameplayStatics::ApplyDamage(Player, 10.0f, nullptr, nullptr, UDamageType::StaticClass());
        TestEqual(TEXT("Causer-less hit raises one more event"), Listener->DamageCalls, 2);
        TestTrue(TEXT("Causer-less hit is Unknown with no causer"), Listener->LastDamageKind == EPSLDamageKind::Unknown && !Listener->bLastHadCauser);

        UGameplayStatics::ApplyDamage(Player, 0.0f, nullptr, Causer, UDamageType::StaticClass());
        TestEqual(TEXT("Zero damage raises no event"), Listener->DamageCalls, 2);

        UGameplayStatics::ApplyDamage(Player, 1000.0f, nullptr, Causer, UDamageType::StaticClass());
        TestEqual(TEXT("Lethal hit raises one event"), Listener->DamageCalls, 3);
        TestEqual(TEXT("Lethal event leaves 0 health"), Listener->LastDamageHealth, 0.0f);
        UGameplayStatics::ApplyDamage(Player, 20.0f, nullptr, Causer, UDamageType::StaticClass());
        TestEqual(TEXT("Damage after death raises no event"), Listener->DamageCalls, 3);
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
