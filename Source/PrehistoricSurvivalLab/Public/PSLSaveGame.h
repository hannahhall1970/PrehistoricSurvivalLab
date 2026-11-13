#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "PSLSaveGame.generated.h"

/** One checkpoint. Prey are not saved; they respawn from the map. */
USTRUCT(BlueprintType)
struct PREHISTORICSURVIVALLAB_API FPSLSaveData
{
    GENERATED_BODY()

    UPROPERTY() int32 SaveVersion = 1;
    UPROPERTY() FName SpeciesId;
    UPROPERTY() FName MapName;
    UPROPERTY() FTransform PlayerTransform;
    UPROPERTY() FRotator ControlRotation = FRotator::ZeroRotator;
    UPROPERTY() float Health = 0.0f;
    UPROPERTY() float Stamina = 0.0f;
    UPROPERTY() float Hunger = 0.0f;
    UPROPERTY() int32 PreyKilled = 0;
    UPROPERTY() float FoodEaten = 0.0f;
    UPROPERTY() float SurvivalSeconds = 0.0f;
    UPROPERTY() FDateTime SavedAtUtc;
    // CRC32 over every field above; see UPSLSaveSubsystem::ComputeChecksum.
    UPROPERTY() uint32 Checksum = 0;
};

UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    static constexpr int32 CurrentVersion = 1;

    UPROPERTY()
    FPSLSaveData Data;
};
