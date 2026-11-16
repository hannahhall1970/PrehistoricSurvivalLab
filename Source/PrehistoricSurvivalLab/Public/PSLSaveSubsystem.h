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

/**
 * Checkpoint storage with validation. SlotName is the injectable BASE name (tests inject it so the user save is untouched).
 * Two ping-pong pairs: manual (Pause, SaveAndExit) = <base> and <base>_B; autosave = <base>_Auto0 and <base>_Auto1.
 * A write never targets the newest valid member of its pair, and is read back before it counts. Load takes the newest valid of all four.
 */
UCLASS()
class PREHISTORICSURVIVALLAB_API UPSLSaveSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    static constexpr const TCHAR* DefaultSlotName = TEXT("PSL_Slot0");
    static constexpr float LowHealthFraction = 0.1f;

    // Autosave is refused when the pawn would starve to death sooner than this (see SecondsToLive).
    static constexpr float MinAutosaveSecondsToLive = 60.0f;

    FString SlotName = DefaultSlotName;

    // Slot of pair member Index (0/1): manual <base>, <base>_B; auto <base>_Auto0, <base>_Auto1.
    static FString SlotNameFor(const FString& BaseSlot, bool bAuto, int32 Index);
    FString GetSlotName(bool bAuto, int32 Index) const { return SlotNameFor(SlotName, bAuto, Index); }

    // Deletes all four slots of a base name; usable without a subsystem (test cleanup after the PIE GameInstance is gone).
    static void DeleteAllSlots(const FString& BaseSlot);

    // CRC32 over every FPSLSaveData field except Checksum itself.
    static uint32 ComputeChecksum(const FPSLSaveData& Data);

    // Pure. Rejects bad version, unknown species, wrong map, non-finite or out-of-bounds transform,
    // Health <= 0 or > max, and checksum mismatch (checked first). Then clamps Stamina/Hunger to [0, max].
    // OutError is a localized reason.
    static bool Validate(FPSLSaveData& InOutData, FText& OutError);

    // Pure gate: never while dead, Health <= 0, or starving (Hunger <= 0) with Health < LowHealthFraction * MaxHealth.
    static bool CanCheckpoint(bool bDead, float Health, float MaxHealth, float Hunger);

    // Pure: idle seconds until starvation kills = Hunger / drain + Health / starveDamage. A non-positive rate is infinite
    // (hunger already 0 counts as 0 s). Idle drain on purpose: sprinting only shortens it and the autosave moment is arbitrary.
    static float SecondsToLive(float Health, float Hunger, float HungerDrainPerSecond, float StarveDamagePerSecond);

    // Stamps version, SavedAtUtc (strictly newer than every valid slot) and checksum, then writes the older/invalid member of the
    // pair for Reason (Autosave -> auto pair, else manual pair) and reads it back. Returns false if the write or the read-back
    // (checksum equal to what was written) failed; the other member is never touched.
    bool WriteData(FPSLSaveData Data, EPSLSaveReason Reason = EPSLSaveReason::Pause);

    // Snapshots the player pawn (species, transform, control rotation, stats, GameInstance run stats) and writes it.
    // Returns false (slots untouched) when there is no live pawn, CanCheckpoint() refuses, or (Autosave only) SecondsToLive is below
    // MinAutosaveSecondsToLive.
    bool SaveCheckpoint(EPSLSaveReason Reason);

    bool HasValidSave() const;

    // Newest valid save (by SavedAtUtc) across all four slots; invalid ones are skipped and logged.
    bool LoadAndValidate(FPSLSaveData& OutData, FText& OutError) const;

    // Test use only. Deletes all four slots.
    void DeleteSave();
};
