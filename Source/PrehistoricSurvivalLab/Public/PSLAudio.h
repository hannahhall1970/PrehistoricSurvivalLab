#pragma once

#include "CoreMinimal.h"

class USoundBase;

namespace PSLAudio
{
    inline constexpr const TCHAR* MenuMusic = TEXT("/Game/Audio/MUS_Menu.MUS_Menu");
    inline constexpr const TCHAR* ValleyMusic = TEXT("/Game/Audio/MUS_Valley.MUS_Valley");
    inline constexpr const TCHAR* Wind = TEXT("/Game/Audio/AMB_Wind.AMB_Wind");
    inline constexpr const TCHAR* Bite = TEXT("/Game/Audio/SFX_Bite.SFX_Bite");
    inline constexpr const TCHAR* GameOver = TEXT("/Game/Audio/SFX_GameOver.SFX_GameOver");
    inline constexpr const TCHAR* Starving = TEXT("/Game/Audio/SFX_Starving.SFX_Starving");

    // Missing assets return nullptr and are skipped by callers.
    PREHISTORICSURVIVALLAB_API USoundBase* Load(const TCHAR* Path);
}
