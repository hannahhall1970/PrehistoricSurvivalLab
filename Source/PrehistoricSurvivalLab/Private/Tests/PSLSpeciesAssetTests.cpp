#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "PSLDinoSpecies.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"

namespace
{
    // The species texts must reference the shared PSLSpecies string table (never a culture-invariant or package-rekeyed copy).
    void PSLCheckSpeciesText(FAutomationTestBase& Test, const FString& What, const FText& Asset, const FText& Builtin)
    {
        FName AssetTable, BuiltinTable;
        FString AssetKey, BuiltinKey;
        Test.TestTrue(What + TEXT(" is a string-table text"), FTextInspector::GetTableIdAndKey(Asset, AssetTable, AssetKey));
        FTextInspector::GetTableIdAndKey(Builtin, BuiltinTable, BuiltinKey);
        Test.TestEqual(What + TEXT(" table"), AssetTable.ToString(), UPSLDinoSpecies::TextTableId.ToString());
        Test.TestEqual(What + TEXT(" table = builtin"), AssetTable.ToString(), BuiltinTable.ToString());
        Test.TestEqual(What + TEXT(" key = builtin"), AssetKey, BuiltinKey);
        Test.TestFalse(What + TEXT(" is not culture-invariant"), Asset.IsCultureInvariant());
        Test.TestEqual(What + TEXT(" text = builtin"), Asset.ToString(), Builtin.ToString());
    }

    const UPSLDinoSpecies* PSLLoadSpeciesAsset(FAutomationTestBase& Test, FName Id)
    {
        const FString Name = TEXT("DA_Species_") + Id.ToString();
        const FString Path = FString::Printf(TEXT("/Game/Data/Species/%s.%s"), *Name, *Name);
        const UPSLDinoSpecies* Asset = LoadObject<UPSLDinoSpecies>(nullptr, *Path);
        if (!Test.TestNotNull(FString::Printf(TEXT("%s loads"), *Path), Asset))
        {
            return nullptr;
        }
        Test.TestEqual(Name + TEXT(" primary asset id"), Asset->GetPrimaryAssetId().ToString(), FPrimaryAssetId(UPSLDinoSpecies::AssetType, Id).ToString());
        if (Test.TestTrue(TEXT("Asset manager initialized"), UAssetManager::IsInitialized()))
        {
            Test.TestEqual(Name + TEXT(" is registered with the asset manager"),
                UAssetManager::Get().GetPrimaryAssetPath(Asset->GetPrimaryAssetId()).ToString(), Path);
        }
        return Asset;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSpeciesAssetsTest, "PSL.Species.Assets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSpeciesAssetsTest::RunTest(const FString& Parameters)
{
    using FFloatField = float UPSLDinoSpecies::*;
    using FAnimField = TSoftObjectPtr<UAnimSequence> UPSLDinoSpecies::*;
    static const TPair<const TCHAR*, FFloatField> Floats[] = {
        {TEXT("CapsuleRadius"), &UPSLDinoSpecies::CapsuleRadius}, {TEXT("CapsuleHalfHeight"), &UPSLDinoSpecies::CapsuleHalfHeight},
        {TEXT("MeshYaw"), &UPSLDinoSpecies::MeshYaw}, {TEXT("MeshScale"), &UPSLDinoSpecies::MeshScale},
        {TEXT("MaxHealth"), &UPSLDinoSpecies::MaxHealth}, {TEXT("MaxStamina"), &UPSLDinoSpecies::MaxStamina},
        {TEXT("WalkSpeed"), &UPSLDinoSpecies::WalkSpeed}, {TEXT("SprintSpeed"), &UPSLDinoSpecies::SprintSpeed},
        {TEXT("BiteDamage"), &UPSLDinoSpecies::BiteDamage}, {TEXT("BiteRadius"), &UPSLDinoSpecies::BiteRadius},
        {TEXT("BiteCooldown"), &UPSLDinoSpecies::BiteCooldown}, {TEXT("MaxHunger"), &UPSLDinoSpecies::MaxHunger},
        {TEXT("HungerDrainPerSecond"), &UPSLDinoSpecies::HungerDrainPerSecond}, {TEXT("FoodPerBite"), &UPSLDinoSpecies::FoodPerBite}};
    static const TPair<const TCHAR*, FAnimField> Anims[] = {
        {TEXT("IdleAnim"), &UPSLDinoSpecies::IdleAnim}, {TEXT("WalkAnim"), &UPSLDinoSpecies::WalkAnim}, {TEXT("RunAnim"), &UPSLDinoSpecies::RunAnim},
        {TEXT("AttackAnim"), &UPSLDinoSpecies::AttackAnim}, {TEXT("DeathAnim"), &UPSLDinoSpecies::DeathAnim}};

    for (const FName Id : {UPSLDinoSpecies::TRexId, UPSLDinoSpecies::ParaId})
    {
        const UPSLDinoSpecies* Asset = PSLLoadSpeciesAsset(*this, Id);
        const UPSLDinoSpecies* Builtin = UPSLDinoSpecies::MakeBuiltin(Id, GetTransientPackage());
        if (!Asset || !TestNotNull(TEXT("Builtin species"), Builtin))
        {
            return false;
        }

        const FString N = Id.ToString() + TEXT(" ");
        TestEqual(N + TEXT("SpeciesId"), Asset->SpeciesId.ToString(), Id.ToString());
        TestTrue(N + TEXT("Diet"), Asset->Diet == Builtin->Diet);
        PSLCheckSpeciesText(*this, N + TEXT("DisplayName"), Asset->DisplayName, Builtin->DisplayName);
        PSLCheckSpeciesText(*this, N + TEXT("Description"), Asset->Description, Builtin->Description);
        for (const TPair<const TCHAR*, FFloatField>& F : Floats)
        {
            TestEqual(N + F.Key, Asset->*F.Value, Builtin->*F.Value);
        }
        TestEqual(N + TEXT("Mesh path"), Asset->Mesh.ToSoftObjectPath().ToString(), Builtin->Mesh.ToSoftObjectPath().ToString());
        TestNotNull(N + TEXT("Mesh loads"), Asset->Mesh.LoadSynchronous());
        for (const TPair<const TCHAR*, FAnimField>& A : Anims)
        {
            TestEqual(N + A.Key + TEXT(" path"), (Asset->*A.Value).ToSoftObjectPath().ToString(), (Builtin->*A.Value).ToSoftObjectPath().ToString());
            TestNotNull(N + A.Key + TEXT(" loads"), (Asset->*A.Value).LoadSynchronous());
        }
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLDevHeroTrexSpeciesTest, "PSL.Species.DevHeroTrex", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLDevHeroTrexSpeciesTest::RunTest(const FString& Parameters)
{
    IConsoleVariable* Cvar = IConsoleManager::Get().FindConsoleVariable(TEXT("psl.Dev.HeroTrex"));
    if (!TestNotNull(TEXT("psl.Dev.HeroTrex cvar registered"), Cvar))
    {
        return false;
    }
    const int32 Saved = Cvar->GetInt();
    const FString OldMesh = TEXT("/Game/Characters/Dino/SK_Trex.SK_Trex");
    const FString HeroMesh = TEXT("/Game/Dev/ArtDir/Hero/SK_HeroTrex.SK_HeroTrex");

    auto SetCvar = [&](int32 Value)
    {
        Cvar->Set(Value, ECVF_SetByCode);
        return Cvar->GetInt() == Value;
    };

    if (TestTrue(TEXT("cvar set to 0"), SetCvar(0)))
    {
        const UPSLDinoSpecies* TRex = UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::TRexId, GetTransientPackage());
        TestEqual(TEXT("cvar 0: TRex mesh is SK_Trex"), TRex->Mesh.ToSoftObjectPath().ToString(), OldMesh);
        TestEqual(TEXT("cvar 0: AttackPlayRate"), TRex->AttackPlayRate, 1.0f);
        TestEqual(TEXT("cvar 0: WalkAnimSpeed"), TRex->WalkAnimSpeed, 0.0f);
        TestEqual(TEXT("cvar 0: Attack Start/End/Contact"), TRex->AttackStartTime + TRex->AttackEndTime + TRex->AttackContactTime, 0.0f);
        TestEqual(TEXT("cvar 0: RunAnimSpeed"), TRex->RunAnimSpeed, 0.0f);
    }

    if (TestTrue(TEXT("cvar set to 1"), SetCvar(1)))
    {
        const UPSLDinoSpecies* TRex = UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::TRexId, GetTransientPackage());
        if (FPackageName::DoesPackageExist(TEXT("/Game/Dev/ArtDir/Hero/SK_HeroTrex")))
        {
            TestEqual(TEXT("cvar 1: hero mesh"), TRex->Mesh.ToSoftObjectPath().ToString(), HeroMesh);
            for (const TCHAR* Clip : {TEXT("Idle"), TEXT("Walk"), TEXT("Run"), TEXT("Attack")})
            {
                const FString Path = FString::Printf(TEXT("/Game/Dev/ArtDir/Hero/A_HeroTrex_%s.A_HeroTrex_%s"), Clip, Clip);
                const TSoftObjectPtr<UAnimSequence>& Anim = FCString::Strcmp(Clip, TEXT("Idle")) == 0 ? TRex->IdleAnim
                    : FCString::Strcmp(Clip, TEXT("Walk")) == 0 ? TRex->WalkAnim
                    : FCString::Strcmp(Clip, TEXT("Run")) == 0 ? TRex->RunAnim : TRex->AttackAnim;
                TestEqual(FString::Printf(TEXT("cvar 1: hero %s clip"), Clip), Anim.ToSoftObjectPath().ToString(), Path);
            }
            TestEqual(TEXT("cvar 1: DeathAnim is the hero death clip"), TRex->DeathAnim.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Dev/ArtDir/Hero/A_HeroTrex_Death.A_HeroTrex_Death")));
            TestEqual(TEXT("cvar 1: MeshScale fits capsule"), TRex->MeshScale, 0.0f);
            TestEqual(TEXT("cvar 1: AttackPlayRate"), TRex->AttackPlayRate, 1.5f);
            TestEqual(TEXT("cvar 1: AttackStartTime"), TRex->AttackStartTime, 0.6f);
            TestEqual(TEXT("cvar 1: AttackEndTime"), TRex->AttackEndTime, 2.1f);
            TestEqual(TEXT("cvar 1: AttackContactTime"), TRex->AttackContactTime, 1.567f);
            TestEqual(TEXT("cvar 1: WalkAnimSpeed"), TRex->WalkAnimSpeed, 60.0f);
            TestEqual(TEXT("cvar 1: RunAnimSpeed"), TRex->RunAnimSpeed, 245.0f);
        }
        else
        {
            TestEqual(TEXT("cvar 1, assets absent: falls back to SK_Trex"), TRex->Mesh.ToSoftObjectPath().ToString(), OldMesh);
            TestEqual(TEXT("cvar 1, assets absent: AttackPlayRate"), TRex->AttackPlayRate, 1.0f);
            TestEqual(TEXT("cvar 1, assets absent: Walk/RunAnimSpeed"), TRex->WalkAnimSpeed + TRex->RunAnimSpeed, 0.0f);
        }

        const UPSLDinoSpecies* Para = UPSLDinoSpecies::MakeBuiltin(UPSLDinoSpecies::ParaId, GetTransientPackage());
        TestEqual(TEXT("Para mesh never changes"), Para->Mesh.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Characters/Prey/SK_Parasaurolophus.SK_Parasaurolophus")));
        TestEqual(TEXT("Para AttackPlayRate"), Para->AttackPlayRate, 1.0f);
        TestEqual(TEXT("Para Walk/RunAnimSpeed"), Para->WalkAnimSpeed + Para->RunAnimSpeed, 0.0f);
        TestEqual(TEXT("Para Attack Start/End/Contact"), Para->AttackStartTime + Para->AttackEndTime + Para->AttackContactTime, 0.0f);
    }

    TestTrue(TEXT("cvar restored"), SetCvar(Saved));
    return true;
}

#endif
