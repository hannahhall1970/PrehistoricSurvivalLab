#include "PSLGameInstance.h"

#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "PSLDinoSpecies.h"
#include "PSLGameUserSettings.h"
#include "PSLSaveSubsystem.h"

UPSLGameInstance::UPSLGameInstance()
    : SpeciesId(UPSLDinoSpecies::TRexId)
{
}

void UPSLGameInstance::ResetRunStats()
{
    SurvivalSeconds = 0.0f;
    PreyKilled = 0;
    FoodEaten = 0.0f;
}

void UPSLGameInstance::StartNewGame(FName InSpeciesId)
{
    SpeciesId = UPSLDinoSpecies::IsKnownSpeciesId(InSpeciesId) ? InSpeciesId : UPSLDinoSpecies::TRexId;
    bContinueRequested = false;
    ResetRunStats();
    UGameplayStatics::OpenLevel(this, FName(ValleyMap));
}

bool UPSLGameInstance::ContinueGame()
{
    const UPSLSaveSubsystem* Saves = GetSubsystem<UPSLSaveSubsystem>();
    FPSLSaveData Data;
    FText Error;
    if (!Saves || !Saves->LoadAndValidate(Data, Error))
    {
        return false;
    }

    SpeciesId = Data.SpeciesId;
    PreyKilled = Data.PreyKilled;
    FoodEaten = Data.FoodEaten;
    SurvivalSeconds = Data.SurvivalSeconds;
    bContinueRequested = true;
    UGameplayStatics::OpenLevel(this, FName(ValleyMap));
    return true;
}

void UPSLGameInstance::ReturnToMenu()
{
    bContinueRequested = false;
    UGameplayStatics::OpenLevel(this, FName(MenuMap));
}

void UPSLGameInstance::QuitGame()
{
    if (OnQuitRequested)
    {
        OnQuitRequested();
        return;
    }
    FPlatformMisc::RequestExit(false);
}

void UPSLGameInstance::OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld)
{
    Super::OnWorldChanged(OldWorld, NewWorld);
    if (NewWorld)
    {
        if (UPSLGameUserSettings* Settings = UPSLGameUserSettings::Get())
        {
            Settings->ApplyCulture();
            Settings->ApplyAudio(NewWorld);
        }
    }
}
