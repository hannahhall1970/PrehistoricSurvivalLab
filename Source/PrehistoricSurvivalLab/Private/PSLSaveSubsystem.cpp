#include "PSLSaveSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Crc.h"
#include "PSLCharacter.h"
#include "PSLDinoSpecies.h"
#include "PSLGameInstance.h"
#include "PSLStatsComponent.h"
#include "PSLSurvivalComponent.h"

#define LOCTEXT_NAMESPACE "PSL"

namespace
{
    template <typename T>
    uint32 Mix(uint32 Crc, const T& Value)
    {
        return FCrc::MemCrc32(&Value, sizeof(T), Crc);
    }

    uint32 MixVector(uint32 Crc, const FVector& V)
    {
        return Mix(Mix(Mix(Crc, V.X), V.Y), V.Z);
    }

    bool Reject(FText& OutError, const FText& Reason)
    {
        OutError = Reason;
        UE_LOG(LogTemp, Warning, TEXT("PSL save rejected: %s"), *Reason.ToString());
        return false;
    }
}

uint32 UPSLSaveSubsystem::ComputeChecksum(const FPSLSaveData& Data)
{
    uint32 Crc = 0;
    Crc = Mix(Crc, Data.SaveVersion);
    Crc = FCrc::StrCrc32(*Data.SpeciesId.ToString(), Crc);
    Crc = FCrc::StrCrc32(*Data.MapName.ToString(), Crc);
    Crc = MixVector(Crc, Data.PlayerTransform.GetLocation());
    const FQuat Rotation = Data.PlayerTransform.GetRotation();
    Crc = Mix(Mix(Mix(Mix(Crc, Rotation.X), Rotation.Y), Rotation.Z), Rotation.W);
    Crc = MixVector(Crc, Data.PlayerTransform.GetScale3D());
    Crc = Mix(Mix(Mix(Crc, Data.ControlRotation.Pitch), Data.ControlRotation.Yaw), Data.ControlRotation.Roll);
    Crc = Mix(Crc, Data.Health);
    Crc = Mix(Crc, Data.Stamina);
    Crc = Mix(Crc, Data.Hunger);
    Crc = Mix(Crc, Data.PreyKilled);
    Crc = Mix(Crc, Data.FoodEaten);
    Crc = Mix(Crc, Data.SurvivalSeconds);
    Crc = Mix(Crc, Data.SavedAtUtc.GetTicks());
    return Crc;
}

bool UPSLSaveSubsystem::Validate(FPSLSaveData& InOutData, FText& OutError)
{
    if (ComputeChecksum(InOutData) != InOutData.Checksum)
    {
        return Reject(OutError, LOCTEXT("SaveBadChecksum", "The save file is corrupted."));
    }

    switch (InOutData.SaveVersion)
    {
    case 1: // Current. Future versions add a migration case above this one.
        break;
    default:
        return Reject(OutError, InOutData.SaveVersion > UPSLSaveGame::CurrentVersion
            ? FText::Format(LOCTEXT("SaveTooNew", "The save was made by a newer version ({0})."), FText::AsNumber(InOutData.SaveVersion))
            : FText::Format(LOCTEXT("SaveTooOld", "The save version {0} is not supported."), FText::AsNumber(InOutData.SaveVersion)));
    }

    if (!UPSLDinoSpecies::IsKnownSpeciesId(InOutData.SpeciesId))
    {
        return Reject(OutError, LOCTEXT("SaveBadSpecies", "The save refers to an unknown species."));
    }
    if (InOutData.MapName != FName(UPSLGameInstance::ValleyMap))
    {
        return Reject(OutError, LOCTEXT("SaveBadMap", "The save belongs to a different map."));
    }

    const FVector Location = InOutData.PlayerTransform.GetLocation();
    if (InOutData.PlayerTransform.ContainsNaN() || InOutData.ControlRotation.ContainsNaN()
        || !FMath::IsFinite(Location.X) || !FMath::IsFinite(Location.Y) || !FMath::IsFinite(Location.Z)
        || FMath::Abs(Location.X) > 1000000.0 || FMath::Abs(Location.Y) > 1000000.0 || FMath::Abs(Location.Z) > 100000.0)
    {
        return Reject(OutError, LOCTEXT("SaveBadTransform", "The saved position is invalid."));
    }

    const UPSLDinoSpecies* Species = UPSLDinoSpecies::MakeBuiltin(InOutData.SpeciesId, GetTransientPackage());
    if (!FMath::IsFinite(InOutData.Health) || InOutData.Health <= 0.0f || InOutData.Health > Species->MaxHealth)
    {
        return Reject(OutError, LOCTEXT("SaveBadHealth", "The saved health is invalid."));
    }
    if (!FMath::IsFinite(InOutData.Stamina) || !FMath::IsFinite(InOutData.Hunger))
    {
        return Reject(OutError, LOCTEXT("SaveBadStats", "The saved stats are invalid."));
    }

    InOutData.Stamina = FMath::Clamp(InOutData.Stamina, 0.0f, Species->MaxStamina);
    InOutData.Hunger = FMath::Clamp(InOutData.Hunger, 0.0f, Species->MaxHunger);
    return true;
}

bool UPSLSaveSubsystem::CanCheckpoint(bool bDead, float Health, float MaxHealth, float Hunger)
{
    if (bDead || Health <= 0.0f)
    {
        return false;
    }
    return !(Hunger <= 0.0f && Health < LowHealthFraction * MaxHealth);
}

float UPSLSaveSubsystem::SecondsToLive(float Health, float Hunger, float HungerDrainPerSecond, float StarveDamagePerSecond)
{
    constexpr float Infinity = TNumericLimits<float>::Max();
    // Hunger already 0 costs no time; no drain with food left never starves; no damage never kills.
    const float HungerSeconds = Hunger <= 0.0f ? 0.0f : (HungerDrainPerSecond <= 0.0f ? Infinity : Hunger / HungerDrainPerSecond);
    const float HealthSeconds = StarveDamagePerSecond <= 0.0f ? Infinity : FMath::Max(Health, 0.0f) / StarveDamagePerSecond;
    return (HungerSeconds >= Infinity || HealthSeconds >= Infinity) ? Infinity : HungerSeconds + HealthSeconds;
}

FString UPSLSaveSubsystem::SlotNameFor(const FString& BaseSlot, bool bAuto, int32 Index)
{
    if (bAuto)
    {
        return FString::Printf(TEXT("%s_Auto%d"), *BaseSlot, Index);
    }
    return Index == 0 ? BaseSlot : BaseSlot + TEXT("_B");
}

namespace
{
    // Loads and validates one slot. A missing slot yields SaveMissing, a bad one the reason it was rejected.
    bool ReadValid(const FString& Slot, FPSLSaveData& OutData, FText& OutError)
    {
        if (!UGameplayStatics::DoesSaveGameExist(Slot, 0))
        {
            OutError = LOCTEXT("SaveMissing", "There is no saved game.");
            return false;
        }
        const UPSLSaveGame* Save = Cast<UPSLSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
        if (!Save)
        {
            return Reject(OutError, LOCTEXT("SaveUnreadable", "The save file could not be read."));
        }
        OutData = Save->Data;
        return UPSLSaveSubsystem::Validate(OutData, OutError);
    }
}

bool UPSLSaveSubsystem::WriteData(FPSLSaveData Data, EPSLSaveReason Reason)
{
    const bool bAuto = Reason == EPSLSaveReason::Autosave;

    // State of all four slots: the target is chosen inside this reason's pair, the stamp must beat every valid slot.
    FPSLSaveData Existing[2];
    bool bValid[2] = { false, false };
    int64 NewestTicks = 0;
    for (const bool bPairAuto : { false, true })
    {
        for (int32 Index = 0; Index < 2; ++Index)
        {
            FPSLSaveData Slot;
            FText Ignored;
            if (ReadValid(GetSlotName(bPairAuto, Index), Slot, Ignored))
            {
                NewestTicks = FMath::Max(NewestTicks, Slot.SavedAtUtc.GetTicks());
                if (bPairAuto == bAuto)
                {
                    Existing[Index] = Slot;
                    bValid[Index] = true;
                }
            }
        }
    }
    // Never over the newest valid member: an invalid or missing member first, else the older one.
    const int32 Target = !bValid[0] ? 0 : !bValid[1] ? 1 : (Existing[0].SavedAtUtc < Existing[1].SavedAtUtc ? 0 : 1);

    Data.SaveVersion = UPSLSaveGame::CurrentVersion;
    Data.SavedAtUtc = FDateTime::UtcNow();
    if (Data.SavedAtUtc.GetTicks() <= NewestTicks)
    {
        Data.SavedAtUtc = FDateTime(NewestTicks + 1); // keeps "newest" well defined even if the clock went backwards
    }
    Data.Checksum = ComputeChecksum(Data);

    UPSLSaveGame* Save = Cast<UPSLSaveGame>(UGameplayStatics::CreateSaveGameObject(UPSLSaveGame::StaticClass()));
    if (!Save)
    {
        return false;
    }
    Save->Data = Data;
    const FString TargetSlot = GetSlotName(bAuto, Target);
    if (!UGameplayStatics::SaveGameToSlot(Save, TargetSlot, 0))
    {
        UE_LOG(LogTemp, Warning, TEXT("PSL save write failed: %s"), *TargetSlot);
        return false;
    }

    // Read back what reached the disk. Integrity only (checksum equals what was written); semantic Validate is left to
    // the loader, which skips a bad slot, so a save we wrote ourselves is never "lost" here.
    const UPSLSaveGame* Loaded = Cast<UPSLSaveGame>(UGameplayStatics::LoadGameFromSlot(TargetSlot, 0));
    if (!Loaded || Loaded->Data.Checksum != Data.Checksum || ComputeChecksum(Loaded->Data) != Data.Checksum)
    {
        UE_LOG(LogTemp, Warning, TEXT("PSL save verify failed: %s"), *TargetSlot);
        return false;
    }
    return true;
}

bool UPSLSaveSubsystem::SaveCheckpoint(EPSLSaveReason Reason)
{
    UGameInstance* GameInstance = GetGameInstance();
    UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
    APlayerController* PC = GameInstance ? GameInstance->GetFirstLocalPlayerController(World) : nullptr;
    APSLCharacter* Character = PC ? Cast<APSLCharacter>(PC->GetPawn()) : nullptr;
    if (!Character || !Character->GetStatsComponent() || !Character->GetSurvivalComponent())
    {
        return false;
    }

    const UPSLStatsComponent* Stats = Character->GetStatsComponent();
    const UPSLSurvivalComponent* Survival = Character->GetSurvivalComponent();
    if (!CanCheckpoint(Stats->IsDead(), Stats->GetHealth(), Stats->GetMaxHealth(), Survival->GetHunger()))
    {
        UE_LOG(LogTemp, Log, TEXT("PSL checkpoint (%d) skipped: player dead or starving at low health."), static_cast<int32>(Reason));
        return false;
    }
    if (Reason == EPSLSaveReason::Autosave)
    {
        // An autosave that cannot be survived would become the Continue target and loop the player into the same death.
        const float Seconds = SecondsToLive(Stats->GetHealth(), Survival->GetHunger(), Survival->HungerDrainPerSecond, Survival->StarveDamagePerSecond);
        if (Seconds < MinAutosaveSecondsToLive)
        {
            UE_LOG(LogTemp, Log, TEXT("PSL checkpoint (%d) skipped: starves to death in %.1f s (< %.0f s)."),
                static_cast<int32>(Reason), Seconds, MinAutosaveSecondsToLive);
            return false;
        }
    }

    FPSLSaveData Data;
    Data.SpeciesId = Character->GetSpecies() ? Character->GetSpecies()->SpeciesId : UPSLDinoSpecies::TRexId;
    Data.MapName = FName(*UGameplayStatics::GetCurrentLevelName(World, true));
    Data.PlayerTransform = Character->GetActorTransform();
    Data.ControlRotation = PC->GetControlRotation();
    Data.Health = Stats->GetHealth();
    Data.Stamina = Survival->GetStamina();
    Data.Hunger = Survival->GetHunger();
    if (const UPSLGameInstance* Session = Cast<UPSLGameInstance>(GameInstance))
    {
        Data.PreyKilled = Session->PreyKilled;
        Data.FoodEaten = Session->FoodEaten;
        Data.SurvivalSeconds = Session->SurvivalSeconds;
    }
    return WriteData(Data, Reason);
}

bool UPSLSaveSubsystem::LoadAndValidate(FPSLSaveData& OutData, FText& OutError) const
{
    bool bFound = false;
    bool bAnyExists = false;
    FText LastError = LOCTEXT("SaveMissing", "There is no saved game.");
    for (const bool bAuto : { false, true })
    {
        for (int32 Index = 0; Index < 2; ++Index)
        {
            const FString Slot = GetSlotName(bAuto, Index);
            FPSLSaveData Candidate;
            FText Error;
            if (ReadValid(Slot, Candidate, Error))
            {
                if (!bFound || Candidate.SavedAtUtc > OutData.SavedAtUtc)
                {
                    OutData = Candidate;
                    bFound = true;
                }
            }
            else if (UGameplayStatics::DoesSaveGameExist(Slot, 0))
            {
                bAnyExists = true;
                LastError = Error;
                UE_LOG(LogTemp, Warning, TEXT("PSL save slot %s skipped: %s"), *Slot, *Error.ToString());
            }
        }
    }
    if (!bFound)
    {
        OutError = bAnyExists ? LastError : LOCTEXT("SaveMissing", "There is no saved game.");
    }
    return bFound;
}

bool UPSLSaveSubsystem::HasValidSave() const
{
    FPSLSaveData Data;
    FText Error;
    return LoadAndValidate(Data, Error);
}

void UPSLSaveSubsystem::DeleteAllSlots(const FString& BaseSlot)
{
    for (const bool bAuto : { false, true })
    {
        for (int32 Index = 0; Index < 2; ++Index)
        {
            UGameplayStatics::DeleteGameInSlot(SlotNameFor(BaseSlot, bAuto, Index), 0);
        }
    }
}

void UPSLSaveSubsystem::DeleteSave()
{
    DeleteAllSlots(SlotName);
}

#undef LOCTEXT_NAMESPACE
