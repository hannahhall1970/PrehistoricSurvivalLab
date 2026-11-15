#include "PSLGameUserSettings.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetInternationalizationLibrary.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"

UPSLGameUserSettings::UPSLGameUserSettings()
    : SC_Master(FSoftObjectPath(TEXT("/Game/Audio/SC_Master.SC_Master")))
    , SC_Music(FSoftObjectPath(TEXT("/Game/Audio/SC_Music.SC_Music")))
    , SC_SFX(FSoftObjectPath(TEXT("/Game/Audio/SC_SFX.SC_SFX")))
    , SM_PSL_Volume(FSoftObjectPath(TEXT("/Game/Audio/SM_PSL_Volume.SM_PSL_Volume")))
{
}

UPSLGameUserSettings* UPSLGameUserSettings::Get()
{
    return GEngine ? Cast<UPSLGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

int32 UPSLGameUserSettings::GetFrameRateChoice() const
{
    const float Limit = GetFrameRateLimit();
    if (Limit <= 0.0f) { return 3; }
    if (Limit <= 30.0f) { return 0; }
    if (Limit <= 60.0f) { return 1; }
    return 2;
}

void UPSLGameUserSettings::SetFrameRateChoice(int32 Choice)
{
    static const float Limits[] = {30.0f, 60.0f, 120.0f, 0.0f};
    SetFrameRateLimit(Limits[FMath::Clamp(Choice, 0, 3)]);
}

void UPSLGameUserSettings::ApplyAudio(UWorld* World)
{
    AppliedMaster = MasterVolume;
    AppliedMusic = MusicVolume;
    AppliedSfx = SfxVolume;
    AppliedClassOverrides.Reset();

    USoundMix* Mix = SM_PSL_Volume.LoadSynchronous();
    if (!Mix)
    {
        return;
    }
    const TPair<TSoftObjectPtr<USoundClass>*, float> Entries[] = {
        {&SC_Master, MasterVolume}, {&SC_Music, MusicVolume}, {&SC_SFX, SfxVolume}};
    TArray<TPair<USoundClass*, float>, TInlineAllocator<3>> Resolved;
    for (const TPair<TSoftObjectPtr<USoundClass>*, float>& Entry : Entries)
    {
        if (USoundClass* Class = Entry.Key->LoadSynchronous())
        {
            AppliedClassOverrides.Add(Class, Entry.Value);
            Resolved.Emplace(Class, Entry.Value);
        }
    }
    if (!World)
    {
        return;
    }
    for (const TPair<USoundClass*, float>& Entry : Resolved)
    {
        UGameplayStatics::SetSoundMixClassOverride(World, Mix, Entry.Key, Entry.Value, 1.0f, 0.1f, true);
    }
    UGameplayStatics::SetBaseSoundMix(World, Mix);
}

float UPSLGameUserSettings::GetAppliedClassOverride(const USoundClass* Class) const
{
    const float* Value = AppliedClassOverrides.Find(const_cast<USoundClass*>(Class));
    return Value ? *Value : -1.0f;
}

FString UPSLGameUserSettings::ResolveCultureName(const FString& Configured)
{
    if (Configured == TEXT("en") || Configured == TEXT("vi"))
    {
        return Configured;
    }
    // The OS culture at startup is the default for an unset or unknown value.
    const FString Os = FInternationalization::Get().GetDefaultCulture()->GetName();
    return Os.StartsWith(TEXT("vi")) ? TEXT("vi") : TEXT("en");
}

void UPSLGameUserSettings::ApplyCulture()
{
    UKismetInternationalizationLibrary::SetCurrentCulture(ResolveCultureName(CultureName), true);
}

void UPSLGameUserSettings::ApplySettings(bool bCheckForCommandLineOverrides)
{
    Super::ApplySettings(bCheckForCommandLineOverrides);
    ApplyCulture();
    ApplyAudio(GEngine ? GEngine->GetCurrentPlayWorld() : nullptr);
}

void UPSLGameUserSettings::LoadSettings(bool bForceReload)
{
    Super::LoadSettings(bForceReload);
    MouseSensitivity = FMath::Clamp(MouseSensitivity, MinSensitivity, MaxSensitivity);
    MasterVolume = FMath::Clamp(MasterVolume, 0.0f, 1.0f);
    MusicVolume = FMath::Clamp(MusicVolume, 0.0f, 1.0f);
    SfxVolume = FMath::Clamp(SfxVolume, 0.0f, 1.0f);
}
