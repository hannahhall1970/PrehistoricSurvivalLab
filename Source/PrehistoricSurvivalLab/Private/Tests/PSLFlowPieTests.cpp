#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Engine/SkeletalMesh.h"
#include "Kismet/GameplayStatics.h"
#include "PSLDinoSpecies.h"
#include "PSLGameInstance.h"
#include "PSLPieTestSupport.h"

namespace
{
using namespace PSLPie;

APSLPreyCharacter* FindPrey(UWorld* World)
{
    TActorIterator<APSLPreyCharacter> It(World);
    return It ? *It : nullptr;
}
}

namespace
{
// Starts PIE on the arena, then enters the valley the way the species screen does:
// UPSLGameInstance::StartNewGame(SpeciesId). The pawn is never configured by the test itself.
bool RunSpeciesInValley(FAutomationTestBase& Test, const TCHAR* TestName, const FName SpeciesId)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(&Test, TestName);
    if (!QueuePieStart(Test, Context))
    {
        return false;
    }
    AddPieStep(Context, TEXT("StartNewGame"), 3.0f, [SpeciesId](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        UPSLGameInstance* GameInstance = Cast<UPSLGameInstance>(Ctx.World->GetGameInstance());
        if (!GameInstance)
        {
            Ctx.MarkInvalid(TEXT("PIE GameInstance is not UPSLGameInstance"));
            return true;
        }
        // OpenLevel replaces the PIE world: drop the old handles and wait for a pawn on the valley map.
        Ctx.RequiredMap = TEXT("L_LostValley");
        Ctx.bReady = false;
        Ctx.World = nullptr;
        Ctx.GameViewport = nullptr;
        Ctx.PC = nullptr;
        Ctx.Character = nullptr;
        GameInstance->StartNewGame(SpeciesId);
        return true;
    });
    AddPieStep(Context, TEXT("WaitForValleyPawn"), 30.0f, [](FPSLPieCtx& Ctx)
    {
        return WaitForPlayer(Ctx);
    });
    AddPieStep(Context, TEXT("SpeciesAssertions"), 3.0f, [SpeciesId](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        Ctx.Test->TestTrue(TEXT("Map is L_LostValley"), Ctx.World->GetMapName().Contains(TEXT("L_LostValley")));
        const UPSLGameInstance* GameInstance = Cast<UPSLGameInstance>(Ctx.World->GetGameInstance());
        if (!Ctx.Test->TestNotNull(TEXT("GameInstance is UPSLGameInstance"), GameInstance))
        {
            return true;
        }
        Ctx.Test->TestTrue(TEXT("Session species is the requested one"), GameInstance->SpeciesId == SpeciesId);

        const UPSLDinoSpecies* Expected = UPSLDinoSpecies::MakeBuiltin(SpeciesId, GetTransientPackage());
        if (!Ctx.Test->TestNotNull(TEXT("Expected built-in species"), Expected))
        {
            return true;
        }
        APSLCharacter* Character = Ctx.Character;
        if (!Ctx.Test->TestNotNull(TEXT("GameMode applied a species"), Character->GetSpecies()))
        {
            return true;
        }
        Ctx.Test->TestTrue(TEXT("Pawn species id"), Character->GetSpecies()->SpeciesId == SpeciesId);

        const USkeletalMesh* Mesh = Character->GetMesh()->GetSkeletalMeshAsset();
        if (Ctx.Test->TestNotNull(TEXT("Pawn has a skeletal mesh"), Mesh))
        {
            Ctx.Test->TestEqual(TEXT("Mesh asset"), Mesh->GetPathName(), Expected->Mesh.ToSoftObjectPath().ToString());
        }
        Ctx.Test->TestEqual(TEXT("Capsule radius"), Character->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), Expected->CapsuleRadius);
        Ctx.Test->TestEqual(TEXT("Capsule half height"), Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), Expected->CapsuleHalfHeight);
        Ctx.Test->TestEqual(TEXT("Walk speed"), Character->GetCharacterMovement()->MaxWalkSpeed, Expected->WalkSpeed);
        Ctx.Test->TestEqual(TEXT("Sprint speed"), Character->SprintSpeed, Expected->SprintSpeed);
        Record(Ctx, TEXT("species"));
        return true;
    });
    QueuePieEnd(Context);
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLFlowParaInValleyTest, "PSL.PIE.Species.ParaInValley", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLFlowParaInValleyTest::RunTest(const FString& Parameters)
{
    return RunSpeciesInValley(*this, TEXT("Flow_ParaInValley"), UPSLDinoSpecies::ParaId);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLFlowTRexInValleyTest, "PSL.PIE.Species.TRexInValley", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLFlowTRexInValleyTest::RunTest(const FString& Parameters)
{
    return RunSpeciesInValley(*this, TEXT("Flow_TRexInValley"), UPSLDinoSpecies::TRexId);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLFlowEatCarcassTest, "PSL.PIE.Hunger.EatCarcass", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLFlowEatCarcassTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("Flow_EatCarcass"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }
    AddPieStep(Context, TEXT("KillAndPlace"), 3.0f, [](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        APSLPreyCharacter* Prey = FindPrey(Ctx.World);
        if (!Prey)
        {
            Ctx.MarkInvalid(TEXT("No prey in L_TestArena"));
            return true;
        }
        Prey->GetStatsComponent()->ApplyDamage(1000.0f);
        Ctx.Test->TestTrue(TEXT("Prey is dead"), Prey->IsDead());

        // Bite center = player + forward * BiteReach; stand so it lands 70 uu past the corpse centre.
        const FVector Corpse = Prey->GetActorLocation();
        const FVector Forward = FVector::ForwardVector;
        const FVector Stand = Corpse - Forward * (Ctx.Character->BiteReach - 70.0f) + FVector(0.0f, 0.0f, 60.0f);
        Ctx.Character->SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
        Ctx.Character->GetCharacterMovement()->StopMovementImmediately();
        Ctx.PC->SetControlRotation(Forward.Rotation());
        Ctx.Character->SetActorRotation(Forward.Rotation());
        Ctx.Character->GetSurvivalComponent()->Hunger = 10.0f;
        return true;
    });
    AddPieStep(Context, TEXT("BiteCorpse"), 4.0f, [bBitten = false, StartTime = -1.0](FPSLPieCtx& Ctx) mutable
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        const double Now = Ctx.World->GetTimeSeconds();
        if (!bBitten)
        {
            bBitten = true;
            StartTime = Now;
            Ctx.Test->TestTrue(TEXT("Bite accepted"), Ctx.Character->TryBite());
            return false;
        }
        // BiteHitDelay (0.35 s) plus margin before the overlap resolves.
        if (Now - StartTime < 0.8)
        {
            return false;
        }
        const float Hunger = Ctx.Character->GetSurvivalComponent()->GetHunger();
        Ctx.Test->TestTrue(FString::Printf(TEXT("Eating raised hunger above 10 (was %.1f)"), Hunger), Hunger > 10.0f);
        APSLPreyCharacter* Prey = FindPrey(Ctx.World);
        if (Prey)
        {
            Ctx.Test->TestTrue(TEXT("Carcass lost meat"), Prey->MeatRemaining < 60.0f);
        }
        Record(Ctx, TEXT("ate"));
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLFlowPauseGameOverTest, "PSL.PIE.PauseGameOver", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLFlowPauseGameOverTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("Flow_PauseGameOver"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }
    // World time of the lethal hit, shared between the steps below.
    const TSharedRef<double> DeathTime = MakeShared<double>(0.0);
    AddPieStep(Context, TEXT("PauseKeyPauses"), 4.0f, [Frames = 0](FPSLPieCtx& Ctx) mutable
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
            Ctx.Test->TestTrue(TEXT("Starts in Playing"), GameMode->GetGameState() == EPSLGameState::Playing);
            // P, not Escape: Escape is the editor's Stop-Play-In-Editor chord and tears the PIE session down.
            // PSL.Input.DefaultBindings verifies that Escape is still mapped to the same action.
            SendKey(Ctx, EKeys::P, IE_Pressed);
            SendKey(Ctx, EKeys::P, IE_Released);
        }
        // Enhanced Input fires the action on the next player-input tick.
        if (++Frames < 10 && GameMode->GetGameState() != EPSLGameState::Paused)
        {
            return false;
        }
        Ctx.Test->TestTrue(TEXT("Pause key pauses"), GameMode->GetGameState() == EPSLGameState::Paused);
        Ctx.Test->TestTrue(TEXT("World is paused"), UGameplayStatics::IsGamePaused(Ctx.World));
        return true;
    });
    AddPieStep(Context, TEXT("ResumeAndKill"), 2.0f, [DeathTime](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        APSLGameMode* GameMode = Ctx.World->GetAuthGameMode<APSLGameMode>();
        GameMode->ResumeGame();
        Ctx.Test->TestTrue(TEXT("Resumed to Playing"), GameMode->GetGameState() == EPSLGameState::Playing);
        Ctx.Test->TestFalse(TEXT("World unpaused"), UGameplayStatics::IsGamePaused(Ctx.World));

        // Lethal starvation: empty stomach and no health left. Starvation tuning is untouched.
        UPSLSurvivalComponent* Survival = Ctx.Character->GetSurvivalComponent();
        UPSLStatsComponent* Stats = Ctx.Character->GetStatsComponent();
        Survival->Hunger = 0.0f;
        Stats->ApplyDamage(Stats->GetHealth());
        *DeathTime = Ctx.World->GetTimeSeconds();
        Ctx.Test->TestTrue(TEXT("Player is dead"), Stats->IsDead());
        // Death alone must not end the game: the delay lets the death clip play.
        Ctx.Test->TestTrue(TEXT("Not Game Over at the moment of death"), GameMode->GetGameState() != EPSLGameState::GameOver);
        return true;
    });
    AddPieStep(Context, TEXT("StarveToGameOver"), 8.0f, [DeathTime](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        APSLGameMode* GameMode = Ctx.World->GetAuthGameMode<APSLGameMode>();
        const double Elapsed = Ctx.World->GetTimeSeconds() - *DeathTime;
        if (GameMode->GetGameState() != EPSLGameState::GameOver)
        {
            if (Elapsed > 3.0)
            {
                Ctx.MarkInvalid(FString::Printf(TEXT("Game Over not reached %.2f s after death (limit 3.0 s)"), Elapsed));
                return true;
            }
            return false;
        }
        Ctx.Test->TestTrue(FString::Printf(TEXT("Game Over within 3.0 s of death (was %.2f s)"), Elapsed), Elapsed <= 3.0);
        Ctx.Test->TestTrue(TEXT("Player died of starvation"), Ctx.Character->GetStatsComponent()->IsDead());
        Ctx.Test->TestTrue(TEXT("World paused in Game Over"), UGameplayStatics::IsGamePaused(Ctx.World));
        Record(Ctx, TEXT("gameover"));
        UGameplayStatics::SetGamePaused(Ctx.World, false);
        return true;
    });
    QueuePieEnd(Context);
    return true;
}

#endif
