#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "PSLGameInstance.generated.h"

UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    static constexpr const TCHAR* ValleyMap = TEXT("L_LostValley");
    static constexpr const TCHAR* MenuMap = TEXT("L_MainMenu");

    // Selected species; defaults to TRex.
    FName SpeciesId;
    // Set by ContinueGame(); the GameMode consumes it (applies the save, then clears it).
    bool bContinueRequested = false;

    // Run stats for the Game Over screen and the save file.
    float SurvivalSeconds = 0.0f;
    int32 PreyKilled = 0;
    float FoodEaten = 0.0f;

    UPSLGameInstance();

    void ResetRunStats();

    // Resets stats, selects Species (unknown ids fall back to TRex), opens the valley.
    void StartNewGame(FName InSpeciesId);
    // Opens the valley with bContinueRequested; false (nothing happens) when there is no valid save.
    bool ContinueGame();
    void ReturnToMenu();
    // Calls OnQuitRequested if bound (tests), else FPlatformMisc::RequestExit(false).
    void QuitGame();

    TFunction<void()> OnQuitRequested;

    // D3: audio mix and culture are per-world / reset on travel, so re-apply the saved settings on every map load.
    virtual void OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld) override;
};
