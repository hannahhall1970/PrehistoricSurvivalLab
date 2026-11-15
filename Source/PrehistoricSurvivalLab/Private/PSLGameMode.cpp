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
#include "PSLAudio.h"
#include "PSLCharacter.h"
#include "PSLDinoSpecies.h"
#include "PSLGameInstance.h"
#include "PSLHUD.h"
#include "PSLPreyCharacter.h"
#include "PSLSaveSubsystem.h"
#include "PSLStatsComponent.h"
#include "PSLSurvivalComponent.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

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
        Music = UGameplayStatics::SpawnSound2D(this, S, 1.f, 1.f, 0.f, nullptr, false, false);
    }
    if (USoundBase* S = PSLAudio::Load(PSLAudio::Wind))
    {
        Ambience = UGameplayStatics::SpawnSound2D(this, S, 1.f, 1.f, 0.f, nullptr, false, false);
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
