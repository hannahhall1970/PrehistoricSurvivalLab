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

    // Playback gain for the music loops. The masters are quiet (peak MUS_Menu -15.1 dBFS / -24.3 LUFS,
    // MUS_Valley -21.3 dBFS / -31.2 LUFS) next to the SFX (peaks -4.7..-12.5 dBFS). These bring both
    // to about -18..-19 LUFS with peaks near -9 dBFS; engine clamps a source at MAX_VOLUME 4.0.
    inline constexpr float MenuMusicGain = 2.0f;
    inline constexpr float ValleyMusicGain = 4.0f;

    // Missing assets return nullptr and are skipped by callers.
    PREHISTORICSURVIVALLAB_API USoundBase* Load(const TCHAR* Path);
}
