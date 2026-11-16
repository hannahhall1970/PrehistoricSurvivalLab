#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "PSLAudio.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectIterator.h"
#include "PSLMenuPreviewActor.h"
#include "PSLDinoSpecies.h"
#include "PSLGameInstance.h"
#include "PSLMainMenuWidget.h"
#include "PSLMenuGameMode.h"
#include "PSLPauseWidget.h"
#include "PSLPieTestSupport.h"
#include "PSLSaveSubsystem.h"
#include "PSLSpeciesSelectWidget.h"
#include "PSLUiButton.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"

namespace
{
using namespace PSLPie;

// Never PSL_Slot0: the developer's own save must survive the test.
constexpr const TCHAR* FlowTestSlot = TEXT("PSL_Test_FullFlow");

// State shared between the latent steps (the menu world has no pawn, so FPSLPieCtx cannot carry it).
struct FFlowState
{
    TWeakObjectPtr<UPSLSaveSubsystem> Saves;
    FString OldSlot;
    bool bSlotSwapped = false;
    bool bRealInput = false;
    int32 Frames = 0;
    FVector Location = FVector::ZeroVector;
    float Health = 0.0f;
    float Stamina = 0.0f;
    float Hunger = 0.0f;
    FName Species;
    FVector SpawnLocation = FVector::ZeroVector;
    FString Map;

    // Idempotent: deletes the test slot and puts the developer's slot name back. The slot file is deleted by name even when the PIE
    // subsystem is already gone (a timed-out step ends PIE before this destructor runs).
    void Cleanup()
    {
        if (bSlotSwapped)
        {
            UGameplayStatics::DeleteGameInSlot(FlowTestSlot, 0);
            if (Saves.IsValid())
            {
                Saves->SlotName = OldSlot;
            }
        }
        bSlotSwapped = false;
    }

    ~FFlowState() { Cleanup(); }
};

// Menu map has no pawn: find the PIE world running it and its menu widget. Never sets Ctx.bReady (that needs a pawn).
UPSLMainMenuWidget* FindMenuWidget(const FPSLPieCtx& Ctx, UWorld** OutWorld = nullptr)
{
    if (!GEditor)
    {
        return nullptr;
    }
    for (const FWorldContext& WorldContext : GEditor->GetWorldContexts())
    {
        UWorld* World = WorldContext.World();
        if (WorldContext.WorldType != EWorldType::PIE || !World || !World->GetMapName().Contains(Ctx.RequiredMap))
        {
            continue;
        }
        const APSLMenuGameMode* GameMode = World->GetAuthGameMode<APSLMenuGameMode>();
        if (GameMode && GameMode->GetMenuWidget())
        {
            if (OutWorld)
            {
                *OutWorld = World;
            }
            return GameMode->GetMenuWidget();
        }
    }
    return nullptr;
}

void ClickLogged(FPSLPieCtx& Ctx, FFlowState& Flow, UWidget* Widget, const TCHAR* What)
{
    Ctx.Test->TestTrue(FString::Printf(TEXT("Click %s delivered"), What), ClickWidget(Ctx, Widget, &Flow.bRealInput));
    if (!Flow.bRealInput)
    {
        Ctx.Test->AddInfo(FString::Printf(TEXT("[fallback] click %s used OnClicked broadcast (no renderer/geometry)"), What));
    }
}

// Drop the dead world's handles so the next WaitForPlayer binds the new PIE world.
void ResetForMapChange(FPSLPieCtx& Ctx, const TCHAR* NextMap)
{
    Ctx.RequiredMap = NextMap;
    Ctx.bReady = false;
    Ctx.World = nullptr;
    Ctx.GameViewport = nullptr;
    Ctx.PC = nullptr;
    Ctx.Character = nullptr;
}

template <typename T>
T* FindFlowScreen(FPSLPieCtx& Ctx)
{
    TArray<UUserWidget*> Found;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(Ctx.World, Found, T::StaticClass(), true);
    return Found.Num() > 0 ? Cast<T>(Found[0]) : nullptr;
}
}

// A packaged game boots on GameDefaultMap: it must be the menu, and the menu map package must exist (a typo boots a black screen).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLStartupMapsTest, "PSL.Config.StartupMaps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLStartupMapsTest::RunTest(const FString& Parameters)
{
    const TCHAR* Section = TEXT("/Script/EngineSettings.GameMapsSettings");
    FString GameDefault, EditorStartup;
    GConfig->GetString(Section, TEXT("GameDefaultMap"), GameDefault, GEngineIni);
    GConfig->GetString(Section, TEXT("EditorStartupMap"), EditorStartup, GEngineIni);
    const FString MenuPath = FString::Printf(TEXT("/Game/Maps/%s"), UPSLGameInstance::MenuMap);
    TestEqual(TEXT("GameDefaultMap is the main menu"), GameDefault, FString::Printf(TEXT("%s.%s"), *MenuPath, UPSLGameInstance::MenuMap));
    TestTrue(TEXT("Main menu map package exists"), FPackageName::DoesPackageExist(MenuPath));
    // Intentional (docs/GAME_SHELL.md): the editor opens on the valley.
    TestEqual(TEXT("EditorStartupMap is the valley"), EditorStartup,
        FString::Printf(TEXT("/Game/Maps/%s.%s"), UPSLGameInstance::ValleyMap, UPSLGameInstance::ValleyMap));
    return true;
}

// The hero preview must keep its framed 3/4 view: bounded sway around the placed yaw, never a full turn.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLMenuPreviewSwayTest, "PSL.Menu.PreviewSway", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLMenuPreviewSwayTest::RunTest(const FString& Parameters)
{
    constexpr float Amp = APSLMenuPreviewActor::SwayAmplitudeDegrees;
    constexpr float Period = APSLMenuPreviewActor::SwayPeriodSeconds;
    TestEqual(TEXT("No offset at start"), APSLMenuPreviewActor::SwayYawOffset(0.0f), 0.0f, 1.0e-4f);
    TestEqual(TEXT("Full amplitude at a quarter period"), APSLMenuPreviewActor::SwayYawOffset(Period * 0.25f), Amp, 1.0e-3f);
    TestEqual(TEXT("Periodic"), APSLMenuPreviewActor::SwayYawOffset(Period * 3.3f), APSLMenuPreviewActor::SwayYawOffset(Period * 0.3f), 1.0e-2f);
    float MaxAbs = 0.0f;
    for (float T = 0.0f; T <= 600.0f; T += 0.37f)
    {
        MaxAbs = FMath::Max(MaxAbs, FMath::Abs(APSLMenuPreviewActor::SwayYawOffset(T)));
    }
    TestTrue(FString::Printf(TEXT("Sway stays within +-%.0f deg over 10 min (max %.3f)"), Amp, MaxAbs), MaxAbs <= Amp + 1.0e-3f);
    TestTrue(TEXT("Amplitude keeps the 3/4 view (< 45 deg)"), Amp > 0.0f && Amp < 45.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLMenuFullFlowPieTest, "PSL.PIE.Menu.FullFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLMenuFullFlowPieTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("Menu_FullFlow"));
    const TSharedRef<FFlowState> Flow = MakeShared<FFlowState>();
    Context->RequiredMap = TEXT("L_MainMenu");
    // Same as QueuePieStart minus its WaitForPlayer step (L_MainMenu has no pawn).
    if (!TestTrue(TEXT("AutomationOpenMap returned true"), AutomationOpenMap(TEXT("/Game/Maps/L_MainMenu"))))
    {
        return false;
    }

    AddPieStep(Context, TEXT("WaitForMenu"), 30.0f, [](FPSLPieCtx& Ctx)
    {
        return Ctx.bInvalid || FindMenuWidget(Ctx) != nullptr;
    });
    AddPieStep(Context, TEXT("UseTestSlot"), 4.0f, [Flow](FPSLPieCtx& Ctx)
    {
        UWorld* World = nullptr;
        UPSLMainMenuWidget* Menu = Ctx.bInvalid ? nullptr : FindMenuWidget(Ctx, &World);
        if (!Menu)
        {
            return true;
        }
        UGameInstance* GameInstance = World->GetGameInstance();
        UPSLSaveSubsystem* Saves = GameInstance ? GameInstance->GetSubsystem<UPSLSaveSubsystem>() : nullptr;
        if (!Ctx.Test->TestNotNull(TEXT("Save subsystem"), Saves))
        {
            return true;
        }
        // Remember the old slot only once, and swap before anything can write.
        Flow->Saves = Saves;
        Flow->OldSlot = Saves->SlotName;
        Flow->bSlotSwapped = true;
        Saves->SlotName = FlowTestSlot;
        Saves->DeleteSave();
        Menu->RefreshContinue();
        Ctx.Test->TestFalse(TEXT("Continue disabled without a save"), Menu->GetContinueButton()->GetIsEnabled());
        return true;
    });
    // Let Slate lay the menu out so ClickWidget has real geometry when rendering.
    AddPieStep(Context, TEXT("MenuLayout"), 2.0f, [Flow](FPSLPieCtx& Ctx)
    {
        return Ctx.bInvalid || ++Flow->Frames >= 5;
    });
    AddPieStep(Context, TEXT("ClickNewGame"), 4.0f, [Flow](FPSLPieCtx& Ctx)
    {
        UPSLMainMenuWidget* Menu = Ctx.bInvalid ? nullptr : FindMenuWidget(Ctx);
        if (!Menu)
        {
            return true;
        }
        ClickLogged(Ctx, *Flow, Menu->GetNewGameButton(), TEXT("NewGame"));
        Ctx.Test->TestTrue(TEXT("Species screen visible"), Menu->GetSpeciesWidget()->IsVisible());
        Flow->Frames = 0;
        return true;
    });
    // One Next click per frame (max 4) until the species screen reports Para.
    AddPieStep(Context, TEXT("SelectPara"), 6.0f, [Flow](FPSLPieCtx& Ctx)
    {
        UPSLMainMenuWidget* Menu = Ctx.bInvalid ? nullptr : FindMenuWidget(Ctx);
        if (!Menu)
        {
            return true;
        }
        UPSLSpeciesSelectWidget* Species = Menu->GetSpeciesWidget();
        if (Species->GetCurrentSpeciesId() != UPSLDinoSpecies::ParaId && Flow->Frames < 4)
        {
            ++Flow->Frames;
            ClickLogged(Ctx, *Flow, Species->GetNextButton(), TEXT("Next"));
            return false;
        }
        if (!Ctx.Test->TestTrue(TEXT("Para selected within 4 clicks"), Species->GetCurrentSpeciesId() == UPSLDinoSpecies::ParaId))
        {
            Ctx.bInvalid = true;
            return true;
        }
        // Confirm opens the valley and replaces the PIE world.
        ClickLogged(Ctx, *Flow, Species->GetConfirmButton(), TEXT("Confirm"));
        ResetForMapChange(Ctx, TEXT("L_LostValley"));
        return true;
    });
    AddPieStep(Context, TEXT("WaitForValleyPawn"), 30.0f, [](FPSLPieCtx& Ctx)
    {
        return Ctx.bInvalid || WaitForPlayer(Ctx);
    });
    // Species assert, then make state non-default through the real damage API and real hunger drain.
    AddPieStep(Context, TEXT("AssertParaAndDirty"), 8.0f, [Flow, DirtyStart = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        if (DirtyStart < 0.0)
        {
            const UPSLDinoSpecies* Species = Ctx.Character->GetSpecies();
            Ctx.Test->TestTrue(TEXT("Pawn species is Para"), Species && Species->SpeciesId == UPSLDinoSpecies::ParaId);
            Ctx.Test->TestTrue(TEXT("Valley map loaded"), Ctx.World->GetMapName().Contains(TEXT("L_LostValley")));
            Ctx.Character->GetStatsComponent()->ApplyDamage(20.0f);
            // Move off the spawn point so a restore that silently respawns at PlayerStart fails the location check.
            Flow->SpawnLocation = Ctx.Character->GetActorLocation();
            Ctx.Test->TestTrue(TEXT("Pawn moved off spawn"), Ctx.Character->TeleportTo(Flow->SpawnLocation + FVector(400.0, 300.0, 100.0), Ctx.Character->GetActorRotation()));
            DirtyStart = Ctx.World->GetTimeSeconds();
        }
        // Three seconds of world time: Para drains 0.4 hunger/s.
        return Ctx.World->GetTimeSeconds() - DirtyStart >= 3.0;
    });
    // P, never Escape (Escape is the editor Stop-PIE chord). Vitals are recorded once the world is paused, so nothing drifts before the save.
    AddPieStep(Context, TEXT("OpenPause"), 6.0f, [Flow, bSent = false](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        APSLGameMode* GameMode = Ctx.World->GetAuthGameMode<APSLGameMode>();
        if (!bSent)
        {
            bSent = true;
            // Real sprint drain and hunger tick right before pausing, so a restore that resets either vital to max fails below.
            UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
            Survival->SetSprintRequested(true);
            Survival->UpdateStamina(3.0f, true);
            Survival->SetSprintRequested(false);
            Survival->UpdateHunger(10.0f);
            SendKey(Ctx, EKeys::P, IE_Pressed);
            SendKey(Ctx, EKeys::P, IE_Released);
        }
        if (GameMode->GetGameState() != EPSLGameState::Paused || !FindFlowScreen<UPSLPauseWidget>(Ctx))
        {
            return false;
        }
        const UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
        Flow->Location = Ctx.Character->GetActorLocation();
        Flow->Health = Ctx.Character->GetStatsComponent()->GetHealth();
        Flow->Stamina = Survival->GetStamina();
        Flow->Hunger = Survival->GetHunger();
        Flow->Species = Ctx.Character->GetSpecies()->SpeciesId;
        Flow->Map = Ctx.World->GetMapName();
        Ctx.Test->TestTrue(TEXT("Health below max after damage"), Flow->Health < Ctx.Character->GetStatsComponent()->GetMaxHealth());
        Ctx.Test->TestTrue(TEXT("Recorded location is away from spawn"), FVector::Dist2D(Flow->Location, Flow->SpawnLocation) > 200.0);
        Ctx.Test->TestTrue(TEXT("Hunger drained well below max"), Flow->Hunger < Survival->GetMaxHunger() - 3.0f);
        Ctx.Test->TestTrue(TEXT("Stamina drained well below max"), Flow->Stamina < Survival->GetMaxStamina() - 20.0f);
        Ctx.Test->AddInfo(FString::Printf(TEXT("Recorded: %s health %.2f stamina %.2f hunger %.2f at %s"),
            *Flow->Species.ToString(), Flow->Health, Flow->Stamina, Flow->Hunger, *Flow->Location.ToString()));
        return true;
    });
    AddPieStep(Context, TEXT("ClickSave"), 4.0f, [Flow](FPSLPieCtx& Ctx)
    {
        UPSLPauseWidget* Pause = Ctx.IsUsable() ? FindFlowScreen<UPSLPauseWidget>(Ctx) : nullptr;
        if (!Ctx.Test->TestNotNull(TEXT("Pause widget"), Pause))
        {
            return true;
        }
        ClickLogged(Ctx, *Flow, Pause->GetSaveButton(), TEXT("Save"));
        Ctx.Test->TestTrue(TEXT("Save slot holds a valid save"), Flow->Saves.IsValid() && Flow->Saves->HasValidSave());
        // Same namespace/key/source as LOCTEXT("Pause_Saved", "Saved") in PSLPauseWidget.cpp.
        Ctx.Test->TestEqual(TEXT("Toast says Saved"), Pause->GetToastText()->GetText().ToString(), NSLOCTEXT("PSL", "Pause_Saved", "Saved").ToString());
        return true;
    });
    AddPieStep(Context, TEXT("ClickMainMenu"), 4.0f, [Flow](FPSLPieCtx& Ctx)
    {
        UPSLPauseWidget* Pause = Ctx.IsUsable() ? FindFlowScreen<UPSLPauseWidget>(Ctx) : nullptr;
        if (!Ctx.Test->TestNotNull(TEXT("Pause widget"), Pause))
        {
            return true;
        }
        // ReturnToMenu replaces the PIE world: click first, then drop the old handles.
        ClickLogged(Ctx, *Flow, Pause->GetMainMenuButton(), TEXT("MainMenu"));
        ResetForMapChange(Ctx, TEXT("L_MainMenu"));
        return true;
    });
    AddPieStep(Context, TEXT("WaitForMenuAgain"), 30.0f, [](FPSLPieCtx& Ctx)
    {
        return Ctx.bInvalid || FindMenuWidget(Ctx) != nullptr;
    });
    // Returning from gameplay must rebuild the menu once: one menu widget and one menu-music component in the new world.
    AddPieStep(Context, TEXT("NoDuplicateMenu"), 2.0f, [](FPSLPieCtx& Ctx)
    {
        UWorld* World = nullptr;
        if (Ctx.bInvalid || !FindMenuWidget(Ctx, &World))
        {
            return true;
        }
        TArray<UUserWidget*> Menus;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Menus, UPSLMainMenuWidget::StaticClass(), true);
        Ctx.Test->TestEqual(TEXT("Exactly one main menu widget after ReturnToMenu"), Menus.Num(), 1);
        const USoundBase* MenuSound = PSLAudio::Load(PSLAudio::MenuMusic);
        int32 MusicComponents = 0;
        for (TObjectIterator<UAudioComponent> It; It; ++It)
        {
            MusicComponents += (MenuSound && It->Sound == MenuSound && It->GetWorld() == World && IsValid(*It)) ? 1 : 0;
        }
        // SpawnSound2D returns null without an audio device (possible headless), so exactly one is required only with a device.
        const bool bHasDevice = World->GetAudioDevice().IsValid();
        Ctx.Test->AddInfo(FString::Printf(TEXT("After ReturnToMenu: %d menu widget(s), %d MUS_Menu component(s), audio device %d"),
            Menus.Num(), MusicComponents, bHasDevice ? 1 : 0));
        if (bHasDevice && MenuSound)
        {
            Ctx.Test->TestEqual(TEXT("Exactly one MUS_Menu audio component after ReturnToMenu"), MusicComponents, 1);
        }
        else
        {
            Ctx.Test->TestTrue(TEXT("At most one MUS_Menu audio component after ReturnToMenu"), MusicComponents <= 1);
        }
        return true;
    });
    AddPieStep(Context, TEXT("ClickContinue"), 4.0f, [Flow](FPSLPieCtx& Ctx)
    {
        UPSLMainMenuWidget* Menu = Ctx.bInvalid ? nullptr : FindMenuWidget(Ctx);
        if (!Menu)
        {
            return true;
        }
        Menu->RefreshContinue();
        if (!Ctx.Test->TestTrue(TEXT("Continue enabled with the saved game"), Menu->GetContinueButton()->GetIsEnabled()))
        {
            Ctx.bInvalid = true;
            return true;
        }
        ClickLogged(Ctx, *Flow, Menu->GetContinueButton(), TEXT("Continue"));
        ResetForMapChange(Ctx, TEXT("L_LostValley"));
        return true;
    });
    AddPieStep(Context, TEXT("WaitForRestoredPawn"), 30.0f, [](FPSLPieCtx& Ctx)
    {
        // WaitForPlayer also holds 0.5 s of world time after the pawn appears: RestoreFromSave (next tick) has run by then.
        return Ctx.bInvalid || WaitForPlayer(Ctx);
    });
    // Stamina note: the world is paused while saving, so stamina cannot regen between the record and the save; it is also
    // compared against the save itself, which is the value Continue actually restores.
    AddPieStep(Context, TEXT("AssertRestored"), 3.0f, [Flow](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        FPSLSaveData Saved;
        FText Error;
        Ctx.Test->TestTrue(TEXT("Save loads and validates"), Flow->Saves.IsValid() && Flow->Saves->LoadAndValidate(Saved, Error));
        const UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
        Ctx.Test->TestTrue(TEXT("Map is L_LostValley"), Ctx.World->GetMapName().Contains(TEXT("L_LostValley")) && Flow->Map.Contains(TEXT("L_LostValley")));
        const UPSLDinoSpecies* Species = Ctx.Character->GetSpecies();
        Ctx.Test->TestTrue(TEXT("Species restored as Para"), Species && Species->SpeciesId == Flow->Species && Flow->Species == UPSLDinoSpecies::ParaId);
        Ctx.Test->TestTrue(TEXT("Location within 50 uu"), FVector::Dist(Ctx.Character->GetActorLocation(), Flow->Location) <= 50.0);
        Ctx.Test->TestEqual(TEXT("Health restored"), Ctx.Character->GetStatsComponent()->GetHealth(), Flow->Health, 1.0f);
        Ctx.Test->TestEqual(TEXT("Hunger restored"), Survival->GetHunger(), Flow->Hunger, 1.0f);
        // Stamina regenerates while unpaused after the restore: allow regen for the new world's whole lifetime only (~0.6 s here),
        // which stays far below max, so a restore that resets stamina to max still fails.
        const float RegenSlack = Survival->StaminaRegenPerSecond * Ctx.World->GetTimeSeconds() + 1.0f;
        Ctx.Test->TestEqual(TEXT("Save holds the recorded stamina"), Saved.Stamina, Flow->Stamina, 1.0f);
        Ctx.Test->TestTrue(FString::Printf(TEXT("Stamina restored: %.2f in [%.2f, %.2f]"), Survival->GetStamina(), Flow->Stamina - 1.0f, Flow->Stamina + RegenSlack),
            Survival->GetStamina() >= Flow->Stamina - 1.0f && Survival->GetStamina() <= Flow->Stamina + RegenSlack);
        Ctx.Test->TestTrue(TEXT("Restored stamina not reset to max"), Survival->GetStamina() < Survival->GetMaxStamina() - 10.0f);
        return true;
    });
    // Slot cleanup also runs from ~FFlowState, because a lost PIE context skips step bodies.
    AddPieStep(Context, TEXT("Cleanup"), 2.0f, [Flow](FPSLPieCtx&)
    {
        Flow->Cleanup();
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

#endif
