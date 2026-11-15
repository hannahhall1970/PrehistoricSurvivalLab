#pragma once

#include "Modules/ModuleManager.h"

class FPrehistoricSurvivalLabModule : public FDefaultGameModuleImpl
{
public:
    // Registers the PSLSpecies string table. Load order is not guaranteed, but string-table texts resolve lazily at display time.
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
