#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "PSLDinoSpecies.h"
#include "PSLGameInstance.h"
#include "PSLGameOverWidget.h"
#include "PSLMainMenuWidget.h"
#include "PSLPauseWidget.h"
#include "PSLPieTestSupport.h"
#include "PSLSaveSubsystem.h"
#include "PSLUiButton.h"

namespace
{
using namespace PSLPie;

// Never PSL_Slot0: the developer's own save must survive the tests.
constexpr const TCHAR* ShellTestSlot = TEXT("PSL_Test_Shell");

UPSLSaveSubsystem* UseTestSlot(FPSLPieCtx& Ctx)
{
    UGameInstance* GameInstance = Ctx.World->GetGameInstance();
    UPSLSaveSubsystem* Saves = GameInstance ? GameInstance->GetSubsystem<UPSLSaveSubsystem>() : nullptr;
    if (Saves)
    {
        Saves->SlotName = ShellTestSlot;
    }
    return Saves;
}

template <typename T>
T* FindScreen(FPSLPieCtx& Ctx)
{
    TArray<UUserWidget*> Found;
    // Top level only: a removed widget stays alive until GC, so an unfiltered lookup still finds it.
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(Ctx.World, Found, T::StaticClass(), true);
    return Found.Num() > 0 ? Cast<T>(Found[0]) : nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLMenuQuitPieTest, "PSL.PIE.Menu.Quit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLMenuQuitPieTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("Menu_Quit"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }
    const TSharedRef<bool> bQuitCalled = MakeShared<bool>(false);
    const TSharedRef<bool> bRealInput = MakeShared<bool>(false);
    AddPieStep(Context, TEXT("ContinueTracksSave"), 4.0f, [](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        UPSLSaveSubsystem* Saves = UseTestSlot(Ctx);
        if (!Ctx.Test->TestNotNull(TEXT("Save subsystem"), Saves))
        {
            return true;
        }
        Saves->DeleteSave();
        UPSLMainMenuWidget* Menu = CreateWidget<UPSLMainMenuWidget>(Ctx.PC.Get(), UPSLMainMenuWidget::StaticClass());
        if (!Ctx.Test->TestNotNull(TEXT("Main menu created"), Menu))
        {
            return true;
        }
        Menu->AddToViewport();
        Menu->RefreshContinue();
        Ctx.Test->TestFalse(TEXT("Continue disabled without a save"), Menu->GetContinueButton()->GetIsEnabled());
        // SaveCheckpoint here stamps L_TestArena: it must write, and Continue must still refuse it (wrong map).
        Ctx.Test->TestTrue(TEXT("Checkpoint written"), Saves->SaveCheckpoint(EPSLSaveReason::Pause));
        Menu->RefreshContinue();
        Ctx.Test->TestFalse(TEXT("Continue disabled for a save from another map"), Menu->GetContinueButton()->GetIsEnabled());
        // Valid L_LostValley fixture; the real SaveCheckpoint -> Main Menu -> Continue path is PSL.PIE.Menu.FullFlow (M2b3).
        FPSLSaveData Data;
        Data.SpeciesId = UPSLDinoSpecies::TRexId;
        Data.MapName = FName(UPSLGameInstance::ValleyMap);
        Data.Health = 10.0f;
        Ctx.Test->TestTrue(TEXT("Valid save written"), Saves->WriteData(Data));
        Menu->RefreshContinue();
        Ctx.Test->TestTrue(TEXT("Continue enabled with a save"), Menu->GetContinueButton()->GetIsEnabled());
        Saves->DeleteSave();
        return true;
    });
    // Let Slate lay the widget out so the cached geometry is real when rendering.
    AddPieStep(Context, TEXT("Layout"), 2.0f, [Frames = 0](FPSLPieCtx& Ctx) mutable
    {
        return !Ctx.IsUsable() || ++Frames >= 5;
    });
    AddPieStep(Context, TEXT("ClickQuit"), 4.0f, [bQuitCalled, bRealInput, Frames = 0, bClicked = false](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        UPSLGameInstance* GameInstance = Cast<UPSLGameInstance>(Ctx.World->GetGameInstance());
        UPSLMainMenuWidget* Menu = FindScreen<UPSLMainMenuWidget>(Ctx);
        if (!Ctx.Test->TestNotNull(TEXT("GameInstance is UPSLGameInstance"), GameInstance) || !Ctx.Test->TestNotNull(TEXT("Menu on screen"), Menu))
        {
            return true;
        }
        if (!bClicked)
        {
            bClicked = true;
            GameInstance->OnQuitRequested = [bQuitCalled]() { *bQuitCalled = true; };
            Ctx.Test->TestTrue(TEXT("Quit click delivered"), ClickWidget(Ctx, Menu->GetQuitButton(), &*bRealInput));
        }
        if (!*bQuitCalled && ++Frames < 10)
        {
            return false;
        }
        GameInstance->OnQuitRequested = nullptr;
        Ctx.Test->TestTrue(TEXT("Quit requests the GameInstance quit hook"), *bQuitCalled);
        if (FApp::CanEverRender())
        {
            Ctx.Test->TestTrue(TEXT("Rendered run used real Slate input, not the OnClicked fallback"), *bRealInput);
        }
        Menu->RemoveFromParent();
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLMenuPauseGameOverPieTest, "PSL.PIE.Menu.PauseGameOver", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLMenuPauseGameOverPieTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("Menu_PauseGameOver"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }
    AddPieStep(Context, TEXT("PauseKeyShowsPause"), 4.0f, [Frames = 0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        APSLGameMode* GameMode = Ctx.World->GetAuthGameMode<APSLGameMode>();
        if (!GameMode)
        {
            Ctx.MarkInvalid(TEXT("No APSLGameMode"));
            return true;
        }
        if (Frames == 0)
        {
            UseTestSlot(Ctx);
            // P, never Escape: Escape is the editor's Stop-PIE chord.
            SendKey(Ctx, EKeys::P, IE_Pressed);
            SendKey(Ctx, EKeys::P, IE_Released);
        }
        if (++Frames < 10 && GameMode->GetGameState() != EPSLGameState::Paused)
        {
            return false;
        }
        Ctx.Test->TestTrue(TEXT("P pauses"), GameMode->GetGameState() == EPSLGameState::Paused);
        const UPSLPauseWidget* Pause = FindScreen<UPSLPauseWidget>(Ctx);
        if (Ctx.Test->TestNotNull(TEXT("Pause widget on screen"), Pause))
        {
            Ctx.Test->TestTrue(TEXT("Pause widget in viewport"), Pause->IsInViewport());
            Ctx.Test->TestTrue(TEXT("Pause widget visible"), Pause->IsVisible());
            if (FApp::CanEverRender())
            {
                Ctx.Test->TestTrue(TEXT("Pause widget has keyboard focus"), Pause->HasKeyboardFocus() || Pause->HasFocusedDescendants());
            }
        }
        return true;
    });
    AddPieStep(Context, TEXT("ClickResume"), 4.0f, [Frames = 0, Pause = TWeakObjectPtr<UPSLPauseWidget>()](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        APSLGameMode* GameMode = Ctx.World->GetAuthGameMode<APSLGameMode>();
        // Look the widget up once: after Resume it is off the viewport and FindScreen no longer returns it.
        if (!Pause.IsValid())
        {
            Pause = FindScreen<UPSLPauseWidget>(Ctx);
            if (!Ctx.Test->TestNotNull(TEXT("Pause widget"), Pause.Get()))
            {
                return true;
            }
            ClickWidget(Ctx, Pause->GetResumeButton());
        }
        if (GameMode->GetGameState() != EPSLGameState::Playing && ++Frames < 10)
        {
            return false;
        }
        Ctx.Test->TestTrue(TEXT("Resume returns to Playing"), GameMode->GetGameState() == EPSLGameState::Playing);
        Ctx.Test->TestFalse(TEXT("World unpaused"), UGameplayStatics::IsGamePaused(Ctx.World));
        Ctx.Test->TestFalse(TEXT("Pause widget left the viewport"), Pause.IsValid() && Pause->IsInViewport());
        Ctx.Test->TestNull(TEXT("Pause widget removed"), FindScreen<UPSLPauseWidget>(Ctx));
        return true;
    });
    // Starvation damage must actually land (it sets the cause), so leave one hit point and let the survival tick finish it.
    AddPieStep(Context, TEXT("Starve"), 6.0f, [bPrepared = false](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
        UPSLStatsComponent* Stats = Ctx.Character->GetStatsComponent();
        if (!bPrepared)
        {
            bPrepared = true;
            Survival->Hunger = 0.0f;
            Stats->ApplyDamage(Stats->GetHealth() - 1.0f);
        }
        if (!Survival->DiedOfStarvation())
        {
            return false;
        }
        Stats->ApplyDamage(Stats->GetHealth());
        Ctx.Test->TestTrue(TEXT("Player is dead"), Stats->IsDead());
        return true;
    });
    AddPieStep(Context, TEXT("GameOverShows"), 8.0f, [](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        APSLGameMode* GameMode = Ctx.World->GetAuthGameMode<APSLGameMode>();
        const UPSLGameOverWidget* GameOver = FindScreen<UPSLGameOverWidget>(Ctx);
        if (GameMode->GetGameState() != EPSLGameState::GameOver || !GameOver)
        {
            return false;
        }
        Ctx.Test->TestTrue(TEXT("Game Over widget in viewport"), GameOver->IsInViewport());
        Ctx.Test->TestEqual(TEXT("Starvation title"), GameOver->GetTitleText()->GetText().ToString(), FString(TEXT("YOU STARVED")));
        Ctx.Test->TestTrue(TEXT("Continue button reflects the save slot"), GameOver->GetContinueButton()->GetIsEnabled() == UseTestSlot(Ctx)->HasValidSave());
        return true;
    });
    AddPieStep(Context, TEXT("ClickRetry"), 3.0f, [](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        UPSLGameOverWidget* GameOver = FindScreen<UPSLGameOverWidget>(Ctx);
        if (!Ctx.Test->TestNotNull(TEXT("Game Over widget"), GameOver))
        {
            return true;
        }
        // Retry opens the valley and replaces the PIE world: click first, then drop the old handles and wait for the new pawn.
        ClickWidget(Ctx, GameOver->GetRetryButton());
        Ctx.RequiredMap = TEXT("L_LostValley");
        Ctx.bReady = false;
        Ctx.World = nullptr;
        Ctx.GameViewport = nullptr;
        Ctx.PC = nullptr;
        Ctx.Character = nullptr;
        return true;
    });
    AddPieStep(Context, TEXT("WaitForNewPawn"), 30.0f, [](FPSLPieCtx& Ctx)
    {
        return WaitForPlayer(Ctx);
    });
    AddPieStep(Context, TEXT("RetryAssertions"), 3.0f, [](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        Ctx.Test->TestTrue(TEXT("Retry loads the valley"), Ctx.World->GetMapName().Contains(TEXT("L_LostValley")));
        const UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
        // The new pawn starts full and drains HungerDrainPerSecond per second of its own life (not sprinting).
        // Tolerance: 0.25 s of drain, covering the spawn frame that ticks before/after creation time.
        const float Age = Ctx.Character->GetGameTimeSinceCreation();
        const float Expected = FMath::Max(0.0f, Survival->GetMaxHunger() - Survival->HungerDrainPerSecond * Age);
        Ctx.Test->TestTrue(TEXT("New pawn hunger never above max"), Survival->GetHunger() <= Survival->GetMaxHunger());
        Ctx.Test->TestEqual(TEXT("New pawn has full hunger minus drain over its age"), Survival->GetHunger(), Expected, Survival->HungerDrainPerSecond * 0.25f);
        Ctx.Test->TestFalse(TEXT("New pawn is alive"), Ctx.Character->GetStatsComponent()->IsDead());
        Ctx.Test->TestTrue(TEXT("New session is playing"), Ctx.World->GetAuthGameMode<APSLGameMode>()->GetGameState() == EPSLGameState::Playing);
        // Autosave may have written the test slot during the run; leave nothing behind.
        if (UPSLSaveSubsystem* Saves = UseTestSlot(Ctx))
        {
            Saves->DeleteSave();
        }
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

#endif
