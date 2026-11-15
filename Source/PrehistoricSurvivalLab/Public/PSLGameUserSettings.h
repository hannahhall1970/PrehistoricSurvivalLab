#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "PSLGameUserSettings.generated.h"

class USoundClass;
class USoundMix;
class UWorld;

/** Persisted game settings: graphics via UGameUserSettings, plus audio mix, mouse look and UI culture. */
UCLASS(config=GameUserSettings, configdonotcheckdefaults)
class PREHISTORICSURVIVALLAB_API UPSLGameUserSettings : public UGameUserSettings
{
    GENERATED_BODY()

public:
    UPSLGameUserSettings();

    // GEngine's settings object when it is a UPSLGameUserSettings, else null.
    static UPSLGameUserSettings* Get();

    static constexpr float MinSensitivity = 0.2f;
    static constexpr float MaxSensitivity = 3.0f;

    UPROPERTY(Config) float MouseSensitivity = 1.0f;
    UPROPERTY(Config) bool bInvertY = false;
    // 0..1.
    UPROPERTY(Config) float MasterVolume = 1.0f;
    UPROPERTY(Config) float MusicVolume = 0.8f;
    UPROPERTY(Config) float SfxVolume = 1.0f;
    // "en" or "vi"; empty means the OS default (vi* -> vi, else en).
    UPROPERTY(Config) FString CultureName;

    // Authored by the content task; soft so a missing asset only skips the audible part.
    UPROPERTY(EditAnywhere, Category="Audio") TSoftObjectPtr<USoundClass> SC_Master;
    UPROPERTY(EditAnywhere, Category="Audio") TSoftObjectPtr<USoundClass> SC_Music;
    UPROPERTY(EditAnywhere, Category="Audio") TSoftObjectPtr<USoundClass> SC_SFX;
    UPROPERTY(EditAnywhere, Category="Audio") TSoftObjectPtr<USoundMix> SM_PSL_Volume;

    void SetMouseSensitivity(float Value) { MouseSensitivity = FMath::Clamp(Value, MinSensitivity, MaxSensitivity); }
    void SetMasterVolume(float Value) { MasterVolume = FMath::Clamp(Value, 0.0f, 1.0f); }
    void SetMusicVolume(float Value) { MusicVolume = FMath::Clamp(Value, 0.0f, 1.0f); }
    void SetSfxVolume(float Value) { SfxVolume = FMath::Clamp(Value, 0.0f, 1.0f); }

    // 0 = 30, 1 = 60, 2 = 120, 3 = unlimited.
    int32 GetFrameRateChoice() const;
    void SetFrameRateChoice(int32 Choice);

    // Resolves the SoundMix and the Master/Music/SFX classes and records each applied value first (the three totals and
    // per class via GetAppliedClassOverride), so they are readable without a world or an audio device. Only with a world
    // are the SoundMix class overrides set and the mix set as the world audio device's base mix (idempotent).
    void ApplyAudio(UWorld* World);
    float GetAppliedMasterOverride() const { return AppliedMaster; }
    float GetAppliedMusicOverride() const { return AppliedMusic; }
    float GetAppliedSfxOverride() const { return AppliedSfx; }
    // Value recorded for Class by the last ApplyAudio, or -1 when that class was not resolved/applied.
    float GetAppliedClassOverride(const USoundClass* Class) const;

    // Switches the current culture to ResolveCultureName(CultureName) and saves it to config.
    void ApplyCulture();

    // Empty or unknown -> OS culture vi* ? "vi" : "en".
    static FString ResolveCultureName(const FString& Configured);

    virtual void ApplySettings(bool bCheckForCommandLineOverrides) override;
    virtual void LoadSettings(bool bForceReload = false) override;

private:
    float AppliedMaster = -1.0f;
    float AppliedMusic = -1.0f;
    float AppliedSfx = -1.0f;
    // Per resolved sound class: the volume ApplyAudio set (or would set, without a world). Keys are held for GC.
    UPROPERTY(Transient) TMap<TObjectPtr<USoundClass>, float> AppliedClassOverrides;
};
