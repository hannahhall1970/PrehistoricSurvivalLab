#include "Misc/AutomationTest.h"
#include "PSLCharacter.h"
#include "PSLAudioBuildLibrary.h"
#include "Sound/SoundClass.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLAudioBuildLibraryTest, "PSL.Audio.BuildLibrary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLAudioBuildLibraryTest::RunTest(const FString& Parameters)
{
    const auto Make = []() { return NewObject<USoundClass>(GetTransientPackage(), NAME_None, RF_Transient); };
    USoundClass* Master = Make();
    USoundClass* Music = Make();
    USoundClass* Sfx = Make();
    USoundClass* Other = Make();

    // Both sides of the link must agree: ParentClass on the child, exactly one entry in the parent's ChildClasses.
    const auto ExpectParent = [this](const TCHAR* What, USoundClass* Child, USoundClass* Parent)
    {
        TestTrue(FString::Printf(TEXT("%s: parent"), What), Child->ParentClass == Parent);
        if (Parent)
        {
            TestEqual(FString::Printf(TEXT("%s: listed once"), What), Parent->ChildClasses.FilterByPredicate([Child](const USoundClass* C) { return C == Child; }).Num(), 1);
        }
    };

    TestTrue(TEXT("music under master"), UPSLAudioBuildLibrary::SetSoundClassParent(Music, Master));
    TestTrue(TEXT("sfx under master"), UPSLAudioBuildLibrary::SetSoundClassParent(Sfx, Master));
    TestTrue(TEXT("idempotent"), UPSLAudioBuildLibrary::SetSoundClassParent(Music, Master));
    ExpectParent(TEXT("music"), Music, Master);
    ExpectParent(TEXT("sfx"), Sfx, Master);
    TestEqual(TEXT("master children"), Master->ChildClasses.Num(), 2);

    // Reparenting removes the child from the old parent.
    TestTrue(TEXT("music under other"), UPSLAudioBuildLibrary::SetSoundClassParent(Music, Other));
    ExpectParent(TEXT("moved"), Music, Other);
    TestFalse(TEXT("old parent dropped child"), Master->ChildClasses.Contains(Music));
    TestTrue(TEXT("music back under master"), UPSLAudioBuildLibrary::SetSoundClassParent(Music, Master));
    TestFalse(TEXT("other dropped child"), Other->ChildClasses.Contains(Music));

    // Rejected calls log one error each and leave the hierarchy untouched.
    AddExpectedError(TEXT("PSL SetSoundClassParent: null child"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("null child"), UPSLAudioBuildLibrary::SetSoundClassParent(nullptr, Master));
    AddExpectedError(TEXT("PSL SetSoundClassParent: class cannot parent itself"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("self parent"), UPSLAudioBuildLibrary::SetSoundClassParent(Master, Master));
    AddExpectedError(TEXT("PSL SetSoundClassParent: cycle"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("cycle"), UPSLAudioBuildLibrary::SetSoundClassParent(Master, Music));
    TestTrue(TEXT("master still root"), Master->ParentClass == nullptr);
    TestFalse(TEXT("music has no children"), Music->ChildClasses.Contains(Master));
    ExpectParent(TEXT("after rejects"), Music, Master);
    TestEqual(TEXT("master children after rejects"), Master->ChildClasses.Num(), 2);

    // nullptr parent detaches to root.
    TestTrue(TEXT("detach sfx"), UPSLAudioBuildLibrary::SetSoundClassParent(Sfx, nullptr));
    TestTrue(TEXT("sfx root"), Sfx->ParentClass == nullptr);
    TestFalse(TEXT("master dropped sfx"), Master->ChildClasses.Contains(Sfx));
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AudioDevice.h"
#include "AudioThread.h"
#include "PSLAudio.h"
#include "PSLGameUserSettings.h"
#include "PSLPieTestSupport.h"
#include "PSLUiStyle.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#include "UObject/StrongObjectPtr.h"

namespace PSLAudioTest
{
using namespace PSLPie;

constexpr const TCHAR* MixPath = TEXT("/Game/Audio/SM_PSL_Volume.SM_PSL_Volume");

struct FWaveSpec
{
    const TCHAR* Name;
    bool bMusic;
    bool bLoop;
    int32 Channels;
    int32 Frames;
};

// Channels and frame counts of the sha256-pinned SourceArt/Audio WAVs (all 44.1 kHz).
constexpr FWaveSpec Waves[] = {
    {TEXT("MUS_Menu"), true, true, 2, 1794870},
    {TEXT("MUS_Valley"), true, true, 2, 2156490},
    {TEXT("AMB_Wind"), false, true, 1, 1323000},
    {TEXT("SFX_UI_Click"), false, false, 1, 5292},
    {TEXT("SFX_UI_Hover"), false, false, 1, 3528},
    {TEXT("SFX_Bite"), false, false, 1, 15435},
    {TEXT("SFX_Eat"), false, false, 1, 26460},
    {TEXT("SFX_GameOver"), false, false, 1, 123480},
    {TEXT("SFX_Starving"), false, false, 1, 22050},
};

template <typename T>
T* LoadAudio(const TCHAR* Name)
{
    return LoadObject<T>(nullptr, *FString::Printf(TEXT("/Game/Audio/%s.%s"), Name, Name));
}

struct FAudioRefs
{
    TStrongObjectPtr<USoundMix> Mix;
    TStrongObjectPtr<USoundClass> Master;
    TStrongObjectPtr<USoundClass> Music;
    TStrongObjectPtr<USoundClass> Sfx;
};

struct FDeviceVolumes
{
    bool bRead = false;
    bool bBaseMix = false;
    float Master = -1.0f;
    float Music = -1.0f;
    float Sfx = -1.0f;
};

// Sound class state lives on the audio thread (GetSoundClassCurrentProperties checks IsInAudioThread):
// run the read there and block on a fence, so nothing captured outlives this call.
FDeviceVolumes ReadDeviceVolumes(UWorld* World, const FAudioRefs& Refs)
{
    FDeviceVolumes Out;
    FAudioDevice* Device = World ? World->GetAudioDeviceRaw() : nullptr;
    if (!Device)
    {
        return Out;
    }
    FDeviceVolumes* OutPtr = &Out;
    USoundMix* Mix = Refs.Mix.Get();
    USoundClass* Classes[] = {Refs.Master.Get(), Refs.Music.Get(), Refs.Sfx.Get()};
    FAudioThread::RunCommandOnAudioThread([Device, Mix, Classes, OutPtr]()
    {
        const auto Volume = [Device](USoundClass* Class)
        {
            const FSoundClassProperties* Properties = Device->GetSoundClassCurrentProperties(Class);
            return Properties ? Properties->Volume : -1.0f;
        };
        OutPtr->Master = Volume(Classes[0]);
        OutPtr->Music = Volume(Classes[1]);
        OutPtr->Sfx = Volume(Classes[2]);
        const FSoundMixState* State = Device->GetSoundMixModifiers().Find(Mix);
        OutPtr->bBaseMix = State && State->IsBaseSoundMix;
        OutPtr->bRead = true;
    });
    FAudioCommandFence Fence;
    Fence.BeginFence();
    Fence.Wait();
    return Out;
}
} // namespace PSLAudioTest

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLAudioAssetsTest, "PSL.Audio.Assets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLAudioAssetsTest::RunTest(const FString& Parameters)
{
    using namespace PSLAudioTest;
    USoundClass* Master = LoadAudio<USoundClass>(TEXT("SC_Master"));
    USoundClass* Music = LoadAudio<USoundClass>(TEXT("SC_Music"));
    USoundClass* Sfx = LoadAudio<USoundClass>(TEXT("SC_SFX"));
    USoundMix* Mix = LoadAudio<USoundMix>(TEXT("SM_PSL_Volume"));
    if (!TestNotNull(TEXT("SC_Master"), Master) || !TestNotNull(TEXT("SC_Music"), Music)
        || !TestNotNull(TEXT("SC_SFX"), Sfx) || !TestNotNull(TEXT("SM_PSL_Volume"), Mix))
    {
        return false;
    }

    // The engine propagates volume only through ChildClasses (RecurseIntoSoundClasses), so both sides must agree.
    TestTrue(TEXT("SC_Master is a root class"), Master->ParentClass == nullptr);
    TestTrue(TEXT("SC_Music parent is SC_Master"), Music->ParentClass == Master);
    TestTrue(TEXT("SC_SFX parent is SC_Master"), Sfx->ParentClass == Master);
    TestEqual(TEXT("SC_Master has 2 children"), Master->ChildClasses.Num(), 2);
    TestTrue(TEXT("SC_Master lists SC_Music"), Master->ChildClasses.Contains(Music));
    TestTrue(TEXT("SC_Master lists SC_SFX"), Master->ChildClasses.Contains(Sfx));
    TestEqual(TEXT("SC_Music has no children"), Music->ChildClasses.Num(), 0);
    TestEqual(TEXT("SC_SFX has no children"), Sfx->ChildClasses.Num(), 0);
    for (USoundClass* Class : {Master, Music, Sfx})
    {
        TestEqual(FString::Printf(TEXT("%s volume"), *Class->GetName()), Class->Properties.Volume, 1.0f, 1.0e-6f);
    }
    TestEqual(TEXT("SM_PSL_Volume has no class effects (volumes come from overrides)"), Mix->SoundClassEffects.Num(), 0);

    const FSoftObjectPath& DefaultMix = GetDefault<UAudioSettings>()->DefaultBaseSoundMix;
    TestEqual(TEXT("AudioSettings.DefaultBaseSoundMix"), DefaultMix.ToString(), FString(MixPath));
    TestTrue(TEXT("DefaultBaseSoundMix resolves to SM_PSL_Volume"), DefaultMix.TryLoad() == Mix);

    const UPSLGameUserSettings* Defaults = GetDefault<UPSLGameUserSettings>();
    TestTrue(TEXT("settings SC_Master"), Defaults->SC_Master.LoadSynchronous() == Master);
    TestTrue(TEXT("settings SC_Music"), Defaults->SC_Music.LoadSynchronous() == Music);
    TestTrue(TEXT("settings SC_SFX"), Defaults->SC_SFX.LoadSynchronous() == Sfx);
    TestTrue(TEXT("settings SM_PSL_Volume"), Defaults->SM_PSL_Volume.LoadSynchronous() == Mix);

    for (const FWaveSpec& Spec : Waves)
    {
        USoundWave* Wave = LoadAudio<USoundWave>(Spec.Name);
        if (!TestNotNull(Spec.Name, Wave))
        {
            continue;
        }
        TestTrue(FString::Printf(TEXT("%s routed to %s"), Spec.Name, Spec.bMusic ? TEXT("SC_Music") : TEXT("SC_SFX")),
            Wave->SoundClassObject == (Spec.bMusic ? Music : Sfx));
        TestTrue(FString::Printf(TEXT("%s looping == %d"), Spec.Name, Spec.bLoop ? 1 : 0), static_cast<bool>(Wave->bLooping) == Spec.bLoop);
        TestEqual(FString::Printf(TEXT("%s channels"), Spec.Name), Wave->NumChannels, Spec.Channels);
        TestEqual(FString::Printf(TEXT("%s duration"), Spec.Name), Wave->Duration, Spec.Frames / 44100.0f, 1.0e-3f);
        TestEqual(FString::Printf(TEXT("%s volume"), Spec.Name), Wave->Volume, 1.0f, 1.0e-6f);
    }

    // Every path the game plays from resolves (a typo there is silent at runtime: PSLAudio::Load is quiet).
    const FString EatPath = GetDefault<APSLCharacter>()->EatSound.ToSoftObjectPath().ToString();
    const TCHAR* PlayedPaths[] = {PSLAudio::MenuMusic, PSLAudio::ValleyMusic, PSLAudio::Wind, PSLAudio::Bite, PSLAudio::GameOver,
        PSLAudio::Starving, FPSLUiStyle::ClickSoundPath, FPSLUiStyle::HoverSoundPath, *EatPath};
    for (const TCHAR* Path : PlayedPaths)
    {
        TestNotNull(FString::Printf(TEXT("played sound %s"), Path), Cast<USoundWave>(PSLAudio::Load(Path)));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLAudioEffectiveVolumeTest, "PSL.Audio.EffectiveVolume", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPSLAudioEffectiveVolumeTest::RunTest(const FString& Parameters)
{
    using namespace PSLAudioTest;
    UPSLGameUserSettings* Settings = UPSLGameUserSettings::Get();
    const TSharedRef<FAudioRefs> Refs = MakeShared<FAudioRefs>();
    Refs->Mix.Reset(LoadAudio<USoundMix>(TEXT("SM_PSL_Volume")));
    Refs->Master.Reset(LoadAudio<USoundClass>(TEXT("SC_Master")));
    Refs->Music.Reset(LoadAudio<USoundClass>(TEXT("SC_Music")));
    Refs->Sfx.Reset(LoadAudio<USoundClass>(TEXT("SC_SFX")));
    if (!TestNotNull(TEXT("settings"), Settings) || !TestNotNull(TEXT("SM_PSL_Volume"), Refs->Mix.Get())
        || !TestNotNull(TEXT("SC_Master"), Refs->Master.Get()) || !TestNotNull(TEXT("SC_Music"), Refs->Music.Get())
        || !TestNotNull(TEXT("SC_SFX"), Refs->Sfx.Get()))
    {
        return false;
    }
    const FVector3f Saved(Settings->MasterVolume, Settings->MusicVolume, Settings->SfxVolume);

    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, TEXT("AudioEffectiveVolume"));
    if (!QueuePieStart(*this, Context))
    {
        return false;
    }

    // (Master, Music, SFX) slider values; the device must report Master = M, Music = M*Mu, SFX = M*S.
    for (const FVector3f Case : {FVector3f(0.5f, 0.6f, 0.8f), FVector3f(1.0f, 0.0f, 1.0f)})
    {
        AddPieStep(Context, TEXT("ApplyAudio"), 2.0f, [Settings, Case](FPSLPieCtx& Ctx)
        {
            if (Ctx.IsUsable())
            {
                Settings->SetMasterVolume(Case.X);
                Settings->SetMusicVolume(Case.Y);
                Settings->SetSfxVolume(Case.Z);
                Settings->ApplyAudio(Ctx.World);
            }
            return true;
        });
        // Overrides fade in over 0.1 s and the base mix over its FadeInTime: poll until settled, then assert.
        AddPieStep(Context, TEXT("EffectiveVolume"), 8.0f, [Refs, Case, Start = 0.0](FPSLPieCtx& Ctx) mutable
        {
            if (!Ctx.IsUsable())
            {
                return true;
            }
            const double Now = FPlatformTime::Seconds();
            Start = Start > 0.0 ? Start : Now;
            const FDeviceVolumes V = ReadDeviceVolumes(Ctx.World, *Refs);
            const float WantMusic = Case.X * Case.Y;
            const float WantSfx = Case.X * Case.Z;
            const bool bSettled = V.bBaseMix && FMath::IsNearlyEqual(V.Master, Case.X, 1.0e-3f)
                && FMath::IsNearlyEqual(V.Music, WantMusic, 1.0e-3f) && FMath::IsNearlyEqual(V.Sfx, WantSfx, 1.0e-3f);
            if (!bSettled && Now - Start < 5.0)
            {
                return false;
            }
            const FString Label = FString::Printf(TEXT("M=%.2f Mu=%.2f S=%.2f"), Case.X, Case.Y, Case.Z);
            Ctx.Test->TestTrue(Label + TEXT(": PIE audio device answered"), V.bRead);
            Ctx.Test->TestTrue(Label + TEXT(": SM_PSL_Volume is the base mix"), V.bBaseMix);
            Ctx.Test->TestEqual(Label + TEXT(": SC_Master effective volume"), V.Master, Case.X, 1.0e-3f);
            Ctx.Test->TestEqual(Label + TEXT(": SC_Music effective volume"), V.Music, WantMusic, 1.0e-3f);
            Ctx.Test->TestEqual(Label + TEXT(": SC_SFX effective volume"), V.Sfx, WantSfx, 1.0e-3f);
            Ctx.Test->AddInfo(FString::Printf(TEXT("%s: device Master=%.4f Music=%.4f SFX=%.4f base=%d after %.2fs"),
                *Label, V.Master, V.Music, V.Sfx, V.bBaseMix ? 1 : 0, Now - Start));
            return true;
        });
    }

    QueuePieEnd(Context);
    // After PIE ends and outside FPSLPieStep (which skips its body once PIE is lost), so a lost PIE world does not skip it:
    // the in-memory sliders go back to the user's values. It does not run if the automation queue itself aborts; nothing is saved either way.
    TFunction<bool()> Restore = [Settings, Saved]()
    {
        Settings->SetMasterVolume(Saved.X);
        Settings->SetMusicVolume(Saved.Y);
        Settings->SetSfxVolume(Saved.Z);
        Settings->ApplyAudio(nullptr);
        return true;
    };
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(MoveTemp(Restore)));
    return true;
}

#endif
