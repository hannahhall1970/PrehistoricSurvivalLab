#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/TimerHandle.h"
#include "PSLGameMode.generated.h"

class APSLCharacter;
class UAudioComponent;
class APSLPreyCharacter;
class UUserWidget;

UENUM(BlueprintType)
enum class EPSLGameState : uint8
{
    Playing,
    Paused,
    GameOver
};

/**
 * Session flow: Playing / Paused / GameOver, species + continue restore on the player pawn, 60 s autosave,
 * run stats on UPSLGameInstance, and the prey respawner (keeps the living-prey count at the number placed in the map).
 */
UCLASS()
class PREHISTORICSURVIVALLAB_API APSLGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    static constexpr float GameOverDelay = 2.0f;
    static constexpr float AutosaveInterval = 60.0f;
    static constexpr float SessionTickInterval = 1.0f;
    static constexpr float PreyRespawnDelay = 45.0f;
    static constexpr float PreyRespawnRetryDelay = 10.0f;
    static constexpr float PreyRespawnMinDistance = 3000.0f;
    static constexpr float PreyRespawnMaxDistance = 6000.0f;

    APSLGameMode();

    UFUNCTION(BlueprintPure, Category="Flow")
    EPSLGameState GetGameState() const { return State; }

    // Playing -> Paused: SetGamePaused(true), UI-only input. No-op in other states.
    UFUNCTION(BlueprintCallable, Category="Flow")
    void PauseGame();

    // Paused -> Playing: SetGamePaused(false), game-only input. No-op in other states.
    UFUNCTION(BlueprintCallable, Category="Flow")
    void ResumeGame();

    // Pause <-> Resume; bound to the pause input action. No-op in GameOver.
    void TogglePause();

    // Retry = a new game with the same species (via the GameInstance).
    UFUNCTION(BlueprintCallable, Category="Flow")
    void Retry();

    // Living prey this world should keep alive; set at BeginPlay from the placed prey.
    int32 GetPlacedPreyCount() const { return PlacedPreyCount; }

    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void ApplySessionToPawn(APlayerController* PC);
    void RestoreFromSave(APlayerController* PC);
    void SetInputForState();
    void EnterGameOver();
    void TickSession();
    void OnCarcassDestroyed();
    void HandlePreyKilled(APSLPreyCharacter* Prey, AActor* Killer);
    int32 CountLivingPrey() const;
    void OpenScreen(TSubclassOf<UUserWidget> WidgetClass);
    void CloseScreen();

    UFUNCTION()
    void SpawnReplacementPrey();

    // L_LostValley only (cvar psl.Predator.Spawn): one predator on a navmesh point 4000-6000 uu from the player.
    // The valley ships an empty navmesh, so SpawnPredator rebuilds it once, waits for the build, then runs one
    // deterministic candidate pass (in range, out of view, complete path, no collision). No fallback spawn.
    void SpawnPredator();
    bool SpawnPredatorAt(const FVector& NavPoint, const FVector& PlayerLocation, float ViewDot, float PathLength);
    static constexpr int32 MaxPredatorSpawnAttempts = 240; // 60 s at 0.25 s; a full valley rebuild takes seconds
    static constexpr float PredatorSpawnRetryDelay = 0.25f;
    int32 PredatorSpawnAttempts = 0;
    bool bPredatorNavRebuildRequested = false;

    UFUNCTION()
    void HandlePlayerDeath();

    UFUNCTION()
    void HandleAteFood(float Amount);

    EPSLGameState State = EPSLGameState::Playing;
    int32 PlacedPreyCount = 0;
    float AutosaveAccum = 0.0f;
    double LastSessionTime = 0.0;
    FTimerHandle GameOverTimer;
    FTimerHandle SessionTimer;
    FDelegateHandle PreyKilledHandle;
    FDelegateHandle CarcassHandle;
    TWeakObjectPtr<APSLCharacter> Player;
    // Pause / Game Over screens. Created on entry to the state, removed on leaving it or on EndPlay.
    UPROPERTY(Transient)
    TObjectPtr<UUserWidget> ScreenWidget;

    UPROPERTY(Transient)
    TObjectPtr<UAudioComponent> Music;

    UPROPERTY(Transient)
    TObjectPtr<UAudioComponent> Ambience;
};
