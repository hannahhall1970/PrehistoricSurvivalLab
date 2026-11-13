#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PSLSaveGame.h"
#include "PSLSaveSubsystem.generated.h"

UENUM()
enum class EPSLSaveReason : uint8
{
    Autosave,
    Pause,
    SaveAndExit
};

/** One-slot checkpoint storage with validation. Tests inject SlotName so the user save is untouched. */
UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLSaveSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    static constexpr const TCHAR* DefaultSlotName = TEXT("PSL_Slot0");
    static constexpr float LowHealthFraction = 0.1f;

    FString SlotName = DefaultSlotName;

    // CRC32 over every FPSLSaveData field except Checksum itself.
    static uint32 ComputeChecksum(const FPSLSaveData& Data);

    // Pure. Rejects bad version, unknown species, wrong map, non-finite or out-of-bounds transform,
    // Health <= 0 or > max, and checksum mismatch (checked first). Then clamps Stamina/Hunger to [0, max].
    // OutError is a localized reason.
    static bool Validate(FPSLSaveData& InOutData, FText& OutError);

    // Pure gate: never while dead, Health <= 0, or starving (Hunger <= 0) with Health < LowHealthFraction * MaxHealth.
    static bool CanCheckpoint(bool bDead, float Health, float MaxHealth, float Hunger);

    // Stamps version, SavedAtUtc and checksum, then writes the slot. Returns false if the write failed.
    bool WriteData(FPSLSaveData Data);

    // Snapshots the player pawn (species, transform, control rotation, stats, GameInstance run stats) and writes it.
    // Returns false (slot untouched) when there is no live pawn or CanCheckpoint() refuses.
    bool SaveCheckpoint(EPSLSaveReason Reason);

    bool HasValidSave() const;

    bool LoadAndValidate(FPSLSaveData& OutData, FText& OutError) const;

    // Test use only.
    void DeleteSave();
};
