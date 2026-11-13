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

bool UPSLSaveSubsystem::WriteData(FPSLSaveData Data)
{
    Data.SaveVersion = UPSLSaveGame::CurrentVersion;
    Data.SavedAtUtc = FDateTime::UtcNow();
    Data.Checksum = ComputeChecksum(Data);

    UPSLSaveGame* Save = Cast<UPSLSaveGame>(UGameplayStatics::CreateSaveGameObject(UPSLSaveGame::StaticClass()));
    if (!Save)
    {
        return false;
    }
    Save->Data = Data;
    return UGameplayStatics::SaveGameToSlot(Save, SlotName, 0);
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
    return WriteData(Data);
}

bool UPSLSaveSubsystem::LoadAndValidate(FPSLSaveData& OutData, FText& OutError) const
{
    if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
    {
        OutError = LOCTEXT("SaveMissing", "There is no saved game.");
        return false;
    }
    const UPSLSaveGame* Save = Cast<UPSLSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
    if (!Save)
    {
        return Reject(OutError, LOCTEXT("SaveUnreadable", "The save file could not be read."));
    }
    OutData = Save->Data;
    return Validate(OutData, OutError);
}

bool UPSLSaveSubsystem::HasValidSave() const
{
    FPSLSaveData Data;
    FText Error;
    return LoadAndValidate(Data, Error);
}

void UPSLSaveSubsystem::DeleteSave()
{
    UGameplayStatics::DeleteGameInSlot(SlotName, 0);
}

#undef LOCTEXT_NAMESPACE
