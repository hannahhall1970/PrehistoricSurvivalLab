#include "PSLGameMode.h"

#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "PSLGameOverWidget.h"
#include "PSLPauseWidget.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "PSLAudio.h"
#include "PSLCharacter.h"
#include "PSLDinoSpecies.h"
#include "PSLGameInstance.h"
#include "PSLHUD.h"
#include "PSLPreyCharacter.h"
#include "PSLPredatorCharacter.h"
#include "PSLSaveSubsystem.h"
#include "PSLStatsComponent.h"
#include "PSLSurvivalComponent.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarPredatorSpawn(
    TEXT("psl.Predator.Spawn"), 1,
    TEXT("Spawn one predator in L_LostValley at BeginPlay: 0 off, 1 normal play only (not under automation), 2 always (PSL.PIE.LostValley.Predator)."));

APSLGameMode::APSLGameMode()
{
    DefaultPawnClass = APSLCharacter::StaticClass();
    HUDClass = APSLHUD::StaticClass();
}

void APSLGameMode::BeginPlay()
{
    Super::BeginPlay();

    UWorld* World = GetWorld();
    GetWorldTimerManager().SetTimer(SessionTimer, this, &APSLGameMode::TickSession, SessionTickInterval, true);

    // Prey respawner: only worlds that placed prey keep a population (L_TestArena tests destroy no carcass within 45 s).
    for (TActorIterator<APSLPreyCharacter> It(World); It; ++It)
    {
        ++PlacedPreyCount;
    }
    if (PlacedPreyCount > 0)
    {
        CarcassHandle = APSLPreyCharacter::OnAnyCarcassDestroyed().AddUObject(this, &APSLGameMode::OnCarcassDestroyed);
    }
    PreyKilledHandle = APSLPreyCharacter::OnAnyPreyKilled().AddUObject(this, &APSLGameMode::HandlePreyKilled);

    if (USoundBase* S = PSLAudio::Load(PSLAudio::ValleyMusic))
    {
        Music = UGameplayStatics::SpawnSound2D(this, S, PSLAudio::ValleyMusicGain, 1.f, 0.f, nullptr, false, false);
    }
    if (USoundBase* S = PSLAudio::Load(PSLAudio::Wind))
    {
        Ambience = UGameplayStatics::SpawnSound2D(this, S, 1.f, 1.f, 0.f, nullptr, false, false);
    }
    // After the player has logged in and been placed (next tick), so the distance is measured from the real spawn.
    // Automation only spawns it when a test opts in (mode 2), so every other test that loads the valley stays predator-free.
    const int32 PredatorMode = CVarPredatorSpawn.GetValueOnGameThread();
    if (World->GetMapName().Contains(TEXT("L_LostValley")) && PredatorMode >= (GIsAutomationTesting ? 2 : 1))
    {
        GetWorldTimerManager().SetTimerForNextTick(this, &APSLGameMode::SpawnPredator);
    }
}

void APSLGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(SessionTimer);
    GetWorldTimerManager().ClearTimer(GameOverTimer);
    APSLPreyCharacter::OnAnyCarcassDestroyed().Remove(CarcassHandle);
    APSLPreyCharacter::OnAnyPreyKilled().Remove(PreyKilledHandle);
    if (Music)
    {
        Music->Stop();
    }
    if (Ambience)
    {
        Ambience->Stop();
    }
    if (State != EPSLGameState::Playing)
    {
        UGameplayStatics::SetGamePaused(this, false);
    }
    CloseScreen();
    Super::EndPlay(EndPlayReason);
}

void APSLGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
    Super::HandleStartingNewPlayer_Implementation(NewPlayer);
    ApplySessionToPawn(NewPlayer);
}

void APSLGameMode::ApplySessionToPawn(APlayerController* PC)
{
    APSLCharacter* Character = PC ? Cast<APSLCharacter>(PC->GetPawn()) : nullptr;
    if (!Character)
    {
        return;
    }

    UPSLGameInstance* Session = Cast<UPSLGameInstance>(GetGameInstance());
    const FName SpeciesId = Session ? Session->SpeciesId : UPSLDinoSpecies::TRexId;
    Character->ApplySpecies(UPSLDinoSpecies::MakeBuiltin(SpeciesId, Character));

    Player = Character;
    Character->OnAteFood.AddDynamic(this, &APSLGameMode::HandleAteFood);
    if (Character->GetStatsComponent())
    {
        Character->GetStatsComponent()->OnDeath.AddDynamic(this, &APSLGameMode::HandlePlayerDeath);
    }

    if (Session && Session->bContinueRequested)
    {
        Session->bContinueRequested = false;
        // The pawn's BeginPlay (and the survival component's) reset stamina/hunger after login; restore on the next tick.
        GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, WeakPC = TWeakObjectPtr<APlayerController>(PC)]()
        {
            if (WeakPC.IsValid())
            {
                RestoreFromSave(WeakPC.Get());
            }
        }));
    }
    SetInputForState();
}

void APSLGameMode::RestoreFromSave(APlayerController* PC)
{
    APSLCharacter* Character = Cast<APSLCharacter>(PC->GetPawn());
    UPSLSaveSubsystem* Saves = GetGameInstance() ? GetGameInstance()->GetSubsystem<UPSLSaveSubsystem>() : nullptr;
    FPSLSaveData Data;
    FText Error;
    if (!Character || !Saves || !Saves->LoadAndValidate(Data, Error))
    {
        UE_LOG(LogTemp, Warning, TEXT("PSL continue failed, starting at the PlayerStart: %s"), *Error.ToString());
        return;
    }
    UE_LOG(LogTemp, Display, TEXT("PSL continue loaded save from %s (species %s, hunger %.1f)"), *Data.SavedAtUtc.ToIso8601(),
        *Data.SpeciesId.ToString(), Data.Hunger);

    Character->SetActorTransform(Data.PlayerTransform, false, nullptr, ETeleportType::TeleportPhysics);
    PC->SetControlRotation(Data.ControlRotation);
    Character->GetStatsComponent()->SetHealth(Data.Health);
    Character->GetSurvivalComponent()->RestoreState(Data.Stamina, Data.Hunger);
}

void APSLGameMode::SetInputForState()
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC)
    {
        return;
    }
    if (State == EPSLGameState::Playing)
    {
        PC->SetInputMode(FInputModeGameOnly());
        PC->bShowMouseCursor = false;
    }
    else
    {
        PC->SetInputMode(FInputModeUIOnly());
        PC->bShowMouseCursor = true;
    }
}

void APSLGameMode::PauseGame()
{
    if (State != EPSLGameState::Playing)
    {
        return;
    }
    State = EPSLGameState::Paused;
    UGameplayStatics::SetGamePaused(this, true);
    SetInputForState();
    OpenScreen(UPSLPauseWidget::StaticClass());
}

void APSLGameMode::ResumeGame()
{
    if (State != EPSLGameState::Paused)
    {
        return;
    }
    State = EPSLGameState::Playing;
    UGameplayStatics::SetGamePaused(this, false);
    SetInputForState();
    CloseScreen();
}

void APSLGameMode::TogglePause()
{
    if (State == EPSLGameState::Playing)
    {
        PauseGame();
    }
    else if (State == EPSLGameState::Paused)
    {
        ResumeGame();
    }
}

void APSLGameMode::HandlePlayerDeath()
{
    if (State == EPSLGameState::GameOver || GetWorldTimerManager().IsTimerActive(GameOverTimer))
    {
        return;
    }
    GetWorldTimerManager().SetTimer(GameOverTimer, this, &APSLGameMode::EnterGameOver, GameOverDelay, false);
}

void APSLGameMode::EnterGameOver()
{
    State = EPSLGameState::GameOver;
    UGameplayStatics::SetGamePaused(this, true);
    if (USoundBase* S = PSLAudio::Load(PSLAudio::GameOver))
    {
        UGameplayStatics::PlaySound2D(this, S);
    }
    SetInputForState();

    // Cause is read now: the title must not depend on the pawn later.
    bool bStarved = false;
    if (const APSLCharacter* Character = Player.Get())
    {
        bStarved = Character->GetSurvivalComponent() && Character->GetSurvivalComponent()->DiedOfStarvation();
        // Desaturate the world behind the screen through the pawn camera's own post-process settings.
        if (UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>())
        {
            Camera->PostProcessBlendWeight = 1.0f;
            Camera->PostProcessSettings.bOverride_ColorSaturation = true;
            Camera->PostProcessSettings.ColorSaturation = FVector4(0.2, 0.2, 0.2, 1.0);
        }
    }
    OpenScreen(UPSLGameOverWidget::StaticClass());
    if (UPSLGameOverWidget* GameOver = Cast<UPSLGameOverWidget>(ScreenWidget))
    {
        GameOver->SetStarved(bStarved);
    }
}

void APSLGameMode::OpenScreen(TSubclassOf<UUserWidget> WidgetClass)
{
    CloseScreen();
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC || !WidgetClass)
    {
        return;
    }
    ScreenWidget = CreateWidget<UUserWidget>(PC, WidgetClass);
    if (ScreenWidget)
    {
        ScreenWidget->AddToViewport();
        ScreenWidget->SetUserFocus(PC);
        ScreenWidget->SetKeyboardFocus();
    }
}

void APSLGameMode::CloseScreen()
{
    if (ScreenWidget)
    {
        ScreenWidget->RemoveFromParent();
        ScreenWidget = nullptr;
    }
}

void APSLGameMode::Retry()
{
    CloseScreen();
    UGameplayStatics::SetGamePaused(this, false);
    State = EPSLGameState::Playing;
    UPSLGameInstance* Session = Cast<UPSLGameInstance>(GetGameInstance());
    const FName SpeciesId = Session ? Session->SpeciesId : UPSLDinoSpecies::TRexId;
    if (Session)
    {
        Session->StartNewGame(SpeciesId);
    }
}

void APSLGameMode::TickSession()
{
    // Runs on world time, so it does not advance while paused or in Game Over.
    if (State != EPSLGameState::Playing)
    {
        return;
    }
    const APSLCharacter* Character = Player.Get();
    if (!Character || !Character->GetStatsComponent() || Character->GetStatsComponent()->IsDead())
    {
        return;
    }
    if (UPSLGameInstance* Session = Cast<UPSLGameInstance>(GetGameInstance()))
    {
        Session->SurvivalSeconds += SessionTickInterval;
    }

    AutosaveAccum += SessionTickInterval;
    if (AutosaveAccum >= AutosaveInterval)
    {
        AutosaveAccum = 0.0f;
        if (UPSLSaveSubsystem* Saves = GetGameInstance() ? GetGameInstance()->GetSubsystem<UPSLSaveSubsystem>() : nullptr)
        {
            Saves->SaveCheckpoint(EPSLSaveReason::Autosave);
        }
    }
}

void APSLGameMode::HandleAteFood(float Amount)
{
    if (UPSLGameInstance* Session = Cast<UPSLGameInstance>(GetGameInstance()))
    {
        Session->FoodEaten += Amount;
    }
}

void APSLGameMode::HandlePreyKilled(APSLPreyCharacter* Prey, AActor* Killer)
{
    UPSLGameInstance* Session = Cast<UPSLGameInstance>(GetGameInstance());
    if (Session && Killer && Killer == Player.Get())
    {
        ++Session->PreyKilled;
    }
}

int32 APSLGameMode::CountLivingPrey() const
{
    int32 Count = 0;
    for (TActorIterator<APSLPreyCharacter> It(GetWorld()); It; ++It)
    {
        if (!It->IsDead() && !It->IsActorBeingDestroyed())
        {
            ++Count;
        }
    }
    return Count;
}

void APSLGameMode::OnCarcassDestroyed()
{
    FTimerHandle Fire;
    GetWorldTimerManager().SetTimer(Fire, this, &APSLGameMode::SpawnReplacementPrey, PreyRespawnDelay, false);
}

void APSLGameMode::SpawnReplacementPrey()
{
    if (CountLivingPrey() >= PlacedPreyCount)
    {
        return;
    }

    UWorld* World = GetWorld();
    UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World);
    const APSLCharacter* Character = Player.Get();
    if (Nav && Character)
    {
        const FVector Origin = Character->GetActorLocation();
        for (int32 Attempt = 0; Attempt < 8; ++Attempt)
        {
            FNavLocation Point;
            if (Nav->GetRandomReachablePointInRadius(Origin, PreyRespawnMaxDistance, Point)
                && FVector::Dist2D(Origin, Point.Location) >= PreyRespawnMinDistance)
            {
                FActorSpawnParameters Params;
                Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
                World->SpawnActor<APSLPreyCharacter>(APSLPreyCharacter::StaticClass(), FTransform(Point.Location + FVector(0.0f, 0.0f, 60.0f)), Params);
                return;
            }
        }
    }

    FTimerHandle RetryHandle;
    GetWorldTimerManager().SetTimer(RetryHandle, this, &APSLGameMode::SpawnReplacementPrey, PreyRespawnRetryDelay, false);
}

void APSLGameMode::SpawnPredator()
{
    UWorld* World = GetWorld();
    UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World);
    ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr);
    const APSLCharacter* Character = Player.Get();
    bool bNavEmpty = true;
    bool bBuilding = false;
    if (Recast && Character)
    {
        // GetAllNavMeshTiles / GetNavMeshTilesCount list the tile pool (every slot has a nonzero ref), so an empty
        // serialized navmesh still reports >1000 "tiles". Populated tiles have a header, which GetNavMeshBounds sums.
        bNavEmpty = !Recast->GetNavMeshBounds().IsValid;
        bBuilding = Nav->IsNavigationBeingBuilt(World);
        if (bNavEmpty || bBuilding)
        {
            // The valley ships a serialized empty RecastNavMesh, the engine skips load-time rebuilds
            // (bForceRebuildOnLoad=false) and Dynamic generation only rebuilds dirty areas: build it once ourselves.
            const bool bRebuild = bNavEmpty && !bPredatorNavRebuildRequested && !bBuilding;
            bPredatorNavRebuildRequested |= bRebuild;
            if (bRebuild || PredatorSpawnAttempts == 0 || PredatorSpawnAttempts % 20 == 0)
            {
                UE_LOG(LogTemp, Display, TEXT("PSL predator nav: empty=%d building=%d rebuild_requested=%d bounds=%s agent=%s radius=%.0f height=%.0f"),
                    bNavEmpty, bBuilding, bPredatorNavRebuildRequested, *Recast->GetNavMeshBounds().ToString(),
                    *Recast->GetConfig().Name.ToString(), Recast->GetConfig().AgentRadius, Recast->GetConfig().AgentHeight);
            }
            if (bRebuild)
            {
                Recast->RebuildAll();
            }
        }
        else
        {
            UE_LOG(LogTemp, Display, TEXT("PSL predator nav: empty=%d building=%d rebuild_requested=%d bounds=%s agent=%s radius=%.0f height=%.0f"),
                bNavEmpty, bBuilding, bPredatorNavRebuildRequested, *Recast->GetNavMeshBounds().ToString(),
                *Recast->GetConfig().Name.ToString(), Recast->GetConfig().AgentRadius, Recast->GetConfig().AgentHeight);

            // The nav is ready: one deterministic candidate pass; the result cannot change, so never retry.
            const FVector Origin = Character->GetActorLocation();
            FNavLocation PlayerNav;
            if (!Nav->ProjectPointToNavigation(Origin, PlayerNav, FVector(300.0, 300.0, 1000.0), Recast))
            {
                UE_LOG(LogTemp, Warning, TEXT("PSL predator not spawned: player off navmesh"));
                return;
            }
            const FVector ViewDir = FRotator(0.0, Character->GetControlRotation().Yaw, 0.0).Vector();
            FCollisionQueryParams Params(NAME_None, false, Character);
            int32 i = 0;
            double MaxCompleteDist = 0.0;
            for (const double Radius : {5000.0, 4500.0, 5500.0, 4200.0, 5800.0})
            {
                for (int32 Step = 0; Step < 24; ++Step, ++i)
                {
                    const double Bearing = Step * 15.0;
                    const FVector Sample = Origin + FVector(1.0, 0.0, 0.0).RotateAngleAxis(Bearing, FVector::UpVector) * Radius;
                    FNavLocation Proj;
                    const bool bProjected = Nav->ProjectPointToNavigation(Sample, Proj, FVector(300.0, 300.0, 3000.0), Recast);
                    double Dist = 0.0;
                    double ViewDot = 0.0;
                    double PathLength = 0.0;
                    bool bCollide = false;
                    const TCHAR* PathText = TEXT("skipped");
                    const TCHAR* Reason = TEXT("no_projection");
                    if (bProjected)
                    {
                        Dist = FVector::Dist2D(Origin, Proj.Location);
                        ViewDot = FVector::DotProduct(ViewDir, (Proj.Location - Origin).GetSafeNormal2D());
                        // Path is evaluated for every projected candidate so max_complete_dist is diagnostic even when out of range.
                        const FPathFindingResult R = Nav->FindPathSync(FPathFindingQuery(this, *Recast, Proj.Location, PlayerNav.Location));
                        const bool bComplete = R.IsSuccessful() && R.Path.IsValid() && !R.Path->IsPartial();
                        PathText = bComplete ? TEXT("complete") : (R.IsPartial() ? TEXT("partial") : TEXT("none"));
                        if (bComplete)
                        {
                            PathLength = R.Path->GetLength();
                            MaxCompleteDist = FMath::Max(MaxCompleteDist, Dist);
                        }
                        if (Dist < 4000.0 || Dist > 6000.0)
                        {
                            Reason = TEXT("out_of_range");
                        }
                        else if (ViewDot >= 0.5)
                        {
                            Reason = TEXT("in_view");
                        }
                        else if (!bComplete)
                        {
                            Reason = R.IsPartial() ? TEXT("partial_path") : TEXT("no_path");
                        }
                        else
                        {
                            bCollide = World->OverlapBlockingTestByChannel(Proj.Location + FVector(0.0, 0.0, 160.0), FQuat::Identity,
                                ECC_Pawn, FCollisionShape::MakeCapsule(70.f, 140.f), Params);
                            Reason = TEXT("collision");
                            if (!bCollide)
                            {
                                Reason = SpawnPredatorAt(Proj.Location, Origin, ViewDot, PathLength) ? TEXT("ok") : TEXT("spawn_failed");
                            }
                        }
                    }
                    UE_LOG(LogTemp, Display, TEXT("PSL predator candidate i=%d r=%.0f bearing=%.0f xy=(%.0f,%.0f) projected=%d proj=(%.0f,%.0f,%.0f) dist=%.0f view_dot=%.2f path=%s collide=%d reason=%s"),
                        i, Radius, Bearing, Sample.X, Sample.Y, bProjected, Proj.Location.X, Proj.Location.Y, Proj.Location.Z,
                        Dist, ViewDot, PathText, bCollide, Reason);
                    if (FCString::Strcmp(Reason, TEXT("ok")) == 0)
                    {
                        return;
                    }
                }
            }
            UE_LOG(LogTemp, Warning, TEXT("PSL predator not spawned: candidates=%d max_complete_dist=%.0f (need 4000-6000, out of view, complete path, no collision)"),
                i, MaxCompleteDist);
            return;
        }
    }
    if (++PredatorSpawnAttempts < MaxPredatorSpawnAttempts)
    {
        FTimerHandle RetryHandle;
        GetWorldTimerManager().SetTimer(RetryHandle, this, &APSLGameMode::SpawnPredator, PredatorSpawnRetryDelay, false);
        return;
    }
    UE_LOG(LogTemp, Warning, TEXT("PSL predator not spawned after %d attempts: navdata=%d player=%d navempty=%d navbuilding=%d"),
        PredatorSpawnAttempts, Recast != nullptr, Character != nullptr, bNavEmpty, bBuilding);
}

bool APSLGameMode::SpawnPredatorAt(const FVector& NavPoint, const FVector& PlayerLocation, float ViewDot, float PathLength)
{
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
    const FVector Location = NavPoint + FVector(0.0, 0.0, 160.0);
    if (!GetWorld()->SpawnActor<APSLPredatorCharacter>(APSLPredatorCharacter::StaticClass(), FTransform(Location), Params))
    {
        return false;
    }
    UE_LOG(LogTemp, Display, TEXT("PSL predator spawned at %.0f,%.0f,%.0f dist=%.0f view_dot=%.2f path_len=%.0f"),
        Location.X, Location.Y, Location.Z, FVector::Dist2D(PlayerLocation, NavPoint), ViewDot, PathLength);
    return true;
}
