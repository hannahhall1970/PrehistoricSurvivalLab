#include "PrehistoricSurvivalLab.h"

#include "PSLDinoSpecies.h"

void FPrehistoricSurvivalLabModule::StartupModule()
{
    UPSLDinoSpecies::RegisterTextTable();
}

void FPrehistoricSurvivalLabModule::ShutdownModule()
{
    UPSLDinoSpecies::UnregisterTextTable();
}

IMPLEMENT_PRIMARY_GAME_MODULE(FPrehistoricSurvivalLabModule, PrehistoricSurvivalLab, "PrehistoricSurvivalLab");
